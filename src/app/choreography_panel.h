// choreography_panel.h - The Choreography dock: lays out an NPC scene step by step (walk to a
// point, play an animation, say a line, wait, face a point), chooses what starts it and plays
// it back in the map views. The plan and its helpers live in the map (choreography.h); every
// change is one undo step, and the scene's .vcd is written into the mod right away.
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

signals:
	/// Files of the mod changed (a voice line was imported): the game's lists should be read again.
	void modChanged();

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

	Editor* m_editor;
	int m_sceneId = 0;
	int m_step = -1;
	double m_stepTime = 0;
	bool m_filling = false;
	std::string m_actor;   ///< The NPC the panel was opened for (new steps use it)
	std::optional<std::vector<SoundScriptEntry>> m_scripts;

	QComboBox* m_scenes;
	QPushButton* m_newScene;
	QPushButton* m_deleteScene;
	QLineEdit* m_name;
	QComboBox* m_trigger;
	QComboBox* m_after;
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
