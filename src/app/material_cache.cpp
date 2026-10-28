// material_cache.cpp - Background material loading (see material_cache.h).

#include "material_cache.h"

#include <QMetaObject>
#include <QPainter>

#include <QMutex>
#include <QMutexLocker>

#include <algorithm>
#include <map>

#include <sme/filesystem.h>
#include <sme/heightmap.h>
#include <sme/material.h>
#include <sme/relief_pattern.h>
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
			case ReliefSource::Kind::Color: map = heightFromTexture(*image); break;   // its layout (bricks, tiles), else the brightness
		}
		if (map) result = toQImage(heightImage(*map));
	}
	QMutexLocker lock(&g_heightsMutex);
	g_heights[key] = result;
	return result;
}

/// "[0.5 0.5 0.5]", "{128 128 128}" (0..255) or one number for all three.
void parseTint(const std::string& text, float out[3]) {
	QString value = QString::fromStdString(text).trimmed();
	const bool bytes = value.startsWith('{');
	value.remove('[').remove(']').remove('{').remove('}');
	const auto parts = value.split(' ', Qt::SkipEmptyParts);
	for (int i = 0; i < 3 && !parts.isEmpty(); ++i) {
		bool ok = false;
		const float v = parts.value(std::min<int>(i, int(parts.size()) - 1)).toFloat(&ok);
		if (ok) out[i] = std::max(0.f, bytes ? v / 255.f : v);
	}
}

/// $envmap and what shapes it: tint, mask (own texture, the base or the normal map's alpha) and
/// the fresnel term, as LightmappedGeneric / VertexLitGeneric / Water read them.
void readEnvmap(const GameFileSystem& fs, const Material& material, MaterialInfo& info) {
	const auto envmap = material.param("$envmap");
	if (envmap.empty() || envmap == "0") return;
	info.envmap = true;
	if (const auto tint = material.param("$envmaptint"); !tint.empty()) parseTint(tint, info.envmapTint);
	const QString shader = QString::fromStdString(material.shader).toLower();
	const bool water = shader == "water";
	// Water reflects mostly at grazing angles; the other shaders the same at every angle.
	info.fresnel = water ? 0.f : 1.f;
	if (const auto fresnel = material.param("$fresnelreflection"); !fresnel.empty()) {
		bool ok = false;
		const float value = QString::fromStdString(fresnel).toFloat(&ok);
		if (ok) info.fresnel = std::clamp(value, 0.f, 1.f);
	}
	const auto maskImage = [&](const std::string& texture, bool alpha) {
		const auto image = loadTexture(fs, texture, 256);
		if (!image) return QImage();
		QImage rgba = toQImage(*image).convertToFormat(QImage::Format_RGBA8888);
		if (alpha) {
			// The alpha channel becomes the mask in all three colours.
			for (int y = 0; y < rgba.height(); ++y) {
				auto* p = rgba.scanLine(y);
				for (int x = 0; x < rgba.width(); ++x, p += 4) p[0] = p[1] = p[2] = p[3];
			}
		}
		return rgba;
	};
	if (const auto mask = material.param("$envmapmask"); !mask.empty()) {
		info.envmapMaskImage = maskImage(mask, false);
		if (!info.envmapMaskImage.isNull()) info.envmapMask = MaterialInfo::EnvmapMask::Texture;
	} else if (material.flag("$normalmapalphaenvmapmask")) {
		if (const auto bump = material.param("$bumpmap"); !bump.empty()) {
			info.envmapMaskImage = maskImage(bump, true);
			if (!info.envmapMaskImage.isNull()) info.envmapMask = MaterialInfo::EnvmapMask::Texture;
		}
	} else if (material.flag("$basealphaenvmapmask")) {
		// Brush shaders mask with the inverse of the base alpha, model shaders with the alpha itself.
		const bool brush = shader.contains("lightmappedgeneric") || shader.contains("worldvertextransition");
		info.envmapMask = brush ? MaterialInfo::EnvmapMask::InvertedBaseAlpha : MaterialInfo::EnvmapMask::BaseAlpha;
	}
}

/// `live`: the material editor's text instead of the file, with its height map (see MaterialCache::setLive).
std::shared_ptr<MaterialInfo> loadInfo(const GameFileSystem& fs, const QString& name, unsigned maxSize, bool relief, const QString* liveVmt = nullptr,
                                       const QImage& liveHeights = {}) {
	auto info = std::make_shared<MaterialInfo>();
	info->name = name;
	// Sky by name too: the sky shows even where the game's toolsskybox material is missing.
	info->isSky = isSkyMaterialName(name.toStdString());
	std::string error;
	const auto material = liveVmt ? parseMaterial(liveVmt->toStdString(), name.toStdString(), &error) : loadMaterial(fs, name.toStdString(), &error);
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
	if (const auto scale = material->param("$decalscale"); !scale.empty()) {
		bool ok = false;
		const float value = QString::fromStdString(scale).toFloat(&ok);
		if (ok && value > 0) info->decalScale = value;
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
	if (maxSize > 128) {
		readEnvmap(fs, *material, *info);
	}
	if (relief && liveVmt) {
		// The editor's own height map (not saved yet), at the depth it has now.
		const std::string shader = toLower(material->shader);
		if (!liveHeights.isNull() && (shader == "fidelity_parallax" || shader == "fidelity_worldvertextransition")) {
			info->height = liveHeights;
			const auto depth = QString::fromStdString(material->param("$parallaxdepth"));
			info->parallaxDepth = depth.isEmpty() ? 0.02f : depth.toFloat();
		}
	} else if (relief) {
		// Where Mapsmith keeps relief off (windows, signs, smooth surfaces - Game enhancements writes the
		// same into the mod), the view shows none either; a material already on the shader shows its own.
		auto source = fidelityReliefSource(name.toStdString(), *material);
		if (source && source->kind != ReliefSource::Kind::HeightMap && !reliefFitInGame(fs, name.toStdString(), *material).suits) source.reset();
		if (source) {
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
	m_live.clear();   // the material editor's changes belong to the old game
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

void MaterialCache::setLive(const QString& rawName, const QString& vmt, const QImage& heights) {
	const QString name = normalizedMaterialName(rawName);
	m_live.insert(name, {vmt, heights});
	reload(name);
}

void MaterialCache::clearLive(const QString& rawName) {
	const QString name = normalizedMaterialName(rawName);
	if (m_live.remove(name) > 0) reload(name);
}

void MaterialCache::reload(const QString& rawName) {
	const QString name = normalizedMaterialName(rawName);
	m_loaded.remove(name);
	m_pending.remove(name);
	(void)get(name);
}

bool MaterialCache::isKnown(const QString& rawName) const {
	const QString name = normalizedMaterialName(rawName);
	return m_loaded.contains(name) || m_pending.contains(name);
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
		const auto live = m_live.constFind(name);
		const std::optional<Live> shown = live != m_live.constEnd() ? std::optional<Live>(*live) : std::nullopt;
		// Decoded on a pool thread; the result is handed back to the GUI thread. The generation
		// number drops results that were started before clear() (game changed).
		// The ticket drops an older read of the same material that finishes after a newer one (live edits).
		const quint64 ticket = ++m_ticket;
		m_tickets.insert(name, ticket);
		m_pool.start([this, fs, name, maxSize, generation, relief, shown, ticket] {
			auto info = shown ? loadInfo(*fs, name, maxSize, relief, &shown->vmt, shown->heights) : loadInfo(*fs, name, maxSize, relief);
			QMetaObject::invokeMethod(this, [this, name, info, generation, ticket] { finished(name, info, generation, ticket); }, Qt::QueuedConnection);
		});
	}
	return nullptr;
}

void MaterialCache::finished(const QString& name, std::shared_ptr<MaterialInfo> info, quint64 generation, quint64 ticket) {
	if (generation != m_generation || m_tickets.value(name) != ticket) {
		return; // game changed meanwhile, or read again since
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
