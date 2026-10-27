// material_editor.h - The Materials page of the side bar: edits a VMT with fields for what mappers change
// most and a big live preview with different lights. Laid out like Source 2's material editor:
// recent materials and a search on the left, then the parameters in groups - a group with a
// switch in its header is a feature (normal map, relief, reflections, ...) and folds away when
// it is off - and the preview with its tool bar. The groups shown depend on the kind of
// material (wall, blended ground, model, water); every other parameter is in a table.
//
// mapbase-fidelity's shaders have switches too, also before the mod has them (the preview shows
// them already; the game after deploy.bat): relief (Fidelity_Parallax: real depth from a height
// map - made here from the normal map or the texture, turned upside down, with painted zones that
// stay flat or are inverted), water with the sun's glint and foam (Water_Fidelity) and liquid in
// glass props (Fidelity_Liquid).
//
// Saving writes the VMT into the mod (a material from the game's VPKs is saved as a copy in the
// mod's materials folder; one deploy.bat made becomes the mod's own and deploy.bat leaves it
// alone from then on); "Save as new" makes a copy under another name.

#pragma once

#include <QDialog>
#include <QFutureWatcher>
#include <QImage>
#include <QWidget>

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <sme/heightmap.h>
#include <sme/material.h>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSlider;
class QTableWidget;
class QVBoxLayout;

namespace sme::app {

class Editor;
class FeatureGroup;
class MaterialPreview;

/// Paints relief zones over a texture: left mouse paints the chosen zone, right mouse erases.
class HeightZonePainter : public QWidget {
	Q_OBJECT

public:
	HeightZonePainter(const QImage& texture, unsigned zonesWidth, unsigned zonesHeight, std::vector<std::uint8_t> zones, QWidget* parent = nullptr);
	/// Zone painted with the left mouse button, and the brush size in texels.
	void setZone(HeightZone zone) { m_zone = zone; }
	void setBrush(int texels) { m_brush = texels; }
	/// Removes all zones; zones() holds one HeightZone per texel.
	void clear();
	[[nodiscard]] const std::vector<std::uint8_t>& zones() const { return m_zones; }
	[[nodiscard]] QSize sizeHint() const override;

protected:
	void paintEvent(QPaintEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;

private:
	void paintAt(const QPointF& pos, HeightZone zone);
	[[nodiscard]] QRectF imageRect() const;

	QImage m_texture;
	unsigned m_width, m_height;
	std::vector<std::uint8_t> m_zones;
	HeightZone m_zone = HeightZone::Flat;
	int m_brush = 12;
};

/// The Materials page: edits one VMT with a live preview (see the file comment).
class MaterialEditor : public QWidget {
	Q_OBJECT

public:
	explicit MaterialEditor(Editor* editor, QWidget* parent = nullptr);
	/// Shows a material ("concrete/wall01") for editing.
	void openMaterial(const QString& name);
	[[nodiscard]] QString materialName() const { return m_name; }
	[[nodiscard]] QString shader() const { return m_shader; }
	/// Current value of a parameter as it would be saved ("" if not set).
	[[nodiscard]] QString value(const QString& key) const;
	/// Where Save writes ("<mod>/materials/<name>.vmt").
	[[nodiscard]] QString savePath(const QString& name) const;
	/// Saves into the mod; with `asName` a new material of that name. False with a message on failure.
	bool save(const QString& asName = {});
	/// The mod has mapbase-fidelity's shaders (relief, water, liquid switches work).
	[[nodiscard]] bool fidelityShaders() const;
	/// Relief on/off (Fidelity_Parallax). On without a height map makes one (normal map, else texture).
	void setRelief(bool on);
	/// Makes the height map again: from the normal map (`fromNormalMap`) or the texture's brightness.
	void makeHeightMap(bool fromNormalMap);
	void setHeightInverted(bool inverted);
	/// Zones painted onto the height map (HeightZone per texel of `width` x `height`).
	void setHeightZones(std::vector<std::uint8_t> zones, unsigned width, unsigned height);
	/// The height map as it would be saved (invert and zones applied), if there is one.
	[[nodiscard]] std::optional<HeightMap> currentHeights() const;
	/// Materials opened lately (newest first), as the list on the left shows them.
	[[nodiscard]] QStringList recentMaterials() const;

signals:
	/// A material was written (the views and the browser read it again).
	void materialSaved(const QString& name);
	/// Preview textures were read (tests wait for this).
	void previewUpdated();
	/// A height map was made or read.
	void heightsReady();

private:
	enum class Kind { Wall, Blend, Model, Water, Unlit, Other };
	[[nodiscard]] Kind kind() const;
	[[nodiscard]] QString baseShader() const;   ///< The shader without the fidelity variant
	void buildFields();
	/// A group of parameters; `toggle` (may be null) is the feature switch in its header.
	QFormLayout* section(FeatureGroup*& group, const QString& title, QCheckBox* toggle = nullptr);
	/// Turns a feature off (its parameters are kept aside and come back when it is turned on again).
	void setFeature(const QString& feature, bool on);
	/// A texture name field with a small picture of the texture in front of it.
	QWidget* textureField(const QString& key, const QString& placeholder);
	QWidget* colorField(const QString& key, const QString& placeholder, QLineEdit** field = nullptr);
	QWidget* sliderField(const QString& key, double minimum, double maximum, double fallback, int decimals, QSlider** slider = nullptr);
	void loadValuesIntoFields();
	void updateSections();
	void setValue(const QString& key, const QString& value);   ///< "" removes the parameter
	void setShader(const QString& shader);
	void refreshPreview(bool reloadTextures);
	void refreshHeights();
	void loadHeightSource();
	void fillOtherTable();
	void paintZones();
	void rememberRecent(const QString& name);

	Editor* m_editor;
	QString m_name;
	std::string m_originalText;           ///< VMT text as read (empty for a new material)
	QString m_generatedBy;                ///< "parallax" etc. when deploy.bat made this material
	std::optional<Material> m_base;       ///< Patch materials: what they include, resolved
	bool m_isPatch = false;
	QString m_shader;
	std::map<QString, QString> m_values;  ///< lowercase key -> value (resolved material + edits)
	std::map<QString, std::optional<QString>> m_edits;
	bool m_loading = false;
	bool m_shaderChanged = false;

	// Relief height map: what it starts from, and what the mapper did to it.
	std::optional<HeightMap> m_heightSource;
	QString m_heightOrigin;               ///< Shown to the mapper ("made from the normal map", ...)
	bool m_heightInverted = false;
	std::vector<std::uint8_t> m_zones;
	unsigned m_zonesWidth = 0, m_zonesHeight = 0;
	bool m_heightDirty = false;           ///< Save writes a new height map
	int m_heightGeneration = 0;

	QLineEdit* m_search;
	QListWidget* m_recent;
	QVBoxLayout* m_column = nullptr;      ///< The parameter groups
	std::map<QString, QStringList> m_featureKeys;   ///< Feature switch -> the parameters it owns
	std::map<QString, QString> m_parked;  ///< Parameters of features turned off (lowercase key -> value)
	std::set<QString> m_forcedOn;         ///< Features turned on that have no value yet (empty texture field)
	QLabel* m_title;
	QLabel* m_where;
	QLabel* m_notice;
	QComboBox* m_kindBox;
	QLabel* m_shaderName;
	std::map<QString, QLineEdit*> m_textureFields;
	std::map<QString, QLabel*> m_thumbs;  ///< Small pictures in front of the texture fields
	std::map<QString, QLineEdit*> m_colorFields;
	std::map<QString, QSlider*> m_sliders;
	std::map<QString, QWidget*> m_rows;   ///< Rows hidden by kind ("$basetexture2", ...)
	FeatureGroup* m_texturesBox;
	FeatureGroup* m_bumpBox;
	FeatureGroup* m_reliefBox;
	FeatureGroup* m_envBox;
	FeatureGroup* m_alphaBox;
	FeatureGroup* m_detailBox;
	FeatureGroup* m_phongBox;
	FeatureGroup* m_waterBox;
	FeatureGroup* m_glintBox;
	FeatureGroup* m_liquidBox;
	FeatureGroup* m_otherBox;
	QCheckBox* m_bumpOn;
	QCheckBox* m_envOn;
	QCheckBox* m_detailOn;
	QComboBox* m_surface;
	QCheckBox* m_ssbump;
	QCheckBox* m_relief;
	QLabel* m_heightLabel;
	QCheckBox* m_invert;
	QPushButton* m_fromNormals;
	QPushButton* m_fromTexture;
	QPushButton* m_zonesButton;
	std::vector<QLabel*> m_fidelityNotes;   ///< "The game shows this after deploy.bat" while the mod lacks the shaders
	QComboBox* m_reflection;
	QComboBox* m_mask;
	QComboBox* m_transparency;
	QCheckBox* m_nocull;
	QCheckBox* m_selfillum;
	QDoubleSpinBox* m_detailScale;
	QComboBox* m_detailBlend;
	QCheckBox* m_phong;
	QCheckBox* m_fidelityWater;
	QCheckBox* m_liquid;
	QComboBox* m_fillMode;
	QCheckBox* m_splash;
	QCheckBox* m_spill;
	QTableWidget* m_other;
	MaterialPreview* m_preview;
	QComboBox* m_shape;
	QComboBox* m_lighting;
	QPushButton* m_save;
	QPushButton* m_saveAs;
	QLabel* m_status;
	QFutureWatcher<std::map<QString, QImage>> m_textureWatcher;
	QImage m_baseImage;                   ///< For painting zones
};

} // namespace sme::app
