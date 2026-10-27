// material_preview.h - Live preview of a material for the material editor: a plane, cube or
// sphere with the base texture, normal map (also self-shadowed "ssbump"), detail texture,
// reflections (env_cubemap seen as a sky), masks, transparency, self-illumination and phong,
// relief from a height map (parallax occlusion mapping, as mapbase-fidelity's Fidelity_Parallax),
// water (reflection, fog colour, the sun's glint) and liquid in a glass (Fidelity_Liquid), under a
// choice of lights (sun, indoor, night, unlit). Left drag turns the object, right drag moves the
// light. An approximation of the game's shaders, not the game's renderer.

#pragma once

#include <QImage>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLWidget>
#include <QTimer>
#include <QVector3D>

#include <array>
#include <memory>

namespace sme::app {

/// OpenGL widget that draws the material preview (see the file comment).
class MaterialPreview : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
	Q_OBJECT

public:
	enum class Shape { Plane, Cube, Sphere };
	enum class Lighting { Sun, Indoor, Night, Unlit };
	enum Slot { Base, Bump, Mask, Detail, Height, SlotCount };
	enum class Mode { Surface, Water, Liquid };

	/// Material parameters the preview's shader uses (set from the editor's fields).
	struct Settings {
		bool ssbump = false;
		bool envmap = false;
		int maskMode = 0;              ///< 0 none, 1 $envmapmask, 2 base alpha, 3 normal map alpha
		int alphaMode = 0;             ///< 0 opaque, 1 translucent, 2 alpha test, 3 additive
		bool selfillum = false;
		bool phong = false;
		QVector3D envTint{1, 1, 1};
		QVector3D selfillumTint{1, 1, 1};
		float detailScale = 4;
		int detailBlend = 0;
		float phongBoost = 1;
		float phongExponent = 5;
		float alphaRef = 0.5f;
		Mode mode = Mode::Surface;
		float parallaxDepth = 0;       ///< In texture units (Fidelity_Parallax's $parallaxdepth); 0 = no relief
		float parallaxAO = 0.35f;
		float waterGlint = 1;          ///< Water_Fidelity's sun glint (0 = plain water)
		QVector3D reflectTint{1, 1, 1};
		QVector3D fogColor{0.1f, 0.18f, 0.2f};
		float reflectAmount = 0.8f;
		QVector3D liquidColor{0.55f, 0.35f, 0.12f};
		float liquidAlpha = 0.85f;
		float fill = 0.55f;            ///< 0..1 of the shape's height
		QVector3D glassColor{0.75f, 0.85f, 0.8f};
		float glassAlpha = 0.3f;
	};

	explicit MaterialPreview(QWidget* parent = nullptr);
	~MaterialPreview() override;

	void setTexture(Slot slot, const QImage& image);   ///< Null image: slot unused
	/// What to draw: material parameters, shape and light.
	void setSettings(const Settings& settings);
	void setShape(Shape shape);
	void setLighting(Lighting lighting);

protected:
	void initializeGL() override;
	void paintGL() override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void wheelEvent(QWheelEvent* event) override;
	void showEvent(QShowEvent* event) override;
	void hideEvent(QHideEvent* event) override;

private:
	void buildMesh();
	void upload(Slot slot);

	std::unique_ptr<QOpenGLShaderProgram> m_program;
	GLuint m_vao = 0, m_vbo = 0;
	GLsizei m_vertexCount = 0;
	std::array<GLuint, SlotCount> m_textures{};
	std::array<QImage, SlotCount> m_images;
	std::array<bool, SlotCount> m_dirty{};
	Settings m_settings;
	Shape m_shape = Shape::Plane;
	Lighting m_lighting = Lighting::Sun;
	bool m_meshDirty = true;
	bool m_ready = false;
	float m_yaw = 20, m_pitch = 15, m_distance = 3.2f;
	float m_lightYaw = 40, m_lightPitch = 45;
	QPointF m_last;
	QTimer* m_clock;   ///< Moves the water while it is shown
	float m_time = 0;
};

} // namespace sme::app
