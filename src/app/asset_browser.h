// asset_browser.h - Browser for materials (with category filters and thumbnails) and models
// (with previews). Items can be dragged into the 3D view: materials onto faces, models
// become props. Sounds have their own tab (sound_browser.h), entities sit at the entity tool.

#pragma once

#include <QAbstractListModel>
#include <QFutureWatcher>
#include <QSortFilterProxyModel>
#include <QStringList>
#include <QWidget>

#include <atomic>

#include <sme/assets.h>

class QLabel;
class QLineEdit;
class QListView;
class QComboBox;
class QTabWidget;
class QCheckBox;
class QListWidget;

namespace sme::app {

class Editor;
class MaterialCache;

/// All materials of the game with their categories (filled in the background).
class MaterialListModel : public QAbstractListModel {
	Q_OBJECT

public:
	struct Item {
		QString name;                 ///< "concrete/wall01"
		QString shader;
		MaterialCategory category;
		bool classified = false;
	};

	MaterialListModel(MaterialCache* thumbnails, QObject* parent = nullptr);
	void setItems(std::vector<Item> items);
	[[nodiscard]] const std::vector<Item>& items() const { return m_items; }
	[[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
	[[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
	[[nodiscard]] Qt::ItemFlags flags(const QModelIndex& index) const override;
	[[nodiscard]] QStringList mimeTypes() const override;
	[[nodiscard]] QMimeData* mimeData(const QModelIndexList& indexes) const override;
	void thumbnailReady(const QString& name);

private:
	MaterialCache* m_thumbnails;
	std::vector<Item> m_items;
	QHash<QString, int> m_rows;
};

class MaterialFilter : public QSortFilterProxyModel {
	Q_OBJECT

public:
	enum class Category { All, Brush, Blend, Model, Tool, Translucent, Water, Decal, Sky, UsedInMap };
	using QSortFilterProxyModel::QSortFilterProxyModel;
	void setText(const QString& text);
	void setCategory(Category category);
	void setUsed(const QSet<QString>& used);

protected:
	[[nodiscard]] bool filterAcceptsRow(int row, const QModelIndex& parent) const override;

private:
	QStringList m_words;
	Category m_category = Category::Brush;
	QSet<QString> m_used;
};

class ModelThumbnails;

/// All models of the game with preview pictures (rendered when shown).
class ModelListModel : public QAbstractListModel {
	Q_OBJECT

public:
	ModelListModel(ModelThumbnails* thumbnails, QObject* parent = nullptr);
	void setModels(const QStringList& models);
	[[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
	[[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
	[[nodiscard]] Qt::ItemFlags flags(const QModelIndex& index) const override;
	[[nodiscard]] QStringList mimeTypes() const override;
	[[nodiscard]] QMimeData* mimeData(const QModelIndexList& indexes) const override;
	void thumbnailReady(const QString& path);

private:
	ModelThumbnails* m_thumbnails;
	QStringList m_models;
	QHash<QString, int> m_rows;
};

/// Result of the background indexing of a game's assets.
struct AssetIndex {
	std::vector<MaterialListModel::Item> materials;
	QStringList models;
	QStringList sounds;
};

class AssetBrowser : public QWidget {
	Q_OBJECT

public:
	AssetBrowser(Editor* editor, QWidget* parent = nullptr);
	~AssetBrowser() override;

	/// All material names ("concrete/wall01") once indexed (used for "did you mean").
	[[nodiscard]] std::vector<std::string> materialNames() const;
	[[nodiscard]] QStringList modelNames() const { return m_models; }
	[[nodiscard]] QStringList soundNames() const { return m_sounds; }
	void showMaterials();
	void showModels();
	[[nodiscard]] QListView* modelView() const { return m_modelView; }

signals:
	void materialsIndexed();

private:
	void reload();
	void updateUsedMaterials();
	void showMaterialInfo(const QModelIndex& index);
	void updateRecent();

	Editor* m_editor;
	MaterialCache* m_thumbnails;
	QTabWidget* m_tabs;

	MaterialListModel* m_materialModel;
	MaterialFilter* m_materialFilter;
	QListView* m_materialView;
	QLineEdit* m_materialSearch;
	QComboBox* m_materialCategory;
	QLabel* m_materialInfo;
	QListWidget* m_recent;
	QFutureWatcher<AssetIndex> m_indexWatcher;
	std::atomic<int> m_generation{0};

	QStringList m_models, m_sounds;
	ModelThumbnails* m_modelThumbnails;
	ModelListModel* m_modelModel;
	QListView* m_modelView;
	QLineEdit* m_modelSearch;
	QLabel* m_modelInfo;
};

} // namespace sme::app
