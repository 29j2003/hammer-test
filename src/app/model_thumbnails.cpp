// model_thumbnails.cpp - Software-rendered model previews (see model_thumbnails.h).

#include "model_thumbnails.h"

#include "material_cache.h"
#include "model_cache.h"

#include <QPainter>
#include <QPointer>

#include <algorithm>
#include <cmath>
#include <vector>

namespace sme::app {

QImage renderModelPreview(const ModelGeometry& model, int size, const std::function<QImage(const std::string&)>& texture) {
	const int s = size * 2;   // drawn twice as large, then scaled down (smooth edges)
	QImage image(s, s, QImage::Format_ARGB32);
	image.fill(Qt::transparent);
	if (model.bounds.empty() || model.indices.empty()) {
		return image.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
	}
	// Camera in front of the model (Source models look along +X), a bit to the left and above.
	const Vec3 center = model.bounds.center();
	const double radius = std::max(1.0, length(model.bounds.size()) * 0.5);
	constexpr double TAN_HALF_FOV = 0.2679;   // 30° field of view
	const Vec3 toEye = normalize(Vec3{1.0, 0.65, 0.5});
	const Vec3 eye = center + toEye * (radius / 0.2588 * 1.02);
	const Vec3 forward = -toEye;
	const Vec3 right = normalize(cross(forward, Vec3{0, 0, 1}));
	const Vec3 up = cross(right, forward);
	const Vec3 light = normalize(Vec3{0.6, 0.35, 0.75});

	struct Projected {
		double x, y, invZ;
	};
	std::vector<Projected> projected(model.vertices.size());
	for (std::size_t i = 0; i < model.vertices.size(); ++i) {
		const auto& v = model.vertices[i];
		const Vec3 p = Vec3{v.position[0], v.position[1], v.position[2]} - eye;
		const double z = std::max(0.01, dot(p, forward));
		projected[i] = {s * 0.5 + dot(p, right) / (z * TAN_HALF_FOV) * s * 0.5, s * 0.5 - dot(p, up) / (z * TAN_HALF_FOV) * s * 0.5, 1.0 / z};
	}
	std::vector<float> depth(static_cast<std::size_t>(s) * static_cast<std::size_t>(s), 0.f);   // 1/z, larger is closer
	auto* pixels = reinterpret_cast<QRgb*>(image.bits());

	for (const auto& mesh : model.meshes) {
		QImage tex = texture(mesh.material);
		if (!tex.isNull() && tex.format() != QImage::Format_ARGB32) {
			tex = tex.convertToFormat(QImage::Format_ARGB32);
		}
		const int tw = tex.isNull() ? 1 : tex.width(), th = tex.isNull() ? 1 : tex.height();
		const auto* texels = tex.isNull() ? nullptr : reinterpret_cast<const QRgb*>(tex.constBits());
		const std::uint32_t end = std::min<std::uint32_t>(mesh.firstIndex + mesh.indexCount, static_cast<std::uint32_t>(model.indices.size()));
		for (std::uint32_t t = mesh.firstIndex; t + 2 < end; t += 3) {
			const std::uint32_t ia = model.indices[t], ib = model.indices[t + 1], ic = model.indices[t + 2];
			if (ia >= projected.size() || ib >= projected.size() || ic >= projected.size()) {
				continue;
			}
			const auto &a = projected[ia], &b = projected[ib], &c = projected[ic];
			const double area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
			if (std::abs(area) < 1e-9) {
				continue;
			}
			// Flat shade per triangle, lit from both sides.
			const auto& va = model.vertices[ia];
			const auto& vb = model.vertices[ib];
			const auto& vc = model.vertices[ic];
			Vec3 n{va.normal[0] + vb.normal[0] + vc.normal[0], va.normal[1] + vb.normal[1] + vc.normal[1], va.normal[2] + vb.normal[2] + vc.normal[2]};
			n = length(n) > 0 ? normalize(n) : Vec3{0, 0, 1};
			if (dot(n, forward) > 0) {
				n = -n;
			}
			const double shade = 0.38 + 0.62 * std::max(0.0, dot(n, light));
			const int x0 = std::max(0, static_cast<int>(std::floor(std::min({a.x, b.x, c.x}))));
			const int x1 = std::min(s - 1, static_cast<int>(std::ceil(std::max({a.x, b.x, c.x}))));
			const int y0 = std::max(0, static_cast<int>(std::floor(std::min({a.y, b.y, c.y}))));
			const int y1 = std::min(s - 1, static_cast<int>(std::ceil(std::max({a.y, b.y, c.y}))));
			for (int y = y0; y <= y1; ++y) {
				for (int x = x0; x <= x1; ++x) {
					const double px = x + 0.5, py = y + 0.5;
					const double w0 = ((b.x - px) * (c.y - py) - (b.y - py) * (c.x - px)) / area;
					const double w1 = ((c.x - px) * (a.y - py) - (c.y - py) * (a.x - px)) / area;
					const double w2 = 1 - w0 - w1;
					if (w0 < 0 || w1 < 0 || w2 < 0) {
						continue;
					}
					const double invZ = w0 * a.invZ + w1 * b.invZ + w2 * c.invZ;
					float& d = depth[static_cast<std::size_t>(y) * static_cast<std::size_t>(s) + static_cast<std::size_t>(x)];
					if (invZ <= d) {
						continue;
					}
					QRgb color = qRgb(170, 170, 175);
					if (texels) {
						// Perspective-correct texture coordinates.
						const double u = (w0 * va.uv[0] * a.invZ + w1 * vb.uv[0] * b.invZ + w2 * vc.uv[0] * c.invZ) / invZ;
						const double v = (w0 * va.uv[1] * a.invZ + w1 * vb.uv[1] * b.invZ + w2 * vc.uv[1] * c.invZ) / invZ;
						const int tx = ((static_cast<int>(std::floor(u * tw)) % tw) + tw) % tw;
						const int ty = ((static_cast<int>(std::floor(v * th)) % th) + th) % th;
						color = texels[static_cast<std::size_t>(ty) * static_cast<std::size_t>(tw) + static_cast<std::size_t>(tx)];
						if (qAlpha(color) < 128) {
							continue;   // alpha-tested parts (leaves, fences)
						}
					}
					d = static_cast<float>(invZ);
					pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(s) + static_cast<std::size_t>(x)] =
						qRgb(std::min(255, static_cast<int>(qRed(color) * shade)), std::min(255, static_cast<int>(qGreen(color) * shade)),
						     std::min(255, static_cast<int>(qBlue(color) * shade)));
				}
			}
		}
	}
	return image.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

ModelThumbnails::ModelThumbnails(ModelCache* models, MaterialCache* materials, int size, QObject* parent)
	: QObject(parent), m_models(models), m_materials(materials), m_size(size) {
	m_pool.setMaxThreadCount(2);
	m_placeholder = QPixmap(size, size);
	m_placeholder.fill(QColor(45, 47, 52));
	{
		QPainter p(&m_placeholder);
		p.setPen(QColor(110, 115, 125));
		p.drawText(m_placeholder.rect(), Qt::AlignCenter, tr("loading"));
	}
	m_broken = QPixmap(size, size);
	m_broken.fill(QColor(60, 40, 40));
	{
		QPainter p(&m_broken);
		p.setPen(QColor(230, 140, 140));
		p.drawText(m_broken.rect(), Qt::AlignCenter | Qt::TextWordWrap, tr("cannot\nload"));
	}
	m_animationsOnly = QPixmap(size, size);
	m_animationsOnly.fill(QColor(40, 44, 54));
	{
		QPainter p(&m_animationsOnly);
		p.setPen(QColor(150, 170, 210));
		p.drawText(m_animationsOnly.rect(), Qt::AlignCenter | Qt::TextWordWrap, tr("animations\nonly"));
	}
	connect(models, &ModelCache::modelReady, this, [this](const QString& path) {
		if (m_waitingForModel.remove(path)) {
			start(path);
		}
	});
	connect(materials, &MaterialCache::materialReady, this, [this](const QString& name) {
		// Draw again once a texture arrived that was still loading.
		const auto paths = m_waitingForMaterial.take(name);
		for (const auto& path : paths) {
			m_done.remove(path);
			start(path);
		}
	});
}

ModelThumbnails::~ModelThumbnails() {
	++*m_generation;
	m_pool.waitForDone();
}

void ModelThumbnails::clear() {
	++*m_generation;
	m_pool.waitForDone();
	m_done.clear();
	m_running.clear();
	m_waitingForModel.clear();
	m_waitingForMaterial.clear();
}

QPixmap ModelThumbnails::get(const QString& path) {
	if (const auto it = m_done.constFind(path); it != m_done.constEnd()) {
		return *it;
	}
	if (!m_running.contains(path) && !m_waitingForModel.contains(path)) {
		start(path);
	}
	return m_placeholder;
}

void ModelThumbnails::start(const QString& path) {
	const auto geometry = m_models->get(path);
	if (!geometry) {
		if (m_models->failed(path)) {
			m_done.insert(path, m_broken);
			emit thumbnailReady(path);
		} else {
			m_waitingForModel.insert(path);
		}
		return;
	}
	if (geometry->empty()) {
		m_done.insert(path, m_animationsOnly);
		emit thumbnailReady(path);
		return;
	}
	// Textures that are ready now; missing ones trigger one more drawing when they arrive.
	QHash<QString, QImage> textures;
	for (const auto& mesh : geometry->meshes) {
		const QString name = normalizedMaterialName(QString::fromStdString(mesh.material));
		if (textures.contains(name)) {
			continue;
		}
		if (const auto info = m_materials->get(name)) {
			textures.insert(name, info->image);
		} else {
			textures.insert(name, QImage());
			m_waitingForMaterial[name].insert(path);
		}
	}
	m_running.insert(path);
	const auto generationCounter = m_generation;
	const int generation = *generationCounter;
	const int size = m_size;
	QPointer<ModelThumbnails> self(this);
	m_pool.start([self, path, geometry, textures, size, generationCounter, generation] {
		if (*generationCounter != generation) {
			return;
		}
		const QImage image = renderModelPreview(*geometry, size, [&](const std::string& material) {
			return textures.value(normalizedMaterialName(QString::fromStdString(material)));
		});
		QMetaObject::invokeMethod(
			self, [self, path, image, generationCounter, generation] {
				if (!self || *generationCounter != generation) {
					return;
				}
				self->m_running.remove(path);
				self->m_done.insert(path, QPixmap::fromImage(image));
				emit self->thumbnailReady(path);
			},
			Qt::QueuedConnection);
	});
}

} // namespace sme::app
