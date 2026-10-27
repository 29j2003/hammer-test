// sound_browser.cpp - Sounds tab (see sound_browser.h).

#include "sound_browser.h"

#include "editor.h"
#include "sound_player.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMimeData>
#include <QPushButton>
#include <QShortcut>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <sme/filesystem.h>
#include <sme/game.h>
#include <sme/text.h>

namespace sme::app {

namespace {

constexpr int KIND_ROLE = Qt::UserRole;
constexpr int NAME_ROLE = Qt::UserRole + 1;
constexpr int INDEX_ROLE = Qt::UserRole + 2;

/// Tree whose sound items can be dragged into the 3D view.
class SoundTree : public QTreeWidget {
public:
	using QTreeWidget::QTreeWidget;

protected:
	QStringList mimeTypes() const override { return {"application/x-sme-sound"}; }
	QMimeData* mimeData(const QList<QTreeWidgetItem*>& items) const override {
		if (items.isEmpty() || items.first()->data(0, KIND_ROLE).toInt() == 0) {
			return nullptr;
		}
		auto* mime = new QMimeData;
		// "<kind>:<name>" - the view places an ambient_generic or an env_soundscape.
		const QString payload = QStringLiteral("%1:%2").arg(items.first()->data(0, KIND_ROLE).toInt()).arg(items.first()->data(0, NAME_ROLE).toString());
		mime->setData("application/x-sme-sound", payload.toUtf8());
		mime->setText(items.first()->data(0, NAME_ROLE).toString());
		return mime;
	}
};

QTreeWidgetItem* groupItem(QTreeWidgetItem* parent, QMap<QString, QTreeWidgetItem*>& groups, const QString& name) {
	auto it = groups.find(name);
	if (it == groups.end()) {
		auto* item = new QTreeWidgetItem(parent, QStringList(name));
		item->setFlags(item->flags() & ~Qt::ItemIsDragEnabled);
		it = groups.insert(name, item);
	}
	return *it;
}

} // namespace

SoundBrowser::SoundBrowser(Editor* editor, QWidget* parent) : QWidget(parent), m_editor(editor), m_player(new SoundPlayer(this)) {
	auto* v = new QVBoxLayout(this);
	v->setContentsMargins(4, 4, 4, 4);
	auto* row = new QHBoxLayout;
	m_search = new QLineEdit;
	m_search->setPlaceholderText(tr("Search sounds, files and soundscapes (words: door metal) ..."));
	m_search->setClearButtonEnabled(true);
	m_kind = new QComboBox;
	m_kind->addItem(tr("Everything"), 0);
	m_kind->addItem(tr("Sound scripts"), int(ScriptSound));
	m_kind->addItem(tr("Sound files"), int(FileSound));
	m_kind->addItem(tr("Soundscapes"), int(SoundscapeEntry));
	m_play = new QPushButton(tr("▶ Play"));
	m_play->setToolTip(SoundPlayer::available() ? tr("Play the selected sound (Space)") : tr("This build cannot play sounds"));
	m_play->setEnabled(SoundPlayer::available());
	row->addWidget(m_search, 1);
	row->addWidget(m_kind);
	row->addWidget(m_play);
	v->addLayout(row);
	m_tree = new SoundTree;
	m_tree->setColumnCount(2);
	m_tree->setHeaderLabels({tr("Name"), tr("Details")});
	m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
	m_tree->header()->setStretchLastSection(false);
	m_tree->header()->resizeSection(1, 220);
	m_tree->setUniformRowHeights(true);
	m_tree->setDragEnabled(true);
	m_tree->setDragDropMode(QAbstractItemView::DragOnly);
	v->addWidget(m_tree, 1);
	m_info = new QLabel(tr("No game loaded"));
	m_info->setWordWrap(true);
	m_info->setTextInteractionFlags(Qt::TextSelectableByMouse);
	v->addWidget(m_info);

	m_filterTimer.setSingleShot(true);
	m_filterTimer.setInterval(180);   // thousands of entries: filter once typing pauses
	connect(&m_filterTimer, &QTimer::timeout, this, &SoundBrowser::applyFilter);
	connect(m_search, &QLineEdit::textChanged, &m_filterTimer, qOverload<>(&QTimer::start));
	connect(m_kind, &QComboBox::currentIndexChanged, this, &SoundBrowser::applyFilter);
	connect(m_tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) { showItem(item); });
	connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) { useItem(item); });
	connect(m_tree, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem* item) {
		// Files of a sound script entry are added when it is opened.
		if (item->data(0, KIND_ROLE).toInt() != ScriptSound || item->childCount() != 1 || !item->child(0)->text(0).isEmpty()) {
			return;
		}
		delete item->takeChild(0);
		const auto& entry = m_index.scripts[static_cast<std::size_t>(item->data(0, INDEX_ROLE).toInt())];
		for (const auto& wave : entry.waves) {
			auto* child = new QTreeWidgetItem(item, {QString::fromStdString(wave), tr("file")});
			child->setData(0, KIND_ROLE, int(FileSound));
			child->setData(0, NAME_ROLE, QString::fromStdString(wave));
		}
	});
	connect(m_play, &QPushButton::clicked, this, [this] {
		if (m_player->playing()) {
			m_player->stop();
		} else {
			play(m_tree->currentItem());
		}
	});
	auto* space = new QShortcut(QKeySequence(Qt::Key_Space), m_tree, nullptr, nullptr, Qt::WidgetShortcut);
	connect(space, &QShortcut::activated, m_play, &QPushButton::click);
	connect(m_player, &SoundPlayer::stateChanged, this, [this](bool playing) { m_play->setText(playing ? tr("■ Stop") : tr("▶ Play")); });
	connect(m_player, &SoundPlayer::error, this, [this](const QString& text) { m_info->setText(tr("Cannot play: %1").arg(text)); });

	connect(editor, &Editor::gameAboutToChange, this, [this] {
		++m_generation;
		m_player->stop();
		m_watcher.waitForFinished();
	});
	connect(editor, &Editor::gameChanged, this, &SoundBrowser::reload);
	connect(&m_watcher, &QFutureWatcherBase::finished, this, [this] {
		if (m_watcher.future().resultCount() == 0 || m_watcher.isCanceled()) {
			return;
		}
		m_index = m_watcher.result();
		fill();
		emit indexed();
	});
	reload();
}

SoundBrowser::~SoundBrowser() {
	++m_generation;
	m_watcher.waitForFinished();
}

void SoundBrowser::reload() {
	m_tree->clear();
	m_index = {};
	auto* game = m_editor->game();
	if (!game) {
		m_info->setText(tr("No game loaded"));
		return;
	}
	m_info->setText(tr("Reading sound scripts, sound files and soundscapes ..."));
	const GameFileSystem* fs = &game->files;
	const int generation = ++m_generation;
	m_watcher.setFuture(QtConcurrent::run([this, fs, generation] {
		SoundIndex index;
		if (generation != m_generation) return index;
		index.scripts = soundScripts(*fs);
		if (generation != m_generation) return index;
		index.soundscapes = soundscapes(*fs);
		for (const char* extension : {".wav", ".mp3", ".ogg"}) {
			if (generation != m_generation) return index;
			for (auto& file : fs->list("sound", extension)) {
				index.files.push_back(file.substr(6));   // without "sound/"
			}
		}
		std::sort(index.files.begin(), index.files.end());
		return index;
	}));
}

void SoundBrowser::fill() {
	m_tree->setUpdatesEnabled(false);
	m_tree->clear();
	auto* scripts = new QTreeWidgetItem(m_tree, {tr("Sound scripts (%1)").arg(m_index.scripts.size())});
	auto* files = new QTreeWidgetItem(m_tree, {tr("Sound files (%1)").arg(m_index.files.size())});
	auto* scapes = new QTreeWidgetItem(m_tree, {tr("Soundscapes (%1)").arg(m_index.soundscapes.size())});
	for (auto* top : {scripts, files, scapes}) {
		top->setFlags(top->flags() & ~Qt::ItemIsDragEnabled);
		QFont font = top->font(0);
		font.setBold(true);
		top->setFont(0, font);
	}
	// Sound scripts grouped by the part before the dot ("Weapon_Pistol"), files by folder,
	// soundscapes by file.
	QMap<QString, QTreeWidgetItem*> groups;
	for (std::size_t i = 0; i < m_index.scripts.size(); ++i) {
		const auto& entry = m_index.scripts[i];
		const QString name = QString::fromStdString(entry.name);
		const QString prefix = name.contains('.') ? name.section('.', 0, 0) : tr("(no group)");
		auto* item = new QTreeWidgetItem(groupItem(scripts, groups, prefix), {name, QString::fromStdString(entry.channel)});
		item->setData(0, KIND_ROLE, int(ScriptSound));
		item->setData(0, NAME_ROLE, name);
		item->setData(0, INDEX_ROLE, int(i));
		if (!entry.waves.empty()) {
			new QTreeWidgetItem(item);   // placeholder, filled when opened
		}
	}
	groups.clear();
	for (const auto& file : m_index.files) {
		const QString path = QString::fromStdString(file);
		const QString folder = path.contains('/') ? path.section('/', 0, -2) : tr("(sound folder)");
		auto* item = new QTreeWidgetItem(groupItem(files, groups, folder), {path.section('/', -1), path.section('.', -1).toUpper()});
		item->setData(0, KIND_ROLE, int(FileSound));
		item->setData(0, NAME_ROLE, path);
		item->setToolTip(0, path);
	}
	groups.clear();
	for (std::size_t i = 0; i < m_index.soundscapes.size(); ++i) {
		const auto& scape = m_index.soundscapes[i];
		const QString file = QString::fromStdString(scape.file).section('/', -1);
		auto* item = new QTreeWidgetItem(groupItem(scapes, groups, file),
		                                 {QString::fromStdString(scape.name), tr("%1 sounds").arg(scape.sounds.size())});
		item->setData(0, KIND_ROLE, int(SoundscapeEntry));
		item->setData(0, NAME_ROLE, QString::fromStdString(scape.name));
		item->setData(0, INDEX_ROLE, int(i));
	}
	m_tree->setUpdatesEnabled(true);
	m_info->setText(tr("%1 sound script entries, %2 files, %3 soundscapes - drag into the map, double-click sets it on the selected entity")
	                    .arg(m_index.scripts.size())
	                    .arg(m_index.files.size())
	                    .arg(m_index.soundscapes.size()));
	applyFilter();
}

void SoundBrowser::applyFilter() {
	const QStringList words = m_search->text().toLower().split(' ', Qt::SkipEmptyParts);
	const int kind = m_kind->currentData().toInt();
	m_tree->setUpdatesEnabled(false);
	for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
		auto* top = m_tree->topLevelItem(i);
		const int topKind = i + 1;   // same order as Kind
		const bool kindVisible = kind == 0 || kind == topKind;
		int topCount = 0;
		for (int g = 0; g < top->childCount(); ++g) {
			auto* group = top->child(g);
			int visible = 0;
			for (int c = 0; c < group->childCount(); ++c) {
				auto* item = group->child(c);
				const QString name = item->data(0, NAME_ROLE).toString().toLower();
				bool show = kindVisible;
				for (const auto& word : words) {
					show = show && name.contains(word);
				}
				item->setHidden(!show);
				visible += show ? 1 : 0;
			}
			group->setHidden(visible == 0);
			group->setExpanded(!words.isEmpty() && visible <= 50);
			topCount += visible;
		}
		top->setHidden(!kindVisible || topCount == 0);
		top->setExpanded(!words.isEmpty() || kind == topKind);
	}
	m_tree->setUpdatesEnabled(true);
}

void SoundBrowser::showItem(QTreeWidgetItem* item) {
	if (!item) {
		return;
	}
	const int kind = item->data(0, KIND_ROLE).toInt();
	const int index = item->data(0, INDEX_ROLE).toInt();
	if (kind == ScriptSound) {
		const auto& entry = m_index.scripts[static_cast<std::size_t>(index)];
		QStringList waves;
		for (const auto& wave : entry.waves) waves << QString::fromStdString(wave);
		m_info->setText(tr("<b>%1</b> (%2)<br>%3 file(s): %4").arg(QString::fromStdString(entry.name), QString::fromStdString(entry.script)).arg(waves.size()).arg(waves.join(", ")));
	} else if (kind == SoundscapeEntry) {
		const auto& scape = m_index.soundscapes[static_cast<std::size_t>(index)];
		QStringList sounds, includes;
		for (const auto& s : scape.sounds) sounds << QString::fromStdString(s);
		for (const auto& s : scape.includes) includes << QString::fromStdString(s);
		m_info->setText(tr("<b>%1</b> (%2, dsp %3)<br>Plays: %4%5")
		                    .arg(QString::fromStdString(scape.name), QString::fromStdString(scape.file))
		                    .arg(scape.dsp)
		                    .arg(sounds.join(", "))
		                    .arg(includes.isEmpty() ? QString() : tr("<br>Includes: %1").arg(includes.join(", "))));
	} else if (kind == FileSound) {
		m_info->setText(tr("<b>sound/%1</b> - double-click sets it on a selected ambient_generic").arg(item->data(0, NAME_ROLE).toString()));
	}
}

void SoundBrowser::useItem(QTreeWidgetItem* item) {
	const int kind = item ? item->data(0, KIND_ROLE).toInt() : 0;
	if (kind == 0) {
		return;
	}
	const QString name = item->data(0, NAME_ROLE).toString();
	QApplication::clipboard()->setText(name);
	const auto& selection = m_editor->selection();
	std::vector<int> targets;
	std::string key;
	for (const int id : selection.entities) {
		const auto* entity = m_editor->doc().findEntity(id);
		if (!entity) continue;
		const auto cls = entity->classname();
		if (kind == SoundscapeEntry && cls.rfind("env_soundscape", 0) == 0) {
			targets.push_back(id);
			key = "soundscape";
		} else if (kind != SoundscapeEntry && cls == "ambient_generic") {
			targets.push_back(id);
			key = "message";
		}
	}
	if (targets.empty()) {
		emit m_editor->message(tr("%1 copied - select an ambient_generic or env_soundscape to set it, or drag it into the map").arg(name));
		return;
	}
	m_editor->commit(edit::setKeyValue(m_editor->doc(), targets, key, name.toStdString()));
}

void SoundBrowser::play(QTreeWidgetItem* item) {
	auto* game = m_editor->game();
	if (!item || !game) {
		return;
	}
	QString file;
	const int kind = item->data(0, KIND_ROLE).toInt();
	if (kind == FileSound) {
		file = item->data(0, NAME_ROLE).toString();
	} else if (kind == ScriptSound) {
		const auto& entry = m_index.scripts[static_cast<std::size_t>(item->data(0, INDEX_ROLE).toInt())];
		if (!entry.waves.empty()) file = QString::fromStdString(entry.waves.front());
	} else if (kind == SoundscapeEntry) {
		for (const auto& sound : m_index.soundscapes[static_cast<std::size_t>(item->data(0, INDEX_ROLE).toInt())].sounds) {
			if (sound.find('.') != std::string::npos && sound.find('/') != std::string::npos) {
				file = QString::fromStdString(sound);
				break;
			}
		}
	}
	if (file.isEmpty()) {
		return;
	}
	const auto data = game->files.read("sound/" + file.toStdString());
	if (!data) {
		m_info->setText(tr("sound/%1 is missing").arg(file));
		return;
	}
	m_player->play(file, QByteArray(reinterpret_cast<const char*>(data->data()), static_cast<qsizetype>(data->size())));
	m_info->setText(tr("Playing sound/%1").arg(file));
}

} // namespace sme::app
