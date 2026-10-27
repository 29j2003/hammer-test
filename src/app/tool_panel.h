// tool_panel.h - "Tool" dock (left of the views): options of the active tool, like the tool
// properties in Source 2 Hammer. The entity tool shows the entity picker (point/brush entities
// with icons, search, armed class), the block tool its shape settings, the other tools a short how-to.

#pragma once

#include <QHash>
#include <QIcon>
#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QStackedWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QToolButton;

namespace sme {
struct EntityClass;
}

namespace sme::app {

class Editor;
class MaterialCache;
class ModelThumbnails;

/// Entity classes of the game's FGDs, like XBLAH's "New objects": point and brush entities with
/// their editor icons, a search, and the class that is "armed" (placed by the next click, or
/// built from the next box for a brush entity).
class EntityPicker : public QWidget {
	Q_OBJECT

public:
	explicit EntityPicker(Editor* editor, QWidget* parent = nullptr);
	/// Materials for the editor sprites (iconsprite in the FGD); without it classes get color squares.
	void setMaterialCache(MaterialCache* materials);
	/// Model previews for classes that show an editor model instead of a sprite (info_player_start).
	void setModelThumbnails(ModelThumbnails* thumbnails);
	void focusSearch();
	[[nodiscard]] QTreeWidget* tree() const { return m_tree; }
	/// Icon of a class: its editor sprite once loaded, else a square in the class color.
	[[nodiscard]] QIcon iconFor(const QString& classname);

private:
	void reload();
	void filter(const QString& text);
	void showClass(const QString& name);
	void showArmed();
	void remember(const QString& name);
	QTreeWidgetItem* addClassItem(QTreeWidgetItem* parent, const EntityClass& cls);
	void spriteReady(const QString& material);
	void modelReady(const QString& path);

	Editor* m_editor;
	MaterialCache* m_materials = nullptr;
	QLineEdit* m_search;
	QToolButton* m_grouped;
	QTreeWidget* m_tree;
	QTreeWidgetItem* m_recent = nullptr;
	QLabel* m_info;
	QLabel* m_armedIcon;
	QLabel* m_armed;
	QToolButton* m_disarm;
	QCheckBox* m_randomYaw;
	QLabel* m_summary;
	QHash<QString, QList<QTreeWidgetItem*>> m_spriteItems;   ///< sprite material -> items waiting for it
	QHash<QString, QIcon> m_spriteIcons;
	ModelThumbnails* m_thumbnails = nullptr;
	QHash<QString, QList<QTreeWidgetItem*>> m_modelItems;     ///< editor model -> items waiting for its preview
};

class ToolPanel : public QWidget {
	Q_OBJECT

public:
	explicit ToolPanel(Editor* editor, QWidget* parent = nullptr);
	[[nodiscard]] EntityPicker* entityPicker() const { return m_entities; }
	/// Adds the page shown for the block tool (built by the block tool options).
	void setBlockPage(QWidget* page);

signals:
	/// The panel wants to be seen (entity tool chosen).
	void wantsAttention();

private:
	void refresh();

	Editor* m_editor;
	QStackedWidget* m_pages;
	QLabel* m_hint;
	EntityPicker* m_entities;
	QWidget* m_block = nullptr;
};

} // namespace sme::app
