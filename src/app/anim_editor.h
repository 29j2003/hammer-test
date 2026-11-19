// anim_editor.h - The Animation editor window: improve an animation of a model, or make a new
// one, with keys like in Source Filmmaker - no Blender. Pick bones in the 3D view or the bone
// list, turn them with the rings or move them with the arrows (or type the values), and set keys
// on the timeline; the pose between keys is filled in. A Tweak keeps the original animation
// underneath (keys are changes on top of it); a New animation starts from one still pose.
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
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProcess;
class QPushButton;
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
	void setMessage(const QString& text);
	[[nodiscard]] QString studiomdlProgram() const;
	[[nodiscard]] QString modDir() const;
	[[nodiscard]] QString sourceFolder() const;   ///< <mod>/modelsrc/<model>
	[[nodiscard]] QString titleText() const;
	void updateTitle();
	void finishReplace(int code, bool crashed);

	Editor* m_editor;
	MaterialCache* m_materials;
	QPointer<AnimationsPage> m_animations;
	QString m_model;
	QString m_sequence;
	bool m_new = false;
	std::optional<AnimClip> m_pendingClip;   ///< From a project file, used once the model is loaded
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
	AnimEditView* m_view = nullptr;
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
