// properties_panel.h - "Object Properties": smart editing of whatever is selected.
//
// Entities: the class at the top (a drop-down changes it), then tabs Keyvalues / Outputs /
// Inputs with their counts. Keys are grouped by the FGD class that defines them (Targetname,
// the entity's own class, ...) in groups that fold away; each row is "label | editor" with a
// fitting editor (choices, yes/no switches, flags as switches, colors, targets, models,
// animations, sounds, the sky). What the focused key does is shown in the help area at the
// bottom. Outputs are edited as cards; inputs list who triggers this entity. NPCs get a pose
// helper. Faces: material and texture alignment. Brushes: tie to entity. Nothing: worldspawn.

#pragma once

#include "asset_menu.h"

#include <QFutureWatcher>
#include <QImage>
#include <QPointer>
#include <QWidget>

class QAbstractItemModel;
class QStringListModel;

#include <map>

#include <functional>
#include <set>
#include <utility>
#include <vector>

#include <sme/entity_catalog.h>
#include <sme/map.h>

class QCheckBox;
class QGridLayout;
class QLabel;
class QScrollArea;
class QVBoxLayout;
class QLineEdit;
class QMenu;

namespace sme::app {

class Editor;
class AssetBrowser;
class MaterialCache;

/// The Object Properties dock (see the file comment).
class PropertiesPanel : public QWidget {
	Q_OBJECT

public:
	PropertiesPanel(Editor* editor, AssetBrowser* assets, MaterialCache* materials, QWidget* parent = nullptr);

	/// Rebuilds the panel from the current selection (deferred, so editors can finish their signal).
	void scheduleRebuild();
	/// Where the right-click menus of asset fields get their "Show in ..." entries from.
	void setAssetMenuFiller(AssetMenuFiller filler) { m_menuFiller = std::move(filler); }
	/// The right-click menu of a field naming an asset (for tests; the caller owns the menu).
	[[nodiscard]] QMenu* assetFieldMenu(QLineEdit* field, AssetKind kind) const;

signals:
	/// "Edit material" on selected faces.
	void editMaterialRequested(const QString& name);
	/// "Edit soundscape" on a soundscape entity: the soundscape's name (open it on the Sounds page).
	void editSoundscapeRequested(const QString& name);

protected:
	/// Sends wheel turns over unfocused boxes to the scroll area (see rebuild()).
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	/// Gives a field or label naming an asset the "Show in ..." entries in its right-click menu.
	void attachAssetMenu(QLineEdit* field, AssetKind kind);
	void attachAssetMenu(QWidget* widget, const AssetRef& asset);
	void rebuild();
	void buildEntity(QVBoxLayout* layout, const std::vector<int>& entityIds);
	/// Keyvalues tab: groups by defining FGD class, flags, other keys, position, add-key row.
	/// Returns the number of keys shown.
	int buildKeyvalues(QVBoxLayout* layout, const std::vector<int>& entityIds, const EntityClass* definition);
	/// A group that folds away (its state is kept); returns the grid for "label | editor" rows.
	QGridLayout* addGroup(QVBoxLayout* layout, const QString& name, int count);
	/// One "label | editor" row with its help text (shown in the help area while it has the focus).
	void addRow(QGridLayout* grid, const QString& label, QWidget* editor, const QString& help);
	/// Smart Edit off: every key as plain text (like Hammer), keys can be added and removed.
	void buildRawKeys(QVBoxLayout* layout, const std::vector<int>& entityIds, const EntityClass* definition);
	/// Key + value fields and an Add button (both modes).
	void buildAddKeyRow(QVBoxLayout* layout, const std::vector<int>& entityIds);
	void buildOutputs(QVBoxLayout* layout, int entityId);
	/// Connections of other entities that target this one, each with "Go to".
	void buildIncoming(QVBoxLayout* layout, const Entity& entity);
	[[nodiscard]] int incomingCount(const Entity& entity) const;
	/// worldspawn "skyname": the game's skies with a picture of each.
	QWidget* makeSkyPicker(const QString& value);
	void loadSkies();
	void showHelp(QWidget* focused);
	void buildNpcHelper(QVBoxLayout* layout, const Entity& entity, const EntityClass* definition);
	void buildFaces(QVBoxLayout* layout);
	/// env_cubemap: its room box for parallax-corrected reflections (Mapbase).
	void buildCubemapBox(QVBoxLayout* layout, const Entity& cubemap);
	/// func_instance: its file, the $variables it fills in, collapse (properties_instance.cpp).
	void buildInstanceBox(QVBoxLayout* layout, const Entity& funcInstance);
	/// Soundscape entities: which soundscape (with a button to edit it), and where it is heard -
	/// a sphere (radius, or the whole map) or a block (trigger_soundscape) with its size.
	void buildSoundscapeArea(QVBoxLayout* layout, const Entity& soundscape);
	void buildBrushes(QVBoxLayout* layout);
	QWidget* makeEditor(const EntityProperty& property, const std::vector<int>& ids, const QString& value, bool mixed);
	void setKey(const std::vector<int>& ids, const std::string& key, const QString& value);
	/// Sets several keys in one undo step (pasted "key" "value" lines).
	void pasteKeys(const std::vector<int>& ids, const std::vector<std::pair<QString, QString>>& keys);

	[[nodiscard]] QStringList targetNames() const;
	/// One list model per kind of name (models, sounds, particles) shared by every field that
	/// offers them: tens of thousands of names are copied once per game, not on every click.
	QAbstractItemModel* sharedNames(const QString& kind, const QStringList& names);
	[[nodiscard]] QString modelFor(const Entity& entity) const;
	[[nodiscard]] QStringList sequencesFor(const Entity& entity) const;

	Editor* m_editor;
	AssetBrowser* m_assets;
	std::map<QString, std::pair<QStringListModel*, qsizetype>> m_sharedNames;   ///< kind -> model, name count
	AssetMenuFiller m_menuFiller;
	MaterialCache* m_materials;
	QScrollArea* m_scroll;
	QLineEdit* m_filter;
	QCheckBox* m_smartEdit;
	QPointer<QWidget> m_content;
	QLabel* m_help;                        ///< What the focused key does
	QString m_defaultHelp;                 ///< Shown while no key has the focus (the class description)
	bool m_pending = false;
	QString m_filterText;
	int m_tab = 0;                         ///< Keyvalues, Outputs, Inputs
	std::set<QString> m_collapsed;         ///< Folded groups (kept in the settings)
	// Skies of the current game, read once in the background: name and picture.
	std::vector<std::pair<QString, QImage>> m_skies;
	QString m_skiesGame;                   ///< Game the sky list belongs to
	QFutureWatcher<std::vector<std::pair<QString, QImage>>> m_skyWatcher;
};

/// First sentence of an FGD description, shortened for the line under each field.
[[nodiscard]] QString shortDescription(const std::string& description, int maxLength = 110);

} // namespace sme::app
