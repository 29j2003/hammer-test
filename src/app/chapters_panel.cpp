// chapters_panel.cpp - Project > Chapters (see chapters_panel.h).

#include "chapters_panel.h"

#include "editor.h"
#include "lazy_refresh.h"
#include "ui_helpers.h"

#include <QApplication>
#include <QComboBox>
#include <QCompleter>
#include <QDir>
#include <QDirIterator>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

#include <sme/asset_import.h>
#include <sme/game.h>
#include <sme/localization.h>
#include <sme/text.h>
#include <sme/texture.h>

#include <functional>

namespace sme::app {

namespace {

constexpr const char* CARD_MIME = "application/x-sme-chapter";

/// A card that can be dragged by its handle and takes a dropped card.
class ChapterCard : public QFrame {
public:
	ChapterCard(int index, std::function<void(int from, int to)> move) : m_index(index), m_move(std::move(move)) {
		setObjectName("chapterCard");
		setProperty("role", "card");
		setAcceptDrops(true);
	}
	[[nodiscard]] int index() const { return m_index; }

protected:
	void dragEnterEvent(QDragEnterEvent* event) override {
		if (event->mimeData()->hasFormat(CARD_MIME)) event->acceptProposedAction();
	}
	void dropEvent(QDropEvent* event) override {
		const int from = event->mimeData()->data(CARD_MIME).toInt();
		event->acceptProposedAction();
		if (from != m_index) m_move(from, m_index);
	}

private:
	int m_index;
	std::function<void(int, int)> m_move;
};

/// The grip on a card: dragging it starts moving the card.
class DragHandle : public QLabel {
public:
	explicit DragHandle(int index) : QLabel(QStringLiteral("⠿")), m_index(index) {
		setObjectName("chapterDrag");
		setToolTip(QObject::tr("Drag to move the chapter"));
		setCursor(Qt::OpenHandCursor);
		setStyleSheet("font-size: 14pt; color: #8a8a8a; padding: 0 4px;");
	}

protected:
	void mousePressEvent(QMouseEvent* event) override { m_press = event->position().toPoint(); }
	void mouseMoveEvent(QMouseEvent* event) override {
		if (!(event->buttons() & Qt::LeftButton) || (event->position().toPoint() - m_press).manhattanLength() < QApplication::startDragDistance()) return;
		auto* mime = new QMimeData;
		mime->setData(CARD_MIME, QByteArray::number(m_index));
		auto* drag = new QDrag(this);
		drag->setMimeData(mime);
		if (auto* card = parentWidget()) drag->setPixmap(card->grab().scaledToWidth(320, Qt::SmoothTransformation));
		drag->exec(Qt::MoveAction);
	}

private:
	int m_index;
	QPoint m_press;
};

QToolButton* cardButton(const QString& text, const QString& tip, const QString& name) {
	auto* b = new QToolButton;
	b->setText(text);
	b->setToolTip(tip);
	b->setObjectName(name);
	b->setAutoRaise(true);
	return b;
}

/// A picture shown in a frame of a fixed size (dark when there is none).
QLabel* pictureLabel(const QImage& image, QSize size, const QString& empty, const QString& name) {
	auto* label = new QLabel;
	label->setObjectName(name);
	label->setFixedSize(size);
	label->setAlignment(Qt::AlignCenter);
	label->setStyleSheet("background: #1e1f22; border: 1px solid #3a3c40; color: #8a8a8a;");
	if (image.isNull()) {
		label->setText(empty);
		label->setWordWrap(true);
	} else {
		label->setPixmap(QPixmap::fromImage(image.scaled(size - QSize(2, 2), Qt::KeepAspectRatio, Qt::SmoothTransformation)));
	}
	return label;
}

QString mapsText(const std::vector<std::string>& maps) {
	QStringList out;
	for (const auto& m : maps) out << qs(m);
	return out.join(", ");
}

} // namespace

ChaptersPanel::ChaptersPanel(Editor* editor, QWidget* parent) : QWidget(parent), m_editor(editor) {
	setObjectName("chaptersPanel");
	auto* outer = new QVBoxLayout(this);
	outer->setContentsMargins(12, 8, 12, 8);
	outer->setSpacing(6);
	outer->addWidget(noteLabel(tr("The chapters of New Game. Drag a card by ⠿ to reorder. Saving writes cfg/chapterN.cfg, the titles, "
	                              "the pictures, scripts/ChapterBackgrounds.txt and scripts/chapters.txt (Mapbase: unlocks chapters). "
	                              "Restore original puts the mod's files back.")));

	auto* bar = new QHBoxLayout;
	auto* add = new QPushButton(tr("+ Chapter"));
	add->setObjectName("chaptersAdd");
	connect(add, &QPushButton::clicked, this, &ChaptersPanel::addChapter);
	bar->addWidget(add);
	bar->addSpacing(12);
	bar->addWidget(new QLabel(tr("Titles in")));
	m_languageBox = new QComboBox;
	m_languageBox->setObjectName("chaptersLanguage");
	connect(m_languageBox, &QComboBox::activated, this, [this](int i) { setLanguage(m_languageBox->itemData(i).toString()); });
	bar->addWidget(m_languageBox);
	bar->addStretch(1);
	m_restore = new QPushButton(tr("Restore original"));
	m_restore->setObjectName("chaptersRestore");
	m_restore->setToolTip(tr("Puts back every chapter file Mapsmith changed in the mod and removes the ones it made"));
	connect(m_restore, &QPushButton::clicked, this, [this] { restore(); });
	bar->addWidget(m_restore);
	m_save = new QPushButton(tr("Save"));
	m_save->setObjectName("chaptersSave");
	m_save->setDefault(true);
	connect(m_save, &QPushButton::clicked, this, [this] { save(); });
	bar->addWidget(m_save);
	outer->addLayout(bar);

	// The cards take the room; the checks below keep a few lines.
	auto* scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	auto* column = new QWidget(scroll->viewport());
	m_cards = new QVBoxLayout(column);
	m_cards->setContentsMargins(0, 0, 0, 0);
	m_cards->setSpacing(6);
	scroll->setWidget(column);
	outer->addWidget(scroll, 1);
	outer->addWidget(capsLabel(tr("Checks")));
	m_checks = new QListWidget;
	m_checks->setObjectName("chaptersChecks");
	m_checks->setMaximumHeight(120);
	outer->addWidget(m_checks);
	m_status = new QLabel;
	m_status->setObjectName("chaptersStatus");
	m_status->setWordWrap(true);
	outer->addWidget(m_status);

	auto* lazy = new RefreshWhenShown(this, [this] { reload(); });
	connect(editor, &Editor::gameChanged, lazy, &RefreshWhenShown::request);
	lazy->request();
}

void ChaptersPanel::reload() {
	const auto* entry = m_editor->gameEntry();
	m_set = entry ? loadChapters(entry->gameDir, m_editor->game() ? &m_editor->game()->files : nullptr) : ChapterSet{};
	if (m_set.languages.empty()) m_set.languages.push_back("english");
	m_modified = false;
	m_languageBox->clear();
	for (const auto& language : m_set.languages) m_languageBox->addItem(qs(languageDisplayName(language)), qs(language));
	if (m_languageBox->findData(m_language) < 0) m_language = QStringLiteral("english");
	m_languageBox->setCurrentIndex(std::max(0, m_languageBox->findData(m_language)));
	setEnabled(entry != nullptr);
	m_restore->setEnabled(entry && hasChapterChanges(entry->gameDir));
	setStatus(entry ? tr("%n chapter(s) in %1", nullptr, int(m_set.chapters.size())).arg(qs(entry->label)) : tr("No game chosen"));
	rebuild();
}

void ChaptersPanel::setLanguage(const QString& language) {
	if (language == m_language) return;
	m_language = language;
	if (const int i = m_languageBox->findData(language); i >= 0) m_languageBox->setCurrentIndex(i);
	rebuild();
}

QStringList ChaptersPanel::mapNames() const {
	QStringList names;
	const auto* entry = m_editor->gameEntry();
	if (!entry) return names;
	const auto addFrom = [&](const QString& dir, const QString& pattern) {
		QDirIterator it(dir, {pattern}, QDir::Files);
		while (it.hasNext()) names << QFileInfo(it.next()).completeBaseName();
	};
	addFrom(fromPath(entry->gameDir / "maps"), "*.bsp");
	addFrom(m_editor->gameSettings().mapDir, "*.vmf");
	names.removeDuplicates();
	names.sort(Qt::CaseInsensitive);
	return names;
}

MapState ChaptersPanel::mapState(const std::string& map) const {
	if (!isSafeMapName(map)) return MapState::Missing;
	const auto* entry = m_editor->gameEntry();
	if (entry && resolveI(entry->gameDir, "maps/" + map + ".bsp")) return MapState::Compiled;
	if (const auto* game = m_editor->game(); game && game->files.exists("maps/" + map + ".bsp")) return MapState::Compiled;
	const QString mapDir = m_editor->gameSettings().mapDir;
	if (!mapDir.isEmpty() && QFileInfo::exists(QDir(mapDir).filePath(qs(map) + ".vmf"))) return MapState::SourceOnly;
	return MapState::Missing;
}

bool ChaptersPanel::hasLoadingPicture(const std::string& background) const {
	if (!isSafeMapName(background)) return false;
	const auto* game = m_editor->game();
	const auto* entry = m_editor->gameEntry();
	const std::string vmt = "materials/console/" + background + ".vmt";
	return (entry && resolveI(entry->gameDir, vmt)) || (game && game->files.exists(vmt));
}

QImage ChaptersPanel::loadingPicture(const std::string& background) const {
	if (const auto it = m_set.newLoadingPictures.find(background); it != m_set.newLoadingPictures.end()) return toQImage(it->second);
	const auto* game = m_editor->game();
	if (!game || !isSafeMapName(background)) return {};
	for (const auto* suffix : {"_widescreen", ""}) {
		if (const auto bytes = game->files.read("materials/console/" + background + suffix + ".vtf")) {
			if (const auto image = decodeTexture(*bytes, 256)) return toQImage(*image);
		}
	}
	return {};
}

void ChaptersPanel::rebuild() {
	while (auto* item = m_cards->takeAt(0)) {
		if (auto* w = item->widget()) w->deleteLater();
		delete item;
	}
	for (int i = 0; i < static_cast<int>(m_set.chapters.size()); ++i) m_cards->addWidget(makeCard(i));
	if (m_set.chapters.empty()) {
		m_cards->addWidget(noteLabel(tr("No chapters yet - New Game has nothing to offer. Add one and pick the map it starts.")));
	}
	m_cards->addStretch(1);
	updateChecks();
}

QWidget* ChaptersPanel::makeCard(int index) {
	const auto& chapter = m_set.chapters[std::size_t(index)];
	auto* card = new ChapterCard(index, [this](int from, int to) {
		QMetaObject::invokeMethod(this, [this, from, to] { moveChapter(from, to); }, Qt::QueuedConnection);
	});
	auto* h = new QHBoxLayout(card);
	h->setContentsMargins(6, 6, 6, 6);
	h->addWidget(new DragHandle(index), 0, Qt::AlignTop);

	// The picture New Game shows.
	auto* pictureColumn = new QVBoxLayout;
	QImage picture;
	if (!chapter.picture.empty()) {
		if (const auto image = decodeTexture(chapter.picture)) {
			picture = toQImage(*image);
			if (picture.width() >= int(CHAPTER_PICTURE_SHOWN_WIDTH) && picture.height() >= int(CHAPTER_PICTURE_SHOWN_HEIGHT) &&
			    picture.width() == int(CHAPTER_PICTURE_WIDTH)) {
				picture = picture.copy(0, 0, int(CHAPTER_PICTURE_SHOWN_WIDTH), int(CHAPTER_PICTURE_SHOWN_HEIGHT));
			}
		}
	}
	pictureColumn->addWidget(pictureLabel(picture, QSize(156, 90), tr("No picture"), "chapterPicture"));
	auto* setPictureButton = new QPushButton(tr("Picture ..."));
	setPictureButton->setObjectName("chapterSetPicture");
	setPictureButton->setToolTip(tr("Choose a screenshot - it is cropped to the frame New Game shows (152x86 in a 256x128 texture)"));
	connect(setPictureButton, &QPushButton::clicked, this, [this, index] {
		if (const auto photo = askPicture(tr("Picture for chapter %1").arg(index + 1))) setPicture(index, *photo);
	});
	pictureColumn->addWidget(setPictureButton);
	if (chapter.pictureFromGame) pictureColumn->addWidget(noteLabel(tr("From the base game")));
	pictureColumn->addStretch(1);
	h->addLayout(pictureColumn);

	// Title, maps, background.
	auto* fields = new QVBoxLayout;
	auto* head = new QHBoxLayout;
	auto* name = new QLabel(tr("Chapter %1").arg(index + 1));
	name->setStyleSheet("font-weight: 600;");
	head->addWidget(name, 1);
	auto* play = new QPushButton(tr("▶ Play"));
	play->setObjectName("chapterPlay");
	play->setToolTip(tr("Start the game at this chapter's map"));
	play->setEnabled(!chapter.map.empty() && mapState(chapter.map) == MapState::Compiled);
	connect(play, &QPushButton::clicked, this, [this, index] { emit playRequested(qs(m_set.chapters[std::size_t(index)].map)); });
	head->addWidget(play);
	auto* up = cardButton(QStringLiteral("↑"), tr("Earlier"), "chapterUp");
	auto* down = cardButton(QStringLiteral("↓"), tr("Later"), "chapterDown");
	auto* remove = cardButton(QStringLiteral("✕"), tr("Remove this chapter"), "chapterRemove");
	up->setEnabled(index > 0);
	down->setEnabled(index + 1 < static_cast<int>(m_set.chapters.size()));
	connect(up, &QToolButton::clicked, this, [this, index] { QMetaObject::invokeMethod(this, [this, index] { moveChapter(index, index - 1); }, Qt::QueuedConnection); });
	connect(down, &QToolButton::clicked, this, [this, index] { QMetaObject::invokeMethod(this, [this, index] { moveChapter(index, index + 1); }, Qt::QueuedConnection); });
	connect(remove, &QToolButton::clicked, this, [this, index] { QMetaObject::invokeMethod(this, [this, index] { removeChapter(index); }, Qt::QueuedConnection); });
	head->addWidget(up);
	head->addWidget(down);
	head->addWidget(remove);
	fields->addLayout(head);

	const QStringList maps = mapNames();
	const auto mapCombo = [&](const std::string& value, const QString& objectName, const QString& tip) {
		auto* combo = new QComboBox;
		combo->setObjectName(objectName);
		combo->setEditable(true);
		combo->addItems(maps);
		combo->setCurrentText(qs(value));
		combo->setToolTip(tip);
		combo->completer()->setFilterMode(Qt::MatchContains);
		combo->setMinimumWidth(180);
		return combo;
	};
	auto* form = new QFormLayout;
	auto* title = new QLineEdit(qs(chapter.titles.count(m_language.toStdString()) ? chapter.titles.at(m_language.toStdString()) : std::string()));
	title->setObjectName("chapterTitle");
	title->setPlaceholderText(m_language == "english" ? tr("Title shown in New Game") : qs(chapter.title("english")));
	connect(title, &QLineEdit::textEdited, this, [this, index](const QString& text) { setTitle(index, text); });
	auto* map = mapCombo(chapter.map, "chapterMap", tr("The map this chapter starts (cfg/chapter%1.cfg)").arg(index + 1));
	connect(map, &QComboBox::currentTextChanged, this, [this, index](const QString& text) { setMap(index, text); });
	auto* more = new QLineEdit(mapsText(chapter.moreMaps));
	more->setObjectName("chapterMoreMaps");
	more->setPlaceholderText(tr("e.g. mymod_01b, mymod_01c"));
	more->setToolTip(tr("The chapter's other maps (or the start of their names). Mapbase unlocks the chapter in New Game "
	                    "when the player reaches one, and saves show the chapter's title (scripts/chapters.txt)."));
	connect(more, &QLineEdit::editingFinished, this, [this, index, more] { setMoreMaps(index, more->text()); });
	form->addRow(tr("Title"), title);
	form->addRow(tr("Starts map"), map);
	form->addRow(tr("Also maps"), more);
	auto* backgroundRow = new QHBoxLayout;
	auto* background = mapCombo(chapter.background, "chapterBackground",
	                            tr("Map running behind the menu once the player reached this chapter (scripts/ChapterBackgrounds.txt)"));
	connect(background, &QComboBox::currentTextChanged, this, [this, index](const QString& text) { setBackground(index, text); });
	backgroundRow->addWidget(background, 1);
	auto* loading = pictureLabel(chapter.background.empty() ? QImage() : loadingPicture(chapter.background), QSize(96, 54), tr("none"), "chapterLoading");
	loading->setToolTip(tr("Loading picture of the background map (materials/console/<map>, also shown while it loads)"));
	backgroundRow->addWidget(loading);
	auto* setLoading = new QPushButton(tr("Loading picture ..."));
	setLoading->setObjectName("chapterSetLoading");
	setLoading->setEnabled(!chapter.background.empty());
	connect(setLoading, &QPushButton::clicked, this, [this, index] {
		const QString bg = qs(m_set.chapters[std::size_t(index)].background);
		if (bg.isEmpty()) return;
		if (const auto photo = askPicture(tr("Loading picture for %1").arg(bg))) setLoadingPicture(bg, *photo);
	});
	backgroundRow->addWidget(setLoading);
	form->addRow(tr("Background"), backgroundRow);
	fields->addLayout(form);
	h->addLayout(fields, 1);
	return card;
}

std::optional<ImageRgba> ChaptersPanel::askPicture(const QString& title) {
	const QString file = QFileDialog::getOpenFileName(this, title, QString(), tr("Pictures (*.png *.jpg *.jpeg *.tga *.bmp)"));
	if (file.isEmpty()) return std::nullopt;
	QFile in(file);
	if (!in.open(QIODevice::ReadOnly) || in.size() > (64 << 20)) {
		setStatus(tr("Cannot read %1").arg(file), true);
		return std::nullopt;
	}
	const QByteArray data = in.readAll();
	std::string error;
	auto image = decodeImageFile(std::as_bytes(std::span(data.constData(), std::size_t(data.size()))), &error);
	if (!image) {
		setStatus(tr("%1 is not a picture Mapsmith can read: %2").arg(QFileInfo(file).fileName(), qs(error)), true);
		return std::nullopt;
	}
	return image;
}

void ChaptersPanel::changed(bool rebuildCards) {
	m_modified = true;
	if (rebuildCards) rebuild();
	else updateChecks();
}

void ChaptersPanel::addChapter() {
	Chapter chapter;
	chapter.titles["english"] = tr("CHAPTER %1").arg(m_set.chapters.size() + 1).toStdString();
	m_set.chapters.push_back(std::move(chapter));
	changed(true);
}

void ChaptersPanel::removeChapter(int index) {
	if (index < 0 || index >= static_cast<int>(m_set.chapters.size())) return;
	m_set.chapters.erase(m_set.chapters.begin() + index);
	changed(true);
}

void ChaptersPanel::moveChapter(int from, int to) {
	const int count = static_cast<int>(m_set.chapters.size());
	if (from < 0 || from >= count || to < 0 || to >= count || from == to) return;
	auto chapter = std::move(m_set.chapters[std::size_t(from)]);
	m_set.chapters.erase(m_set.chapters.begin() + from);
	m_set.chapters.insert(m_set.chapters.begin() + to, std::move(chapter));
	changed(true);
}

void ChaptersPanel::setTitle(int index, const QString& title) {
	if (index < 0 || index >= static_cast<int>(m_set.chapters.size())) return;
	m_set.chapters[std::size_t(index)].titles[m_language.toStdString()] = title.toStdString();
	changed();
}

void ChaptersPanel::setMap(int index, const QString& map) {
	if (index < 0 || index >= static_cast<int>(m_set.chapters.size())) return;
	auto& chapter = m_set.chapters[std::size_t(index)];
	const std::string value = map.trimmed().toStdString();
	if (chapter.map == value) return;
	chapter.map = value;
	changed();
	// The Play button follows the map.
	if (auto* item = m_cards->itemAt(index)) {
		if (auto* play = item->widget() ? item->widget()->findChild<QPushButton*>("chapterPlay") : nullptr) {
			play->setEnabled(!value.empty() && mapState(value) == MapState::Compiled);
		}
	}
}

void ChaptersPanel::setMoreMaps(int index, const QString& maps) {
	if (index < 0 || index >= static_cast<int>(m_set.chapters.size())) return;
	std::vector<std::string> list;
	for (const auto& part : maps.split(QRegularExpression("[,;\\s]+"), Qt::SkipEmptyParts)) list.push_back(part.toStdString());
	auto& chapter = m_set.chapters[std::size_t(index)];
	if (chapter.moreMaps == list) return;
	chapter.moreMaps = std::move(list);
	changed();
}

void ChaptersPanel::setBackground(int index, const QString& map) {
	if (index < 0 || index >= static_cast<int>(m_set.chapters.size())) return;
	auto& chapter = m_set.chapters[std::size_t(index)];
	const std::string value = map.trimmed().toStdString();
	if (chapter.background == value) return;
	chapter.background = value;
	changed();
	if (auto* item = m_cards->itemAt(index); item && item->widget()) {
		if (auto* button = item->widget()->findChild<QPushButton*>("chapterSetLoading")) button->setEnabled(!value.empty());
		if (auto* label = item->widget()->findChild<QLabel*>("chapterLoading")) {
			const QImage image = value.empty() ? QImage() : loadingPicture(value);
			if (image.isNull()) {
				label->setPixmap({});
				label->setText(tr("none"));
			} else {
				label->setPixmap(QPixmap::fromImage(image.scaled(label->size() - QSize(2, 2), Qt::KeepAspectRatio, Qt::SmoothTransformation)));
			}
		}
	}
}

void ChaptersPanel::setPicture(int index, const ImageRgba& photo) {
	if (index < 0 || index >= static_cast<int>(m_set.chapters.size())) return;
	std::string error;
	const auto vtf = encodeMenuVtf(chapterPictureImage(photo), false, &error);
	if (!vtf) {
		setStatus(tr("The picture could not be made: %1").arg(qs(error)), true);
		return;
	}
	auto& chapter = m_set.chapters[std::size_t(index)];
	chapter.picture = *vtf;
	chapter.pictureFromGame = false;
	chapter.pictureNumber = 0;
	changed(true);
}

void ChaptersPanel::setLoadingPicture(const QString& background, const ImageRgba& photo) {
	if (!isSafeMapName(background.toStdString()) || photo.width == 0) return;
	m_set.newLoadingPictures[background.toStdString()] = photo;
	changed(true);
}

void ChaptersPanel::updateChecks() {
	m_problems = checkChapters(
	    m_set, [this](const std::string& map) { return mapState(map); }, [this](const std::string& bg) { return hasLoadingPicture(bg); });
	m_checks->clear();
	for (const auto& problem : m_problems) {
		const QString where = problem.chapter > 0 ? tr("Chapter %1: ").arg(problem.chapter) : QString();
		auto* item = new QListWidgetItem((problem.error ? QStringLiteral("✕ ") : QStringLiteral("• ")) + where + qs(problem.text));
		item->setForeground(problem.error ? QColor(255, 120, 110) : QColor(225, 190, 110));
		m_checks->addItem(item);
	}
	if (m_problems.empty()) m_checks->addItem(tr("✓ Everything looks fine."));
}

bool ChaptersPanel::save(QString* error) {
	const auto* entry = m_editor->gameEntry();
	if (!entry) {
		if (error) *error = tr("No game chosen");
		return false;
	}
	const auto result = saveChapters(entry->gameDir, m_set);
	if (!result.error.empty()) {
		setStatus(qs(result.error), true);
		if (error) *error = qs(result.error);
		return false;
	}
	QStringList files;
	for (const auto& path : result.written) files << QDir(fromPath(entry->gameDir)).relativeFilePath(fromPath(path));
	for (const auto& path : result.removed) files << tr("%1 (removed)").arg(QDir(fromPath(entry->gameDir)).relativeFilePath(fromPath(path)));
	if (auto* game = m_editor->game()) {
		for (const auto& path : result.written) game->files.noteWrittenFile(path);
		for (const auto& path : result.removed) game->files.noteRemovedFile(path);
	}
	reload();
	setStatus(files.isEmpty() ? tr("Nothing changed.") : tr("Saved: %1").arg(files.join(", ")));
	emit saved();
	return true;
}

bool ChaptersPanel::restore(QString* error) {
	const auto* entry = m_editor->gameEntry();
	if (!entry) return false;
	std::string why;
	const bool ok = restoreChapters(entry->gameDir, &why);
	reload();
	if (!ok) {
		setStatus(qs(why), true);
		if (error) *error = qs(why);
		return false;
	}
	setStatus(tr("The mod's chapter files are as they were before."));
	emit saved();
	return true;
}

void ChaptersPanel::setStatus(const QString& text, bool error) {
	m_status->setText(text);
	m_status->setStyleSheet(error ? "color: #ff7a6e;" : "color: #9a9a9a;");
}

} // namespace sme::app
