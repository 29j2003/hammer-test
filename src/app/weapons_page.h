// weapons_page.h - The Weapons page of the side bar: every weapon of the mod (scripts/weapon_*.txt,
// see sme/weapons.h) with its first-person model in 3D as the game shows it (or free around it,
// or the world model), all its animations to play, and four tabs:
//   Animations - the sequences with activity and length; "Edit animation ..." (the animation
//                editor, Animations page until it is there).
//   Sounds     - the script's sound slots down to their wave files (play, replace with an own
//                file at the same path in the mod, edit on the Sounds page, restore) and the
//                animation events (play a sound event, change its sound or frame, remove, add);
//                event changes are built into the mod by rebuilding the model (ModelRebuildJob).
//   Look       - the model's materials with pictures, skins, Edit material, Replace texture
//                (a picture as the texture at the same path in the mod), Enhance textures.
//   Stats      - the script's values (magazine, ammo, HUD slot, Mapbase view model FOV and
//                sway ...) and the skill values of its ammo (damage, most ammo carried), saved
//                as the mod's own copies; Restore original takes them out again.
//   Effects    - "Glow ..." (GlowDialog for the view model's materials: always or only during
//                animations) and the view model's particle effects (EffectsPanel, e.g. sparks
//                during the reload). "Compile into model ..." rebuilds the view model with the
//                same job as Build model and the pending sound edits: both are kept in the
//                model's stored ModelRebuildChanges (sound events per sequence, effects apart),
//                so neither overwrites the other. Effect events in a rebuilt model are not
//                listed on the Sounds tab (removeAddedEvents).
// Everything is read when the page is shown (the game's file system in the background where it
// takes time); nothing runs while the page is hidden.

#pragma once

#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QHash>
#include <QPointer>
#include <QWidget>

#include <map>
#include <memory>
#include <optional>
#include <vector>

#include <sme/assets.h>
#include <sme/localization.h>
#include <sme/model_events.h>
#include <sme/model_rebuild.h>
#include <sme/studio_anim.h>
#include <sme/studio_model.h>
#include <sme/weapons.h>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSlider;
class QSpinBox;
class QTabWidget;
class QTimer;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace sme::app {

class AssetBrowser;
class Editor;
class EffectsPanel;
class GlowDialog;
class MaterialCache;
class ModelRebuildJob;
class ModelViewer;
class SoundPlayer;

/// The Weapons page (see the file comment).
class WeaponsPage : public QWidget {
	Q_OBJECT

public:
	enum class View { FirstPerson, Free, World };
	/// Kinds of rows in the sound tree (Qt::UserRole + 1).
	enum SoundRow { SlotRow = 1, WaveRow, EventRow };

	/// `assets` (may be null) gives the particle systems for the Effects tab.
	WeaponsPage(Editor* editor, MaterialCache* materials, AssetBrowser* assets = nullptr, QWidget* parent = nullptr);
	~WeaponsPage() override;

	/// Reads the weapons again (also when the page is shown after the game changed).
	void refresh();
	/// Chooses a weapon by class ("weapon_smg1"); false if there is none.
	bool selectWeapon(const QString& className);
	[[nodiscard]] QString currentWeapon() const;
	[[nodiscard]] const WeaponScript* currentScript() const;
	/// The model shown (view model or world model).
	[[nodiscard]] QString currentModel() const;
	void setView(View view);
	[[nodiscard]] View view() const { return m_view; }
	/// Chooses a sequence by name; false if the model has none.
	bool selectSequence(const QString& name);
	[[nodiscard]] QString currentSequence() const;
	void setPlaying(bool on);
	[[nodiscard]] bool playing() const { return m_playing; }

	// The tabs, for tests and the main window.
	[[nodiscard]] QListWidget* weaponList() const { return m_list; }
	[[nodiscard]] ModelViewer* viewer() const { return m_viewer; }
	[[nodiscard]] QTabWidget* tabs() const { return m_tabs; }
	[[nodiscard]] QTreeWidget* sequenceTree() const { return m_sequences; }
	[[nodiscard]] QTreeWidget* soundTree() const { return m_sounds; }
	[[nodiscard]] QListWidget* materialList() const { return m_materialList; }
	[[nodiscard]] ModelRebuildJob* rebuildJob() const { return m_job; }
	/// The Effects tab's list of the view model's particle effects.
	[[nodiscard]] EffectsPanel* effectsPanel() const { return m_effects; }
	/// "Glow ...": the glow dialog for the view model's materials (nullptr while it is not shown).
	GlowDialog* openGlow(const QString& material = {});
	/// The editor of a Stats field (script key or skill value name), or nullptr.
	[[nodiscard]] QWidget* statEditor(const QString& key) const { return m_statEditors.value(key); }
	/// Writes the changed Stats values into the mod (script copy, skill.cfg). False with a message.
	bool saveStats();
	/// Takes the weapon's script copy out of the mod and sets its skill values back to the game's.
	bool restoreStats();

	/// Sound actions on a wave ("weapons/smg1/smg1_fire1.wav").
	bool replaceWave(const QString& wave, const QString& sourceFile);
	bool restoreWave(const QString& wave);
	/// Points a script slot ("reload") at another sound script entry; saved into the mod's script copy.
	bool setSlotSound(const QString& slot, const QString& sound);
	/// Event edits waiting for "Build model" (sequences whose list was changed).
	[[nodiscard]] int pendingEventSequences() const { return int(m_pendingEvents.size()); }
	/// The events of a sequence as they will be built (pending edits, else the model's own).
	[[nodiscard]] std::vector<ModelEvent> eventsOf(const QString& sequence) const;
	/// Replaces a sequence's events (pending until buildModel()).
	void setEvents(const QString& sequence, std::vector<ModelEvent> events);
	/// Rebuilds the view model with the pending event edits into the mod. False if it cannot start.
	bool buildModel();
	/// Takes the view model's rebuild out of the mod.
	bool restoreModel();
	/// Replaces the texture of a material of the model with a picture file.
	bool replaceTexture(const QString& material, const QString& pictureFile);
	bool restoreTexture(const QString& material);

signals:
	void openMaterialRequested(const QString& material);
	/// "Edit ..." on a wave: the Sounds page with it.
	void editSoundRequested(const QString& wave);
	/// "Edit animation ...": the model and sequence (the animation editor, or the Animations page).
	void editAnimationRequested(const QString& model, const QString& sequence);
	/// "Enhance textures ...": the model's materials.
	void enhanceTexturesRequested(const QStringList& materials);
	/// A glow was applied or taken out (the mod's files changed).
	void modChanged();

protected:
	void showEvent(QShowEvent* event) override;
	void hideEvent(QHideEvent* event) override;

private:
	void buildUi();
	QWidget* buildAnimationsTab();
	QWidget* buildSoundsTab();
	QWidget* buildLookTab();
	QWidget* buildStatsTab();
	QWidget* buildEffectsTab();
	/// The view model with the effects on top: starts the rebuild with the pending sound edits.
	bool compileEffects(const QString& model, const ModelRebuildChanges& effects);
	void showWeapon();
	void loadModel();
	void showSequences();
	void showSounds();
	void showLook();
	void showStats();
	void updateButtons();
	void tick();
	void applyPose();
	void updateTransport();
	int sequenceIndex() const;
	[[nodiscard]] std::filesystem::path modDir() const;
	/// The text of a #token from the mod's (else the game's) English text file.
	[[nodiscard]] std::string lookupToken(std::string_view token);
	/// The wave a sound name plays ("Weapon_SMG1.Reload" -> its first wave), empty if unknown.
	[[nodiscard]] QStringList wavesOf(const QString& sound) const;
	void playSound(const QString& soundOrWave);
	/// The selected sound tree row's wave / event (empty / -1).
	[[nodiscard]] QString selectedWave() const;
	[[nodiscard]] QTreeWidgetItem* selectedEventItem() const;
	void editSelectedEvent(bool changeSound);
	void addSoundEvent();
	void removeSelectedEvent();
	[[nodiscard]] QString selectedMaterial() const;
	/// The $basetexture of a material ("models/weapons/v_smg1/smg1_sheet"), empty if it has none.
	[[nodiscard]] QString baseTextureOf(const QString& material) const;
	void noteModFile(const std::filesystem::path& file);
	void message(const QString& text);

	Editor* m_editor;
	MaterialCache* m_materials;
	AssetBrowser* m_assets;
	bool m_dirty = true;   ///< Weapons must be read again (game changed) when the page shows

	std::vector<WeaponScript> m_weapons;
	int m_current = -1;
	View m_view = View::FirstPerson;
	QString m_model;
	std::shared_ptr<const ModelGeometry> m_geometry;
	std::optional<AnimatedModel> m_animated;
	std::vector<SequenceEvents> m_modelEvents;                       ///< As the model's file has them
	std::map<std::string, std::vector<ModelEvent>> m_pendingEvents;  ///< Edits not built yet (lower-case sequence)
	int m_generation = 0;
	QFutureWatcher<std::optional<AnimatedModel>> m_loader;
	QFutureWatcher<std::vector<SoundScriptEntry>> m_scriptLoader;
	std::vector<SoundScriptEntry> m_scripts;
	bool m_scriptsLoaded = false;
	std::optional<LocalizationFile> m_texts;   ///< The English texts (printname tokens), read once per game
	bool m_textsRead = false;

	// Playback.
	QTimer* m_timer = nullptr;
	QElapsedTimer m_clock;
	bool m_playing = false;
	float m_cycle = 0;

	// Widgets.
	QListWidget* m_list = nullptr;
	QLabel* m_title = nullptr;
	QComboBox* m_viewMode = nullptr;
	ModelViewer* m_viewer = nullptr;
	QToolButton* m_play = nullptr;
	QSlider* m_scrub = nullptr;
	QLabel* m_time = nullptr;
	QTabWidget* m_tabs = nullptr;
	QTreeWidget* m_sequences = nullptr;
	QPushButton* m_editAnimation = nullptr;
	QTreeWidget* m_sounds = nullptr;
	QPushButton* m_playSound = nullptr;
	QPushButton* m_replaceSound = nullptr;
	QPushButton* m_editSound = nullptr;
	QPushButton* m_restoreSound = nullptr;
	QPushButton* m_slotSound = nullptr;
	QPushButton* m_eventSound = nullptr;
	QPushButton* m_eventFrame = nullptr;
	QPushButton* m_eventRemove = nullptr;
	QPushButton* m_eventAdd = nullptr;
	QPushButton* m_build = nullptr;
	QPushButton* m_restoreModel = nullptr;
	QLabel* m_buildNote = nullptr;
	QPlainTextEdit* m_buildLog = nullptr;
	QListWidget* m_materialList = nullptr;
	QComboBox* m_skin = nullptr;
	QPushButton* m_editMaterial = nullptr;
	QPushButton* m_replaceTexture = nullptr;
	QPushButton* m_restoreTexture = nullptr;
	QPushButton* m_enhance = nullptr;
	QFormLayout* m_statsForm = nullptr;
	QWidget* m_statsBox = nullptr;
	QLabel* m_statsNote = nullptr;
	QPushButton* m_saveStats = nullptr;
	QPushButton* m_restoreStats = nullptr;
	QPushButton* m_glow = nullptr;
	EffectsPanel* m_effects = nullptr;
	QHash<QString, QWidget*> m_statEditors;
	QHash<QString, QString> m_statShown;   ///< Value each editor showed when filled (only changed ones are written)
	ModelRebuildJob* m_job = nullptr;
	SoundPlayer* m_player = nullptr;
};

} // namespace sme::app
