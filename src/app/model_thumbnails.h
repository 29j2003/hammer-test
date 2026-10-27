// model_thumbnails.h - Preview pictures of models for the model browser.
//
// Drawn on the CPU (small software rasterizer with depth buffer, texture and simple light)
// in background threads, so the browser never waits and no OpenGL context is needed.
// Only items the browser actually shows are rendered.

#pragma once

#include <QHash>
#include <QImage>
#include <QObject>
#include <QPixmap>
#include <QSet>
#include <QThreadPool>

#include <atomic>
#include <functional>
#include <memory>

#include <sme/studio_model.h>

namespace sme::app {

class MaterialCache;
class ModelCache;

/// Renders a model to an image (camera from the front-left, slightly above, like Hammer).
/// `texture(material)` returns the material's base texture or a null image.
[[nodiscard]] QImage renderModelPreview(const ModelGeometry& model, int size, const std::function<QImage(const std::string&)>& texture);

class ModelThumbnails : public QObject {
	Q_OBJECT

public:
	ModelThumbnails(ModelCache* models, MaterialCache* materials, int size, QObject* parent = nullptr);
	~ModelThumbnails() override;

	/// The preview if ready, otherwise a placeholder (thumbnailReady follows).
	[[nodiscard]] QPixmap get(const QString& path);
	void clear();

signals:
	void thumbnailReady(const QString& path);

private:
	void start(const QString& path);

	ModelCache* m_models;
	MaterialCache* m_materials;
	int m_size;
	QHash<QString, QPixmap> m_done;
	QSet<QString> m_running;
	QSet<QString> m_waitingForModel;
	QHash<QString, QSet<QString>> m_waitingForMaterial;   ///< material -> models to draw again
	QPixmap m_placeholder, m_broken, m_animationsOnly;
	QThreadPool m_pool;
	std::shared_ptr<std::atomic<int>> m_generation = std::make_shared<std::atomic<int>>(0);
};

} // namespace sme::app
