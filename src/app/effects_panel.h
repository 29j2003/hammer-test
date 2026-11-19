// effects_panel.h - "Effects" of a model (Models page; the Weapons page uses the same widget):
// particle systems at an attachment, always or during part of an animation (a glow sprite while
// reloading, sparks when a part snaps shut ...). Add/Edit opens a small assistant: Effect ->
// Where (attachment, shown as points in the model view) -> When. The list is kept in the mod
// (sme/model_effects.h: modelsrc/<model>/mapsmith_effects.txt); compiling the model takes the
// events and the $keyvalues block from effectEvents/effectKeyValues ("Compile into model"
// emits compileRequested with them).

#pragma once

#include <QDialog>
#include <QWidget>

#include <string>
#include <utility>
#include <vector>

#include <sme/animation_import.h>
#include <sme/model_effects.h>

class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSpinBox;

namespace sme::app {

class AssetBrowser;
class Editor;
class ModelViewer;

/// A model's attachment with its place at rest (for the markers).
struct AttachmentPoint {
	std::string name;
	float position[3] = {0, 0, 0};
};
/// The attachments of a model with their places in the reference pose (bones at rest).
[[nodiscard]] std::vector<AttachmentPoint> attachmentPoints(const GameFileSystem& fs, std::string_view modelPath);

/// The assistant for one effect.
class EffectDialog : public QDialog {
	Q_OBJECT

public:
	EffectDialog(AssetBrowser* assets, ModelViewer* viewer, const std::vector<AttachmentPoint>& attachments,
	             const std::vector<std::pair<std::string, int>>& sequences, ModelEffect effect, QWidget* parent = nullptr);
	~EffectDialog() override;
	[[nodiscard]] ModelEffect effect() const;
	/// Problems with the fields (empty: OK is enabled).
	[[nodiscard]] std::vector<std::string> problems() const;

private:
	void updateState();

	ModelViewer* m_viewer;
	std::vector<AttachmentPoint> m_attachments;
	std::vector<std::pair<std::string, int>> m_sequences;   ///< name, frames
	QComboBox* m_particle;
	QComboBox* m_attachment;
	QComboBox* m_attach;
	QComboBox* m_when;
	QComboBox* m_sequence;
	QSpinBox* m_start;
	QSpinBox* m_end;
	QCheckBox* m_instantly;
	QLabel* m_problems;
	QPushButton* m_ok;
};

/// The list of a model's effects (see the file comment).
class EffectsPanel : public QWidget {
	Q_OBJECT

public:
	EffectsPanel(Editor* editor, AssetBrowser* assets, QWidget* parent = nullptr);
	/// The model the list belongs to (loads its saved effects); `viewer` shows the attachments.
	void setModel(const QString& modelPath, ModelViewer* viewer);
	[[nodiscard]] const std::vector<ModelEffect>& effects() const { return m_effects; }
	[[nodiscard]] const QString& model() const { return m_model; }
	/// Replaces the list (and saves it).
	void setEffects(std::vector<ModelEffect> effects);
	/// The .qc text the effects become: event lines per sequence and the $keyvalues block.
	[[nodiscard]] QString qcText() const;

signals:
	/// "Compile into model": the events and the particles block for the model's .qc.
	void compileRequested(const QString& modelPath, const std::vector<SequenceEvent>& events, const QString& keyValues);

private:
	void refresh();
	void save();
	void edit(int index);

	Editor* m_editor;
	AssetBrowser* m_assets;
	ModelViewer* m_viewer = nullptr;
	QString m_model;
	std::vector<ModelEffect> m_effects;
	std::vector<AttachmentPoint> m_attachments;
	std::vector<std::pair<std::string, int>> m_sequences;
	QListWidget* m_list;
	QPushButton* m_add;
	QPushButton* m_editButton;
	QPushButton* m_remove;
	QPushButton* m_compile;
	QLabel* m_note;
};

} // namespace sme::app
