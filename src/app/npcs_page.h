// npcs_page.h - The NPCs page of the side bar: every NPC of the game and mod (sme/npcs.h - only
// classes with a model, no helpers) with its model in 3D (free orbit, from all sides), its
// animations to play, and five tabs:
//   Stats      - health and damage (the NPC's skill values) and the damage of the weapons it
//                carries when NPCs shoot them, saved into the mod's cfg/skill.cfg; Restore
//                original sets them back to the game's.
//   Weapons    - the weapons it can carry (FGD additionalequipment); "Place in map" arms the
//                entity tool with the NPC, the chosen weapon and the chosen model.
//   Look       - model (citizen heads ...), skin, the model's materials: Edit material,
//                Enhance textures, Glow.
//   Sounds     - the sound script entries named after the NPC down to their wave files: play,
//                replace with an own file at the same path in the mod, edit, restore.
//   Animations - the sequences with activity and length; "Edit animation ..." opens the
//                Animation editor.
// Everything is read when the page is shown; nothing runs while it is hidden.

#pragma once

#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QHash>
#include <QWidget>

#include <memory>
#include <optional>
#include <vector>

#include <sme/assets.h>
#include <sme/npcs.h>
#include <sme/studio_anim.h>
#include <sme/studio_model.h>

class QComboBox;
class QFormLayout;
class QLabel;
class QListWidget;
class QPushButton;
class QSlider;
class QTabWidget;
class QTimer;
class QToolButton;
class QTreeWidget;

namespace sme::app {

class Editor;
class GlowDialog;
class MaterialCache;
class ModelViewer;
class SoundPlayer;

/// The NPCs page (see the file comment).
class NpcsPage : public QWidget {
	Q_OBJECT

public:
	NpcsPage(Editor* editor, MaterialCache* materials, QWidget* parent = nullptr);
	~NpcsPage() override;

	/// Reads the NPCs again.
	void refresh();
	/// Chooses an NPC by class ("npc_combine_s"); false if it is not listed.
	bool selectNpc(const QString& className);
	[[nodiscard]] QString currentNpc() const;
	[[nodiscard]] const NpcInfo* currentInfo() const;
	/// The model shown.
	[[nodiscard]] QString currentModel() const { return m_model; }
	/// Shows another of the NPC's models (a citizen's head); false if it has no such model.
	bool selectModel(const QString& model);
	bool selectSequence(const QString& name);
	[[nodiscard]] QString currentSequence() const;
	void setPlaying(bool on);
	[[nodiscard]] bool playing() const { return m_playing; }
	/// The weapon chosen on the Weapons tab (empty: none).
	[[nodiscard]] QString currentWeapon() const;
	bool selectWeapon(const QString& weaponClass);

	[[nodiscard]] QListWidget* npcList() const { return m_list; }
	[[nodiscard]] ModelViewer* viewer() const { return m_viewer; }
	[[nodiscard]] QTabWidget* tabs() const { return m_tabs; }
	[[nodiscard]] QTreeWidget* sequenceTree() const { return m_sequences; }
	[[nodiscard]] QTreeWidget* soundTree() const { return m_sounds; }
	[[nodiscard]] QListWidget* materialList() const { return m_materialList; }
	/// The editor of a skill value on the Stats tab, or nullptr.
	[[nodiscard]] QWidget* statEditor(const QString& name) const { return m_statEditors.value(name); }
	/// Writes the changed skill values into the mod's cfg/skill.cfg. False with a message.
	bool saveStats();
	/// Sets the NPC's skill values back to the game's.
	bool restoreStats();
	/// Arms the entity tool with the NPC, weapon and model (placeRequested).
	void placeInMap();
	bool replaceWave(const QString& wave, const QString& sourceFile);
	bool restoreWave(const QString& wave);
	GlowDialog* openGlow();

signals:
	void placeRequested(const QString& classname, const std::vector<std::pair<std::string, std::string>>& keys);
	void openMaterialRequested(const QString& material);
	void editSoundRequested(const QString& wave);
	void editAnimationRequested(const QString& model, const QString& sequence);
	void enhanceTexturesRequested(const QStringList& materials);
	/// A glow was applied or taken out (the mod's files changed).
	void modChanged();

protected:
	void showEvent(QShowEvent* event) override;
	void hideEvent(QHideEvent* event) override;

private:
	void buildUi();
	QWidget* buildStatsTab();
	QWidget* buildWeaponsTab();
	QWidget* buildLookTab();
	QWidget* buildSoundsTab();
	QWidget* buildAnimationsTab();
	void showNpc();
	void loadModel();
	void showSequences();
	void showSounds();
	void showLook();
	void showStats();
	void showWeapons();
	void updateButtons();
	void tick();
	void applyPose();
	void updateTransport();
	[[nodiscard]] int sequenceIndex() const;
	[[nodiscard]] std::filesystem::path modDir() const;
	/// The game's skill values (the mod's cfg/skill.cfg if it has one, else the game's).
	[[nodiscard]] std::map<std::string, std::string> skillValues(bool modFirst) const;
	[[nodiscard]] QString selectedWave() const;
	void playSound(const QString& soundOrWave);
	void noteModFile(const std::filesystem::path& file);
	void message(const QString& text);

	Editor* m_editor;
	MaterialCache* m_materials;
	bool m_dirty = true;

	std::vector<NpcInfo> m_npcs;
	int m_current = -1;
	QString m_model;
	std::shared_ptr<const ModelGeometry> m_geometry;
	std::optional<AnimatedModel> m_animated;
	int m_generation = 0;
	QFutureWatcher<std::optional<AnimatedModel>> m_loader;
	QFutureWatcher<std::vector<SoundScriptEntry>> m_scriptLoader;
	std::vector<SoundScriptEntry> m_scripts;
	bool m_scriptsLoaded = false;

	QTimer* m_timer = nullptr;
	QElapsedTimer m_clock;
	bool m_playing = false;
	float m_cycle = 0;

	QListWidget* m_list = nullptr;
	QLabel* m_title = nullptr;
	ModelViewer* m_viewer = nullptr;
	QToolButton* m_play = nullptr;
	QSlider* m_scrub = nullptr;
	QLabel* m_time = nullptr;
	QTabWidget* m_tabs = nullptr;
	QFormLayout* m_statsForm = nullptr;
	QLabel* m_statsNote = nullptr;
	QPushButton* m_saveStats = nullptr;
	QPushButton* m_restoreStats = nullptr;
	QHash<QString, QWidget*> m_statEditors;
	QHash<QString, QString> m_statShown;
	QListWidget* m_weaponList = nullptr;
	QPushButton* m_place = nullptr;
	QComboBox* m_modelBox = nullptr;
	QComboBox* m_skin = nullptr;
	QListWidget* m_materialList = nullptr;
	QPushButton* m_editMaterial = nullptr;
	QPushButton* m_enhance = nullptr;
	QPushButton* m_glow = nullptr;
	QTreeWidget* m_sounds = nullptr;
	QPushButton* m_playSound = nullptr;
	QPushButton* m_replaceSound = nullptr;
	QPushButton* m_editSound = nullptr;
	QPushButton* m_restoreSound = nullptr;
	QTreeWidget* m_sequences = nullptr;
	QPushButton* m_editAnimation = nullptr;
	SoundPlayer* m_player = nullptr;
};

} // namespace sme::app
