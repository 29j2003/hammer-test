// features_panel.h - Project > Features: the mod's features (mapbase-fidelity, its graphics and
// sound add-on) - installed from Mapsmith itself, and which of them the mod has.
//
// Install: Mapsmith brings the add-on ready-built (the feature bundle next to Mapsmith.exe,
// sme/mod_install.h). "Install features" copies its DLLs, shaders, cfg and VRAD into the mod and
// then runs the bundle's deploy.ps1 for the generated materials (through the Build from source
// tab's runner); a newer Mapsmith updates the mod by itself when it is opened (switchable). No
// compiler, git or script is needed. A Mapbase mod without the features gets one line on the
// Overview offering the install.
//
// The checklist (sme/mod_features.h): a tick is saved at once - the switches go into
// cfg/mapsmith_features.cfg (the game runs it at start), the material effects into the Game
// enhancements' settings. The first time a mod with the add-on is opened, a line on the
// Overview offers the checklist once. "Fresh install" starts the add-on over: it shows what the
// add-on's scripts and Mapsmith made in the mod, removes it (never the mapper's files), writes
// the features again and installs anew. FeatureChecklist is the list itself, also used by the
// New mod dialog.

#pragma once

#include <QPointer>
#include <QElapsedTimer>
#include <QWidget>

#include <filesystem>
#include <map>
#include <set>
#include <string>

#include <sme/mod_features.h>
#include <sme/mod_install.h>

class QCheckBox;
class QLabel;
class QProgressBar;
class QTimer;
class QListWidget;
class QPushButton;

namespace sme::app {

class Editor;
class ModCodePanel;

/// Where Mapsmith's feature bundle is: $SME_FEATURES_DIR, else features/ next to the program.
[[nodiscard]] std::filesystem::path featureBundleDir();

/// The features grouped (Graphics, Effects, Sound, Materials) with a checkbox each.
class FeatureChecklist : public QWidget {
	Q_OBJECT

public:
	/// `compact`: descriptions as tooltips only (for dialogs).
	explicit FeatureChecklist(bool compact, QWidget* parent = nullptr);

	/// The features shown unticked.
	void setOff(const std::set<std::string>& off);
	[[nodiscard]] std::set<std::string> off() const;
	[[nodiscard]] QCheckBox* box(const QString& id) const;

signals:
	/// The user ticked or unticked a feature.
	void toggled(const QString& id, bool on);

private:
	std::map<std::string, QCheckBox*> m_boxes;
};

/// The Features tab of the Project page (see the file comment).
class FeaturesPanel : public QWidget {
	Q_OBJECT

public:
	FeaturesPanel(Editor* editor, ModCodePanel* modCode, QWidget* parent = nullptr);

	/// The features this Mapsmith brings (nullopt: none - only the Windows package has them).
	[[nodiscard]] const std::optional<FeatureBundle>& bundle() const { return m_bundle; }
	/// Reads the bundle again (from featureBundleDir()).
	void reloadBundle();
	/// Where the open mod stands.
	enum class InstallState { NoBundle, NoMod, NotInstalled, Current, Outdated };
	[[nodiscard]] InstallState installState() const;
	/// Installs (or updates) the features in the open mod, then the generated materials
	/// (deploy.ps1). False with the reason shown when it cannot.
	bool installNow();
	/// Takes them out again (what was there before comes back), then deploy.ps1 -Remove.
	bool removeNow();

	/// Whether a feature is on for the open mod.
	[[nodiscard]] bool isOn(const QString& id) const;
	/// Ticks or unticks a feature and saves it (as a click does).
	void setOn(const QString& id, bool on);
	/// The mod has made its choice (cfg/mapsmith_features.cfg exists).
	[[nodiscard]] bool chosen() const { return m_chosen; }
	[[nodiscard]] FeatureChecklist* checklist() const { return m_checklist; }

	/// The line for the Overview that offers the checklist once. Not parented: the owner puts it
	/// into its page.
	[[nodiscard]] QWidget* offer() const { return m_offer; }
	/// Shows the offer when the open mod has the add-on and no choice yet (call once it is placed).
	void refreshOffer();

	/// Fresh install: shows what will be removed and asks (inline, no dialog) ...
	void askFreshInstall();
	/// ... and does it: remove, write the features, install (`build`: Update mod, else only the
	/// game files). False with the reason in the status when it cannot start.
	bool startFreshInstall(bool build);
	/// Steps of a fresh install.
	enum class FreshStep { Idle, Removing, Installing, Done, Failed };
	[[nodiscard]] FreshStep freshStep() const { return m_fresh; }
	/// What the confirmation lists (for tests).
	[[nodiscard]] QListWidget* freshList() const { return m_freshList; }

signals:
	/// "Choose features" on the Overview line: show this tab.
	void showRequested();
	/// "Show details" of a fresh install: show the Mod code tab.
	void modCodeRequested();
	/// A fresh install ended.
	void freshInstallFinished(bool ok);
	/// Features were installed, updated or removed: the mod's files are new (read the game again).
	void installed();

private:
	void load();
	void save(const QString& changed = {});
	void updateFresh();
	void scriptFinished(bool ok);
	void setFreshStep(FreshStep step, const QString& detail = {});
	void showInstall();
	void showProgress();
	/// After the files are in: the generated materials (deploy.ps1), then installed().
	void runAfterInstall(ModCodeAction action);
	[[nodiscard]] std::filesystem::path sdkBin() const;

	Editor* m_editor;
	QPointer<ModCodePanel> m_modCode;
	std::filesystem::path m_modDir;
	std::set<std::string> m_off;
	bool m_chosen = false;
	bool m_loading = false;
	FreshStep m_fresh = FreshStep::Idle;
	bool m_freshBuild = false;
	std::optional<FeatureBundle> m_bundle;
	QString m_bundleError;
	/// What the script runner does for this tab after an install or removal.
	enum class AfterRun { None, Deploy, Remove } m_afterRun = AfterRun::None;
	/// The install's progress line under the button (idle: hidden).
	enum class ProgressPhase { Idle, Copying, GameFiles, Done, Failed } m_progressPhase = ProgressPhase::Idle;
	QElapsedTimer m_progressStart;
	QTimer* m_progressClock = nullptr;
	QProgressBar* m_installProgress = nullptr;
	QLabel* m_installStep = nullptr;
	std::set<std::string> m_autoUpdated;   ///< Mods updated by themselves this session

	QLabel* m_installState;
	QPushButton* m_installButton;
	QPushButton* m_removeButton;
	QCheckBox* m_autoUpdate;
	QLabel* m_offerText;
	QPushButton* m_offerInstall;
	QPushButton* m_offerChoose;
	QPushButton* m_offerKeep;

	QLabel* m_note;
	QLabel* m_status;
	FeatureChecklist* m_checklist;
	QPushButton* m_freshButton;
	QWidget* m_freshConfirm;
	QLabel* m_freshCount;
	QListWidget* m_freshList;
	QCheckBox* m_freshBuildBox;
	QPushButton* m_freshGo;
	QLabel* m_freshSteps;
	QWidget* m_offer;
};

} // namespace sme::app
