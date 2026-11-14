// building_dialog.h - "Build buildings": the selected brushes become buildings (see
// sme/building_builder.h) with a live preview. Like the road builder it stays open beside the map
// (not modal): every change plans again on another thread (Editor::runBuildingJob, the newest
// wins) and the views show the result at once (brushes, and the props as ghosts); Build applies it
// as one undo step, Cancel or closing leaves the map as it was.
// It opens on a short page: Style (City 17 presets), Floors, Facade, Roof, Windows (from the facade
// texture / built into the wall / none), Detail, Condition, Seed, "Change a side ..." (a click next
// to a wall, then a small menu). The fields there write into the ones under "Advanced", folded
// away in three sections:
// Shape (block / facade only / model, which sides: presets, a list of every side with its
// own state and windows, "Pick side" takes a click in a view), Floors & windows, Ground floor,
// Roof & bands (roof, cornices, plinth, displacements), Materials (each part's material and its
// texture: scale / fit, shift, rotation), Models (props per slot: drop a model from the asset
// browser on the list, "..." takes the browser's selection, or type the path), Condition &
// variation (damage, seeds, floor variation, more facade materials, per building height/floors).

#pragma once

#include <QDialog>
#include <QFutureWatcher>
#include <QTimer>

#include <array>
#include <atomic>
#include <map>
#include <set>
#include <functional>
#include <memory>
#include <optional>

#include <sme/building_builder.h>

#include "editor.h"

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSlider;
class QSpinBox;
class QTabWidget;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace sme::app {

class AssetBrowser;
class MaterialField;

class BuildingDialog : public QDialog {
	Q_OBJECT

public:
	/// `browserModel` gives the model picked in the asset browser ("" if none).
	BuildingDialog(Editor* editor, std::function<QString()> browserModel, QWidget* parent = nullptr);
	~BuildingDialog() override;

	/// The options as the fields show them.
	[[nodiscard]] BuildingOptions options() const;
	/// The line under the fields (what will be built, or why nothing).
	[[nodiscard]] QString note() const;
	[[nodiscard]] bool planning() const;
	/// Waits for the running plan and shows it (tests).
	void waitForPlan();
	/// The last plan shown (tests).
	[[nodiscard]] const BuildingPlan* shownPlan() const { return m_shown ? &m_shown->second.plan : nullptr; }
	/// The game's materials and models: pictures, suggestions and pickers.
	void setAssets(AssetBrowser* assets);
	/// Adds a prop to the Models list (also what a drop or "..." does); `values`: its other settings.
	void addProp(const QString& model, FacadePropAnchor anchor, const FacadeProp* values = nullptr);
	/// Shows these settings in the fields (editing a building).
	void setOptions(const BuildingOptions& options);
	/// Editing buildings that are there (the selection is one or more of them).
	[[nodiscard]] bool editing() const { return !m_editIds.empty(); }
	/// The settings changed since the dialog showed the buildings (they apply to every one).
	[[nodiscard]] const std::set<std::string>& changed() const { return m_changed; }
	/// Selects the side a point is next to (what a click with "Pick side" does) and offers what it
	/// gets in a small menu.
	void pickSideAt(const Vec3& point);
	/// What a side gets (the side menu, tests).
	void setSideState(const std::pair<int, int>& key, SideState state);
	/// Applies a look preset to the fields (the Style box does it).
	void applyStyle(BuildingStyle style);
	/// Tests: pickSideAt selects without the menu.
	void setPickMenuQuiet(bool quiet) { m_pickMenuQuiet = quiet; }

public slots:
	void accept() override;
	void done(int result) override;

private:
	void refresh();
	void planFinished();
	void showPlan(const Editor::BuildingJob& job, const Editor::BuildingResult& result);
	/// The side list follows the plan (rows for new sides, the states the presets gave).
	void fillSides(const BuildingPlan& plan);
	void fillLots(const BuildingPlan& plan);
	void loadSettings();
	void saveSettings() const;
	/// The simple page shows what the fields behind it say.
	void syncSimple();
	/// The Models rows of the Detail amount follow it (and the game's models).
	void updateDetailProps();
	[[nodiscard]] std::vector<std::string> gameMaterials() const;
	[[nodiscard]] std::vector<std::string> gameModels() const;
	/// A field was changed by hand: noted (editing several buildings applies only what changed).
	void noteChanged(const QString& name);
	/// The selection is (or no longer is) buildings made before: their settings into the fields.
	void followSelection();
	/// Editing one building with a new size: its length and depth.
	[[nodiscard]] std::optional<std::pair<double, double>> newSize() const;

	Editor* m_editor;
	std::function<QString()> m_browserModel;
	QTabWidget* m_tabs;
	// The simple page (it writes into the fields of the Advanced sections)
	QComboBox* m_style;
	QSpinBox* m_simpleFloors;
	MaterialField* m_simpleFacade;
	MaterialField* m_simpleRoof;
	QComboBox* m_simpleWindows;
	QComboBox* m_detail;
	QSlider* m_simpleCondition;
	QSpinBox* m_simpleSeed;
	QToolButton* m_advanced;
	QCheckBox* m_windowsFromTexture;
	QSpinBox* m_textureFloors;
	QCheckBox* m_wholeTiles;
	MaterialField* m_pipeMaterial;
	bool m_syncing = false;
	bool m_fresh = false;           ///< No settings saved yet: the first style is applied
	bool m_pickMenuQuiet = false;
	// Shape
	QComboBox* m_mode;
	QDoubleSpinBox* m_facadeThickness;
	QDoubleSpinBox* m_returnDepth;
	QComboBox* m_sidesPreset;
	QTreeWidget* m_sides;
	QPushButton* m_pickSide;
	QLineEdit* m_lotModel;
	QLabel* m_editInfo;
	QWidget* m_sizeRow;
	QLabel* m_sizeLabel;
	QDoubleSpinBox* m_sizeLength;
	QDoubleSpinBox* m_sizeDepth;
	QPushButton* m_unlink;
	// Floors & windows
	QComboBox* m_heightMode;
	QDoubleSpinBox* m_height;
	QSpinBox* m_floors;
	QDoubleSpinBox* m_floorHeight;
	QDoubleSpinBox* m_groundHeight;
	QDoubleSpinBox* m_topWall;
	QComboBox* m_windowStyle;
	QComboBox* m_layout;
	QSpinBox* m_windowCount;
	QDoubleSpinBox* m_windowWidth;
	QDoubleSpinBox* m_windowHeight;
	QDoubleSpinBox* m_sill;
	QDoubleSpinBox* m_gap;
	QDoubleSpinBox* m_margin;
	QDoubleSpinBox* m_depth;
	QLineEdit* m_pattern;
	QCheckBox* m_sills;
	QCheckBox* m_backing;
	// Ground floor
	QComboBox* m_groundLevel;
	QDoubleSpinBox* m_raise;
	QCheckBox* m_basement;
	QComboBox* m_groundWindows;
	QDoubleSpinBox* m_shopWidth;
	QDoubleSpinBox* m_shopHeight;
	QComboBox* m_doorSides;
	QSpinBox* m_doorCount;
	QComboBox* m_doorPlace;
	QDoubleSpinBox* m_doorWidth;
	QDoubleSpinBox* m_doorHeight;
	QDoubleSpinBox* m_stepDepth;
	// Roof & bands
	QComboBox* m_roof;
	QDoubleSpinBox* m_parapet;
	QDoubleSpinBox* m_roofAngle;
	QDoubleSpinBox* m_overhang;
	QCheckBox* m_cornices;
	QDoubleSpinBox* m_corniceHeight;
	QDoubleSpinBox* m_corniceDepth;
	QCheckBox* m_topCornice;
	QCheckBox* m_plinth;
	QDoubleSpinBox* m_plinthHeight;
	QCheckBox* m_foundation;
	QCheckBox* m_wallDisp;
	QCheckBox* m_roofDisp;
	QComboBox* m_dispPower;
	QDoubleSpinBox* m_dispStrength;
	// Materials and their textures
	std::array<MaterialField*, 9> m_materials{};
	struct TextureRow {
		QComboBox* fit;
		QDoubleSpinBox* scale;
		QDoubleSpinBox* shiftU;
		QDoubleSpinBox* shiftV;
		QDoubleSpinBox* rotation;
	};
	std::array<TextureRow, static_cast<std::size_t>(BuildingPart::Count)> m_textures{};
	// Models
	QTreeWidget* m_props;
	// Condition & variation
	QDoubleSpinBox* m_damage;
	QSpinBox* m_damageSeed;
	QSpinBox* m_seed;
	QSpinBox* m_floorVariation;
	QLineEdit* m_variants;
	QCheckBox* m_touching;
	QTreeWidget* m_lots;

	QLabel* m_note;
	QDialogButtonBox* m_buttons;
	QTimer m_soon;
	bool m_finished = false;
	bool m_filling = false;   ///< Filling the side / lot lists (their signals do not plan again)
	std::map<std::pair<int, int>, SideSettings> m_sideSettings;
	std::map<int, LotSettings> m_lotSettings;
	QFutureWatcher<Editor::BuildingResult> m_watcher;
	std::shared_ptr<std::atomic<bool>> m_stop;
	std::optional<Editor::BuildingJob> m_running;
	bool m_again = false;
	std::optional<std::pair<Editor::BuildingJob, Editor::BuildingResult>> m_shown;
	std::string m_shownKey;
	std::vector<SolidRef> m_previewSolids;
	unsigned m_previewSerial = ~0u;
	AssetBrowser* m_assets = nullptr;
	std::vector<int> m_editIds;          ///< The buildings being edited
	std::set<std::string> m_changed;
	bool m_sizeTouched = false;
};

} // namespace sme::app
