// texture_enhance_dialog.h - "Enhance textures ...": the chosen materials made sharper and larger
// and given bump maps (sme/texture_enhance.h), in the background, with a before/after comparison
// of the selected one (a divider to drag), and "Remove enhancements" for the same materials.
//
// Opened from the Materials page (the "..." menu: every material listed; right-click: the selected
// ones) or by other editors with their own list (openTextureEnhanceDialog, e.g. a model's
// materials). After writing, the views read the materials again and Game enhancements runs.

#pragma once

#include <QDialog>
#include <QFutureWatcher>
#include <QImage>
#include <QStringList>
#include <QTimer>

#include <atomic>
#include <memory>
#include <vector>

#include <sme/texture_enhance.h>

class QCheckBox;
class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QTreeWidget;

namespace sme::app {

class Editor;

/// Before and after side by side over the same spot: the divider follows the mouse while dragged.
class EnhanceCompareView : public QWidget {
	Q_OBJECT
public:
	explicit EnhanceCompareView(QWidget* parent = nullptr);
	void setPictures(const QImage& before, const QImage& after, const QString& afterLabel);
	void clear(const QString& text);
	/// Where the divider is (0 = only after, 1 = only before).
	[[nodiscard]] double split() const { return m_split; }
	void setSplit(double split);
	[[nodiscard]] QSize sizeHint() const override { return {360, 300}; }

protected:
	void paintEvent(QPaintEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;

private:
	QImage m_before, m_after;
	QString m_afterLabel, m_text;
	double m_split = 0.5;
};

class TextureEnhanceDialog : public QDialog {
	Q_OBJECT
public:
	TextureEnhanceDialog(Editor* editor, const QStringList& materials, QWidget* parent = nullptr);
	~TextureEnhanceDialog() override;

	/// Plans the list again (another set of materials).
	void setMaterials(const QStringList& materials);
	/// The options as chosen now.
	[[nodiscard]] EnhanceOptions options() const;
	/// Planning, making a preview or writing is going on.
	[[nodiscard]] bool busy() const;
	/// The plans of the list (after planning finished).
	[[nodiscard]] const std::vector<MaterialEnhancement>& plans() const { return m_plans; }
	/// Starts writing (what "Apply" does) / removing.
	void apply();
	void removeAll();

signals:
	/// Materials were written or their enhancement removed (the views read them again).
	void materialsWritten(const QStringList& materials);
	/// A run finished (tests wait for it).
	void runFinished();

private:
	struct PlanResult;
	struct RunResult;
	void replan();
	void planned();
	void fillList();
	void showPreview();
	void previewReady();
	void ran();
	void setRunning(bool on);
	void stopAll();

	Editor* m_editor;
	QStringList m_materials;
	std::vector<MaterialEnhancement> m_plans;
	AiUpscaler m_ai;
	QTreeWidget* m_list = nullptr;
	QLabel* m_summary = nullptr;
	EnhanceCompareView* m_compare = nullptr;
	QComboBox* m_factor = nullptr;
	QComboBox* m_method = nullptr;
	QComboBox* m_maxSize = nullptr;
	QCheckBox* m_bumps = nullptr;
	QCheckBox* m_bigBumps = nullptr;
	QCheckBox* m_masks = nullptr;
	QCheckBox* m_compress = nullptr;
	QLabel* m_note = nullptr;
	QProgressBar* m_progress = nullptr;
	QLabel* m_status = nullptr;
	QPushButton* m_apply = nullptr;
	QPushButton* m_remove = nullptr;
	QPushButton* m_cancel = nullptr;
	QTimer m_replanSoon;
	QTimer m_tick;
	QFutureWatcher<std::vector<MaterialEnhancement>> m_planWatcher;
	QFutureWatcher<std::optional<EnhancePreview>> m_previewWatcher;
	QFutureWatcher<std::shared_ptr<RunResult>> m_runWatcher;
	std::shared_ptr<std::atomic<bool>> m_stop = std::make_shared<std::atomic<bool>>(false);
	std::shared_ptr<std::atomic<bool>> m_previewStop = std::make_shared<std::atomic<bool>>(false);
	std::shared_ptr<std::atomic<int>> m_done = std::make_shared<std::atomic<int>>(0);
	int m_total = 0;
	int m_planGeneration = 0;
	bool m_running = false;
	bool m_previewAgain = false;
};

/// Opens the dialog for these materials ("brick/wall01", "models/weapons/v_smg1/smg1_sheet")
/// - the way the other editors call it. Returns it (non-modal, deletes itself when closed).
TextureEnhanceDialog* openTextureEnhanceDialog(Editor* editor, const QStringList& materials, QWidget* parent);

} // namespace sme::app
