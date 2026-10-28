// mod_code_panel.cpp - Mod code tab of the Project page (see mod_code_panel.h).

#include "mod_code_panel.h"

#include "editor.h"
#include "source_control_panel.h"

#include <QCheckBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QFutureWatcher>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QSplitter>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <algorithm>
#include <iterator>

#include <sme/game.h>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace sme::app {

namespace {

#ifdef Q_OS_WIN
constexpr bool onWindows = true;
#else
constexpr bool onWindows = false;
#endif

QGroupBox* card(const QString& title) {
	auto* box = new QGroupBox(title.toUpper());
	box->setProperty("role", "section");
	return box;
}

QPushButton* button(const QString& text, const QString& name, const QString& tip) {
	auto* b = new QPushButton(text);
	b->setObjectName(name);
	b->setToolTip(tip);
	return b;
}

QCheckBox* checkBox(const QString& text, const QString& name, const QString& tip) {
	auto* c = new QCheckBox(text);
	c->setObjectName(name);
	c->setToolTip(tip);
	return c;
}

QString qs(std::string_view text) {
	return QString::fromUtf8(text.data(), qsizetype(text.size()));
}

/// The scripts' output: PowerShell writes in the console's code page on Windows, UTF-8 elsewhere.
QString decode(const QByteArray& bytes) {
#ifdef Q_OS_WIN
	if (bytes.isEmpty()) return {};
	const int length = MultiByteToWideChar(CP_OEMCP, 0, bytes.constData(), int(bytes.size()), nullptr, 0);
	QString text(length, Qt::Uninitialized);
	MultiByteToWideChar(CP_OEMCP, 0, bytes.constData(), int(bytes.size()), reinterpret_cast<wchar_t*>(text.data()), length);
	return text;
#else
	return QString::fromUtf8(bytes);
#endif
}

/// The steps shown in the list (Start is not one).
constexpr UpdateStep listedSteps[] = {UpdateStep::VisualStudio, UpdateStep::Projects, UpdateStep::Build, UpdateStep::Dlls,
                                      UpdateStep::Vrad,         UpdateStep::GameFiles, UpdateStep::Done};

QString actionName(ModCodeAction action) {
	switch (action) {
		case ModCodeAction::Update:
		case ModCodeAction::UpdateNoVrad: return ModCodePanel::tr("Update mod");
		case ModCodeAction::Deploy: return ModCodePanel::tr("Install game files");
		case ModCodeAction::Report: return ModCodePanel::tr("Make report");
		case ModCodeAction::RemoveGameFiles: return ModCodePanel::tr("Remove game files");
	}
	return {};
}

QString duration(qint64 ms) {
	const qint64 seconds = ms / 1000;
	return QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

} // namespace

QString reportHowTo() {
	return ModCodePanel::tr("Something looks wrong in the game (a wall, an effect, a sound)?\n"
	                        "1. In the game, stand where it happens, open the console and type: fidelity_report\n"
	                        "2. Close the game and press Make report: Mapsmith packs what it wrote into fidelity_report.zip\n"
	                        "   and opens its folder - send that zip.");
}

ModCodePanel::ModCodePanel(Editor* editor, QWidget* parent) : QWidget(parent), m_editor(editor) {
	setObjectName("modCode");
	auto* v = new QVBoxLayout(this);
	v->setContentsMargins(12, 10, 12, 10);
	v->setSpacing(10);

	// Not needed for the features: Project > Features installs them.
	m_bundleNote = new QLabel(tr("<b>Only for building the mod from its own source code.</b> The mod's features (ambient occlusion, "
	                             "light shafts, liquid in bottles ...) come with Mapsmith: Project &gt; Features installs and updates "
	                             "them - no compiler, no scripts. Make report works here either way."));
	m_bundleNote->setObjectName("modCodeBundleNote");
	m_bundleNote->setWordWrap(true);
	m_bundleNote->setProperty("role", "description");
	v->addWidget(m_bundleNote);

	// Where the source code is.
	auto* source = card(tr("Source code of the mod"));
	auto* sv = new QVBoxLayout(source);
	auto* folderRow = new QHBoxLayout;
	m_folderLabel = new QLabel;
	m_folderLabel->setObjectName("modCodeFolder");
	m_folderLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	m_folderLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	folderRow->addWidget(m_folderLabel, 1);
	auto* find = button(tr("Find"), "modCodeFind", tr("Look for the mod's source code next to the Source control folder and in the usual places"));
	auto* choose = button(tr("Choose folder ..."), "modCodeChoose", tr("The folder of the mod's source code (with fidelity and sp)"));
	auto* openFolder = button(tr("Open folder"), "modCodeOpenFolder", tr("Show the folder in the file manager"));
	folderRow->addWidget(find);
	folderRow->addWidget(choose);
	folderRow->addWidget(openFolder);
	sv->addLayout(folderRow);
	m_state = new QLabel;
	m_state->setObjectName("modCodeState");
	m_state->setProperty("role", "description");
	m_state->setWordWrap(true);
	sv->addWidget(m_state);
	v->addWidget(source);

	// What can be run.
	auto* run = card(tr("Build and install"));
	auto* rv = new QVBoxLayout(run);
	auto* actions = new QHBoxLayout;
	m_update = button(tr("Update mod"), "modCodeUpdate",
	                  tr("Build the code with Visual Studio 2022 and install everything into the mod: DLLs, VRAD with baked AO, game files"));
	m_update->setProperty("accent", true);
	m_deploy = button(tr("Install game files only"), "modCodeDeploy", tr("Copy shaders, cfg and generated materials into the mod (no build)"));
	m_report = button(tr("Make report"), "modCodeReport", reportHowTo());
	m_cancel = button(tr("Cancel"), "modCodeCancel", tr("Stop the running script and the build it started"));
	for (auto* b : {m_update, m_deploy, m_report}) actions->addWidget(b);
	actions->addStretch(1);
	actions->addWidget(m_cancel);
	rv->addLayout(actions);
	m_noVrad = checkBox(tr("Skip VRAD (faster; map compiles then bake no ambient occlusion)"), "modCodeNoVrad",
	                    tr("Update mod leaves VRAD out (update.ps1 -NoVrad)"));
	rv->addWidget(m_noVrad);
	m_platformNote = new QLabel;
	m_platformNote->setObjectName("modCodePlatform");
	m_platformNote->setProperty("role", "description");
	m_platformNote->setWordWrap(true);
	rv->addWidget(m_platformNote);
	v->addWidget(run);

	// Automatic: after a pull, at start.
	auto* automatic = card(tr("Automatic"));
	auto* av = new QVBoxLayout(automatic);
	m_autoAfterPull = checkBox(tr("Update the mod after every Pull that brings new code"), "modCodeAutoAfterPull",
	                           tr("Pull in Source control (or Pull & update here) runs Update mod by itself"));
	m_checkOnStart = checkBox(tr("Look for new commits when Mapsmith starts (git fetch)"), "modCodeCheckOnStart",
	                          tr("The Overview then says how many changes are waiting"));
	m_pullOnStart = checkBox(tr("Pull and update automatically when Mapsmith starts"), "modCodePullOnStart",
	                         tr("Gets the new commits and updates the mod when the mod is opened"));
	for (auto* c : {m_autoAfterPull, m_checkOnStart, m_pullOnStart}) av->addWidget(c);
	auto* newsRow = new QHBoxLayout;
	m_news = new QLabel;
	m_news->setObjectName("modCodeNews");
	m_news->setWordWrap(true);
	m_check = button(tr("Check now"), "modCodeCheck", tr("Ask the remote for new commits (git fetch; changes nothing here)"));
	m_pullUpdate = button(tr("Pull && update"), "modCodePullUpdate", tr("Get the new commits (git pull) and update the mod"));
	newsRow->addWidget(m_news, 1);
	newsRow->addWidget(m_check);
	newsRow->addWidget(m_pullUpdate);
	av->addLayout(newsRow);
	auto* network = new QLabel(tr("Mapsmith uses your git for this and only contacts the remote when one of these is on or you press a button."));
	network->setProperty("role", "description");
	network->setWordWrap(true);
	av->addWidget(network);
	v->addWidget(automatic);

	// The run: steps | summary, progress and log.
	auto* split = new QSplitter(Qt::Horizontal);
	split->setChildrenCollapsible(false);
	m_steps = new QListWidget;
	m_steps->setObjectName("modCodeSteps");
	m_steps->setSelectionMode(QAbstractItemView::NoSelection);
	m_steps->setFocusPolicy(Qt::NoFocus);
	for (const auto step : listedSteps) m_steps->addItem(qs(updateStepName(step)));
	m_steps->setMinimumWidth(200);
	split->addWidget(m_steps);
	auto* right = new QWidget;
	auto* rightLayout = new QVBoxLayout(right);
	rightLayout->setContentsMargins(0, 0, 0, 0);
	m_progress = new QProgressBar;
	m_progress->setObjectName("modCodeProgress");
	m_progress->setTextVisible(false);
	m_progress->setRange(0, int(listedSteps[std::size(listedSteps) - 1]));
	m_progress->setValue(0);
	rightLayout->addWidget(m_progress);
	m_summary = new QLabel;
	m_summary->setObjectName("modCodeSummary");
	m_summary->setWordWrap(true);
	m_summary->setTextFormat(Qt::RichText);
	m_summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
	rightLayout->addWidget(m_summary);
	m_log = new QPlainTextEdit;
	m_log->setObjectName("modCodeLog");
	m_log->setReadOnly(true);
	m_log->setMaximumBlockCount(5000);
	m_log->setLineWrapMode(QPlainTextEdit::NoWrap);
	rightLayout->addWidget(m_log, 1);
	split->addWidget(right);
	split->setStretchFactor(1, 1);
	v->addWidget(split, 1);

	// The line on the Overview tab.
	auto* banner = new QFrame;
	banner->setObjectName("modCodeBanner");
	banner->setFrameShape(QFrame::StyledPanel);
	auto* bh = new QHBoxLayout(banner);
	bh->setContentsMargins(10, 6, 10, 6);
	m_bannerText = new QLabel;
	m_bannerText->setObjectName("modCodeBannerText");
	m_bannerText->setWordWrap(true);
	bh->addWidget(m_bannerText, 1);
	m_bannerPull = button(tr("Pull && update"), "modCodeBannerPull", tr("Get the new commits and update the mod"));
	m_bannerUpdate = button(tr("Update mod"), "modCodeBannerUpdate", tr("Build and install the mod's code"));
	auto* show = button(tr("Show"), "modCodeBannerShow", tr("Open Project > Mod code"));
	bh->addWidget(m_bannerPull);
	bh->addWidget(m_bannerUpdate);
	bh->addWidget(show);
	banner->setVisible(false);
	m_banner = banner;

	connect(find, &QPushButton::clicked, this, [this] {
		m_root.clear();
		showRepository();
		findRepository();
	});
	connect(choose, &QPushButton::clicked, this, [this] {
		const QString folder = QFileDialog::getExistingDirectory(this, tr("Source code of the mod"), m_root);
		if (!folder.isEmpty()) setRepository(folder);
	});
	connect(openFolder, &QPushButton::clicked, this, [this] {
		if (!m_root.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(m_root));
	});
	connect(m_update, &QPushButton::clicked, this, [this] { start(ModCodeAction::Update); });
	connect(m_deploy, &QPushButton::clicked, this, [this] { start(ModCodeAction::Deploy); });
	connect(m_report, &QPushButton::clicked, this, [this] { start(ModCodeAction::Report); });
	connect(m_cancel, &QPushButton::clicked, this, &ModCodePanel::cancel);
	connect(m_check, &QPushButton::clicked, this, &ModCodePanel::checkForChanges);
	connect(m_pullUpdate, &QPushButton::clicked, this, &ModCodePanel::pullAndUpdate);
	connect(m_bannerPull, &QPushButton::clicked, this, &ModCodePanel::pullAndUpdate);
	connect(m_bannerUpdate, &QPushButton::clicked, this, [this] { start(ModCodeAction::Update); });
	connect(show, &QPushButton::clicked, this, &ModCodePanel::showRequested);
	const auto saveOption = [this](QCheckBox* box, const QString& name) {
		connect(box, &QCheckBox::toggled, this, [this, name](bool on) { setOption(name, on); });
	};
	saveOption(m_noVrad, "noVrad");
	saveOption(m_autoAfterPull, "autoAfterPull");
	saveOption(m_checkOnStart, "checkOnStart");
	saveOption(m_pullOnStart, "pullOnStart");
	connect(editor, &Editor::gameChanged, this, &ModCodePanel::loadForGame);
	loadForGame();
}

ModCodePanel::~ModCodePanel() {
	// A running build goes on by itself otherwise; the banner may not be in a page yet.
	for (auto* process : findChildren<QProcess*>(Qt::FindDirectChildrenOnly)) {
		process->disconnect(this);
		if (process->state() != QProcess::NotRunning) {
			process->kill();
			process->waitForFinished(2000);
		}
	}
	if (m_banner && !m_banner->parent()) delete m_banner;
}

void ModCodePanel::setCommandFactory(CommandFactory factory) {
	m_factory = std::move(factory);
	updateButtons();
}

void ModCodePanel::setFeatureBundle(std::optional<FeatureBundle> bundle, std::function<void()> install) {
	m_bundle = std::move(bundle);
	m_installBundle = std::move(install);
	showRepository();
}

bool ModCodePanel::hasCheckout() const {
	return modCodeScripts(toPath(m_root)).has_value();
}

std::optional<ModCodeScripts> ModCodePanel::scripts() const {
	if (auto checkout = modCodeScripts(toPath(m_root))) return checkout;
	if (m_bundle) return bundledScripts(*m_bundle);
	return std::nullopt;
}

QString ModCodePanel::settingsKey(const QString& name) const {
	return QStringLiteral("games/%1/modCode/%2").arg(QString(m_gameKey).replace('/', '_'), name);
}

bool ModCodePanel::option(const QString& name) const {
	return !m_gameKey.isEmpty() && QSettings().value(settingsKey(name), false).toBool();
}

void ModCodePanel::setOption(const QString& name, bool on) {
	if (!m_gameKey.isEmpty()) QSettings().setValue(settingsKey(name), on);
}

void ModCodePanel::loadForGame() {
	const QString key = m_editor->gameKey();
	if (key == m_gameKey) {   // the same mod opened again (e.g. after an update): keep the run
		showRepository();
		return;
	}
	m_gameKey = key;
	m_root.clear();
	m_newChanges = 0;
	m_changedSinceUpdate = false;
	++m_searchGeneration;
	for (const auto& [box, name] : {std::pair{m_noVrad, "noVrad"}, {m_autoAfterPull, "autoAfterPull"}, {m_checkOnStart, "checkOnStart"},
	                                {m_pullOnStart, "pullOnStart"}}) {
		const QSignalBlocker block(box);
		box->setChecked(option(name));
	}
	if (!key.isEmpty()) {
		const QString saved = QSettings().value(settingsKey("folder")).toString();
		if (modCodeScripts(toPath(saved))) m_root = saved;
	}
	showRepository();
	if (m_gameKey.isEmpty()) return;
	if (m_root.isEmpty()) {
		findRepository();
		return;
	}
	runStartupCheck();
}

void ModCodePanel::runStartupCheck() {
	if (m_gameKey.isEmpty() || m_root.isEmpty() || !m_startChecked.insert(m_gameKey).second) return;
	if (option("pullOnStart")) {
		pullAndUpdate();
	} else if (option("checkOnStart")) {
		checkForChanges();
	} else {
		// No network: only whether the checkout moved on since the last update (a pull elsewhere).
		compareWithLastUpdate([this](bool changed) {
			m_changedSinceUpdate = changed;
			showRepository();
		});
	}
}

void ModCodePanel::findRepository() {
	const auto* entry = m_editor->gameEntry();
	if (!entry) return;
	std::vector<std::filesystem::path> roots;
	// The Source control tab's folder (often the checkout itself) and the folder it is in.
	const QString scm = QSettings().value(QStringLiteral("games/%1/repository").arg(QString(m_gameKey).replace('/', '_'))).toString();
	if (!scm.isEmpty()) {
		roots.push_back(toPath(scm));
		roots.push_back(toPath(QFileInfo(scm).absolutePath()));
	}
	std::vector<std::filesystem::path> drives;
	if (onWindows) {
		for (const auto& volume : QStorageInfo::mountedVolumes()) {
			if (volume.isValid() && volume.isReady() && !volume.isReadOnly()) drives.push_back(toPath(volume.rootPath()));
		}
	}
	const auto usual = modCodeSearchRoots(toPath(QDir::homePath()), drives);
	roots.insert(roots.end(), usual.begin(), usual.end());
	const std::string modName = QFileInfo(fromPath(entry->gameDir)).fileName().toStdString();
	m_state->setText(tr("Looking for the mod's source code ..."));
	auto* watcher = new QFutureWatcher<std::optional<std::filesystem::path>>(this);
	connect(watcher, &QFutureWatcher<std::optional<std::filesystem::path>>::finished, this,
	        [this, watcher, generation = ++m_searchGeneration] {
		        const auto found = watcher->result();
		        watcher->deleteLater();
		        if (generation != m_searchGeneration || !m_root.isEmpty()) return;   // another mod or a chosen folder since
		        if (found) {
			        setRepository(fromPath(*found));
			        log(tr("Found the mod's source code: %1").arg(QDir::toNativeSeparators(m_root)));
			        runStartupCheck();
		        } else {
			        showRepository();
		        }
	        });
	watcher->setFuture(QtConcurrent::run([modName, roots] { return findModCode(modName, roots); }));
}

void ModCodePanel::setRepository(const QString& folder) {
	++m_searchGeneration;   // a search still running does not overwrite this
	m_root = QDir::cleanPath(folder);
	if (!m_gameKey.isEmpty()) QSettings().setValue(settingsKey("folder"), m_root);
	m_newChanges = 0;
	m_changedSinceUpdate = false;
	showRepository();
}

void ModCodePanel::showRepository() {
	const auto scripts = modCodeScripts(toPath(m_root));
	m_folderLabel->setText(m_root.isEmpty() ? tr("No source code chosen") : QDir::toNativeSeparators(m_root));
	m_folderLabel->setToolTip(QDir::toNativeSeparators(m_root));
	if (m_gameKey.isEmpty()) {
		m_state->setText(tr("Open a mod first (the game button at the top)."));
	} else if (m_root.isEmpty()) {
		m_state->setText(m_bundle ? tr("No source code chosen - not needed: the features come with Mapsmith (Project > Features). "
		                               "Only if you build the mod yourself, press Choose folder ... and pick the folder with "
		                               "fidelity and sp in it.")
		                          : tr("No source code found for this mod. If the mod is built from its own code (Mapbase with "
		                               "mapbase-fidelity), press Choose folder ... and pick the folder with fidelity and sp in it."));
	} else if (!scripts) {
		m_state->setText(tr("This folder has no fidelity/update.ps1 - choose the folder of the mod's source code."));
	} else {
		QStringList have{QStringLiteral("update")};
		if (!scripts->deploy.empty()) have << QStringLiteral("deploy");
		if (!scripts->report.empty()) have << QStringLiteral("report");
		const QString last = QSettings().value(settingsKey("updatedAt")).toString();
		m_state->setText(tr("Scripts found: %1. %2").arg(have.join(QStringLiteral(", ")),
		                                                 last.isEmpty() ? tr("Not updated from Mapsmith yet.") : tr("Last update: %1.").arg(last)));
	}
	if (m_newChanges > 0) {
		m_news->setText(tr("%n new change(s) on the remote.", nullptr, m_newChanges));
	} else if (m_changedSinceUpdate) {
		m_news->setText(tr("The code changed since the last update."));
	} else {
		m_news->clear();
	}
	// Without source code "Update mod" installs the features Mapsmith brings.
	const bool fromBundle = !scripts && m_bundle && m_installBundle;
	m_update->setText(fromBundle ? tr("Install features") : tr("Update mod"));
	m_update->setToolTip(fromBundle ? tr("Install the features Mapsmith brings into the mod (as Project > Features does)")
	                                : tr("Build the code with Visual Studio 2022 and install everything into the mod: DLLs, VRAD with baked AO, game files"));
	m_noVrad->setVisible(!fromBundle);
	m_platformNote->setVisible(!onWindows && !m_factory && !fromBundle);
	m_platformNote->setText(QStandardPaths::findExecutable(QStringLiteral("pwsh")).isEmpty()
	                            ? tr("Building the mod's code needs Windows with Visual Studio 2022. Installing the game files and "
	                                 "making reports here needs PowerShell 7 (pwsh), which is not installed.")
	                            : tr("Building the mod's code needs Windows with Visual Studio 2022. Here you can install the game "
	                                 "files and make reports."));
	updateButtons();
	updateBanner();
}

bool ModCodePanel::canRun(ModCodeAction action, QString* why) const {
	const auto fail = [why](const QString& text) {
		if (why) *why = text;
		return false;
	};
	if (running()) return fail(tr("A script is running."));
	if (!m_editor->gameEntry()) return fail(tr("Open the mod first."));
	const bool update = action == ModCodeAction::Update || action == ModCodeAction::UpdateNoVrad;
	if (update && !hasCheckout() && m_bundle && m_installBundle) return true;   // installs the features Mapsmith brings
	const auto scripts = this->scripts();
	if (!scripts || (update && scripts->update.empty())) return fail(tr("Choose the mod's source code first (Project > Build from source)."));
	if (((action == ModCodeAction::Deploy || action == ModCodeAction::RemoveGameFiles) && scripts->deploy.empty()) ||
	    (action == ModCodeAction::Report && scripts->report.empty())) {
		return fail(tr("This source code has no script for that."));
	}
	if (m_factory) return true;
	if (modCodeNeedsWindows(action) && !onWindows) {
		return fail(tr("Building the mod's code needs Windows with Visual Studio 2022."));
	}
	if (!onWindows && QStandardPaths::findExecutable(QStringLiteral("pwsh")).isEmpty()) {
		return fail(tr("PowerShell 7 (pwsh) is not installed."));
	}
	return true;
}

void ModCodePanel::updateButtons() {
	const bool busy = running() || m_gitProcess;
	for (const auto& [b, action] : {std::pair{m_update, ModCodeAction::Update}, {m_deploy, ModCodeAction::Deploy}, {m_report, ModCodeAction::Report}}) {
		QString why;
		b->setEnabled(!busy && canRun(action, &why));
		b->setProperty("why", why);
	}
	m_bannerUpdate->setEnabled(!busy && canRun(ModCodeAction::Update));
	m_cancel->setVisible(running());
	const bool repo = modCodeScripts(toPath(m_root)).has_value() && !SourceControlPanel::gitProgram().isEmpty();
	m_check->setEnabled(repo && !busy);
	m_pullUpdate->setEnabled(repo && !busy);
	m_bannerPull->setEnabled(repo && !busy);
}

void ModCodePanel::updateBanner() {
	QString text;
	bool pull = false, update = false;
	const bool isUpdate = m_action == ModCodeAction::Update || m_action == ModCodeAction::UpdateNoVrad;
	if (running()) {
		text = isUpdate ? tr("Updating the mod's code: %1 (%2/%3) ...")
		                      .arg(qs(updateStepName(m_run.step())))
		                      .arg(std::max(1, int(m_run.step())))
		                      .arg(int(UpdateStep::Done))
		                : tr("%1 is running ...").arg(actionName(m_action));
	} else if (m_hasRun && !m_run.succeeded() && isUpdate) {
		text = tr("The last update of the mod failed: %1").arg(qs(m_run.advice()));
		update = true;
	} else if (m_newChanges > 0) {
		text = tr("%n new change(s) in the mod's code.", nullptr, m_newChanges);
		pull = true;
	} else if (m_changedSinceUpdate) {
		text = tr("The mod's code changed since its last update.");
		update = true;
	}
	m_bannerText->setText(text);
	m_bannerPull->setVisible(pull);
	m_bannerUpdate->setVisible(update);
	if (m_banner->parent()) m_banner->setVisible(!text.isEmpty());
}

void ModCodePanel::log(const QString& text, const QString& color) {
	if (text.isEmpty()) return;
	if (color.isEmpty()) {
		m_log->appendPlainText(text);
	} else {
		m_log->appendHtml(QStringLiteral("<span style='color:%1; white-space:pre'>%2</span>").arg(color, text.toHtmlEscaped()));
	}
	m_log->verticalScrollBar()->setValue(m_log->verticalScrollBar()->maximum());
}

bool ModCodePanel::start(ModCodeAction action) {
	if (action == ModCodeAction::Update && m_noVrad->isChecked()) action = ModCodeAction::UpdateNoVrad;
	QString why;
	if (!canRun(action, &why)) {
		if (!running()) m_summary->setText(QStringLiteral("<span style='color:#ff8a7a'>%1</span>").arg(why.toHtmlEscaped()));
		return false;
	}
	if ((action == ModCodeAction::Update || action == ModCodeAction::UpdateNoVrad) && !hasCheckout()) {
		m_installBundle();   // no source code: the features Mapsmith brings (they run Install game files after)
		return true;
	}
	const auto scripts = this->scripts();
	const auto modDir = m_editor->gameEntry()->gameDir;
	const auto command = m_factory ? m_factory(action, *scripts, modDir) : modCodeCommand(*scripts, action, modDir, onWindows);
	if (!command) {
		m_summary->setText(tr("This source code has no script for that."));
		return false;
	}
	m_action = action;
	m_run = ScriptRun();
	m_hasRun = true;
	m_cancelled = false;
	m_updateQueued = false;
	m_pending[0].clear();
	m_pending[1].clear();
	m_log->clear();
	QStringList args;
	for (const auto& arg : command->args) args << QString::fromStdString(arg);
	log(QStringLiteral("> %1 %2").arg(QString::fromStdString(command->program), args.join(' ')), QStringLiteral("#8fa0b0"));

	auto* process = new QProcess(this);
	m_process = process;
	process->setProgram(QString::fromStdString(command->program));
	process->setArguments(args);
	process->setWorkingDirectory(fromPath(command->workingDir));
#ifdef Q_OS_WIN
	// No console window of its own (the output comes here).
	process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* arguments) { arguments->flags |= CREATE_NO_WINDOW; });
#endif
	process->setStandardInputFile(QProcess::nullDevice());   // nothing can wait for input
	connect(process, &QProcess::readyReadStandardOutput, this, [this] { readOutput(false); });
	connect(process, &QProcess::readyReadStandardError, this, [this] { readOutput(true); });
	connect(process, &QProcess::finished, this, [this, process](int code, QProcess::ExitStatus status) {
		if (process != m_process) return;
		processEnded(status == QProcess::NormalExit && !m_cancelled ? code : -1);
	});
	connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
		if (error != QProcess::FailedToStart || process != m_process) return;
		log(tr("Could not start %1: %2").arg(process->program(), process->errorString()), QStringLiteral("#ff8a7a"));
		m_run.feed(onWindows ? "  FAILED   Windows PowerShell could not be started." : "  FAILED   PowerShell 7 (pwsh) could not be started - install it to run the scripts here.");
		processEnded(1);
	});
	m_clock.start();
	process->start();
	showRun();
	updateButtons();
	updateBanner();
	return true;
}

void ModCodePanel::cancel() {
	if (!m_process) return;
	m_cancelled = true;
	log(tr("Stopping ..."), QStringLiteral("#ffc070"));
#ifdef Q_OS_WIN
	// The whole tree: PowerShell, MSBuild and the compilers it started.
	QProcess::startDetached(QStringLiteral("taskkill"), {"/PID", QString::number(m_process->processId()), "/T", "/F"});
#endif
	m_process->kill();
}

void ModCodePanel::readOutput(bool errorChannel) {
	if (!m_process) return;
	QByteArray& pending = m_pending[errorChannel ? 1 : 0];
	pending += errorChannel ? m_process->readAllStandardError() : m_process->readAllStandardOutput();
	qsizetype end;
	while ((end = pending.indexOf('\n')) >= 0) {
		QByteArray line = pending.left(end);
		pending.remove(0, end + 1);
		if (line.endsWith('\r')) line.chop(1);
		handleLine(decode(line), errorChannel);
	}
	showRun();
	updateBanner();
}

void ModCodePanel::handleLine(const QString& line, bool errorChannel) {
	const std::string text = line.toStdString();
	if (errorChannel) {
		m_run.feedError(text);
		log(line, QStringLiteral("#ff8a7a"));
		return;
	}
	m_run.feed(text);
	switch (classifyScriptLine(text).kind) {
		case ScriptLine::Kind::Failed:
		case ScriptLine::Kind::Error: log(line, QStringLiteral("#ff8a7a")); break;
		case ScriptLine::Kind::Warning:
		case ScriptLine::Kind::Skipped: log(line, QStringLiteral("#ffc070")); break;
		case ScriptLine::Kind::Ok:
		case ScriptLine::Kind::Success: log(line, QStringLiteral("#8fdc9a")); break;
		case ScriptLine::Kind::Info: log(line); break;
	}
}

void ModCodePanel::processEnded(int exitCode) {
	if (!m_process) return;
	QProcess* process = m_process;
	if (process->state() != QProcess::NotRunning) {
		process->disconnect(this);
		process->kill();
	} else {
		readOutput(false);
		readOutput(true);
	}
	for (int channel = 0; channel < 2; ++channel) {
		if (!m_pending[channel].isEmpty()) handleLine(decode(m_pending[channel]), channel == 1);
		m_pending[channel].clear();
	}
	process->deleteLater();
	m_process = nullptr;
	m_run.finish(exitCode);
	const bool ok = m_run.succeeded();
	const bool isUpdate = m_action == ModCodeAction::Update || m_action == ModCodeAction::UpdateNoVrad;
	log(ok ? tr("Finished in %1.").arg(duration(m_clock.elapsed())) : tr("Stopped after %1 (exit code %2).").arg(duration(m_clock.elapsed())).arg(exitCode),
	    ok ? QStringLiteral("#8fdc9a") : QStringLiteral("#ff8a7a"));
	if (ok && isUpdate) {
		QSettings().setValue(settingsKey("updatedAt"), QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")));
		m_changedSinceUpdate = false;
		// Which commit the mod is built from now: a later pull of new code updates again.
		if (!SourceControlPanel::gitProgram().isEmpty()) {
			git({"rev-parse", "HEAD"}, [this](int code, const QString& out) {
				if (code == 0) QSettings().setValue(settingsKey("updatedHead"), out.trimmed());
			});
		}
	}
	if (ok && m_action == ModCodeAction::Report) {
		// Where the zips are: that folder opens, so they can be sent right away.
		const QString folder = m_run.outputs().empty() ? fromPath(m_editor->gameEntry() ? m_editor->gameEntry()->gameDir : std::filesystem::path())
		                                               : QFileInfo(QString::fromStdString(m_run.outputs().front())).absolutePath();
		if (!folder.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
	}
	showRun();
	showRepository();
	emit finished(ok);
	if (ok && (isUpdate || m_action == ModCodeAction::Deploy)) emit modUpdated();
	if (m_updateQueued) {
		m_updateQueued = false;
		start(ModCodeAction::Update);
	}
}

void ModCodePanel::showRun() {
	const bool isUpdate = m_action == ModCodeAction::Update || m_action == ModCodeAction::UpdateNoVrad;
	m_steps->setVisible(isUpdate || !m_hasRun);
	const UpdateStep current = m_run.step();
	const bool failed = m_run.finished() && !m_run.succeeded();
	const bool vradSkipped = std::any_of(m_run.skipped().begin(), m_run.skipped().end(),
	                                     [](const std::string& line) { return line.rfind("VRAD", 0) == 0; });
	for (int i = 0; i < m_steps->count(); ++i) {
		const UpdateStep step = listedSteps[i];
		QString mark = QStringLiteral("○");
		QColor color(140, 140, 140);
		if (m_hasRun && isUpdate) {
			if (step < current || (step == current && m_run.succeeded())) {
				const bool skipped = step == UpdateStep::Vrad && vradSkipped;
				mark = skipped ? QStringLiteral("–") : QStringLiteral("✓");
				color = skipped ? QColor(240, 180, 70) : QColor(92, 207, 122);
			} else if (step == current || (current == UpdateStep::Start && step == UpdateStep::VisualStudio)) {
				mark = failed ? QStringLiteral("✗") : QStringLiteral("▶");
				color = failed ? QColor(235, 100, 90) : QColor(110, 170, 240);
			}
		}
		auto* item = m_steps->item(i);
		item->setText(mark + QStringLiteral("  ") + qs(updateStepName(step)));
		item->setForeground(color);
		item->setData(Qt::UserRole, mark);
	}
	if (!m_hasRun) {
		m_progress->setRange(0, int(UpdateStep::Done));
		m_progress->setValue(0);
		m_summary->setText(tr("Update mod does what update.bat does - in the background, without a console window."));
		return;
	}
	if (running()) {
		if (isUpdate) {
			m_progress->setRange(0, int(UpdateStep::Done));
			m_progress->setValue(int(current));
			m_summary->setText(tr("<b>%1</b> (%2/%3) - %4")
			                       .arg(qs(updateStepName(current == UpdateStep::Start ? UpdateStep::VisualStudio : current)).toHtmlEscaped())
			                       .arg(std::max(1, int(current)))
			                       .arg(int(UpdateStep::Done))
			                       .arg(duration(m_clock.elapsed())));
		} else {
			m_progress->setRange(0, 0);   // no steps: just busy
			m_summary->setText(tr("<b>%1</b> is running ...").arg(actionName(m_action)));
		}
		return;
	}
	m_progress->setRange(0, int(UpdateStep::Done));
	m_progress->setValue(m_run.succeeded() ? int(UpdateStep::Done) : int(current));
	QString html;
	if (m_run.succeeded()) {
		switch (m_action) {
			case ModCodeAction::Update:
			case ModCodeAction::UpdateNoVrad:
				html = tr("<span style='color:#8fdc9a'><b>✓ The mod is up to date</b></span> (%1). Start the mod, or compile a map.")
				           .arg(duration(m_clock.elapsed()));
				break;
			case ModCodeAction::Deploy: html = tr("<span style='color:#8fdc9a'><b>✓ Game files installed into the mod.</b></span>"); break;
			case ModCodeAction::RemoveGameFiles:
				html = tr("<span style='color:#8fdc9a'><b>✓ The game files deploy.bat made are gone.</b></span>");
				break;
			case ModCodeAction::Report: {
				QStringList zips;
				for (const auto& zip : m_run.outputs()) zips << QDir::toNativeSeparators(QString::fromStdString(zip)).toHtmlEscaped();
				html = tr("<span style='color:#8fdc9a'><b>✓ Report packed.</b></span> Send %1 - its folder is open.")
				           .arg(zips.isEmpty() ? tr("the zip") : zips.join(QStringLiteral(", ")));
				break;
			}
		}
		for (const auto& line : m_run.skipped()) html += QStringLiteral("<br>") + tr("Skipped: %1").arg(qs(line).toHtmlEscaped());
	} else {
		html = isUpdate ? tr("<span style='color:#ff8a7a'><b>✗ The update stopped at \"%1\".</b></span>")
		                      .arg(qs(updateStepName(current == UpdateStep::Start ? UpdateStep::VisualStudio : current)).toHtmlEscaped())
		                : tr("<span style='color:#ff8a7a'><b>✗ %1 did not work.</b></span>").arg(actionName(m_action));
		html += QStringLiteral("<br>") + qs(m_run.advice()).toHtmlEscaped();
		if (!m_run.errors().empty()) {
			html += QStringLiteral("<br><code>");
			int shown = 0;
			for (const auto& line : m_run.errors()) {
				if (shown++ == 6) break;
				html += qs(line).toHtmlEscaped() + QStringLiteral("<br>");
			}
			html += QStringLiteral("</code>");
		}
	}
	m_summary->setText(html);
}

void ModCodePanel::git(const QStringList& args, std::function<void(int, const QString&)> done) {
	const QString program = SourceControlPanel::gitProgram();
	if (program.isEmpty() || m_root.isEmpty() || m_gitProcess) {
		if (done) done(-1, {});
		return;
	}
	auto* process = new QProcess(this);
	m_gitProcess = process;
	auto env = QProcessEnvironment::systemEnvironment();
	env.insert("GIT_TERMINAL_PROMPT", "0");   // never wait for a password on a hidden console
	env.insert("LC_ALL", "C");
	process->setProcessEnvironment(env);
	process->setWorkingDirectory(m_root);
	process->setStandardInputFile(QProcess::nullDevice());
	const bool network = args.value(0) == "fetch" || args.value(0) == "pull";
	if (network) log(QStringLiteral("> git %1").arg(args.join(' ')), QStringLiteral("#8fa0b0"));
	connect(process, &QProcess::finished, this, [this, process, done = std::move(done), network](int code, QProcess::ExitStatus status) {
		const QString out = QString::fromUtf8(process->readAllStandardOutput());
		const QString err = QString::fromUtf8(process->readAllStandardError()).trimmed();
		const int exit = status == QProcess::NormalExit ? code : -1;
		if (network) {
			if (!out.trimmed().isEmpty()) log(out.trimmed());
			if (!err.isEmpty()) log(err, exit == 0 ? QString() : QStringLiteral("#ff8a7a"));
		}
		process->deleteLater();
		m_gitProcess = nullptr;
		updateButtons();
		if (done) done(exit, out);
	});
	connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
		if (error != QProcess::FailedToStart) return;
		log(tr("Could not start git: %1").arg(process->errorString()), QStringLiteral("#ff8a7a"));
		process->deleteLater();
		m_gitProcess = nullptr;
		updateButtons();
	});
	process->start(program, QStringList{"-c", "color.ui=false"} + args);
	updateButtons();
}

void ModCodePanel::compareWithLastUpdate(std::function<void(bool)> done) {
	const QString updated = QSettings().value(settingsKey("updatedHead")).toString();
	git({"rev-parse", "HEAD"}, [updated, done = std::move(done)](int code, const QString& out) {
		// Never updated from Mapsmith: unknown, so not "changed" (the user may have built it in Visual Studio).
		done(code == 0 && !updated.isEmpty() && out.trimmed() != updated);
	});
}

void ModCodePanel::checkForChanges() {
	git({"fetch", "--prune"}, [this](int code, const QString&) {
		if (code != 0) {
			showRepository();
			return;
		}
		git({"rev-list", "--count", "HEAD..@{upstream}"}, [this](int code, const QString& out) {
			m_newChanges = code == 0 ? out.trimmed().toInt() : 0;
			log(m_newChanges > 0 ? tr("%n new change(s) on the remote.", nullptr, m_newChanges) : tr("No new changes on the remote."));
			compareWithLastUpdate([this](bool changed) {
				m_changedSinceUpdate = changed;
				showRepository();
			});
		});
	});
}

void ModCodePanel::pullAndUpdate() {
	git({"rev-parse", "HEAD"}, [this](int, const QString& before) {
		git({"pull", "--no-rebase"}, [this, before = before.trimmed()](int code, const QString&) {
			if (code != 0) {
				m_summary->setText(QStringLiteral("<span style='color:#ff8a7a'>%1</span>")
				                       .arg(tr("Pull did not work - the log says why (local changes in the way, or no network).").toHtmlEscaped()));
				showRepository();
				return;
			}
			m_newChanges = 0;
			git({"rev-parse", "HEAD"}, [this, before](int, const QString& after) {
				const QString updated = QSettings().value(settingsKey("updatedHead")).toString();
				if (after.trimmed() != before || updated.isEmpty() || updated != after.trimmed()) {
					if (running()) {
						m_updateQueued = true;
					} else {
						start(ModCodeAction::Update);
					}
				} else {
					log(tr("Already up to date - nothing to build."));
					showRepository();
				}
			});
		});
	});
}

void ModCodePanel::afterPull(const QString& root, bool changed) {
	if (!changed || !option("autoAfterPull") || m_root.isEmpty()) return;
	const QString pulled = QFileInfo(root).canonicalFilePath();
	if (pulled.isEmpty() || pulled != QFileInfo(m_root).canonicalFilePath()) return;
	log(tr("New code was pulled - updating the mod."));
	if (running()) {
		m_updateQueued = true;
	} else {
		start(ModCodeAction::Update);
	}
}

} // namespace sme::app
