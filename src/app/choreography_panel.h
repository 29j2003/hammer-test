// choreography_panel.h - The Choreography dock: every scene of the map in a list grouped by NPC
// (search, new, copy, rename, delete, select in map), and the chosen scene's tabs: Steps (walk to a
// point, play an animation, say a line, wait, face a point, set a variable), Starts (what starts
// it, several allowed), Conditions (all must hold) and Variables (the map's). It plays a scene back
// in the map views. The plan and its logic live in the map (choreography.h); every change is one
// undo step, and the scene's .vcd is written into the mod right away. The list, starts, conditions
// and variables are built in choreography_panel_logic.cpp.
//
// The map views draw the scene of the selected NPC (viewport_scenes.cpp); this panel tells them
// which step to show (Editor::setSceneFocus) and asks them for points (Add walk, Face, Place
// again). Voice lines are imported into sound/<mod>/vo/<scene>/ with a sound script entry on
// CHAN_VOICE and a caption in resource/closecaption_english.txt (.dat compiled next to it).

#pragma once

#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <sme/assets.h>
#include <sme/choreography.h>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSlider;
class QTabWidget;
class QTableWidget;
class QTreeWidget;
class QVBoxLayout;

namespace sme::app {

class Editor;
class SoundPlayer;

class ChoreographyPanel : public QWidget {
	Q_OBJECT

public:
	explicit ChoreographyPanel(Editor* editor, QWidget* parent = nullptr);
	~ChoreographyPanel() override;

	/// Shows the scenes of entity `entityId`: an NPC's first scene (a new one if it has none) or
	/// the scene entity itself.
	void openFor(int entityId);
	/// The scene shown (0: none) and its step (-1: none).
	[[nodiscard]] int sceneId() const { return m_sceneId; }
	[[nodiscard]] int currentStep() const { return m_step; }
	void setCurrentStep(int step);
	/// Imports a sound file as the current Say step's voice line (adds a Say step if the current
	/// step is none); `caption` (optional) becomes its subtitle. False with a message if it fails.
	bool importVoiceLine(const QString& path, const QString& caption = {});

	/// Shows scene `sceneId` (the list's choice).
	void showScene(int sceneId);

signals:
	/// Files of the mod changed (a voice line was imported): the game's lists should be read again.
	void modChanged();
	/// "Select in map": the views should move to the selection.
	void frameSelectionRequested();

protected:
	void showEvent(QShowEvent* event) override;

private:
	void refresh();
	void fillStepEditor();
	void updateIssues(const ChoreoScene& scene);
	[[nodiscard]] std::optional<ChoreoScene> currentScene() const;
	/// Writes the scene (one undo step) and its .vcd.
	void apply(const ChoreoScene& scene, const char* undoName);
	/// Changes the current step (one undo step).
	void editStep(const std::function<void(ChoreoStep&)>& change, const char* undoName);
	/// Adds a step after the current one and shows it.
	void addStep(ChoreoStep step, const char* undoName);
	void addPointStep(ChoreoStep::Kind kind);
	[[nodiscard]] std::string defaultActor() const;
	[[nodiscard]] QString actorModel(const std::string& actor) const;
	void onSelectionChanged();
	void onFocusChanged();
	void pushFocus();
	void stepsMoved();
	void startPreview();
	void stopPreview();
	void previewTick();
	void playStep();
	[[nodiscard]] double currentStepLength() const;
	/// Length of a sound script entry's (first) wave in seconds, nullopt if it cannot be read.
	[[nodiscard]] std::optional<double> soundSeconds(const std::string& name);
	[[nodiscard]] const std::vector<SoundScriptEntry>& scripts();
	void playSound(const std::string& name);

	// choreography_panel_logic.cpp: the scenes list, starts, conditions, variables.
	[[nodiscard]] QWidget* buildManager();
	[[nodiscard]] QWidget* buildStartsPage();
	[[nodiscard]] QWidget* buildConditionsPage();
	[[nodiscard]] QWidget* buildVariablesPage();
	[[nodiscard]] QWidget* buildVariableStepRow();
	void refreshManager();
	void refreshStarts(const ChoreoScene& scene);
	void fillStartEditor();
	void refreshConditions(const ChoreoScene& scene);
	void fillConditionEditor();
	void refreshVariables();
	void fillVariableStep(const ChoreoStep& step);
	/// Changes the current start / condition (one undo step).
	void editStart(const std::function<void(ChoreoStart&)>& change, const char* undoName);
	void editCondition(const std::function<void(ChoreoCondition&)>& change, const char* undoName);
	void addStart(ChoreoStart::Kind kind);
	void addCondition(ChoreoCondition::Kind kind);
	void selectSceneInMap();
	/// Problems of a scene for the list's State column ("" when fine).
	[[nodiscard]] QString sceneState(int sceneId, const ChoreoScene& scene, const std::vector<ChoreoLogicIssue>& logic) const;
	/// Names of the map's entities (for start targets), scenes and variables.
	[[nodiscard]] QStringList entityNames() const;

	Editor* m_editor;
	int m_sceneId = 0;
	int m_step = -1;
	double m_stepTime = 0;
	bool m_filling = false;
	std::string m_actor;   ///< The NPC the panel was opened for (new steps use it)
	std::optional<std::vector<SoundScriptEntry>> m_scripts;

	// The scenes list.
	QLineEdit* m_filter = nullptr;
	QTreeWidget* m_tree = nullptr;
	QPushButton* m_newScene = nullptr;
	QPushButton* m_copyScene = nullptr;
	QPushButton* m_deleteScene = nullptr;
	QPushButton* m_selectScene = nullptr;
	QLineEdit* m_name;
	QTabWidget* m_tabs = nullptr;
	// Starts.
	int m_start = -1;
	QListWidget* m_starts = nullptr;
	QPushButton* m_addStart = nullptr;
	QPushButton* m_removeStart = nullptr;
	QWidget* m_startBox = nullptr;
	QLabel* m_startKind = nullptr;
	QWidget* m_startTargetRow = nullptr;
	QComboBox* m_startTarget = nullptr;
	QWidget* m_startOutputRow = nullptr;
	QComboBox* m_startOutput = nullptr;
	QCheckBox* m_startOnce = nullptr;
	QDoubleSpinBox* m_startDelay = nullptr;
	QWidget* m_startARow = nullptr;
	QLabel* m_startALabel = nullptr;
	QDoubleSpinBox* m_startA = nullptr;
	QLabel* m_startBLabel = nullptr;
	QDoubleSpinBox* m_startB = nullptr;
	QPushButton* m_startSelect = nullptr;
	QLabel* m_startNote = nullptr;
	// Conditions.
	int m_condition = -1;
	QListWidget* m_conditions = nullptr;
	QPushButton* m_addCondition = nullptr;
	QPushButton* m_removeCondition = nullptr;
	QWidget* m_conditionBox = nullptr;
	QLabel* m_conditionKind = nullptr;
	QWidget* m_conditionNameRow = nullptr;
	QComboBox* m_conditionName = nullptr;
	QWidget* m_conditionCompareRow = nullptr;
	QComboBox* m_conditionCompare = nullptr;
	QDoubleSpinBox* m_conditionValue = nullptr;
	// Variables.
	QTableWidget* m_variables = nullptr;
	QPushButton* m_deleteVariable = nullptr;
	// Set variable step.
	QWidget* m_varRow = nullptr;
	QComboBox* m_stepVariable = nullptr;
	QComboBox* m_stepOp = nullptr;
	QDoubleSpinBox* m_stepValue = nullptr;
	QListWidget* m_steps;
	QPushButton* m_remove;
	QWidget* m_editor_box;
	QComboBox* m_actorBox;
	QCheckBox* m_withPrevious;
	QWidget* m_moveRow;
	QCheckBox* m_run;
	QPushButton* m_place;
	QWidget* m_animRow;
	QComboBox* m_sequence;
	QCheckBox* m_gesture;
	QWidget* m_sayRow;
	QLineEdit* m_sound;
	QPushButton* m_voice;
	QLineEdit* m_caption;
	QWidget* m_secondsRow;
	QDoubleSpinBox* m_seconds;
	QWidget* m_scrubRow;
	QSlider* m_scrub;
	QPushButton* m_playStep;
	QLabel* m_issues;
	QPushButton* m_preview;
	QLabel* m_length;
	QLabel* m_subtitle;

	QTimer m_timer;          ///< Drives Play step and Preview scene
	QElapsedTimer m_clock;
	bool m_previewing = false;
	bool m_stepPlaying = false;
	std::vector<bool> m_spoken;   ///< Preview: Say steps whose sound started
	std::unique_ptr<SoundPlayer> m_player;
};

} // namespace sme::app
