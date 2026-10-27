// properties_panel.h - "Object Properties": smart editing of whatever is selected.
//
// Entities: every FGD key gets a fitting editor (choices, flags, colors, targets, models,
// animations, sounds) with its description right below it; outputs are edited in a table
// with drop-downs for outputs, targets and the target's inputs. NPCs get a pose helper.
// Faces: material and texture alignment. Brushes: tie to entity. Nothing: world settings.

#pragma once

#include <QPointer>
#include <QWidget>

#include <functional>
#include <vector>

#include <sme/entity_catalog.h>
#include <sme/map.h>

class QCheckBox;
class QScrollArea;
class QVBoxLayout;
class QLineEdit;

namespace sme::app {

class Editor;
class AssetBrowser;
class MaterialCache;

class PropertiesPanel : public QWidget {
	Q_OBJECT

public:
	PropertiesPanel(Editor* editor, AssetBrowser* assets, MaterialCache* materials, QWidget* parent = nullptr);

	/// Rebuilds the panel from the current selection (deferred, so editors can finish their signal).
	void scheduleRebuild();

private:
	void rebuild();
	void buildEntity(QVBoxLayout* layout, const std::vector<int>& entityIds);
	/// Smart Edit off: every key as plain text (like Hammer), keys can be added and removed.
	void buildRawKeys(QVBoxLayout* layout, const std::vector<int>& entityIds, const EntityClass* definition);
	/// Key + value fields and an Add button (both modes).
	void buildAddKeyRow(QVBoxLayout* layout, const std::vector<int>& entityIds);
	void buildOutputs(QVBoxLayout* layout, int entityId);
	void buildIncoming(QVBoxLayout* layout, const Entity& entity);
	void buildNpcHelper(QVBoxLayout* layout, const Entity& entity, const EntityClass* definition);
	void buildFaces(QVBoxLayout* layout);
	void buildBrushes(QVBoxLayout* layout);
	QWidget* makeEditor(const EntityProperty& property, const std::vector<int>& ids, const QString& value, bool mixed);
	void setKey(const std::vector<int>& ids, const std::string& key, const QString& value);
	/// Sets several keys in one undo step (pasted "key" "value" lines).
	void pasteKeys(const std::vector<int>& ids, const std::vector<std::pair<QString, QString>>& keys);

	[[nodiscard]] QStringList targetNames() const;
	[[nodiscard]] QString modelFor(const Entity& entity) const;
	[[nodiscard]] QStringList sequencesFor(const Entity& entity) const;

	Editor* m_editor;
	AssetBrowser* m_assets;
	MaterialCache* m_materials;
	QScrollArea* m_scroll;
	QLineEdit* m_filter;
	QCheckBox* m_smartEdit;
	QPointer<QWidget> m_content;
	bool m_pending = false;
	QString m_filterText;
};

/// First sentence of an FGD description, shortened for the line under each field.
[[nodiscard]] QString shortDescription(const std::string& description, int maxLength = 110);

} // namespace sme::app
