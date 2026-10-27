// material_cache.cpp - Background material loading (see material_cache.h).

#include "material_cache.h"

#include <QMetaObject>
#include <QPainter>

#include <QMutex>
#include <QMutexLocker>

#include <map>

#include <sme/filesystem.h>
#include <sme/heightmap.h>
#include <sme/material.h>
#include <sme/text.h>
#include <sme/visibility.h>
#include <sme/texture.h>

namespace sme::app {

namespace {

QImage toQImage(const ImageRgba& image) {
	QImage out(image.width, image.height, QImage::Format_RGBA8888);
	for (int y = 0; y < image.height; ++y) {
		std::memcpy(out.scanLine(y), image.pixels.data() + static_cast<std::size_t>(y) * image.width * 4, static_cast<std::size_t>(image.width) * 4);
	}
	return out;
}

// Height maps made for the relief preview, by source texture (walls share normal maps); emptied
// when the game changes. A null image: the texture could not be made into heights.
QMutex g_heightsMutex;
std::map<std::string, QImage> g_heights;

QImage reliefHeights(const GameFileSystem& fs, const ReliefSource& source) {
	const std::string key = std::to_string(int(source.kind)) + ":" + toLower(source.texture);
	{
		QMutexLocker lock(&g_heightsMutex);
		if (const auto it = g_heights.find(key); it != g_heights.end()) return it->second;
	}
	QImage result;
	if (const auto image = loadTexture(fs, source.texture, 512)) {
		std::optional<HeightMap> map;
		switch (source.kind) {
			case ReliefSource::Kind::HeightMap: map = heightFromImage(*image); break;
			case ReliefSource::Kind::NormalMap: map = heightFromNormalMap(*image, false); break;
			case ReliefSource::Kind::SSBump: map = heightFromNormalMap(*image, true); break;
			case ReliefSource::Kind::Color: map = heightFromColor(*image); break;
		}
		if (map) result = toQImage(heightImage(*map));
	}
	QMutexLocker lock(&g_heightsMutex);
	g_heights[key] = result;
	return result;
}

std::shared_ptr<MaterialInfo> loadInfo(const GameFileSystem& fs, const QString& name, unsigned maxSize, bool relief) {
	auto info = std::make_shared<MaterialInfo>();
	info->name = name;
	// Sky by name too: the sky shows even where the game's toolsskybox material is missing.
	info->isSky = isSkyMaterialName(name.toStdString());
	std::string error;
	const auto material = loadMaterial(fs, name.toStdString(), &error);
	if (!material) {
		info->error = QString::fromStdString(error);
		return info;
	}
	info->found = true;
	info->shader = QString::fromStdString(material->shader);
	info->isTool = material->isTool();
	info->isSky = info->isSky || material->flag("%compilesky") || material->flag("%compile2dsky");
	info->isNodraw = material->flag("%compilenodraw");
	if (material->flag("$additive")) {
		info->kind = MaterialInfo::Kind::Additive;
	} else if (material->flag("$translucent") || material->flag("%compiletrigger") || material->flag("%compilewater") ||
	           material->shader == "Water" || material->shader == "water") {
		info->kind = MaterialInfo::Kind::Translucent;
	} else if (material->flag("$alphatest")) {
		info->kind = MaterialInfo::Kind::AlphaTest;
	}
	const auto preview = material->previewTexture();
	if (!preview.empty()) {
		if (const auto image = loadTexture(fs, preview, maxSize, &error)) {
			info->image = toQImage(*image);
		} else {
			info->error = QString::fromStdString(error);
		}
	}
	if (const auto second = material->param("$basetexture2"); !second.empty() && maxSize > 128) {
		if (const auto image = loadTexture(fs, second, maxSize)) {
			info->image2 = toQImage(*image);
		}
	}
	if (relief) {
		if (const auto source = fidelityReliefSource(name.toStdString(), *material)) {
			info->height = reliefHeights(fs, *source);
			info->parallaxDepth = info->height.isNull() ? 0.f : float(source->depth);
		}
	}
	// Tool textures meant to be see-through in the editor.
	if (info->isTool && (material->flag("%compiletrigger") || material->flag("%compileclip") || material->flag("%compileplayerclip") ||
	                     material->flag("%compilenpcclip") || material->flag("%compilehint") || material->flag("%compileskip") ||
	                     material->flag("%compileareaportal") || material->flag("%compileorigin"))) {
		info->kind = MaterialInfo::Kind::Translucent;
	}
	return info;
}

} // namespace

QString normalizedMaterialName(const QString& name) {
	return QString::fromStdString(normalizeMaterialName(name.toStdString()));
}

MaterialCache::MaterialCache(Size size, QObject* parent) : QObject(parent), m_size(size) {
	m_pool.setMaxThreadCount(std::max(2, QThread::idealThreadCount() - 1));
}

MaterialCache::~MaterialCache() {
	m_pool.clear();
	m_pool.waitForDone();
}

void MaterialCache::setFileSystem(const GameFileSystem* fs) {
	m_pool.clear();
	m_pool.waitForDone();
	m_fs = fs;
	{
		QMutexLocker lock(&g_heightsMutex);
		g_heights.clear();
	}
	clear();
}

void MaterialCache::clear() {
	++m_generation;
	m_loaded.clear();
	m_pending.clear();
}

void MaterialCache::setRelief(bool on) {
	if (on == m_relief) {
		return;
	}
	m_relief = on;
	// Read what is shown again; the old pictures stay until the new ones are there.
	const auto names = m_loaded.keys();
	for (const auto& name : names) {
		m_pending.remove(name);
		m_loaded.remove(name);
		(void)get(name);
	}
}

void MaterialCache::reload(const QString& rawName) {
	const QString name = normalizedMaterialName(rawName);
	m_loaded.remove(name);
	m_pending.remove(name);
	(void)get(name);
}

std::shared_ptr<const MaterialInfo> MaterialCache::get(const QString& rawName) {
	const QString name = normalizedMaterialName(rawName);
	if (const auto it = m_loaded.constFind(name); it != m_loaded.constEnd()) {
		return *it;
	}
	if (!m_fs) {
		auto info = std::make_shared<MaterialInfo>();
		info->name = name;
		info->error = tr("No game loaded");
		m_loaded.insert(name, info);
		return info;
	}
	if (!m_pending.contains(name)) {
		m_pending.insert(name);
		const auto* fs = m_fs;
		const unsigned maxSize = m_size == Size::Thumbnail ? 128 : 1024;
		const quint64 generation = m_generation;
		const bool relief = m_relief && m_size == Size::Viewport;
		// Decoded on a pool thread; the result is handed back to the GUI thread. The generation
		// number drops results that were started before clear() (game changed).
		m_pool.start([this, fs, name, maxSize, generation, relief] {
			auto info = loadInfo(*fs, name, maxSize, relief);
			QMetaObject::invokeMethod(this, [this, name, info, generation] { finished(name, info, generation); }, Qt::QueuedConnection);
		});
	}
	return nullptr;
}

void MaterialCache::finished(const QString& name, std::shared_ptr<MaterialInfo> info, quint64 generation) {
	if (generation != m_generation) {
		return; // game changed meanwhile
	}
	m_pending.remove(name);
	m_loaded.insert(name, std::move(info));
	emit materialReady(name);
}

const QImage& MaterialCache::checkerboard() {
	static const QImage image = [] {
		QImage img(64, 64, QImage::Format_RGBA8888);
		QPainter p(&img);
		p.fillRect(0, 0, 64, 64, QColor(255, 0, 255));
		p.fillRect(0, 0, 32, 32, QColor(0, 0, 0));
		p.fillRect(32, 32, 32, 32, QColor(0, 0, 0));
		return img;
	}();
	return image;
}

} // namespace sme::app
