// model_cache.h - Loads model triangles (MDL/VVD/VTX) in background threads and keeps them
// for every view and the model previews. The views draw a box until a model is ready.

#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QThreadPool>

#include <atomic>
#include <memory>

#include <sme/studio_model.h>

namespace sme {
class EntityCatalog;
class GameFileSystem;
}

namespace sme::app {

/// Model triangles by path, loaded in the background (see the file comment).
class ModelCache : public QObject {
	Q_OBJECT

public:
	explicit ModelCache(QObject* parent = nullptr);
	~ModelCache() override;

	/// Game files to load from (nullptr: none). `catalog` learns the model sizes (entity boxes).
	void setFileSystem(const GameFileSystem* fs, const EntityCatalog* catalog);
	/// The model if it is loaded; otherwise starts loading it (modelReady follows) and returns nullptr.
	/// `path` may be a variant key (modelVariantKey: skin and body groups).
	[[nodiscard]] std::shared_ptr<const ModelGeometry> get(const QString& path);
	/// Loading failed (missing or broken files).
	[[nodiscard]] bool failed(const QString& path) const;
	[[nodiscard]] QString error(const QString& path) const;
	/// Drops a model and its variants (its files changed - e.g. just built): the next get() reads them again.
	void forget(const QString& path);

signals:
	/// A model finished loading or failed (see failed()); on the GUI thread.
	void modelReady(const QString& path);

private:
	struct Entry {
		std::shared_ptr<const ModelGeometry> geometry;
		bool loading = false;
		QString error;
	};

	const GameFileSystem* m_fs = nullptr;
	const EntityCatalog* m_catalog = nullptr;
	QHash<QString, Entry> m_entries;
	QThreadPool m_pool;
	std::shared_ptr<std::atomic<int>> m_generation = std::make_shared<std::atomic<int>>(0);
};

} // namespace sme::app
