// import_dialog.h - "Import ..." on the Sounds, Models, Materials and Particles pages: brings the
// mapper's own files into the mod. One dialog per kind: the file and what it holds (length and
// format, the picture, the model's size and materials, the particle systems), where it goes and
// under which name, the options, and after importing one line saying what was written - with
// Undo. Existing files are never replaced unless "Replace" is ticked.
//
// The work is done by src/core asset_import / model_import; models are compiled with the game's
// studiomdl in the background. Every import is listed in the mod (cfg/mapsmith_imports.txt), so
// "Remove import" works after a restart too.

#pragma once

#include <QDialog>
#include <QPointer>
#include <QString>

#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

#include <sme/asset_import.h>
#include <sme/audio.h>
#include <sme/model_import.h>
#include <sme/texture.h>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QProcess;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QVBoxLayout;

namespace sme::app {

class Editor;
class SoundPlayer;

/// The import dialog for one kind of file (see the file comment).
class ImportDialog : public QDialog {
	Q_OBJECT

public:
	enum class Kind { Sound, Material, Model, Particles };

	ImportDialog(Editor* editor, Kind kind, QWidget* parent = nullptr);
	~ImportDialog() override;

	/// Reads a file to import (also what "Choose file ..." does). False with the reason shown.
	bool setSourceFile(const QString& path);
	/// Imports with the current settings. Models compile in the background: finished() follows.
	bool importNow();
	/// Takes the last import back.
	bool undoLast();

	[[nodiscard]] Kind kind() const { return m_kind; }
	[[nodiscard]] const std::optional<ImportRecord>& lastImport() const { return m_last; }
	/// The line under the buttons (what happened).
	[[nodiscard]] QString resultText() const;
	/// Whether a model is being compiled now.
	[[nodiscard]] bool busy() const;
	/// studiomdl for the current game: the setting, else next to VBSP.
	[[nodiscard]] static QString studiomdlPath(Editor* editor);
	/// Mapsmith's sample files for a kind (samples/sounds, ...; empty without samples).
	[[nodiscard]] static QString sampleFolder(Kind kind);
	/// File dialog filter for a kind.
	[[nodiscard]] static QString fileFilter(Kind kind);

signals:
	/// Something was imported or taken back: the game's files should be read again.
	void modChanged();
	/// An import (of any kind) is complete; `ok` false if it failed.
	void finished(bool ok);

private:
	void buildCommon();
	void buildSound(QFormLayout* form);
	void buildMaterial(QFormLayout* form);
	void buildModel(QFormLayout* form);
	void buildParticles(QFormLayout* form);
	void updateTarget();
	void showResult(const QString& text, bool ok);
	void finishImport(const ImportRecord& record, const QString& text);
	[[nodiscard]] std::filesystem::path modDir() const;
	[[nodiscard]] std::vector<std::filesystem::path> targets() const;
	[[nodiscard]] SoundImportOptions soundOptions() const;
	[[nodiscard]] MaterialImportOptions materialOptions() const;
	[[nodiscard]] ModelImportOptions modelOptions() const;
	bool importModel();
	void modelCompiled(int exitCode);

	Editor* m_editor;
	Kind m_kind;
	QString m_source;
	// What the source holds.
	std::shared_ptr<DecodedAudio> m_audio;
	std::optional<ImageRgba> m_image;
	std::optional<ImageRgba> m_normalImage;
	std::optional<ImportMesh> m_mesh;
	std::vector<std::byte> m_pcf;

	QLabel* m_file = nullptr;
	QLabel* m_preview = nullptr;           ///< Picture or description of the source
	QLineEdit* m_folder = nullptr;
	QLineEdit* m_name = nullptr;
	QLabel* m_target = nullptr;            ///< Where it goes ("sound/custom/hum.wav")
	QCheckBox* m_replace = nullptr;        ///< Shown when the target exists
	QPushButton* m_import = nullptr;
	QPushButton* m_undo = nullptr;
	QLabel* m_result = nullptr;
	QPlainTextEdit* m_details = nullptr;   ///< studiomdl's output (models)
	QProgressBar* m_progress = nullptr;
	// Sound
	QComboBox* m_rate = nullptr;
	QComboBox* m_channels = nullptr;
	QCheckBox* m_normalize = nullptr;
	QCheckBox* m_loop = nullptr;
	QCheckBox* m_script = nullptr;
	QLineEdit* m_scriptName = nullptr;
	QDoubleSpinBox* m_volume = nullptr;
	QSpinBox* m_pitch = nullptr;
	QComboBox* m_soundLevel = nullptr;
	QPushButton* m_play = nullptr;
	SoundPlayer* m_player = nullptr;
	// Material
	QComboBox* m_use = nullptr;
	QComboBox* m_surface = nullptr;
	QComboBox* m_alpha = nullptr;
	QComboBox* m_maxSize = nullptr;
	QCheckBox* m_compress = nullptr;
	QLabel* m_normalFile = nullptr;
	// Model
	QComboBox* m_modelKind = nullptr;
	QComboBox* m_units = nullptr;
	QCheckBox* m_yUp = nullptr;
	QComboBox* m_collision = nullptr;
	QDoubleSpinBox* m_mass = nullptr;
	QListWidget* m_materials = nullptr;
	// Particles
	QCheckBox* m_precache = nullptr;
	QListWidget* m_systems = nullptr;

	std::optional<ImportRecord> m_last;
	std::optional<ModelImportPlan> m_plan;
	QPointer<QProcess> m_process;
};

} // namespace sme::app
