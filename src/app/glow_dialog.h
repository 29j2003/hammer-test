// glow_dialog.h - "Glow ..." on the Models page (and the Weapons page): makes parts of a model's
// material glow - where (bright parts found automatically, a picture, everywhere), colour and
// brightness, steady or pulsing, always or only while certain animations play. The model view
// shows it at once; Apply writes it into the mod (sme/model_glow.h), "Restore original" takes it
// out again. Not modal: the model can be turned while choosing.

#pragma once

#include <QDialog>
#include <QImage>

#include <optional>
#include <string>
#include <vector>

#include <sme/asset_import.h>
#include <sme/model_glow.h>
#include <sme/texture.h>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QPushButton;
class QRadioButton;
class QSlider;

namespace sme::app {

class Editor;
class ModelViewer;

/// The dialog (see the file comment).
class GlowDialog : public QDialog {
	Q_OBJECT

public:
	/// `materials`: the model's materials ("models/weapons/v_smg1/smg1"); `animations`: its
	/// sequences with their activities ("reload  ACT_VM_RELOAD"), as (sequence, activity).
	/// `viewer` (may be null) shows the glow while the dialog is open.
	GlowDialog(Editor* editor, ModelViewer* viewer, QString model, QStringList materials,
	           std::vector<std::pair<std::string, std::string>> animations, QWidget* parent = nullptr);
	~GlowDialog() override;

	/// The settings the fields show.
	[[nodiscard]] GlowSettings settings() const;
	/// Picks a material of the list (tests, the material list's "Make it glow ...").
	void selectMaterial(const QString& material);
	/// Sets the fields from settings (tests).
	void setSettings(const GlowSettings& settings);
	/// Share of the texture that glows with the current fields (0..1).
	[[nodiscard]] double coverage() const { return m_coverage; }
	/// Writes the glow into the mod; false with the reason shown in the dialog.
	bool apply();
	/// Takes Mapsmith's glow of the material out of the mod again.
	bool restore();
	/// The mask the preview shows (tests).
	[[nodiscard]] const QImage& maskImage() const { return m_maskImage; }

signals:
	/// Apply or Restore changed the mod's files.
	void modChanged();

private:
	void loadMaterial();
	void updatePreview();
	void updateButtons();
	[[nodiscard]] std::optional<ImportRecord> currentRecord() const;

	Editor* m_editor;
	ModelViewer* m_viewer;
	QString m_model;
	std::vector<std::pair<std::string, std::string>> m_animations;
	QComboBox* m_material;
	QRadioButton* m_auto;
	QRadioButton* m_picture;
	QRadioButton* m_whole;
	QSlider* m_threshold;
	QPushButton* m_choosePicture;
	QPushButton* m_color;
	QSlider* m_strength;
	QCheckBox* m_pulse;
	QDoubleSpinBox* m_pulseSeconds;
	QRadioButton* m_always;
	QRadioButton* m_during;
	QListWidget* m_animationList;
	QDoubleSpinBox* m_from;
	QDoubleSpinBox* m_to;
	QLabel* m_maskView;
	QLabel* m_info;
	QPushButton* m_apply;
	QPushButton* m_restore;
	QColor m_colorValue = Qt::white;
	std::optional<ImageRgba> m_base;      ///< The material's base texture (at most 1024)
	std::optional<ImageRgba> m_pictureImage;
	QImage m_maskImage;
	double m_coverage = 0;
	bool m_loading = false;
};

} // namespace sme::app
