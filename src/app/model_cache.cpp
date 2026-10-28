// model_cache.cpp - Background model loading (see model_cache.h).

#include "model_cache.h"

#include <QPointer>
#include <QRunnable>

#include <sme/entity_catalog.h>
#include <sme/filesystem.h>

namespace sme::app {

ModelCache::ModelCache(QObject* parent) : QObject(parent) {
	m_pool.setMaxThreadCount(std::max(1, QThread::idealThreadCount() / 2));
}

ModelCache::~ModelCache() {
	++*m_generation;
	m_pool.waitForDone();
}

void ModelCache::setFileSystem(const GameFileSystem* fs, const EntityCatalog* catalog) {
	++*m_generation;
	m_pool.waitForDone();   // the old file system must not be read any more
	m_fs = fs;
	m_catalog = catalog;
	m_entries.clear();
}

std::shared_ptr<const ModelGeometry> ModelCache::get(const QString& path) {
	if (!m_fs || path.isEmpty()) {
		return nullptr;
	}
	auto& entry = m_entries[path];
	if (entry.geometry || entry.loading || !entry.error.isEmpty()) {
		return entry.geometry;
	}
	entry.loading = true;
	const GameFileSystem* fs = m_fs;
	const auto generationCounter = m_generation;
	const int generation = *generationCounter;
	// Loaded on a pool thread, stored on the GUI thread. The shared generation counter lets a job
	// see that the game changed (skip the work) and `self` that the cache is gone.
	QPointer<ModelCache> self(this);
	m_pool.start([self, fs, path, generationCounter, generation] {
		if (*generationCounter != generation) {
			return;
		}
		std::string error;
		// A key with "#..." is a variant (skin, body groups - a door with its handle).
		auto geometry = loadModelVariant(*fs, parseModelVariant(path.toStdString()), &error);
		auto shared = geometry ? std::make_shared<const ModelGeometry>(std::move(*geometry)) : nullptr;
		QMetaObject::invokeMethod(
			self, [self, path, shared, error, generationCounter, generation] {
				if (!self || *generationCounter != generation) {
					return;
				}
				auto& entry = self->m_entries[path];
				entry.loading = false;
				entry.geometry = shared;
				entry.error = shared ? QString() : QString::fromStdString(error.empty() ? "unknown error" : error);
				if (shared && self->m_catalog) {
					self->m_catalog->setModelBounds(parseModelVariant(path.toStdString()).path, shared->bounds);
				}
				emit self->modelReady(path);
			},
			Qt::QueuedConnection);
	});
	return nullptr;
}

bool ModelCache::failed(const QString& path) const {
	const auto it = m_entries.constFind(path);
	return it != m_entries.constEnd() && !it->error.isEmpty();
}

QString ModelCache::error(const QString& path) const {
	const auto it = m_entries.constFind(path);
	return it != m_entries.constEnd() ? it->error : QString();
}

} // namespace sme::app
