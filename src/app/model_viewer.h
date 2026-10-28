// model_viewer.h - Interactive 3D view of one model for the Models page: textured with a soft
// light from the viewer, a floor grid for scale and an optional wireframe on top. Left drag
// orbits, right or middle drag moves, the wheel zooms, double-click frames the model again.
// Only draws when something changed (a new model, a texture arriving, the mouse).

#pragma once

#include <QHash>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLWidget>
#include <QPointF>
#include <QVector3D>

#include <memory>

#include <sme/studio_model.h>

namespace sme::app {

class MaterialCache;

/// OpenGL widget showing a model (see the file comment).
class ModelViewer : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
	Q_OBJECT

public:
	explicit ModelViewer(MaterialCache* materials, QWidget* parent = nullptr);
	~ModelViewer() override;

	/// Shows this model (nullptr: nothing). `frame` puts the camera around it again; switching
	/// skins or body groups keeps the view.
	void setModel(std::shared_ptr<const ModelGeometry> model, bool frame = true);
	[[nodiscard]] const std::shared_ptr<const ModelGeometry>& model() const { return m_model; }
	/// Text in the middle while there is no model ("Loading ...", "Cannot load: ...").
	void setMessage(const QString& message);
	[[nodiscard]] QString message() const { return m_message; }
	void setWireframe(bool on);
	[[nodiscard]] bool wireframe() const { return m_wireframe; }
	/// Camera around the whole model, seen from the front-left and a little above.
	void frameModel();
	/// Orbit angles in degrees and distance in units (for tests and the reset button).
	[[nodiscard]] float yaw() const { return m_yaw; }
	[[nodiscard]] float distance() const { return m_distance; }

protected:
	void initializeGL() override;
	void paintGL() override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	void mouseDoubleClickEvent(QMouseEvent* event) override;
	void wheelEvent(QWheelEvent* event) override;

private:
	struct Texture {
		GLuint id = 0;
		bool alphaTest = false;
		bool translucent = false;
	};
	void upload();
	void releaseGeometry();
	[[nodiscard]] Texture textureFor(const QString& material);

	MaterialCache* m_materials;
	std::shared_ptr<const ModelGeometry> m_model;
	bool m_dirty = false;                 ///< m_model not uploaded yet
	QString m_message;
	bool m_wireframe = false;
	std::unique_ptr<QOpenGLShaderProgram> m_program, m_lineProgram;
	GLuint m_vao = 0, m_vbo = 0, m_ibo = 0, m_gridVao = 0, m_gridVbo = 0;
	int m_gridVertices = 0;
	QHash<QString, Texture> m_textures;   ///< Uploaded base textures by material name
	GLuint m_white = 0;                   ///< For materials without a texture
	bool m_initialized = false;
	// Orbit camera around m_center.
	QVector3D m_center;
	float m_yaw = 35, m_pitch = 18, m_distance = 200;
	QPointF m_lastMouse;
	Qt::MouseButton m_dragButton = Qt::NoButton;
};

} // namespace sme::app
