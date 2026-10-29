// new_mod_dialog.cpp - "New mod ..." dialog (see new_mod_dialog.h).

#include "new_mod_dialog.h"

#include "editor.h"
#include "features_panel.h"

#include <QButtonGroup>
#include <QFrame>
#include <QRadioButton>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QDir>
#include <QDateTime>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QSettings>
#include <QPushButton>
#include <QVBoxLayout>

#include <sme/fidelity_effects.h>
#include <sme/game.h>
#include <sme/mod_features.h>
#include <sme/text.h>

#include <algorithm>

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
	// A Mapbase mod gets Mapsmith's features right away: the game code (Mapbase with the add-on),
	// shaders, settings - the mod runs without anything else to put into bin. The generated
	// materials follow when the Features tab sees the mod (deploy.ps1 runs there).
	if (mod.base == ModBase::Mapbase) {
		if (const auto bundle = readFeatureBundle(featureBundleDir())) {
			const QFileInfo vrad(editor->gameSettings().vrad);
			const auto installed = installFeatures(*bundle, created.folder, vrad.exists() ? toPath(vrad.absolutePath()) : std::filesystem::path());
			if (installed.ok) QSettings().setValue(QStringLiteral("features/deployPending/%1").arg(fromPath(created.folder)), true);
		}
		// Whatever is still missing in bin/ (no features in this copy of Mapsmith): Mapbase's own
		// game code and shaders from its game folder next to the mod - without game_shader_dx9.dll
		// the game would draw its SDK_* materials as wireframe.
		(void)copyMapbaseBin(steam.sourceModsDir(), created.folder);
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
	if (dialog.modTemplate().base == ModBase::Mapbase && editor->gameEntry()) {
		const auto modDir = editor->gameEntry()->gameDir;
		const auto bin = checkModBin(modDir, nullptr);
		QStringList missing;
		for (const auto& file : bin.missing) missing << QString::fromStdString(file);
		if (!missing.isEmpty()) {
			note += QObject::tr("\n\nMapbase: the mod's bin folder still lacks %1 - copy them from Mapbase (mapbase_episodic/bin) or use the "
			                    "Windows version of Mapsmith, which brings them.")
			            .arg(missing.join(QStringLiteral(", ")));
		} else if (readInstalledFeatures(modDir)) {
			note += QObject::tr("\n\nMapbase's game code with Mapsmith's features is installed - the mod runs as it is.");
		} else {
			note += QObject::tr("\n\nMapbase's game code and shaders are in the mod - it runs as it is.");
		}
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

	// First choice: what the mod is made from - as big, plain cards (Mapbase first: it is what
	// Mapsmith's features and most modern HL2 mods use).
	auto* baseTitle = new QLabel(tr("1. What should your mod be based on?"));
	baseTitle->setProperty("role", "heading");
	v->addWidget(baseTitle);
	const auto& steam = editor->discovery().steam;
	const bool hasMapbase = !mapbaseMounts(steam.sourceModsDir()).empty();
	m_base = new QButtonGroup(this);
	const auto card = [this, v](ModBase base, const QString& name, const QString& text, bool enabled) {
		auto* frame = new QFrame;
		frame->setObjectName(QStringLiteral("newModBaseCard%1").arg(int(base)));
		frame->setProperty("role", "card");
		auto* h = new QHBoxLayout(frame);
		h->setContentsMargins(10, 8, 10, 8);
		auto* radio = new QRadioButton(name);
		radio->setObjectName(QStringLiteral("newModBase%1").arg(int(base)));
		QFont font = radio->font();
		font.setBold(true);
		radio->setFont(font);
		radio->setEnabled(enabled);
		auto* about = new QLabel(text);
		about->setWordWrap(true);
		about->setProperty("role", "description");
		auto* column = new QVBoxLayout;
		column->addWidget(radio);
		column->addWidget(about);
		h->addLayout(column, 1);
		m_base->addButton(radio, int(base));
		v->addWidget(frame);
		return radio;
	};
	auto* mapbase = card(ModBase::Mapbase, tr("Half-Life 2 with Mapbase (recommended)"),
	                     hasMapbase ? tr("Half-Life 2 and its episodes plus Mapbase: many more entities and better lighting. Mapsmith's "
	                                     "features (better graphics and sound) are installed into the mod right away.")
	                                : tr("Needs Mapbase's shared content (the \"mapbase_shared\" folder in sourcemods), which was not "
	                                     "found. Install Mapbase from its ModDB page, then open this dialog again."),
	                     hasMapbase);
	auto* plain = card(ModBase::Singleplayer, tr("Half-Life 2 (plain)"),
	                   tr("Half-Life 2 and its episodes as Valve made them (Source SDK Base 2013 Singleplayer), without Mapbase."), true);
	card(ModBase::Multiplayer, tr("Multiplayer"), tr("Half-Life 2: Deathmatch content (Source SDK Base 2013 Multiplayer)."), true);
	(hasMapbase ? mapbase : plain)->setChecked(true);

	auto* nameTitle = new QLabel(tr("2. Name it"));
	nameTitle->setProperty("role", "heading");
	v->addWidget(nameTitle);
	auto* form = new QFormLayout;
	m_title = new QLineEdit;
	m_title->setObjectName("newModTitle");
	m_title->setPlaceholderText(tr("My Mod"));
	m_folder = new QLineEdit;
	m_folder->setObjectName("newModFolder");
	m_folder->setPlaceholderText(tr("mymod"));
	form->addRow(tr("Title (shown in Steam)"), m_title);
	form->addRow(tr("Folder name"), m_folder);
	v->addLayout(form);

	// Maps to take along: the open one (checked) and the others of the current game's map folder.
	auto* mapsTitle = new QLabel(tr("3. Maps to take along (optional)"));
	mapsTitle->setProperty("role", "heading");
	v->addWidget(mapsTitle);
	m_maps = new QListWidget;
	m_maps->setObjectName("newModMaps");
	QStringList paths;
	if (!editor->filePath().isEmpty()) paths << editor->filePath();
	if (editor->game()) {
		// Real maps only (no autosave copies or cordon exports), the newest first.
		QFileInfoList found;
		for (const auto& file : mapSourceFiles(editor->gameSettings().mapDir.toStdWString())) {
			found << QFileInfo(QString::fromStdWString(file.wstring()));
		}
		std::sort(found.begin(), found.end(), [](const QFileInfo& a, const QFileInfo& b) { return a.lastModified() > b.lastModified(); });
		for (const QFileInfo& info : std::as_const(found)) {
			const QString path = info.filePath();
			if (paths.size() >= 500) break;
			if (!paths.contains(path)) paths << path;
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
	                                  "(one click under Project > Features); change them any time there."));
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
	mod.base = static_cast<ModBase>(m_base->checkedId());
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
