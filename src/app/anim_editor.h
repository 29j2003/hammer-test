// anim_editor.h - The Animation editor window, in two modes chosen at the top:
// - Pose (all animations): change how the model holds itself in every animation at once - drag a
//   hand's grip (the arm follows), bend the fingers with the Curl/Spread sliders, check it on any
//   animation ("Check on"), Build puts it into the model (a pose layer, anim_edit.h).
// - Animate (one animation): improve an animation of a model, or make a new
//   one, with keys like in Source Filmmaker - no Blender. Work in 3D (orbit, Front/Side/Top, the
// player's view beside it for weapons): drag a hand's or foot's grip and the limb follows (IK),
// or pick bones and turn them with the rings / move them with the arrows (or type the values).
// Auto key keys every move at the playhead; Set key (K) does it by hand. Between keys the pose
// is filled in: Smooth, Linear, Step, Ease in, Ease out per key; the motion path shows where the
// chosen bone goes. A Tweak keeps the original animation underneath (keys are changes on top of
// it); a New animation starts from one still pose. "Animation" menu: duplicate, start from
// another animation (copy and change it, or from its first pose), new from scratch, change only
// from the playhead on (earlier frames stay the original), cut before/after the playhead.
//
// Results (see anim_edit.h):
// - Preview in Mapsmith: the animation plays on the Animations page at once.
// - Build as new animation: Mapsmith's model copy with the new animation (animation_import).
// - Replace in the model: the model is taken apart (model_export), the sequence's animation
//   replaced, and studiomdl builds the model again into the mod at the same path - the game then
//   loads the mod's model instead of its own (needed for a weapon's first-person model). The
//   mod's earlier files are kept aside (backups/; the first time in original/, with the model as
//   it was, which stays the base of the tweak), and "Restore the original model" puts them back.
//   After the build the header checksums of .mdl, .vvd, .vtx and .phy are compared - the engine
//   refuses a model whose files do not belong together.
// - Save: the keys as a project file (.smanim) below the mod's modelsrc/ to go on later.
// - Build the pose: every animation of the model with the pose layer on top (and the animations
//   built from this editor before, so nothing built is lost: <modelsrc>/rebuild.txt records them).

#pragma once

#include <QFutureWatcher>
#include <QPointer>
#include <QWidget>

#include <memory>
#include <optional>

#include <sme/anim_edit.h>
#include <sme/model_export.h>
#include <sme/studio_anim.h>
#include <sme/studio_model.h>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProcess;
class QPushButton;
class QSlider;
class QSpinBox;
class QTimer;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace sme::app {

class AnimationsPage;
class AnimEditView;
class AnimTimeline;
class Editor;
class MaterialCache;

/// See the file comment.
class AnimationEditor : public QWidget {
	Q_OBJECT

public:
	AnimationEditor(Editor* editor, MaterialCache* materials, AnimationsPage* animations, QWidget* parent = nullptr);
	~AnimationEditor() override;

	/// Opens a model to tweak one of its sequences (New: a new animation starting from that
	/// sequence's first frame; empty sequence: from the rest pose). A project file saved for that
	/// model and name is picked up again. Loads in the background (loaded() follows).
	void openModel(const QString& model, const QString& sequence, bool newAnimation = false);
	/// Opens a model in Pose mode (hand/arm pose for all its animations); a saved pose is picked up.
	void openPose(const QString& model);
	/// Pose (all animations) or Animate (one animation). The other mode's work stays (not saved).
	void setPoseMode(bool on);
	[[nodiscard]] bool poseMode() const { return m_pose; }
	/// Pose mode: the animation the pose is shown on.
	void setPoseSequence(const QString& sequence);
	/// Finger sliders of a hand (index into AnimEditor::hands()): curl -0.25 .. 1, spread -1 .. 1.
	void setHandShape(int hand, float curl, float spread);
	/// Builds the pose into the model (all animations; see the file comment). False if it cannot start.
	bool buildPose();
	/// Opens a project file (.smanim).
	bool openProject(const QString& path, QString* error = nullptr);
	/// Whether a model and its clip are loaded.
	[[nodiscard]] bool ready() const { return m_edit != nullptr; }
	[[nodiscard]] QString model() const { return m_model; }

	// --- For the window's parts and tests --------------------------------------------------------
	[[nodiscard]] AnimEditor* clipEditor() const { return m_edit.get(); }
	[[nodiscard]] AnimEditView* view() const { return m_view; }
	[[nodiscard]] AnimTimeline* timeline() const { return m_timeline; }
	[[nodiscard]] QTreeWidget* boneTree() const { return m_bones; }
	[[nodiscard]] int frame() const { return m_frame; }
	void setFrame(int frame);
	/// Chooses bones (by index; the last one gets the gizmo).
	void selectBones(const std::vector<int>& bones);
	[[nodiscard]] const std::vector<int>& selectedBones() const { return m_selection; }
	void setPlaying(bool on);
	[[nodiscard]] bool playing() const { return m_playing; }
	/// The actions of the buttons and keys.
	void keySelection();
	void deleteKey();
	void copyPose();
	void pastePose();
	void mirrorSelection();
	/// How the motion leaves the keys of the chosen bones (all bones if none) at the playhead.
	void setInterpolation(KeyInterp interp);
	/// Auto key: a move keys at the playhead by itself; off: a move only changes a key that is there.
	void setAutoKey(bool on);
	[[nodiscard]] bool autoKey() const;
	/// The same animation under a new name (its own project file); the original stays.
	void duplicateAnimation(const QString& name);
	/// Starts over from a sequence of the model: copy it and change it (Tweak), or from its first
	/// pose (New); an empty sequence: from the rest pose.
	void startFrom(const QString& sequence, bool copyMotion, const QString& name);
	/// Frames before the playhead stay the original (keys there go).
	void editFromPlayhead();
	/// Cuts the frames before / after the playhead.
	void cutBeforePlayhead();
	void cutAfterPlayhead();
	/// Player's view of a weapon beside the 3D view.
	void setSplitView(bool on);
	[[nodiscard]] AnimEditView* playerView() const { return m_playerView; }
	bool save(QString* error = nullptr);
	[[nodiscard]] QString projectPath() const;
	/// Writes the animation .smd and shows it on the Animations page (`build`: and builds it into
	/// Mapsmith's model copy). False with a message if that is not possible.
	bool previewInMapsmith(bool build = false);
	/// Rebuilds the model into the mod with `sequence` (empty: the tweaked one) replaced; the
	/// result arrives with replaceFinished. False if it cannot start.
	bool replaceInModel(const QString& sequence = {});
	/// Puts the mod's model back as it was before the first replace.
	bool restoreOriginal(QString* error = nullptr);
	/// Whether the mod holds a model Mapsmith rebuilt (restoreOriginal() has something to do).
	[[nodiscard]] bool replaced() const;
	void setStudiomdlProgram(const QString& program) { m_studiomdlOverride = program; }
	[[nodiscard]] QString log() const;

signals:
	/// The model and its animations are loaded (ok) or could not be.
	void loaded(bool ok);
	void replaceFinished(bool ok);

protected:
	void closeEvent(QCloseEvent* event) override;

private:
	struct Loaded {
		std::optional<AnimatedModel> model;
		QString error;
	};
	void buildUi();
	void startLoading();
	void loadingDone();
	void geometryArrived();
	void beginEditing();
	void fillBoneTree();
	void filterBones();
	void refresh();          ///< Pose, skeleton, keys, fields - after any change
	void refreshFields();
	void applyFields();      ///< The typed values become the active bone's key
	void tick();
	void showKeyMenu(int frame, bool allBones, const QPoint& at);
	void startClip(AnimClip clip);   ///< A new clip on the loaded model
	void showStartFromDialog(bool fromScratch);
	/// After a drag: without Auto key a move where the bone had no key is taken back.
	void finishDrag();
	void setMessage(const QString& text);
	[[nodiscard]] QString studiomdlProgram() const;
	[[nodiscard]] QString modDir() const;
	[[nodiscard]] QString sourceFolder() const;   ///< <mod>/modelsrc/<model>
	[[nodiscard]] QString titleText() const;
	void updateTitle();
	void finishReplace(int code, bool crashed);
	/// Takes the model apart with these frames in place and builds it into the mod (see replaceInModel).
	bool startRebuild(std::map<std::string, SequenceReplacement> replacements);
	[[nodiscard]] AnimClip defaultClip() const;   ///< Animate mode on the opened animation (or the idle)
	[[nodiscard]] AnimClip poseClip() const;      ///< The saved pose of the model, or an empty one
	/// Saved project of a name below the model's modelsrc/ (nullopt if none or broken).
	[[nodiscard]] std::optional<AnimClip> savedClip(const std::string& name) const;
	/// What was built into the mod's model by this editor: sequence (lower case) -> project name;
	/// "#pose" when the pose is in it.
	[[nodiscard]] std::map<std::string, std::string> rebuildRecord() const;
	void writeRebuildRecord(const std::map<std::string, std::string>& record) const;
	void applyMode();        ///< Shows the tools of the mode, its hint
	void fillHands();        ///< Sliders for the model's hands
	void refreshHands();     ///< Slider positions from the clip
	void fillPoseSequences();

	Editor* m_editor;
	MaterialCache* m_materials;
	QPointer<AnimationsPage> m_animations;
	QString m_model;
	QString m_sequence;
	bool m_new = false;
	std::optional<AnimClip> m_pendingClip;   ///< From a project file, used once the model is loaded
	bool m_pose = false;                     ///< Pose mode (all animations)
	bool m_openPose = false;                 ///< Start in Pose mode once loaded
	std::unique_ptr<AnimEditor> m_parked;    ///< The other mode's clip, kept while switching
	int m_parkedSavedRevision = 0;
	std::map<std::string, std::string> m_pendingRecord;   ///< Entries for rebuild.txt once the build succeeds
	std::shared_ptr<const ModelGeometry> m_geometry;
	std::optional<AnimatedModel> m_animated;
	QFutureWatcher<Loaded> m_loader;
	int m_generation = 0;
	std::unique_ptr<AnimEditor> m_edit;
	std::vector<int> m_selection;
	int m_frame = 0;
	float m_playFrame = 0;
	bool m_playing = false;
	int m_shownRevision = -1;
	int m_savedRevision = 0;
	ClipPose m_clipboard;
	bool m_filling = false;

	// Widgets.
	QLabel* m_title = nullptr;
	QToolButton* m_modePose = nullptr;
	QToolButton* m_modeAnimate = nullptr;
	QWidget* m_animateTools = nullptr;
	QWidget* m_poseTools = nullptr;
	QComboBox* m_poseSequence = nullptr;
	QLabel* m_howTo = nullptr;
	QWidget* m_bonePanel = nullptr;
	QWidget* m_clipBox = nullptr;
	QFormLayout* m_clipForm = nullptr;
	QWidget* m_handsBox = nullptr;
	QWidget* m_handsContent = nullptr;
	struct HandSliders {
		QSlider* curl = nullptr;
		QSlider* spread = nullptr;
		std::vector<QSlider*> fingers;
		QWidget* fingerBox = nullptr;   ///< The per-finger sliders (under More)
	};
	std::vector<HandSliders> m_handSliders;
	bool m_sliderDrag = false;
	AnimEditView* m_view = nullptr;
	AnimEditView* m_playerView = nullptr;
	QCheckBox* m_split = nullptr;
	QCheckBox* m_autoKeyBox = nullptr;
	QCheckBox* m_pathBox = nullptr;
	QComboBox* m_interpBar = nullptr;
	QSpinBox* m_editFromBox = nullptr;
	QWidget* m_moreBox = nullptr;
	QToolButton* m_moreButton = nullptr;
	int m_dragBone = -1;           ///< The bone a drag changes (for Auto key off)
	bool m_dragHadKey = false;
	int m_pathRevision = -1;       ///< The motion path is recomputed when the clip or selection changes
	int m_pathBone = -1;
	std::vector<AnimVec> m_pathPoints;
	AnimTimeline* m_timeline = nullptr;
	QTreeWidget* m_bones = nullptr;
	QLineEdit* m_boneSearch = nullptr;
	QComboBox* m_boneGroup = nullptr;
	QComboBox* m_camera = nullptr;
	QToolButton* m_rotate = nullptr;
	QToolButton* m_move = nullptr;
	QCheckBox* m_ghosts = nullptr;
	QToolButton* m_play = nullptr;
	QSpinBox* m_frameBox = nullptr;
	QLabel* m_frameOf = nullptr;
	QLabel* m_boneName = nullptr;
	QDoubleSpinBox* m_rot[3] = {};
	QDoubleSpinBox* m_pos[3] = {};
	QComboBox* m_interp = nullptr;
	QLineEdit* m_name = nullptr;
	QLineEdit* m_activity = nullptr;
	QSpinBox* m_length = nullptr;
	QDoubleSpinBox* m_fps = nullptr;
	QCheckBox* m_loop = nullptr;
	QLabel* m_modeNote = nullptr;
	QLabel* m_status = nullptr;
	QPlainTextEdit* m_log = nullptr;
	QPushButton* m_undo = nullptr;
	QPushButton* m_redo = nullptr;
	QPushButton* m_compile = nullptr;
	QTimer* m_timer = nullptr;
	qint64 m_lastTick = 0;

	// Replace in the model.
	QFutureWatcher<std::optional<ModelExport>> m_decompile;
	QString m_replaceSequence;
	QPointer<QProcess> m_studiomdl;
	QString m_studiomdlOverride;
	QStringList m_movedAside;      ///< Mod files moved away for the build: back on failure
	QString m_movedTo;
	bool m_firstReplace = false;
};

} // namespace sme::app
