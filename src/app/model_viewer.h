// model_viewer.h - Interactive 3D view of one model for the Models page: textured with a soft
// light from the viewer, a floor grid for scale and an optional wireframe on top. Left drag
// orbits, right or middle drag moves, the wheel zooms, double-click frames the model again.
// Only draws when something changed (a new model, a texture arriving, the mouse).

#pragma once

#include <QHash>
#include <QImage>
#include <QList>
#include <QMatrix4x4>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLWidget>
#include <QPointF>
#include <QVector3D>

#include <memory>
#include <vector>

#include <sme/studio_model.h>

class QPainter;

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
	/// Draws the model's vertices moved into a pose (animation previews; same count as the
	/// model's vertices, empty = the rest pose). Only the vertex buffer is updated.
	void setPose(std::vector<ModelVertex> vertices);
	/// How many poses were uploaded (tests).
	[[nodiscard]] int posesUploaded() const { return m_posesUploaded; }
	void setWireframe(bool on);
	[[nodiscard]] bool wireframe() const { return m_wireframe; }
	/// Camera around the whole model, seen from the front-left and a little above.
	void frameModel();
	/// First-person view of a weapon's view model as the game draws it: the eye at the model's
	/// origin looking along +X, `fov` degrees wide at 4:3 (viewmodel_fov, 54 in Half-Life 2; wider
	/// screens see more at the sides, as in the game). Off: the orbit camera again.
	void setFirstPerson(bool on, float fov = 54);
	[[nodiscard]] bool firstPerson() const { return m_firstPerson; }
	/// Orbit angles in degrees and distance in units (for tests and the reset button).
	[[nodiscard]] float yaw() const { return m_yaw; }
	[[nodiscard]] float pitch() const { return m_pitch; }
	[[nodiscard]] float distance() const { return m_distance; }
	/// What the orbit camera looks at (the middle of the fitted box).
	[[nodiscard]] QVector3D orbitCenter() const { return m_center; }
	/// Orbit camera from these angles in degrees (presets Front/Side/Top), keeping what it looks at.
	void setOrbit(float yaw, float pitch);
	/// Orbit camera around a point, far enough to see a sphere of this radius.
	void focusOn(const QVector3D& center, float radius);

	/// Glow preview ("Glow ..."): the material is drawn like the game's $selfillum - towards
	/// tint * base colour where the mask is white (mask: grey picture in the texture's layout).
	/// An empty mask takes the material's glow away again.
	void setGlow(const QString& material, const QImage& mask, const QVector3D& tint);
	void clearGlow();
	[[nodiscard]] bool hasGlow(const QString& material) const;
	/// Labelled points drawn over the model (attachments for "Effects ..."), in model space.
	struct Marker {
		QVector3D position;
		QString label;
		bool highlighted = false;
	};
	void setMarkers(QList<Marker> markers);
	[[nodiscard]] const QList<Marker>& markers() const { return m_markers; }

protected:
	/// Where the next frame is seen from (subclasses may put the camera elsewhere, e.g. the
	/// animation editor's first-person view of a weapon). fov: vertical, in degrees.
	struct Camera {
		QVector3D eye, center, up{0, 0, 1};
		float fov = 45;
		float nearPlane = 0.5f, farPlane = 10000;
	};
	[[nodiscard]] virtual Camera camera() const;
	/// Projection times view of camera() for the widget's size.
	[[nodiscard]] QMatrix4x4 viewProjection() const;
	/// Drawn on top of the model with a QPainter (helpers: skeletons, gizmos); nothing by default.
	virtual void paintOverlay(QPainter& painter, const QMatrix4x4& viewProj);

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
	void uploadGrid();
	void releaseGeometry();
	/// The box the camera and the grid are fitted to: the shown pose's (view models are posed far
	/// from their rest pose), else the model's rest bounds.
	[[nodiscard]] Bounds viewBounds() const;
	[[nodiscard]] Texture textureFor(const QString& material);

	MaterialCache* m_materials;
	std::shared_ptr<const ModelGeometry> m_model;
	bool m_dirty = false;                 ///< m_model not uploaded yet
	std::vector<ModelVertex> m_pose;      ///< Posed vertices waiting for the next frame
	bool m_poseDirty = false;
	int m_posesUploaded = 0;
	QString m_message;
	bool m_wireframe = false;
	std::unique_ptr<QOpenGLShaderProgram> m_program, m_lineProgram;
	GLuint m_vao = 0, m_vbo = 0, m_ibo = 0, m_gridVao = 0, m_gridVbo = 0;
	int m_gridVertices = 0;
	bool m_gridDirty = true;              ///< Grid to rebuild (model or fitted box changed)
	bool m_frameOnPose = false;           ///< Frame again once the first pose arrives
	Bounds m_fitBounds;                   ///< Box of the pose the view was fitted to (empty: rest bounds)
	QHash<QString, Texture> m_textures;   ///< Uploaded base textures by material name
	GLuint m_white = 0;                   ///< For materials without a texture
	bool m_initialized = false;
	struct Glow {
		QImage mask;          ///< Waiting for upload (null once uploaded)
		GLuint id = 0;
		QVector3D tint;
	};
	QHash<QString, Glow> m_glows;         ///< By normalized material name
	QList<GLuint> m_releasedGlows;        ///< Textures to delete on the next paint
	QList<Marker> m_markers;
	QMatrix4x4 m_lastViewProj;            ///< For placing the markers
	// Orbit camera around m_center.
	QVector3D m_center;
	float m_yaw = 35, m_pitch = 18, m_distance = 200;
	bool m_firstPerson = false;
	float m_viewFov = 54;
	QPointF m_lastMouse;
	Qt::MouseButton m_dragButton = Qt::NoButton;
};

} // namespace sme::app
