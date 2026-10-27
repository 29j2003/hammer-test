// material_preview.h - Live preview of a material for the material editor: a plane, cube or
// sphere with the base texture, normal map (also self-shadowed "ssbump"), detail texture,
// reflections (env_cubemap seen as a sky), masks, transparency, self-illumination and phong,
// under a choice of lights (sun, indoor, night, unlit). Left drag turns the object, right drag
// moves the light. An approximation of the game's shaders, not the game's renderer.

#pragma once

#include <QImage>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLWidget>
#include <QVector3D>

#include <array>
#include <memory>

namespace sme::app {

class MaterialPreview : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
	Q_OBJECT

public:
	enum class Shape { Plane, Cube, Sphere };
	enum class Lighting { Sun, Indoor, Night, Unlit };
	enum Slot { Base, Bump, Mask, Detail, SlotCount };

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
	};

	explicit MaterialPreview(QWidget* parent = nullptr);
	~MaterialPreview() override;

	void setTexture(Slot slot, const QImage& image);   ///< Null image: slot unused
	void setSettings(const Settings& settings);
	void setShape(Shape shape);
	void setLighting(Lighting lighting);

protected:
	void initializeGL() override;
	void paintGL() override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void wheelEvent(QWheelEvent* event) override;

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
};

} // namespace sme::app
