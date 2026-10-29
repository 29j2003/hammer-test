// animations_page.h - The Animations page of the side bar: pick a model (the map's NPCs and
// props first, then every model, with search - only models that really animate, a skeleton with
// more than a still pose, found by reading the headers in the background), see its sequences -
// its own and those of its $includemodel models, with activity and length, filtered by weapon,
// stance and kind (idle, walk/run, actions, gestures, scenes) - and play one in a 3D view (play/pause,
// scrub, speed, loop). It lists which entities of the map play which of the model's animations
// and puts the chosen one on the selected entity ("Use on selected entity": a prop's
// DefaultAnim, a scripted_sequence's fields, an NPC's pose helper). "Add animation ..." brings
// an animation (.smd) onto the model's skeleton (see animation_import.h).

#pragma once

#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QHash>
#include <QPointer>
#include <QWidget>

#include <memory>
#include <optional>

#include <sme/animation_import.h>
#include <sme/studio_anim.h>
#include <sme/studio_model.h>

class QCheckBox;
class QComboBox;
class QFrame;
class QPlainTextEdit;
class QProcess;
class QProgressBar;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSlider;
class QTimer;
class QToolButton;
class QTreeWidget;

namespace sme::app {

class AssetBrowser;
class Editor;
class MaterialCache;
class ModelViewer;

/// The Animations page (see the file comment).
class AnimationsPage : public QWidget {
	Q_OBJECT

public:
	AnimationsPage(Editor* editor, AssetBrowser* assets, MaterialCache* materials, QWidget* parent = nullptr);
	~AnimationsPage() override;

	/// Shows this model ("models/alyx.mdl") and loads its animations.
	void selectModel(const QString& path);
	[[nodiscard]] QString currentModel() const { return m_model; }
	/// Chooses a sequence of the shown model by name (case does not matter); false if it has none of that name.
	bool selectSequence(const QString& name);
	[[nodiscard]] QString currentSequence() const;
	/// The model and sequence an animation field asked for: shown and playing (the sequence is
	/// chosen once the model has loaded).
	void showFor(const QString& model, const QString& sequence);

	/// Playback.
	void setPlaying(bool on);
	[[nodiscard]] bool playing() const { return m_playing; }
	/// Position in the sequence, 0 (first frame) to 1 (last frame).
	void setCycle(float cycle);
	[[nodiscard]] float cycle() const { return m_cycle; }
	void setSpeed(float speed);
	void setLooping(bool on);

	/// The loaded animations (nullptr while loading or without a model).
	[[nodiscard]] const AnimatedModel* animated() const { return m_animated ? &*m_animated : nullptr; }
	[[nodiscard]] ModelViewer* viewer() const { return m_viewer; }
	[[nodiscard]] QListWidget* modelList() const { return m_models; }
	[[nodiscard]] QTreeWidget* sequenceList() const { return m_sequences; }
	[[nodiscard]] QListWidget* usesList() const { return m_uses; }
	[[nodiscard]] QPushButton* useButton() const { return m_use; }
	/// Filters of the sequence list (item data: sequence_weapon bits, 0 = all / stance -1 all, 0
	/// standing, 1 crouching / kind -1 all, else SequenceKind).
	[[nodiscard]] QComboBox* weaponFilter() const { return m_weaponFilter; }
	[[nodiscard]] QComboBox* stanceFilter() const { return m_stanceFilter; }
	[[nodiscard]] QComboBox* kindFilter() const { return m_kindFilter; }
	/// Models still to be checked for animations (0: the list is complete).
	[[nodiscard]] int modelsToCheck() const { return int(m_scanQueue.size()) + (m_scanner.isRunning() ? 1 : 0); }

	/// Puts the current sequence on the selected entity (`key`: which field of a
	/// scripted_sequence; empty: its Pre Action Idle). False with a message if that is not possible.
	/// For an animation only Mapsmith's new model has, the entity switches to that model too.
	bool useOnSelection(const QString& key = {});

	// --- Add animation (see animation_import.h) -------------------------------------------
	/// Reads an .smd and adds it to the shown model as a new animation: it plays at once and is
	/// built into the mod by compileImports(). False with `error` if the file does not fit.
	bool importAnimation(const QString& smdPath, ImportedAnimation options, QString* error = nullptr);
	/// New animations not built yet.
	[[nodiscard]] int pendingImports() const { return int(m_imports.size()); }
	/// Writes the new model's sources below the mod (modelsrc/mapsmith/...) and runs studiomdl
	/// (compileFinished follows). False if it cannot start.
	bool compileImports();
	/// The studiomdl to run (default: studiomdl.exe next to the game's vbsp.exe).
	void setStudiomdlProgram(const QString& program) { m_studiomdlOverride = program; }
	[[nodiscard]] QString studiomdlProgram() const;
	[[nodiscard]] QString compileLog() const;

signals:
	/// The model's animations were read (or could not be).
	void modelLoaded();
	/// An entity in "Used in this map" was double-clicked: select it in the map.
	void entityRequested(int entityId);
	/// studiomdl finished (ok: the new model is in the mod and shown).
	void compileFinished(bool ok);

protected:
	void showEvent(QShowEvent* event) override;
	void hideEvent(QHideEvent* event) override;

private:
	void refreshModels();
	void fillModelList();
	/// Queues the models not checked yet (the map's first) and starts checking them.
	void checkModels();
	void checkNext();
	/// Fills the filter drop-downs with the counts of the shown model's sequences.
	void updateFilters();
	void loadAnimations();
	void showSequences();
	void updateUses();
	void updateTransport();
	void updateUseButton();
	void applyPose();
	void tick();
	[[nodiscard]] int sequenceIndex() const;
	/// "Add animation ...": choose an .smd (the file dialog starts in `startFolder`), name it, preview it.
	void askImport(const QString& startFolder = {});
	void updatePendingBar();
	/// The model the new animations belong to (the original, also when Mapsmith's copy is shown).
	[[nodiscard]] QString originalModel() const;

	Editor* m_editor;
	AssetBrowser* m_assets;
	MaterialCache* m_materials;
	QString m_model;
	QString m_pendingSequence;        ///< Chosen when the model's animations arrive (showFor)
	bool m_pendingPlay = false;
	std::shared_ptr<const ModelGeometry> m_geometry;
	std::optional<AnimatedModel> m_animated;
	QFutureWatcher<std::optional<AnimatedModel>> m_loader;
	int m_generation = 0;

	QLineEdit* m_search = nullptr;
	QListWidget* m_models = nullptr;
	QHash<QString, bool> m_animatedModels;   ///< Lower-case path -> really animates (checked so far)
	QStringList m_scanQueue;                  ///< Models still to check, in order
	QFutureWatcher<QList<QPair<QString, bool>>> m_scanner;
	int m_scanGeneration = 0;
	QTimer* m_listRefresh = nullptr;          ///< Coalesces list updates while checking
	QStringList m_mapModels;          ///< Models the map's entities animate, most used first
	ModelViewer* m_viewer = nullptr;
	QLabel* m_title = nullptr;
	QToolButton* m_play = nullptr;
	QSlider* m_scrub = nullptr;
	QComboBox* m_speed = nullptr;
	QCheckBox* m_loop = nullptr;
	QLabel* m_time = nullptr;
	QLineEdit* m_sequenceSearch = nullptr;
	QComboBox* m_weaponFilter = nullptr;
	QComboBox* m_stanceFilter = nullptr;
	QComboBox* m_kindFilter = nullptr;
	QTreeWidget* m_sequences = nullptr;
	QListWidget* m_uses = nullptr;
	QPushButton* m_use = nullptr;
	QPushButton* m_add = nullptr;
	QPushButton* m_addSample = nullptr;   ///< "Use a sample ...": the dialog in samples/animations
	QTimer* m_timer = nullptr;
	QTimer* m_mapRefresh = nullptr;
	QElapsedTimer m_clock;
	bool m_playing = false;
	float m_cycle = 0;
	float m_speedFactor = 1;
	bool m_looping = true;
	// Add animation.
	struct PendingImport {
		ImportedAnimation options;
		QString smdText;
	};
	std::vector<PendingImport> m_imports;
	QString m_importModel;            ///< The model the pending imports were made for
	QFrame* m_pendingBar = nullptr;
	QLabel* m_pendingLabel = nullptr;
	QPushButton* m_compile = nullptr;
	QProgressBar* m_progress = nullptr;
	QPlainTextEdit* m_log = nullptr;
	QPointer<QProcess> m_studiomdl;
	QString m_studiomdlOverride;
};

} // namespace sme::app
