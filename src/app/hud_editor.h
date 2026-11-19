// hud_editor.h - Project > HUD: where the HUD elements sit, their colours and fonts, with a
// picture of the HUD as the game draws it at a chosen resolution. Drag an element to move it, its
// corner to resize it. The file work (HudLayout.res, ClientScheme.res, vgui positions, saving into
// the mod with a backup) lives in the core (sme/hud_layout.h); this is the user interface on top.

#pragma once

#include <QHash>
#include <QImage>
#include <QWidget>

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <sme/hud_layout.h>

class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTreeWidget;

namespace sme::app {

class Editor;

/// One element as the preview draws it (screen pixels of the chosen resolution).
struct HudPreviewItem {
	QString name;
	QRect rect;
	bool visible = true;
	bool movable = true;        ///< Has its own position (not placed by the game code, not full screen)
	bool fullScreen = false;
	QString label, sample, sample2;
	QColor text = QColor(255, 220, 0, 100), background = QColor(0, 0, 0, 76), bar = QColor(255, 220, 0, 220);
	QFont labelFont, numberFont, smallFont;
	bool additive = true;
	QPoint labelPos, numberPos, number2Pos;   ///< Inside the rect
	QRect barRect;                            ///< Aux power bar inside the rect (empty: none)
	int chunk = 0, gap = 0;
};

/// The picture of the HUD: a background, the elements at their places; drag to move/resize.
class HudPreview : public QWidget {
	Q_OBJECT

public:
	explicit HudPreview(QWidget* parent = nullptr);

	void setScreen(QSize size);
	[[nodiscard]] QSize screen() const { return m_screen; }
	void setItems(std::vector<HudPreviewItem> items);
	void setSelected(const QString& name);
	void setBackground(const QImage& image);
	void setShowOutlines(bool on);
	/// Where an element is drawn in widget coordinates (tests, hit tests).
	[[nodiscard]] QRect widgetRect(const QString& name) const;
	[[nodiscard]] const std::vector<HudPreviewItem>& items() const { return m_items; }

signals:
	void selected(const QString& name);
	/// An element was moved or resized (screen pixels) when the mouse let go.
	void changed(const QString& name, QRect rect, bool resized);

protected:
	void paintEvent(QPaintEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	[[nodiscard]] QSize sizeHint() const override { return {640, 400}; }
	[[nodiscard]] QSize minimumSizeHint() const override { return {320, 200}; }

private:
	[[nodiscard]] QRectF screenArea() const;   ///< Where the screen sits in the widget
	[[nodiscard]] double scale() const;
	[[nodiscard]] HudPreviewItem* item(const QString& name);
	void paintItem(QPainter& p, const HudPreviewItem& item, bool selected) const;

	QSize m_screen{1920, 1080};
	std::vector<HudPreviewItem> m_items;
	QString m_selected;
	QImage m_background;
	bool m_outlines = false;
	// Dragging
	QString m_drag;
	bool m_resize = false;
	QPointF m_pressAt;
	QRect m_startRect;
};

/// Project > HUD.
class HudEditor : public QWidget {
	Q_OBJECT

public:
	explicit HudEditor(Editor* editor, QWidget* parent = nullptr);
	~HudEditor() override;

	/// Reads the HUD files again (mod first, else the game's); unsaved changes are dropped.
	void reload();
	/// Writes the changed files into the mod. False with `error` if something could not be written.
	bool save(QString* error = nullptr);
	/// Puts the mod's HUD files back as they were before Mapsmith changed them.
	bool restoreOriginal(QString* error = nullptr);

	// Edits (also what the controls call).
	void selectElement(const QString& name);
	[[nodiscard]] QString selectedElement() const { return m_selected; }
	void setElementValue(const QString& element, const QString& key, const QString& value);
	void setSchemeColor(const QString& name, const QColor& color);
	void setFontSize(const QString& font, int pixels);
	void setFontFamily(const QString& font, const QString& family);
	void setResolution(QSize size);
	void undo();

	[[nodiscard]] bool dirty() const { return m_layoutDirty || m_schemeDirty; }
	[[nodiscard]] const sme::ResFile* layout() const { return m_layout ? &*m_layout : nullptr; }
	[[nodiscard]] const sme::ResFile* scheme() const { return m_scheme ? &*m_scheme : nullptr; }
	[[nodiscard]] HudPreview* preview() const { return m_preview; }

signals:
	void saved();

private:
	void buildUi();
	void refresh();               ///< Preview, lists and fields from the files
	void refreshProperties();
	void refreshColors();
	void refreshFonts();
	void loadFonts();
	void pushUndo();
	void applyLayout(std::string text);
	void applyScheme(std::string text);
	[[nodiscard]] QFont qtFont(const QString& schemeFont, double previewScale) const;
	[[nodiscard]] QColor color(const std::string& nameOrColor, const QColor& fallback) const;
	void previewChanged(const QString& name, QRect rect, bool resized);
	void updateButtons();
	/// Name of the scheme file's top block ("Scheme").
	[[nodiscard]] std::string_view firstActive() const;

	Editor* m_editor;
	std::optional<sme::ResFile> m_layout, m_scheme;
	bool m_layoutDirty = false, m_schemeDirty = false;
	bool m_layoutFromMod = false, m_schemeFromMod = false;
	std::vector<std::pair<std::string, std::string>> m_undo;   ///< Layout + scheme texts before each edit
	QString m_selected;
	QHash<QString, QString> m_customFamilies;   ///< lower-case scheme font name -> loaded family
	QList<int> m_fontIds;
	bool m_updating = false;

	HudPreview* m_preview = nullptr;
	QComboBox* m_resolution = nullptr;
	QCheckBox* m_outlines = nullptr;
	QListWidget* m_elements = nullptr;
	QLabel* m_source = nullptr;
	QLabel* m_elementNote = nullptr;
	QComboBox* m_anchorX = nullptr;
	QComboBox* m_anchorY = nullptr;
	QSpinBox* m_x = nullptr;
	QSpinBox* m_y = nullptr;
	QSpinBox* m_wide = nullptr;
	QSpinBox* m_tall = nullptr;
	QCheckBox* m_visible = nullptr;
	QPushButton* m_textColor = nullptr;
	QPushButton* m_bgColor = nullptr;
	QComboBox* m_numberFont = nullptr;
	QTreeWidget* m_colors = nullptr;
	QTreeWidget* m_fonts = nullptr;
	QComboBox* m_fontFamily = nullptr;
	QSpinBox* m_fontSize = nullptr;
	QPushButton* m_undoButton = nullptr;
	QPushButton* m_saveButton = nullptr;
	QPushButton* m_restoreButton = nullptr;
};

} // namespace sme::app
