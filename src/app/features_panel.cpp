// features_panel.cpp - Project > Features (see features_panel.h).

#include "features_panel.h"

#include "editor.h"
#include "enhancements.h"
#include "mod_code_panel.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QTimer>
#include <QVBoxLayout>

#include <sme/game.h>

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

QLabel* description(const QString& text) {
	auto* label = new QLabel(text);
	label->setProperty("role", "description");
	label->setWordWrap(true);
	return label;
}

QString qs(const char* text) {
	return QString::fromUtf8(text);
}

} // namespace

std::filesystem::path featureBundleDir() {
	const QString env = qEnvironmentVariable("SME_FEATURES_DIR");
	return toPath(env.isEmpty() ? QCoreApplication::applicationDirPath() + QStringLiteral("/features") : env);
}

// --- The checklist ----------------------------------------------------------------------------

FeatureChecklist::FeatureChecklist(bool compact, QWidget* parent) : QWidget(parent) {
	auto* v = new QVBoxLayout(this);
	v->setContentsMargins(0, 0, 0, 0);
	v->setSpacing(compact ? 6 : 10);
	const int columns = compact ? 3 : 2;
	for (const auto group : {FeatureGroup::Graphics, FeatureGroup::Effects, FeatureGroup::Sound, FeatureGroup::Materials}) {
		auto* box = new QGroupBox(QString::fromUtf8(featureGroupName(group).data(), int(featureGroupName(group).size())));
		box->setObjectName(QStringLiteral("featureGroup:%1").arg(box->title()));
		auto* grid = new QGridLayout(box);
		grid->setHorizontalSpacing(18);
		grid->setVerticalSpacing(compact ? 2 : 8);
		int index = 0;
		for (const auto& feature : modFeatures()) {
			if (feature.group != group) continue;
			const QString id = qs(feature.id);
			auto* check = new QCheckBox(qs(feature.title));
			check->setObjectName(QStringLiteral("feature:%1").arg(id));
			check->setChecked(true);
			QString tip = qs(feature.description);
			QStringList convars;
			for (const char* convar : feature.convars) convars << qs(convar);
			if (!convars.isEmpty()) tip += QStringLiteral("\n(%1)").arg(convars.join(QStringLiteral(", ")));
			check->setToolTip(tip);
			connect(check, &QCheckBox::clicked, this, [this, id](bool on) { emit toggled(id, on); });
			m_boxes[feature.id] = check;
			const int row = index / columns;
			const int column = index % columns;
			if (compact) {
				grid->addWidget(check, row, column);
			} else {
				auto* cell = new QWidget;
				auto* cv = new QVBoxLayout(cell);
				cv->setContentsMargins(0, 0, 0, 0);
				cv->setSpacing(0);
				cv->addWidget(check);
				auto* about = description(qs(feature.description));
				about->setContentsMargins(24, 0, 0, 0);   // under the text, not under the box
				cv->addWidget(about);
				grid->addWidget(cell, row, column, Qt::AlignTop);
			}
			++index;
		}
		for (int c = 0; c < columns; ++c) grid->setColumnStretch(c, 1);
		v->addWidget(box);
	}
}

void FeatureChecklist::setOff(const std::set<std::string>& off) {
	for (auto& [id, check] : m_boxes) {
		const QSignalBlocker block(check);
		check->setChecked(!off.count(id));
	}
}

std::set<std::string> FeatureChecklist::off() const {
	std::set<std::string> out;
	for (const auto& [id, check] : m_boxes) {
		if (!check->isChecked()) out.insert(id);
	}
	return out;
}

QCheckBox* FeatureChecklist::box(const QString& id) const {
	const auto it = m_boxes.find(id.toStdString());
	return it == m_boxes.end() ? nullptr : it->second;
}

// --- The tab ----------------------------------------------------------------------------------

FeaturesPanel::FeaturesPanel(Editor* editor, ModCodePanel* modCode, QWidget* parent)
    : QWidget(parent), m_editor(editor), m_modCode(modCode) {
	setObjectName("features");
	auto* outer = new QVBoxLayout(this);
	outer->setContentsMargins(0, 0, 0, 0);
	auto* scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	outer->addWidget(scroll);
	auto* content = new QWidget;
	scroll->setWidget(content);
	auto* v = new QVBoxLayout(content);
	v->setContentsMargins(12, 10, 12, 10);
	v->setSpacing(10);

	// Install: the features Mapsmith brings, into the mod.
	auto* install = card(tr("Install"));
	install->setObjectName("featuresInstallCard");
	auto* inv = new QVBoxLayout(install);
	inv->addWidget(description(tr("Mapsmith brings mapbase-fidelity ready-built: one click puts it into the mod - the game code, "
	                              "shaders, settings and VRAD with baked ambient occlusion. No compiler, no scripts. What the mod "
	                              "had before is kept and comes back when you remove the features.")));
	m_installState = new QLabel;
	m_installState->setObjectName("featuresInstallState");
	m_installState->setWordWrap(true);
	m_installState->setTextFormat(Qt::RichText);
	inv->addWidget(m_installState);
	auto* installRow = new QHBoxLayout;
	m_installButton = new QPushButton(tr("Install features"));
	m_installButton->setObjectName("featuresInstall");
	m_installButton->setProperty("accent", true);
	m_removeButton = new QPushButton(tr("Remove features"));
	m_removeButton->setObjectName("featuresRemove");
	m_removeButton->setToolTip(tr("Take the features out of the mod again: what was there before comes back"));
	installRow->addWidget(m_installButton);
	installRow->addWidget(m_removeButton);
	installRow->addStretch(1);
	inv->addLayout(installRow);
	m_autoUpdate = new QCheckBox(tr("Update the mod's features by themselves when a newer Mapsmith brings newer ones"));
	m_autoUpdate->setObjectName("featuresAutoUpdate");
	m_autoUpdate->setChecked(QSettings().value(QStringLiteral("features/autoUpdate"), true).toBool());
	inv->addWidget(m_autoUpdate);
	v->addWidget(install);

	auto* intro = card(tr("Features of this mod"));
	auto* iv = new QVBoxLayout(intro);
	iv->addWidget(description(tr("What mapbase-fidelity (the mod's graphics and sound add-on) does in this mod. Untick what the mod "
	                             "should not have - it is switched off for everyone who plays it. Each tick is saved at once; the "
	                             "game uses it from its next start.")));
	m_note = description({});
	m_note->setObjectName("featuresNote");
	iv->addWidget(m_note);
	m_status = new QLabel;
	m_status->setObjectName("featuresStatus");
	m_status->setWordWrap(true);
	iv->addWidget(m_status);
	auto* allRow = new QHBoxLayout;
	auto* allOn = new QPushButton(tr("All on"));
	allOn->setObjectName("featuresAllOn");
	allOn->setToolTip(tr("Tick every feature"));
	allRow->addWidget(allOn);
	allRow->addStretch(1);
	iv->addLayout(allRow);
	v->addWidget(intro);

	m_checklist = new FeatureChecklist(false);
	v->addWidget(m_checklist);

	// Fresh install.
	auto* fresh = card(tr("Fresh install"));
	auto* fv = new QVBoxLayout(fresh);
	fv->addWidget(description(tr("Starts the add-on over in this mod when something got mixed up: removes everything the add-on "
	                             "and Mapsmith made in the mod (generated materials, height maps, shaders, its cfg - never your "
	                             "own maps, materials or other files), writes the features above again and installs everything "
	                             "anew.")));
	auto* freshRow = new QHBoxLayout;
	m_freshButton = new QPushButton(tr("Fresh install ..."));
	m_freshButton->setObjectName("freshInstall");
	m_freshButton->setToolTip(tr("Show what would be removed first - nothing happens before you confirm"));
	freshRow->addWidget(m_freshButton);
	freshRow->addStretch(1);
	fv->addLayout(freshRow);
	// The confirmation, in the page (no dialog): what goes, and how to install again.
	m_freshConfirm = new QFrame;
	m_freshConfirm->setObjectName("freshInstallConfirm");
	static_cast<QFrame*>(m_freshConfirm)->setFrameShape(QFrame::StyledPanel);
	auto* cv = new QVBoxLayout(m_freshConfirm);
	m_freshCount = new QLabel;
	m_freshCount->setWordWrap(true);
	cv->addWidget(m_freshCount);
	m_freshList = new QListWidget;
	m_freshList->setObjectName("freshInstallFiles");
	m_freshList->setMaximumHeight(170);
	m_freshList->setSelectionMode(QAbstractItemView::NoSelection);
	cv->addWidget(m_freshList);
	m_freshBuildBox = new QCheckBox(tr("Build the game code from the mod's own source (needs Visual Studio 2022)"));
	m_freshBuildBox->setObjectName("freshInstallBuild");
	m_freshBuildBox->setToolTip(tr("Only when you build the mod yourself (Project > Build from source). Off: the features Mapsmith "
	                               "brings are installed again"));
	m_freshBuildBox->setChecked(false);
	cv->addWidget(m_freshBuildBox);
	auto* confirmRow = new QHBoxLayout;
	m_freshGo = new QPushButton(tr("Remove and install again"));
	m_freshGo->setObjectName("freshInstallGo");
	m_freshGo->setProperty("accent", true);
	auto* cancel = new QPushButton(tr("Cancel"));
	cancel->setObjectName("freshInstallCancel");
	confirmRow->addWidget(m_freshGo);
	confirmRow->addWidget(cancel);
	confirmRow->addStretch(1);
	cv->addLayout(confirmRow);
	m_freshConfirm->setVisible(false);
	fv->addWidget(m_freshConfirm);
	m_freshSteps = new QLabel;
	m_freshSteps->setObjectName("freshInstallSteps");
	m_freshSteps->setWordWrap(true);
	m_freshSteps->setTextFormat(Qt::RichText);
	m_freshSteps->setVisible(false);
	fv->addWidget(m_freshSteps);
	auto* details = new QPushButton(tr("Show details"));
	details->setObjectName("freshInstallDetails");
	auto* detailsRow = new QHBoxLayout;
	detailsRow->addWidget(details);
	detailsRow->addStretch(1);
	fv->addLayout(detailsRow);
	v->addWidget(fresh);
	v->addStretch(1);

	// The line on the Overview: the checklist, offered once per mod.
	auto* offer = new QFrame;
	offer->setObjectName("featuresOffer");
	offer->setFrameShape(QFrame::StyledPanel);
	auto* oh = new QHBoxLayout(offer);
	oh->setContentsMargins(10, 6, 10, 6);
	m_offerText = new QLabel;
	m_offerText->setObjectName("featuresOfferText");
	m_offerText->setWordWrap(true);
	oh->addWidget(m_offerText, 1);
	m_offerInstall = new QPushButton(tr("Install features"));
	m_offerInstall->setObjectName("featuresOfferInstall");
	m_offerInstall->setProperty("accent", true);
	auto* choose = new QPushButton(tr("Choose features"));
	choose->setObjectName("featuresOfferChoose");
	choose->setProperty("accent", true);
	auto* keep = new QPushButton(tr("Keep all on"));
	keep->setObjectName("featuresOfferKeep");
	oh->addWidget(m_offerInstall);
	oh->addWidget(choose);
	oh->addWidget(keep);
	m_offerChoose = choose;
	m_offerKeep = keep;
	offer->setVisible(false);
	m_offer = offer;

	connect(m_checklist, &FeatureChecklist::toggled, this, [this](const QString& id, bool on) { setOn(id, on); });
	connect(allOn, &QPushButton::clicked, this, [this] {
		m_off.clear();
		m_checklist->setOff(m_off);
		save();
	});
	connect(choose, &QPushButton::clicked, this, [this] {
		save();   // the choice is made (all on so far); the tab shows it
		emit showRequested();
	});
	connect(keep, &QPushButton::clicked, this, [this] { save(); });
	connect(m_installButton, &QPushButton::clicked, this, [this] { installNow(); });
	connect(m_offerInstall, &QPushButton::clicked, this, [this] { installNow(); });
	connect(m_removeButton, &QPushButton::clicked, this, [this] { removeNow(); });
	connect(m_autoUpdate, &QCheckBox::toggled, this, [](bool on) { QSettings().setValue(QStringLiteral("features/autoUpdate"), on); });
	connect(m_freshButton, &QPushButton::clicked, this, &FeaturesPanel::askFreshInstall);
	connect(cancel, &QPushButton::clicked, this, [this] { m_freshConfirm->setVisible(false); });
	connect(m_freshGo, &QPushButton::clicked, this, [this] { startFreshInstall(m_freshBuildBox->isChecked()); });
	connect(details, &QPushButton::clicked, this, &FeaturesPanel::modCodeRequested);
	connect(m_editor, &Editor::gameChanged, this, &FeaturesPanel::load);
	if (auto* enhancements = m_editor->enhancements()) {
		// Material effects switched on the Overview show here too.
		connect(enhancements, &GameEnhancements::changed, this, [this] {
			if (!m_editor->enhancements()->available()) return;
			const auto byEffects = featuresOffBySettings(m_editor->enhancements()->settings());
			for (const auto& feature : modFeatures()) {
				if (!feature.effect) continue;
				if (byEffects.count(feature.id)) m_off.insert(feature.id);
				else if (feature.convars.empty()) m_off.erase(feature.id);
			}
			m_checklist->setOff(m_off);
		});
	}
	if (m_modCode) connect(m_modCode, &ModCodePanel::finished, this, &FeaturesPanel::scriptFinished);
	reloadBundle();
}

void FeaturesPanel::reloadBundle() {
	std::string error;
	m_bundle = readFeatureBundle(featureBundleDir(), &error);
	m_bundleError = QString::fromStdString(error);
	// Without source code of its own, "Update mod" (Build from source, Source control, the Game
	// enhancements' notes) installs these.
	if (m_modCode) m_modCode->setFeatureBundle(m_bundle, [this] { installNow(); });
	load();
}

FeaturesPanel::InstallState FeaturesPanel::installState() const {
	if (!m_bundle) return InstallState::NoBundle;
	if (m_modDir.empty()) return InstallState::NoMod;
	const auto installed = readInstalledFeatures(m_modDir);
	if (!installed) return InstallState::NotInstalled;
	return featuresOutdated(*m_bundle, *installed) ? InstallState::Outdated : InstallState::Current;
}

std::filesystem::path FeaturesPanel::sdkBin() const {
	// VRAD goes where the map compiles take it from (Source SDK Base 2013 Singleplayer's bin).
	const QFileInfo vrad(m_editor->gameSettings().vrad);
	return vrad.exists() ? toPath(vrad.absolutePath()) : std::filesystem::path();
}

void FeaturesPanel::showInstall() {
	const auto state = installState();
	const auto installed = m_modDir.empty() ? std::nullopt : readInstalledFeatures(m_modDir);
	const bool busy = m_afterRun != AfterRun::None || (m_modCode && m_modCode->running());
	QString text;
	switch (state) {
		case InstallState::NoBundle:
			text = tr("This copy of Mapsmith brings no ready-built features (the Windows version from the releases has them).");
			if (!m_bundleError.isEmpty()) m_installState->setToolTip(m_bundleError);
			break;
		case InstallState::NoMod: text = tr("Open a mod first."); break;
		case InstallState::NotInstalled:
			text = tr("Not installed in this mod yet. Mapsmith brings version %1.").arg(QString::fromStdString(m_bundle->version));
			break;
		case InstallState::Current:
			text = tr("<span style='color:#8fdc9a'>✓ Installed</span>: version %1, for %2 mods.")
			           .arg(QString::fromStdString(installed->version),
			                installed->branch == ModBranch::Episodic ? tr("Episode Two") : tr("Half-Life 2"));
			break;
		case InstallState::Outdated:
			text = tr("Installed: version %1. Mapsmith brings %2 - update to get it.")
			           .arg(QString::fromStdString(installed->version), QString::fromStdString(m_bundle->version));
			break;
	}
	if (state != InstallState::NoBundle) m_installState->setToolTip({});
	if (const auto* entry = m_editor->gameEntry(); entry && state == InstallState::NotInstalled && !entry->usesMapbase) {
		text += QStringLiteral("<br>") + tr("This mod does not use Mapbase: the features are built on Mapbase and may not work here.");
	}
	m_installState->setText(text);
	m_installButton->setText(state == InstallState::Outdated ? tr("Update features")
	                         : state == InstallState::Current ? tr("Install again")
	                                                          : tr("Install features"));
	m_installButton->setEnabled(!busy && (state == InstallState::NotInstalled || state == InstallState::Outdated || state == InstallState::Current));
	m_removeButton->setVisible(state == InstallState::Current || state == InstallState::Outdated);
	m_removeButton->setEnabled(!busy);
	m_autoUpdate->setVisible(state != InstallState::NoBundle);
}

bool FeaturesPanel::installNow() {
	if (!m_bundle || m_modDir.empty()) {
		showInstall();
		return false;
	}
	if (m_afterRun != AfterRun::None || (m_modCode && m_modCode->running())) {
		m_installState->setText(tr("Wait until the running script has finished."));
		return false;
	}
	const auto before = readInstalledFeatures(m_modDir);
	const auto result = installFeatures(*m_bundle, m_modDir, sdkBin());
	if (!result.ok) {
		showInstall();
		m_installState->setText(m_installState->text() + QStringLiteral("<br><span style='color:#ff8a7a'>%1</span>")
		                                                     .arg(tr("Could not install: %1").arg(QString::fromStdString(result.error)).toHtmlEscaped()));
		return false;
	}
	m_status->setText(before ? tr("The features were updated to version %1.").arg(QString::fromStdString(m_bundle->version))
	                         : tr("The features are installed (version %1). Start the mod - everything is on.")
	                               .arg(QString::fromStdString(m_bundle->version)));
	if (m_chosen) hookFeatureChoice(m_modDir);   // the add-on is there now: autoexec.cfg runs the choice
	runAfterInstall(ModCodeAction::Deploy);
	return true;
}

bool FeaturesPanel::removeNow() {
	if (m_modDir.empty() || (m_modCode && m_modCode->running()) || m_afterRun != AfterRun::None) return false;
	const auto result = removeFeatures(m_modDir);
	if (!result.ok) {
		m_installState->setText(QStringLiteral("<span style='color:#ff8a7a'>%1</span>")
		                            .arg(tr("Could not remove: %1").arg(QString::fromStdString(result.error)).toHtmlEscaped()));
		return false;
	}
	removeEditorFiles(m_modDir, generatedModFiles(m_modDir));
	m_status->setText(tr("The features were taken out of the mod; what it had before is back."));
	runAfterInstall(ModCodeAction::RemoveGameFiles);
	return true;
}

void FeaturesPanel::runAfterInstall(ModCodeAction action) {
	// The generated materials (liquid, sheen, relief ...) are deploy.ps1's work; without a way to
	// run it here (no PowerShell) the files above are in and that is all.
	if (m_modCode && m_modCode->canRun(action)) {
		m_afterRun = action == ModCodeAction::Deploy ? AfterRun::Deploy : AfterRun::Remove;
		if (m_modCode->start(action)) {
			showInstall();
			return;
		}
		m_afterRun = AfterRun::None;
	}
	showInstall();
	emit installed();
}

bool FeaturesPanel::isOn(const QString& id) const {
	return !m_off.count(id.toStdString());
}

void FeaturesPanel::setOn(const QString& id, bool on) {
	if (!findModFeature(id.toStdString())) return;
	if (on) m_off.erase(id.toStdString());
	else m_off.insert(id.toStdString());
	m_checklist->setOff(m_off);
	save(id);
}

void FeaturesPanel::load() {
	m_loading = true;
	const auto* entry = m_editor->gameEntry();
	m_modDir = entry ? entry->gameDir : std::filesystem::path();
	const auto choice = m_modDir.empty() ? std::nullopt : loadFeatureChoice(m_modDir);
	m_chosen = choice.has_value();
	m_off = choice.value_or(std::set<std::string>());
	// The material effects: the Game enhancements' settings (read from the mod while they are not
	// running, e.g. before the add-on is installed).
	if (m_editor->enhancements() && m_editor->enhancements()->available()) {
		for (const auto& id : featuresOffBySettings(m_editor->enhancements()->settings())) m_off.insert(id);
	} else if (!m_modDir.empty()) {
		for (const auto& id : featuresOffBySettings(loadEnhancementSettings(m_modDir))) m_off.insert(id);
	}
	m_checklist->setOff(m_off);
	// The add-on arrived after the choice was made (New mod dialog, or installed later): from now
	// on autoexec.cfg runs it.
	if (m_chosen && m_editor->hasFidelity() && hookFeatureChoice(m_modDir)) {
		m_status->setText(tr("cfg/autoexec.cfg runs the features now."));
	} else {
		m_status->clear();
	}
	const bool hasMod = !m_modDir.empty();
	m_checklist->setEnabled(hasMod);
	if (!hasMod) {
		m_note->setText(tr("Open a mod first."));
	} else if (!m_editor->hasFidelity()) {
		m_note->setText(tr("This mod does not have mapbase-fidelity yet. The choice is kept and used as soon as the features "
		                   "are installed (above)."));
	} else {
		m_note->setText(tr("Saved in %1 (switches) and cfg/mapsmith_enhancements.txt (material effects).")
		                    .arg(QStringLiteral("cfg/mapsmith_features.cfg")));
	}
	m_loading = false;
	refreshOffer();
	updateFresh();
	showInstall();
	// A new mod got the features from the New mod dialog: its generated materials now.
	const QString pending = QStringLiteral("features/deployPending/%1").arg(fromPath(m_modDir));
	if (!m_modDir.empty() && QSettings().value(pending).toBool()) {
		QSettings().remove(pending);
		QTimer::singleShot(0, this, [this] {
			if (m_afterRun == AfterRun::None && !(m_modCode && m_modCode->running())) runAfterInstall(ModCodeAction::Deploy);
		});
	}
	// A newer Mapsmith brings newer features: the mod gets them by itself (once per session).
	if (installState() == InstallState::Outdated && m_autoUpdate->isChecked() && m_autoUpdated.insert(m_modDir.string()).second) {
		QTimer::singleShot(0, this, [this] {
			if (installState() == InstallState::Outdated && installNow()) {
				m_status->setText(tr("Mapsmith updated the mod's features to version %1 by itself.").arg(QString::fromStdString(m_bundle->version)));
			}
		});
	}
}

void FeaturesPanel::save(const QString& changed) {
	if (m_loading || m_modDir.empty()) return;
	// The switches: cfg/mapsmith_features.cfg (run by autoexec.cfg once the add-on is there).
	const bool ok = saveFeatureChoice(m_modDir, m_off, m_editor->hasFidelity());
	m_chosen = m_chosen || ok;
	// The material effects: the Game enhancements' switches (they write the materials).
	auto* enhancements = m_editor->enhancements();
	if (enhancements && enhancements->available()) {
		for (const auto& feature : modFeatures()) {
			if (!feature.effect) continue;
			const bool on = !m_off.count(feature.id);
			if (enhancements->settings().enabled(*feature.effect) != on) enhancements->setEffectEnabled(*feature.effect, on);
		}
	} else if (!m_modDir.empty()) {
		// No add-on yet: kept for when it comes.
		saveEnhancementSettings(m_modDir, withFeatureEffects(loadEnhancementSettings(m_modDir), m_off));
	}
	if (!ok) {
		m_status->setText(tr("<span style='color:#ff8a7a'>Could not write cfg/mapsmith_features.cfg - is the mod folder read-only?</span>"));
	} else if (const auto* feature = findModFeature(changed.toStdString())) {
		m_status->setText(tr("%1 is %2 - the game uses it from its next start.")
		                      .arg(qs(feature->title), m_off.count(feature->id) ? tr("off") : tr("on")));
	} else {
		m_status->setText(tr("Saved - the game uses it from its next start."));
	}
	refreshOffer();
}

void FeaturesPanel::refreshOffer() {
	// A Mapbase mod without the features: one click installs them. A mod with them and no
	// choice yet: the checklist, offered once.
	const auto* entry = m_editor->gameEntry();
	const bool install = !m_modDir.empty() && m_bundle && entry && entry->usesMapbase && !m_editor->hasFidelity() &&
	                     !readInstalledFeatures(m_modDir);
	const bool choose = !m_modDir.empty() && m_editor->hasFidelity() && !m_chosen;
	m_offerText->setText(install ? tr("<b>Give this mod Mapsmith's features.</b> Ambient occlusion, light shafts, the sound mixer, "
	                                  "liquid in bottles and more - one click installs them, nothing to build.")
	                             : tr("<b>Choose this mod's features.</b> mapbase-fidelity is installed and everything is on: ambient "
	                                  "occlusion, light shafts, the sound mixer, liquid in bottles and more. Pick what the mod should "
	                                  "have - you can change it any time under Project > Features."));
	m_offerInstall->setVisible(install);
	m_offerChoose->setVisible(!install);
	m_offerKeep->setVisible(!install);
	if (m_offer->parent()) m_offer->setVisible(install || choose);
}

void FeaturesPanel::updateFresh() {
	// Whether it can run is told when asked (the source code may be chosen after this tab).
	const bool running = m_fresh == FreshStep::Removing || m_fresh == FreshStep::Installing;
	m_freshButton->setEnabled(!m_modDir.empty() && !running);
	m_freshGo->setEnabled(!running);
}

void FeaturesPanel::askFreshInstall() {
	m_freshList->clear();
	if (m_modDir.empty()) return;
	QString why;
	m_freshBuildBox->setVisible(m_modCode && m_modCode->hasCheckout());
	if (!m_modCode || !m_modCode->canRun(ModCodeAction::RemoveGameFiles, &why)) {
		m_freshCount->setText(tr("<span style='color:#ff8a7a'>%1</span> A fresh install runs the add-on's scripts with PowerShell.")
		                          .arg(why.toHtmlEscaped()));
		m_freshList->setVisible(false);
		m_freshConfirm->setVisible(true);
		updateFresh();
		m_freshGo->setEnabled(false);
		return;
	}
	const auto files = generatedModFiles(m_modDir);
	for (const auto& file : files) {
		auto* item = new QListWidgetItem(QStringLiteral("%1   -   %2").arg(QString::fromStdString(file.path.generic_string()), QString::fromStdString(file.what)));
		item->setData(Qt::UserRole, QString::fromStdString(file.path.generic_string()));
		m_freshList->addItem(item);
	}
	m_freshCount->setText(files.empty()
	                          ? tr("The add-on and Mapsmith made nothing in this mod yet - a fresh install just installs everything.")
	                          : tr("These %n file(s) were made by the add-on or Mapsmith and will be removed. Your own maps, materials "
	                               "and files stay. Then the features are written again and everything is installed anew.",
	                               nullptr, int(files.size())));
	m_freshList->setVisible(!files.empty());
	m_freshConfirm->setVisible(true);
	updateFresh();
}

bool FeaturesPanel::startFreshInstall(bool build) {
	QString why;
	if (m_modDir.empty() || !m_modCode || !m_modCode->canRun(ModCodeAction::RemoveGameFiles, &why)) {
		setFreshStep(FreshStep::Failed, why.isEmpty() ? tr("Open a mod first.") : why);
		return false;
	}
	build = build && m_modCode->hasCheckout();   // building needs the mod's own source code
	if (build && !m_modCode->canRun(ModCodeAction::Update, &why)) {
		setFreshStep(FreshStep::Failed, why);
		return false;
	}
	if (!build && !m_bundle && !m_modCode->hasCheckout()) {
		setFreshStep(FreshStep::Failed, tr("This copy of Mapsmith brings no ready-built features to install again."));
		return false;
	}
	m_freshBuild = build;
	m_freshConfirm->setVisible(false);
	// Mapsmith's own files first (deploy.ps1 leaves them, they count as the mod's own for it),
	// and what an install of the features put in (what was there before comes back).
	removeEditorFiles(m_modDir, generatedModFiles(m_modDir));
	if (!build && m_bundle) {
		const auto removed = removeFeatures(m_modDir);
		if (!removed.ok) {
			setFreshStep(FreshStep::Failed, QString::fromStdString(removed.error));
			return false;
		}
	}
	setFreshStep(FreshStep::Removing);
	if (!m_modCode->start(ModCodeAction::RemoveGameFiles)) {
		setFreshStep(FreshStep::Failed, tr("Removing the game files could not start (see Project > Build from source)."));
		return false;
	}
	return true;
}

void FeaturesPanel::scriptFinished(bool ok) {
	if (m_afterRun != AfterRun::None) {
		// deploy.ps1 after an install (or -Remove after a removal) has finished.
		const bool deploy = m_afterRun == AfterRun::Deploy;
		m_afterRun = AfterRun::None;
		if (!ok) {
			m_status->setText(QStringLiteral("<span style='color:#ff8a7a'>%1</span>")
			                      .arg(deploy ? tr("The features are in, but the generated materials could not be made (the log is on "
			                                       "the Build from source tab).")
			                                  : tr("The generated materials could not all be removed (the log is on the Build from "
			                                       "source tab).")));
		}
		showInstall();
		emit installed();
		if (m_fresh == FreshStep::Installing) {
			setFreshStep(ok ? FreshStep::Done : FreshStep::Failed, ok ? QString() : tr("Making the generated materials did not work."));
			emit freshInstallFinished(ok);
		}
		return;
	}
	if (m_fresh != FreshStep::Removing && m_fresh != FreshStep::Installing) return;
	if (!ok) {
		setFreshStep(FreshStep::Failed, m_fresh == FreshStep::Removing ? tr("Removing the generated files did not work.")
		                                                               : tr("Installing did not work."));
		emit freshInstallFinished(false);
		return;
	}
	if (m_fresh == FreshStep::Removing) {
		// The features again, then the install - once the runner has finished this run.
		m_loading = false;
		save();
		setFreshStep(FreshStep::Installing);
		QTimer::singleShot(0, this, [this] {
			// The features Mapsmith brings (then deploy.ps1), or a build from the mod's own source.
			const bool fromBundle = !m_freshBuild && m_bundle;
			const bool started = fromBundle ? installNow()
			                                : (m_modCode && m_modCode->start(m_freshBuild ? ModCodeAction::Update : ModCodeAction::Deploy));
			if (!started) {
				setFreshStep(FreshStep::Failed, tr("Installing could not start (see Project > Build from source)."));
				emit freshInstallFinished(false);
			} else if (fromBundle && m_afterRun == AfterRun::None) {
				setFreshStep(FreshStep::Done);   // nothing more to run here (no PowerShell)
				emit freshInstallFinished(true);
			}
		});
		return;
	}
	setFreshStep(FreshStep::Done);
	emit freshInstallFinished(true);
}

void FeaturesPanel::setFreshStep(FreshStep step, const QString& detail) {
	const FreshStep previous = m_fresh;
	m_fresh = step;
	const auto mark = [&](int index) {
		// 0 remove, 1 write features, 2 install.
		const int at = step == FreshStep::Removing ? 0 : step == FreshStep::Installing ? 2 : step == FreshStep::Done ? 3 : -1;
		if (step == FreshStep::Failed) {
			const int failedAt = previous == FreshStep::Installing ? 2 : 0;
			return index < failedAt ? QStringLiteral("✓") : index == failedAt ? QStringLiteral("✗") : QStringLiteral("○");
		}
		return index < at ? QStringLiteral("✓") : index == at ? QStringLiteral("…") : QStringLiteral("○");
	};
	QString html = QStringLiteral("%1 %2<br>%3 %4<br>%5 %6")
	                   .arg(mark(0), tr("Remove what the add-on and Mapsmith made"), mark(1), tr("Write the features again"), mark(2),
	                        m_freshBuild ? tr("Install again: build the code from source and install everything")
	                                     : tr("Install the features again (game code, shaders, settings, generated materials)"));
	if (step == FreshStep::Done) html += QStringLiteral("<br><span style='color:#8fdc9a'><b>%1</b></span>").arg(tr("✓ Fresh install done. Start the mod."));
	if (step == FreshStep::Failed && !detail.isEmpty()) html += QStringLiteral("<br><span style='color:#ff8a7a'>%1</span>").arg(detail.toHtmlEscaped());
	m_freshSteps->setText(html);
	m_freshSteps->setVisible(step != FreshStep::Idle);
	updateFresh();
}

} // namespace sme::app
