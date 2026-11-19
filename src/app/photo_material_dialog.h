// photo_material_dialog.h - "New material from photo ..." (Materials page, "..." menu): a photo
// becomes a tileable texture with bump map, gloss and VMT in the mod (core: photo_material.h).
// The preview works on a 256 version of the texture (instant); Create makes the full size.

#pragma once

#include <QDialog>
#include <QImage>
#include <QTimer>

#include <optional>

#include <sme/photo_material.h>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSlider;

namespace sme::app {

class Editor;
class MaterialPreview;

class PhotoMaterialDialog : public QDialog {
	Q_OBJECT

public:
	explicit PhotoMaterialDialog(Editor* editor, QWidget* parent = nullptr);

	/// Loads a picture file; false with a message in the dialog if it cannot be read.
	bool loadPhoto(const QString& path);
	/// The settings as the fields show them.
	[[nodiscard]] PhotoMaterialOptions options() const;
	/// Writes the material into the mod; false (message shown) if it could not.
	bool create(bool overwrite = false);
	/// The last preview (for tests).
	[[nodiscard]] const std::optional<PhotoMaterial>& preview() const { return m_preview; }

signals:
	/// A material was written ("custom/brick"); the game's files should be read again.
	void created(const QString& material);

private:
	void choosePhoto();
	void schedule();
	void updatePreview();
	void showMessage(const QString& text, bool error);

	Editor* m_editor;
	ImageRgba m_photo;
	std::optional<PhotoMaterial> m_preview;
	QTimer m_timer;
	QLineEdit* m_name = nullptr;
	QLineEdit* m_folder = nullptr;
	QComboBox* m_use = nullptr;
	QComboBox* m_surface = nullptr;
	QComboBox* m_size = nullptr;
	QSlider* m_cropSize = nullptr;
	QSlider* m_cropX = nullptr;
	QSlider* m_cropY = nullptr;
	QSlider* m_seam = nullptr;
	QSlider* m_flatten = nullptr;
	QSlider* m_bump = nullptr;
	QSlider* m_gloss = nullptr;
	QLabel* m_before = nullptr;
	QLabel* m_after = nullptr;
	QLabel* m_message = nullptr;
	MaterialPreview* m_3d = nullptr;
	QPushButton* m_create = nullptr;
};

} // namespace sme::app
