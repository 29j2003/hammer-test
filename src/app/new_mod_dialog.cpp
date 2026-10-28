// new_mod_dialog.cpp - "New mod ..." dialog (see new_mod_dialog.h).

#include "new_mod_dialog.h"

#include "editor.h"
#include "features_panel.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include <sme/fidelity_effects.h>
#include <sme/game.h>
#include <sme/mod_features.h>
#include <sme/text.h>

namespace sme::app {

QString createModAndSwitch(Editor* editor, const ModTemplate& mod, const QStringList& maps, QString* error,
                           const std::optional<std::set<std::string>>& featuresOff) {
	const auto& steam = editor->discovery().steam;
	if (steam.sourceModsDir().empty()) {
		if (error) *error = QObject::tr("Steam's sourcemods folder was not found.");
		return {};
	}
	std::vector<std::filesystem::path> files;
	for (const QString& map : maps) files.push_back(toPath(map));
	const auto created = createMod(steam.sourceModsDir(), mod, files);
	if (!created.error.empty()) {
		if (error) *error = QString::fromStdString(created.error);
		return {};
	}
	// The features chosen up front: kept in the mod (autoexec runs them once the add-on is there).
	if (featuresOff) {
		saveFeatureChoice(created.folder, *featuresOff, false);
		saveEnhancementSettings(created.folder, withFeatureEffects({}, *featuresOff));
	}
	// Find it like every other mod and open it.
	editor->discover(fromPath(steam.root()));
	const QString key = QStringLiteral("sourcemods/") + QString::fromStdString(mod.folder);
	QString openError;
	if (!editor->openGame(key, &openError)) {
		if (error) *error = QObject::tr("The mod was created in %1, but could not be opened: %2").arg(fromPath(created.folder), openError);
		return {};
	}
	return key;
}

bool runNewModDialog(Editor* editor, QWidget* parent) {
	NewModDialog dialog(editor, parent);
	if (dialog.exec() != QDialog::Accepted) return false;
	QString error;
	const QString key = createModAndSwitch(editor, dialog.modTemplate(), dialog.mapsToCopy(), &error, dialog.featuresOff());
	if (key.isEmpty()) {
		QMessageBox::warning(parent, QObject::tr("New mod"), error);
		return false;
	}
	QString note = QObject::tr("The mod \"%1\" is ready and open. Start Steam once so it shows up in the library.")
	                   .arg(QString::fromStdString(dialog.modTemplate().title));
	if (dialog.modTemplate().base == ModBase::Mapbase) {
		note += QObject::tr("\n\nMapbase: put Mapbase's game code (bin/client.dll and server.dll, or your own build) into the mod's bin folder.");
	}
	QMessageBox::information(parent, QObject::tr("New mod"), note);
	return true;
}

NewModDialog::NewModDialog(Editor* editor, QWidget* parent) : QDialog(parent), m_editor(editor) {
	setWindowTitle(tr("New mod"));
	setObjectName("newModDialog");
	setMinimumWidth(520);
	auto* v = new QVBoxLayout(this);
	auto* intro = new QLabel(tr("A Source mod of your own in Steam's sourcemods folder: its own name in the Steam library, its own "
	                            "maps, materials and sounds. The chosen maps are copied into its map folder."));
	intro->setWordWrap(true);
	intro->setProperty("role", "description");
	v->addWidget(intro);

	auto* form = new QFormLayout;
	m_title = new QLineEdit;
	m_title->setObjectName("newModTitle");
	m_title->setPlaceholderText(tr("My Mod"));
	m_folder = new QLineEdit;
	m_folder->setObjectName("newModFolder");
	m_folder->setPlaceholderText(tr("mymod"));
	m_base = new QComboBox;
	m_base->setObjectName("newModBase");
	m_base->addItem(tr("Half-Life 2 singleplayer (Source SDK Base 2013 Singleplayer)"), int(ModBase::Singleplayer));
	const auto& steam = editor->discovery().steam;
	if (!mapbaseMounts(steam.sourceModsDir()).empty()) {
		m_base->addItem(tr("Half-Life 2 singleplayer with Mapbase (shared content found)"), int(ModBase::Mapbase));
		m_base->setCurrentIndex(1);
	}
	m_base->addItem(tr("Multiplayer (Source SDK Base 2013 Multiplayer)"), int(ModBase::Multiplayer));
	form->addRow(tr("Title"), m_title);
	form->addRow(tr("Folder"), m_folder);
	form->addRow(tr("Built on"), m_base);
	v->addLayout(form);

	// Maps to take along: the open one (checked) and the others of the current game's map folder.
	v->addWidget(new QLabel(tr("Maps to copy into the new mod")));
	m_maps = new QListWidget;
	m_maps->setObjectName("newModMaps");
	QStringList paths;
	if (!editor->filePath().isEmpty()) paths << editor->filePath();
	if (editor->game()) {
		const QString folder = editor->gameSettings().mapDir;
		QDirIterator it(folder, {QStringLiteral("*.vmf")}, QDir::Files, QDirIterator::Subdirectories);
		while (it.hasNext() && paths.size() < 500) {
			const QString path = it.next();
			if (!paths.contains(path) && !path.endsWith(QLatin1String("_cordon.vmf"))) paths << path;
		}
	}
	for (const QString& path : paths) {
		auto* item = new QListWidgetItem(QFileInfo(path).fileName());
		item->setData(Qt::UserRole, path);
		item->setToolTip(QDir::toNativeSeparators(path));
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		item->setCheckState(path == editor->filePath() ? Qt::Checked : Qt::Unchecked);
		m_maps->addItem(item);
	}
	v->addWidget(m_maps, 1);

	// mapbase-fidelity's features, chosen right away (all on); folded away until wanted.
	auto* featureRow = new QHBoxLayout;
	auto* featureNote = new QLabel(tr("Features of mapbase-fidelity: all on. They are used once the add-on is installed in the mod "
	                                  "(Project > Mod code); change them any time under Project > Features."));
	featureNote->setProperty("role", "description");
	featureNote->setWordWrap(true);
	auto* featureToggle = new QPushButton(tr("Choose features ..."));
	featureToggle->setObjectName("newModChooseFeatures");
	featureToggle->setCheckable(true);
	featureRow->addWidget(featureNote, 1);
	featureRow->addWidget(featureToggle);
	v->addLayout(featureRow);
	m_features = new FeatureChecklist(true);
	m_features->setObjectName("newModFeatures");
	m_features->setVisible(false);
	v->addWidget(m_features);
	connect(featureToggle, &QPushButton::toggled, m_features, &QWidget::setVisible);
	connect(m_features, &FeatureChecklist::toggled, this, [this, featureNote] {
		const auto off = m_features->off();
		featureNote->setText(off.empty() ? tr("Features of mapbase-fidelity: all on.")
		                                 : tr("Features of mapbase-fidelity: %n switched off.", nullptr, int(off.size())));
	});

	m_where = new QLabel;
	m_where->setProperty("role", "description");
	m_where->setWordWrap(true);
	v->addWidget(m_where);
	m_problem = new QLabel;
	m_problem->setProperty("role", "warning");
	m_problem->setWordWrap(true);
	v->addWidget(m_problem);

	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
	m_create = buttons->addButton(tr("Create mod"), QDialogButtonBox::AcceptRole);
	m_create->setProperty("accent", true);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	v->addWidget(buttons);

	connect(m_title, &QLineEdit::textChanged, this, [this](const QString& title) {
		if (!m_folderEdited) {
			const QSignalBlocker block(m_folder);
			m_folder->setText(QString::fromStdString(suggestModFolder(title.toStdString())));
		}
		validate();
	});
	connect(m_folder, &QLineEdit::textEdited, this, [this] {
		m_folderEdited = true;
		validate();
	});
	validate();
}

ModTemplate NewModDialog::modTemplate() const {
	ModTemplate mod;
	mod.title = m_title->text().trimmed().toStdString();
	mod.folder = m_folder->text().trimmed().toStdString();
	mod.base = static_cast<ModBase>(m_base->currentData().toInt());
	if (mod.base == ModBase::Mapbase) mod.extraMounts = mapbaseMounts(m_editor->discovery().steam.sourceModsDir());
	return mod;
}

std::set<std::string> NewModDialog::featuresOff() const {
	return m_features->off();
}

QStringList NewModDialog::mapsToCopy() const {
	QStringList out;
	for (int i = 0; i < m_maps->count(); ++i) {
		if (m_maps->item(i)->checkState() == Qt::Checked) out << m_maps->item(i)->data(Qt::UserRole).toString();
	}
	return out;
}

void NewModDialog::validate() {
	const auto dir = m_editor->discovery().steam.sourceModsDir();
	const std::string folder = m_folder->text().trimmed().toStdString();
	QString problem;
	if (dir.empty()) {
		problem = tr("Steam's sourcemods folder was not found.");
	} else if (m_title->text().trimmed().isEmpty()) {
		problem = tr("Give the mod a title.");
	} else if (!isValidModFolder(folder)) {
		problem = tr("The folder name may only use letters, digits, _ and -.");
	} else if (std::filesystem::exists(dir / folder)) {
		problem = tr("There is already a mod folder called %1.").arg(QString::fromStdString(folder));
	}
	m_where->setText(dir.empty() ? QString() : tr("Will be created in %1").arg(QDir::toNativeSeparators(fromPath(dir / folder))));
	m_problem->setText(problem);
	m_create->setEnabled(problem.isEmpty());
}

} // namespace sme::app
