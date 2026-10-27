// material_editor.h - "Material Editor" dock: edits a VMT with fields for what mappers change
// most (shader, textures, normal map, reflections and their mask, transparency, self-illumination,
// detail texture, phong, surface property) plus a table of every other parameter, and a live
// preview with different lights. Saving writes the VMT into the mod (a material from the game's
// VPKs is saved as an override in the mod's materials folder); "Save as new" makes a copy.

#pragma once

#include <QFutureWatcher>
#include <QImage>
#include <QWidget>

#include <map>
#include <optional>
#include <string>

#include <sme/material.h>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

namespace sme::app {

class Editor;
class MaterialPreview;

class MaterialEditor : public QWidget {
	Q_OBJECT

public:
	explicit MaterialEditor(Editor* editor, QWidget* parent = nullptr);
	/// Shows a material ("concrete/wall01") for editing.
	void openMaterial(const QString& name);
	[[nodiscard]] QString materialName() const { return m_name; }
	/// Current value of a parameter as it would be saved ("" if not set).
	[[nodiscard]] QString value(const QString& key) const;
	/// Where Save writes ("<mod>/materials/<name>.vmt").
	[[nodiscard]] QString savePath(const QString& name) const;
	/// Saves into the mod; with `asName` a new material of that name. False with a message on failure.
	bool save(const QString& asName = {});

signals:
	void materialSaved(const QString& name);
	/// Preview textures were read (tests wait for this).
	void previewUpdated();

private:
	void buildFields();
	void loadValuesIntoFields();
	void setValue(const QString& key, const QString& value);   ///< "" removes the parameter
	void refreshPreview(bool reloadTextures);
	void fillOtherTable();
	QLineEdit* textureField(const QString& key, const QString& placeholder);

	Editor* m_editor;
	QString m_name;
	std::string m_originalText;           ///< VMT text as read (empty for a new material)
	std::optional<Material> m_base;       ///< Patch materials: what they include, resolved
	bool m_isPatch = false;
	QString m_shader;
	std::map<QString, QString> m_values;  ///< lowercase key -> value (resolved material + edits)
	std::map<QString, std::optional<QString>> m_edits;
	bool m_loading = false;

	QLabel* m_title;
	QLabel* m_where;
	QComboBox* m_shaderBox;
	std::map<QString, QLineEdit*> m_textureFields;
	QComboBox* m_surface;
	QCheckBox* m_ssbump;
	QComboBox* m_reflection;
	QLineEdit* m_envTint;
	QComboBox* m_mask;
	QComboBox* m_transparency;
	QCheckBox* m_nocull;
	QCheckBox* m_selfillum;
	QDoubleSpinBox* m_detailScale;
	QComboBox* m_detailBlend;
	QCheckBox* m_phong;
	QDoubleSpinBox* m_phongExponent;
	QDoubleSpinBox* m_phongBoost;
	QLabel* m_parallaxNote;
	QTableWidget* m_other;
	MaterialPreview* m_preview;
	QComboBox* m_shape;
	QComboBox* m_lighting;
	QPushButton* m_save;
	QPushButton* m_saveAs;
	QLabel* m_status;
	QFutureWatcher<std::map<QString, QImage>> m_textureWatcher;
	int m_textureGeneration = 0;
};

} // namespace sme::app
