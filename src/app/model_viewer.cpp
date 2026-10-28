// model_viewer.cpp - Interactive model view of the Models page (see model_viewer.h).

#include "model_viewer.h"

#include "material_cache.h"

#include <QMatrix4x4>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <vector>

namespace sme::app {

namespace {

const char* MODEL_VS = R"(#version 330 core
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;
uniform mat4 u_viewProj;
out vec3 v_normal;
out vec2 v_uv;
void main() {
	v_normal = a_normal;
	v_uv = a_uv;
	gl_Position = u_viewProj * vec4(a_pos, 1.0);
}
)";

// Key light from the camera's upper left plus a cooler fill from below, half-Lambert so the dark
// side keeps its texture (like the engine's model lighting in a lit room).
const char* MODEL_FS = R"(#version 330 core
in vec3 v_normal;
in vec2 v_uv;
uniform sampler2D u_texture;
uniform vec3 u_keyDir;
uniform bool u_alphaTest;
uniform float u_alpha;
out vec4 fragColor;
void main() {
	vec4 base = texture(u_texture, v_uv);
	if (u_alphaTest && base.a < 0.5) discard;
	vec3 n = normalize(v_normal);
	float key = dot(n, u_keyDir) * 0.5 + 0.5;
	float fill = max(dot(n, vec3(0.0, 0.0, -1.0)), 0.0);
	vec3 light = vec3(0.22, 0.23, 0.25) + vec3(1.0, 0.97, 0.92) * key * key * 0.95 + vec3(0.25, 0.3, 0.4) * fill * 0.4;
	fragColor = vec4(base.rgb * light, u_alpha < 1.0 ? base.a * u_alpha : 1.0);
}
)";

const char* LINE_VS = R"(#version 330 core
layout(location = 0) in vec3 a_pos;
uniform mat4 u_viewProj;
void main() {
	gl_Position = u_viewProj * vec4(a_pos, 1.0);
}
)";

const char* LINE_FS = R"(#version 330 core
uniform vec4 u_color;
out vec4 fragColor;
void main() {
	fragColor = u_color;
}
)";

constexpr float FOV = 45.0f;

} // namespace

ModelViewer::ModelViewer(MaterialCache* materials, QWidget* parent) : QOpenGLWidget(parent), m_materials(materials) {
	setMinimumSize(200, 160);
	setFocusPolicy(Qt::ClickFocus);
	setToolTip(tr("Left drag turns, right drag moves, the wheel zooms, double-click frames the model"));
	if (m_materials) {
		// A texture arrived: draw again if this model uses it (uploaded on the next paint).
		connect(m_materials, &MaterialCache::materialReady, this, [this](const QString& name) {
			if (!m_model) return;
			for (const auto& mesh : m_model->meshes) {
				if (normalizedMaterialName(QString::fromStdString(mesh.material)) == name) {
					m_textures.remove(name);   // the placeholder (if any) is replaced
					update();
					return;
				}
			}
		});
	}
}

ModelViewer::~ModelViewer() {
	if (!m_initialized) return;
	makeCurrent();
	releaseGeometry();
	for (const auto& texture : std::as_const(m_textures)) {
		if (texture.id != m_white) glDeleteTextures(1, &texture.id);
	}
	glDeleteTextures(1, &m_white);
	glDeleteVertexArrays(1, &m_gridVao);
	glDeleteBuffers(1, &m_gridVbo);
	m_program.reset();
	m_lineProgram.reset();
	doneCurrent();
}

void ModelViewer::setModel(std::shared_ptr<const ModelGeometry> model, bool frame) {
	m_model = std::move(model);
	m_dirty = true;
	m_pose.clear();
	m_poseDirty = false;
	if (m_model) m_message.clear();
	if (frame) frameModel();
	update();
}

void ModelViewer::setPose(std::vector<ModelVertex> vertices) {
	if (!m_model || (!vertices.empty() && vertices.size() != m_model->vertices.size())) return;
	m_pose = std::move(vertices);
	m_poseDirty = true;
	update();
}

void ModelViewer::setMessage(const QString& message) {
	m_message = message;
	update();
}

void ModelViewer::setWireframe(bool on) {
	m_wireframe = on;
	update();
}

void ModelViewer::frameModel() {
	m_yaw = 35;
	m_pitch = 18;
	if (m_model && !m_model->bounds.empty()) {
		const Vec3 c = m_model->bounds.center();
		m_center = QVector3D(float(c.x), float(c.y), float(c.z));
		const float radius = float(std::max(4.0, length(m_model->bounds.size()) * 0.5));
		m_distance = radius / std::sin(float(FOV * 0.5 * M_PI / 180.0)) * 1.05f;
	} else {
		m_center = {};
		m_distance = 200;
	}
	update();
}

void ModelViewer::initializeGL() {
	initializeOpenGLFunctions();
	m_program = std::make_unique<QOpenGLShaderProgram>();
	m_lineProgram = std::make_unique<QOpenGLShaderProgram>();
	const bool ok = m_program->addShaderFromSourceCode(QOpenGLShader::Vertex, MODEL_VS) &&
	                m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, MODEL_FS) && m_program->link() &&
	                m_lineProgram->addShaderFromSourceCode(QOpenGLShader::Vertex, LINE_VS) &&
	                m_lineProgram->addShaderFromSourceCode(QOpenGLShader::Fragment, LINE_FS) && m_lineProgram->link();
	if (!ok) {
		m_message = tr("OpenGL 3.3 is required for the model view");
		return;
	}
	const quint32 white = 0xffc8c8c8;   // light grey for materials without a texture
	glGenTextures(1, &m_white);
	glBindTexture(GL_TEXTURE_2D, m_white);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &white);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glGenVertexArrays(1, &m_gridVao);
	glGenBuffers(1, &m_gridVbo);
	m_initialized = true;
	m_dirty = true;
}

void ModelViewer::releaseGeometry() {
	if (m_vao) glDeleteVertexArrays(1, &m_vao);
	if (m_vbo) glDeleteBuffers(1, &m_vbo);
	if (m_ibo) glDeleteBuffers(1, &m_ibo);
	m_vao = m_vbo = m_ibo = 0;
}

void ModelViewer::upload() {
	m_dirty = false;
	releaseGeometry();
	// Floor grid under the model: lines every 16 units (64 for big models) for a sense of size.
	std::vector<float> grid;
	if (m_model && !m_model->bounds.empty()) {
		const Bounds& b = m_model->bounds;
		const double extent = std::max({b.size().x, b.size().y, 64.0});
		const double step = extent > 512 ? 64 : 16;
		const double half = std::ceil(extent * 0.75 / step) * step;
		const double cx = std::round(b.center().x / step) * step, cy = std::round(b.center().y / step) * step;
		const float z = float(b.min.z);
		for (double d = -half; d <= half + 0.01; d += step) {
			grid.insert(grid.end(), {float(cx + d), float(cy - half), z, float(cx + d), float(cy + half), z});
			grid.insert(grid.end(), {float(cx - half), float(cy + d), z, float(cx + half), float(cy + d), z});
		}
	}
	m_gridVertices = int(grid.size() / 3);
	glBindVertexArray(m_gridVao);
	glBindBuffer(GL_ARRAY_BUFFER, m_gridVbo);
	glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(grid.size() * sizeof(float)), grid.data(), GL_STATIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
	glBindVertexArray(0);
	if (!m_model || m_model->empty()) return;

	glGenVertexArrays(1, &m_vao);
	glGenBuffers(1, &m_vbo);
	glGenBuffers(1, &m_ibo);
	glBindVertexArray(m_vao);
	glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
	glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(m_model->vertices.size() * sizeof(ModelVertex)), m_model->vertices.data(), GL_STATIC_DRAW);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ibo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(m_model->indices.size() * sizeof(std::uint32_t)), m_model->indices.data(), GL_STATIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ModelVertex), reinterpret_cast<void*>(offsetof(ModelVertex, position)));
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(ModelVertex), reinterpret_cast<void*>(offsetof(ModelVertex, normal)));
	glEnableVertexAttribArray(2);
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(ModelVertex), reinterpret_cast<void*>(offsetof(ModelVertex, uv)));
	glBindVertexArray(0);
}

ModelViewer::Texture ModelViewer::textureFor(const QString& material) {
	const QString name = normalizedMaterialName(material);
	if (const auto it = m_textures.constFind(name); it != m_textures.constEnd()) return *it;
	Texture texture;
	texture.id = m_white;
	const auto info = m_materials ? m_materials->get(name) : nullptr;
	if (!info) return texture;   // loading: grey until materialReady
	texture.alphaTest = info->kind == MaterialInfo::Kind::AlphaTest;
	texture.translucent = info->kind == MaterialInfo::Kind::Translucent || info->kind == MaterialInfo::Kind::Additive;
	const QImage image = info->image.isNull() ? (info->found ? QImage() : MaterialCache::checkerboard()) : info->image;
	if (!image.isNull()) {
		const QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
		glGenTextures(1, &texture.id);
		glBindTexture(GL_TEXTURE_2D, texture.id);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, rgba.width(), rgba.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.constBits());
		glGenerateMipmap(GL_TEXTURE_2D);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	}
	m_textures.insert(name, texture);
	return texture;
}

void ModelViewer::paintGL() {
	glClearColor(0.16f, 0.17f, 0.19f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	if (!m_initialized) return;
	if (m_dirty) {
		upload();
		m_poseDirty = !m_pose.empty();
	}
	if (m_poseDirty && m_vbo && m_model) {
		// The posed vertices replace the rest pose in the buffer (same layout and count).
		const auto& vertices = m_pose.empty() ? m_model->vertices : m_pose;
		glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
		glBufferSubData(GL_ARRAY_BUFFER, 0, GLsizeiptr(vertices.size() * sizeof(ModelVertex)), vertices.data());
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		++m_posesUploaded;
	}
	m_poseDirty = false;

	const qreal dpr = devicePixelRatioF();
	glViewport(0, 0, int(width() * dpr), int(height() * dpr));
	const float pitch = qDegreesToRadians(m_pitch), yaw = qDegreesToRadians(m_yaw);
	const QVector3D eye = m_center + m_distance * QVector3D(std::cos(yaw) * std::cos(pitch), std::sin(yaw) * std::cos(pitch), std::sin(pitch));
	QMatrix4x4 projection, view;
	projection.perspective(FOV, float(std::max(1, width())) / float(std::max(1, height())), std::max(0.5f, m_distance * 0.01f), m_distance * 20 + 1000);
	view.lookAt(eye, m_center, QVector3D(0, 0, 1));
	const QMatrix4x4 viewProj = projection * view;
	// Light from the camera's upper left.
	const QVector3D forward = (m_center - eye).normalized();
	const QVector3D right = QVector3D::crossProduct(forward, QVector3D(0, 0, 1)).normalized();
	const QVector3D keyDir = (-forward + QVector3D(0, 0, 0.9f) - right * 0.5f).normalized();

	glEnable(GL_DEPTH_TEST);
	if (m_gridVertices > 0) {
		m_lineProgram->bind();
		m_lineProgram->setUniformValue("u_viewProj", viewProj);
		m_lineProgram->setUniformValue("u_color", QVector4D(0.38f, 0.40f, 0.44f, 1.0f));
		glBindVertexArray(m_gridVao);
		glDrawArrays(GL_LINES, 0, m_gridVertices);
	}
	if (m_vao && m_model) {
		m_program->bind();
		m_program->setUniformValue("u_viewProj", viewProj);
		m_program->setUniformValue("u_keyDir", keyDir);
		m_program->setUniformValue("u_texture", 0);
		glActiveTexture(GL_TEXTURE0);
		glBindVertexArray(m_vao);
		// Opaque first, then see-through materials blended on top.
		for (const bool translucentPass : {false, true}) {
			if (translucentPass) {
				glEnable(GL_BLEND);
				glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
				glDepthMask(GL_FALSE);
			}
			for (const auto& mesh : m_model->meshes) {
				const Texture texture = textureFor(QString::fromStdString(mesh.material));
				if (texture.translucent != translucentPass) continue;
				glBindTexture(GL_TEXTURE_2D, texture.id);
				m_program->setUniformValue("u_alphaTest", texture.alphaTest);
				m_program->setUniformValue("u_alpha", translucentPass ? 0.999f : 1.0f);
				glDrawElements(GL_TRIANGLES, GLsizei(mesh.indexCount), GL_UNSIGNED_INT, reinterpret_cast<void*>(std::uintptr_t(mesh.firstIndex) * sizeof(std::uint32_t)));
			}
		}
		glDisable(GL_BLEND);
		glDepthMask(GL_TRUE);
		if (m_wireframe) {
			m_lineProgram->bind();
			m_lineProgram->setUniformValue("u_viewProj", viewProj);
			m_lineProgram->setUniformValue("u_color", QVector4D(1.0f, 0.62f, 0.2f, 0.55f));
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glEnable(GL_POLYGON_OFFSET_LINE);
			glPolygonOffset(-1, -1);
			glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
			glDrawElements(GL_TRIANGLES, GLsizei(m_model->indices.size()), GL_UNSIGNED_INT, nullptr);
			glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
			glDisable(GL_POLYGON_OFFSET_LINE);
			glDisable(GL_BLEND);
		}
		glBindVertexArray(0);
	}
	glBindVertexArray(0);
	glDisable(GL_DEPTH_TEST);

	if (!m_message.isEmpty() || !m_model || m_model->empty()) {
		QPainter painter(this);
		painter.setPen(QColor(160, 165, 175));
		QString text = m_message;
		if (text.isEmpty()) text = m_model ? tr("This model holds only animations for other models") : tr("Pick a model on the left");
		painter.drawText(rect().adjusted(12, 12, -12, -12), Qt::AlignCenter | Qt::TextWordWrap, text);
	}
}

void ModelViewer::mousePressEvent(QMouseEvent* event) {
	m_lastMouse = event->position();
	m_dragButton = event->button();
}

void ModelViewer::mouseMoveEvent(QMouseEvent* event) {
	if (m_dragButton == Qt::NoButton) return;
	const QPointF delta = event->position() - m_lastMouse;
	m_lastMouse = event->position();
	if (m_dragButton == Qt::LeftButton) {
		m_yaw -= float(delta.x()) * 0.4f;
		m_pitch = std::clamp(m_pitch + float(delta.y()) * 0.4f, -89.0f, 89.0f);
	} else {
		// Move the point the camera turns around, in the view plane.
		const float pitch = qDegreesToRadians(m_pitch), yaw = qDegreesToRadians(m_yaw);
		const QVector3D toEye(std::cos(yaw) * std::cos(pitch), std::sin(yaw) * std::cos(pitch), std::sin(pitch));
		const QVector3D right = QVector3D::crossProduct(-toEye, QVector3D(0, 0, 1)).normalized();
		const QVector3D up = QVector3D::crossProduct(right, -toEye).normalized();
		const float scale = m_distance * 2 * std::tan(qDegreesToRadians(FOV * 0.5f)) / float(std::max(1, height()));
		m_center += (-right * float(delta.x()) + up * float(delta.y())) * scale;
	}
	update();
}

void ModelViewer::mouseReleaseEvent(QMouseEvent*) {
	m_dragButton = Qt::NoButton;
}

void ModelViewer::mouseDoubleClickEvent(QMouseEvent*) {
	frameModel();
}

void ModelViewer::wheelEvent(QWheelEvent* event) {
	const float steps = float(event->angleDelta().y()) / 120.0f;
	m_distance = std::clamp(m_distance * std::pow(0.85f, steps), 2.0f, 60000.0f);
	update();
}

} // namespace sme::app
