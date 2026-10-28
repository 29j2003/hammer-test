// features_panel.h - Project > Features: which features of mapbase-fidelity (the mod's graphics
// and sound add-on) the mod has, as one checklist - ambient occlusion, light shafts, the sound
// mixer, liquid in bottles ... (sme/mod_features.h). A tick is saved at once: the switches go
// into cfg/mapsmith_features.cfg (the game runs it at start), the material effects into the Game
// enhancements' settings. The first time a mod with the add-on is opened, a line on the
// Overview offers the checklist once. "Fresh install" starts the add-on over: it shows what
// deploy.bat and Mapsmith made in the mod, removes it (deploy.ps1 -Remove and Mapsmith's own
// files - never the mapper's), writes the features again and installs anew through the Mod code
// tab. FeatureChecklist is the list itself, also used by the New mod dialog.

#pragma once

#include <QPointer>
#include <QWidget>

#include <filesystem>
#include <map>
#include <set>
#include <string>

#include <sme/mod_features.h>

class QCheckBox;
class QLabel;
class QListWidget;
class QPushButton;

namespace sme::app {

class Editor;
class ModCodePanel;

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

private:
	void load();
	void save(const QString& changed = {});
	void updateFresh();
	void scriptFinished(bool ok);
	void setFreshStep(FreshStep step, const QString& detail = {});

	Editor* m_editor;
	QPointer<ModCodePanel> m_modCode;
	std::filesystem::path m_modDir;
	std::set<std::string> m_off;
	bool m_chosen = false;
	bool m_loading = false;
	FreshStep m_fresh = FreshStep::Idle;
	bool m_freshBuild = false;

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
