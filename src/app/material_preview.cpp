// material_preview.cpp - Material preview (see material_preview.h).

#include "material_preview.h"

#include <QMatrix4x4>
#include <QMouseEvent>
#include <QWheelEvent>

#include <cmath>
#include <vector>

namespace sme::app {

namespace {

// position(3) normal(3) tangent(3) bitangent(3) uv(2)
constexpr int FLOATS = 14;

const char* VS = R"(#version 330 core
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec3 a_tangent;
layout(location = 3) in vec3 a_bitangent;
layout(location = 4) in vec2 a_uv;
uniform mat4 u_viewProj;
uniform mat4 u_model;
out vec3 v_pos;
out vec3 v_normal;
out vec3 v_tangent;
out vec3 v_bitangent;
out vec2 v_uv;
void main() {
	vec4 world = u_model * vec4(a_pos, 1.0);
	v_pos = world.xyz;
	v_normal = mat3(u_model) * a_normal;
	v_tangent = mat3(u_model) * a_tangent;
	v_bitangent = mat3(u_model) * a_bitangent;
	v_uv = a_uv;
	gl_Position = u_viewProj * world;
}
)";

const char* FS = R"(#version 330 core
in vec3 v_pos;
in vec3 v_normal;
in vec3 v_tangent;
in vec3 v_bitangent;
in vec2 v_uv;
uniform sampler2D u_base;
uniform sampler2D u_bump;
uniform sampler2D u_mask;
uniform sampler2D u_detail;
uniform sampler2D u_height;
uniform int u_hasBase, u_hasBump, u_hasMask, u_hasDetail, u_hasHeight;
uniform int u_ssbump, u_envmap, u_maskMode, u_alphaMode, u_selfillum, u_phong, u_detailBlend, u_unlit, u_mode;
uniform vec3 u_envTint, u_selfTint, u_lightDir, u_lightColor, u_ambient, u_camPos;
uniform vec3 u_reflectTint, u_fogColor, u_liquidColor, u_glassColor;
uniform float u_detailScale, u_phongBoost, u_phongExp, u_alphaRef, u_depth, u_ao, u_glint, u_reflectAmount;
uniform float u_maxShift, u_plane;   // relief: most shift (texture units), reference plane (walls 0.5)
uniform float u_liquidAlpha, u_fill, u_glassAlpha, u_time, u_shapeTop;
out vec4 fragColor;

// The reflected world: sky, horizon, ground and the sun (what env_cubemap would capture outdoors).
vec3 skyOnly(vec3 d) {
	vec3 up = mix(vec3(0.55, 0.65, 0.8), vec3(0.25, 0.45, 0.8), clamp(d.z, 0.0, 1.0));
	vec3 down = mix(vec3(0.35, 0.32, 0.28), vec3(0.2, 0.18, 0.16), clamp(-d.z, 0.0, 1.0));
	return d.z > 0.0 ? up : down;
}

vec3 sky(vec3 d) {
	return skyOnly(d) + u_lightColor * pow(max(dot(d, u_lightDir), 0.0), 300.0) * 4.0;
}

// Relief: walks into the height map (white = top) along the view - the same walk as the game's
// Fidelity_Parallax (fidelity_parallax_common.h, FidelityParallaxShift): 8 to 32 steps, the shift
// at flat angles held to 2.5 x the depth and to the most the game lets it move (4 world units at
// the texture's size), the reference plane of walls (half the relief stands out), and the relief
// fading out at very flat angles. (The preview had 5 x and no limit: at flat angles and with
// depths over 0.01 bricks smeared into long slabs - Julius, 29.10.2026.)
vec2 parallax(vec2 uv, vec3 viewTS, out float height) {
	height = 1.0;
	if (u_hasHeight == 0 || u_depth <= 0.0) return uv;
	float depth = u_depth * smoothstep(0.08, 0.3, abs(viewTS.z));
	if (depth <= 0.0001) return uv;
	float steps = mix(32.0, 8.0, clamp(viewTS.z, 0.0, 1.0));
	vec2 full = viewTS.xy / max(viewTS.z, 0.4) * depth;
	full *= min(1.0, u_maxShift / max(length(full), 1e-6));
	vec2 stepUV = full / steps;
	vec2 current = uv + full * (1.0 - u_plane);
	float layer = 1.0 / steps;
	float layerDepth = 0.0;
	float mapDepth = 1.0 - texture(u_height, current).r;
	float previousMap = mapDepth;
	for (int i = 0; i < 32; ++i) {
		if (layerDepth >= mapDepth || float(i) >= steps) break;
		current -= stepUV;
		previousMap = mapDepth;
		mapDepth = 1.0 - texture(u_height, current).r;
		layerDepth += layer;
	}
	float after = mapDepth - layerDepth, before = previousMap - (layerDepth - layer);
	float weight = after / min(after - before, -0.0001);
	vec2 hit = mix(current, current + stepUV, clamp(weight, 0.0, 1.0));
	height = texture(u_height, hit).r;
	return hit;
}

void main() {
	vec3 N = normalize(v_normal);
	vec3 T = normalize(v_tangent), B = normalize(v_bitangent);
	vec3 V = normalize(u_camPos - v_pos);
	vec3 L = normalize(u_lightDir);

	if (u_mode == 1) {
		// Water: waves from the normal map, reflection by Fresnel, the depth's fog colour below,
		// the sun glinting on the waves (Water_Fidelity).
		vec2 uv = v_uv * 2.0;
		vec3 n1 = texture(u_bump, uv + vec2(u_time * 0.03, u_time * 0.02)).xyz * 2.0 - 1.0;
		vec3 n2 = texture(u_bump, uv * 0.7 - vec2(u_time * 0.02, -u_time * 0.025)).xyz * 2.0 - 1.0;
		vec3 n = u_hasBump == 1 ? normalize(n1 + n2) : vec3(0.0, 0.0, 1.0);
		vec3 Nw = normalize(T * n.x * 0.4 + B * n.y * 0.4 + N * n.z);
		float fresnel = pow(1.0 - max(dot(Nw, V), 0.0), 5.0) * 0.9 + 0.1;
		vec3 reflection = skyOnly(reflect(-V, Nw)) * u_reflectTint * u_reflectAmount;
		vec3 color = mix(u_fogColor * (u_unlit == 1 ? vec3(1.0) : (u_ambient + u_lightColor * 0.5)), reflection, fresnel);
		// The sun mirrored by the waves: small and sharp; Water_Fidelity makes it sparkle.
		float glint = pow(max(dot(reflect(-L, Nw), V), 0.0), 1500.0) * (0.6 + 2.5 * u_glint);
		color += u_lightColor * glint * (u_unlit == 1 ? 0.0 : 1.0);
		fragColor = vec4(color, 1.0);
		return;
	}

	vec2 uv = v_uv;
	float height = 1.0;
	vec3 viewTS = vec3(dot(V, T), dot(V, B), dot(V, N));
	uv = parallax(uv, viewTS, height);

	vec4 base = u_hasBase == 1 ? texture(u_base, uv) : vec4(0.62, 0.62, 0.62, 1.0);
	if (u_hasDetail == 1) {
		vec4 d = texture(u_detail, uv * u_detailScale);
		if (u_detailBlend == 1) base.rgb += d.rgb;                      // additive
		else if (u_detailBlend == 2) base.rgb = mix(base.rgb, d.rgb, d.a); // detail over base
		else base.rgb *= d.rgb * 2.0;                                  // mod2x (default)
	}
	if (u_alphaMode == 2 && base.a < u_alphaRef) discard;

	vec4 nm = u_hasBump == 1 ? texture(u_bump, uv) : vec4(0.5, 0.5, 1.0, 1.0);
	if (u_hasBump == 0 && u_hasHeight == 1 && u_depth > 0.0) {
		// No normal map: the relief's normal from the height map's slopes.
		// As the game (FidelityParallaxHeightNormal): 1.5 texels apart, slopes at most 1.
		vec2 texel = 1.0 / vec2(textureSize(u_height, 0));
		vec2 slope = vec2(texture(u_height, uv + vec2(texel.x * 1.5, 0.0)).r - texture(u_height, uv - vec2(texel.x * 1.5, 0.0)).r,
		                  texture(u_height, uv + vec2(0.0, texel.y * 1.5)).r - texture(u_height, uv - vec2(0.0, texel.y * 1.5)).r);
		slope *= u_depth / max(3.0 * texel, vec2(1e-6));
		slope *= min(1.0, 1.0 / max(length(slope), 1e-6));
		vec3 n = normalize(vec3(-slope, 1.0));
		nm = vec4(n * 0.5 + 0.5, 1.0);
	}
	float diffuse;
	if (u_hasBump == 1 && u_ssbump == 1) {
		// Self-shadowed bump: brightness towards three basis directions.
		vec3 Lt = vec3(dot(L, T), dot(L, B), dot(L, N));
		vec3 b0 = vec3(0.81649658, 0.0, 0.57735027);
		vec3 b1 = vec3(-0.40824829, 0.70710678, 0.57735027);
		vec3 b2 = vec3(-0.40824829, -0.70710678, 0.57735027);
		diffuse = dot(nm.rgb, max(vec3(dot(Lt, b0), dot(Lt, b1), dot(Lt, b2)), 0.0));
	} else {
		vec3 n = nm.xyz * 2.0 - 1.0;
		N = normalize(T * n.x + B * n.y + N * n.z);
		diffuse = max(dot(N, L), 0.0);
	}
	vec3 lighting = u_unlit == 1 ? vec3(1.0) : u_ambient + u_lightColor * diffuse;
	if (u_hasHeight == 1 && u_depth > 0.0) lighting *= 1.0 - (1.0 - height) * u_ao;   // deep parts darker
	vec3 color = base.rgb * lighting;
	if (u_selfillum == 1) color = mix(color, base.rgb * u_selfTint, base.a);

	if (u_envmap == 1) {
		float mask = 1.0;
		if (u_maskMode == 1) mask = u_hasMask == 1 ? texture(u_mask, uv).r : 1.0;
		else if (u_maskMode == 2) mask = base.a;
		else if (u_maskMode == 3) mask = nm.a;
		color += sky(reflect(-V, N)) * u_envTint * mask;
	}
	if (u_phong == 1 && u_unlit == 0) {
		float spec = pow(max(dot(reflect(-L, N), V), 0.0), u_phongExp) * u_phongBoost * (u_hasBump == 1 ? nm.a : 1.0);
		color += u_lightColor * spec;
	}
	float alpha = u_alphaMode == 1 ? base.a : 1.0;

	if (u_mode == 2) {
		// Liquid in a glass: below the fill level the liquid's colour, a foam line at its surface,
		// above it the tinted, see-through glass (the shape's own texture shows on top).
		float level = mix(-u_shapeTop, u_shapeTop, u_fill);
		vec3 glass = mix(color, u_glassColor * (u_unlit == 1 ? vec3(1.0) : (u_ambient + u_lightColor * 0.8)), 0.6)
		             + sky(reflect(-V, normalize(v_normal))) * 0.25;
		if (v_pos.z < level) {
			vec3 liquid = u_liquidColor * (u_unlit == 1 ? vec3(1.0) : (u_ambient + u_lightColor * diffuse));
			float foam = smoothstep(level - 0.06, level, v_pos.z);
			color = mix(liquid, vec3(0.85), foam * 0.7);
			alpha = max(u_liquidAlpha, foam);
		} else {
			color = glass;
			alpha = u_glassAlpha;
		}
	}
	fragColor = vec4(color, alpha);
}
)";

void push(std::vector<float>& out, const QVector3D& p, const QVector3D& n, const QVector3D& t, const QVector3D& b, float u, float v) {
	for (const float f : {p.x(), p.y(), p.z(), n.x(), n.y(), n.z(), t.x(), t.y(), t.z(), b.x(), b.y(), b.z(), u, v}) {
		out.push_back(f);
	}
}

/// A square face: `n` its normal, `t` where u grows, `b` where v grows (down the texture).
void face(std::vector<float>& out, const QVector3D& center, const QVector3D& n, const QVector3D& t, const QVector3D& b) {
	const QVector3D c[4] = {center - t - b, center + t - b, center + t + b, center - t + b};
	const float uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
	for (const int i : {0, 1, 2, 0, 2, 3}) {
		push(out, c[i], n, t, b, uv[i][0], uv[i][1]);
	}
}

} // namespace

MaterialPreview::MaterialPreview(QWidget* parent) : QOpenGLWidget(parent), m_clock(new QTimer(this)) {
	setMinimumSize(200, 200);
	m_clock->setInterval(33);
	connect(m_clock, &QTimer::timeout, this, [this] {
		m_time += 0.033f;
		update();
	});
	setToolTip(tr("Left drag: turn - right drag: move the light - wheel: zoom"));
}

MaterialPreview::~MaterialPreview() {
	if (!m_ready) {
		return;
	}
	makeCurrent();
	glDeleteTextures(SlotCount, m_textures.data());
	glDeleteBuffers(1, &m_vbo);
	glDeleteVertexArrays(1, &m_vao);
	m_program.reset();
	doneCurrent();
}

void MaterialPreview::setTexture(Slot slot, const QImage& image) {
	m_images[slot] = image.isNull() ? QImage() : image.convertToFormat(QImage::Format_RGBA8888);
	m_dirty[slot] = true;
	update();
}

void MaterialPreview::setBaseTextureSize(QSize size) {
	if (size == m_baseSize) return;
	m_baseSize = size;
	update();
}

float MaterialPreview::reliefMaxShift(int pixels) {
	// The game holds the shift to r_fidelity_parallax_maxshift (4 world units); a texture covers its
	// size x 0.25 units (Hammer's default scale).
	return 4.0f / std::max(float(std::max(pixels, 1)) * 0.25f, 1.0f);
}

float MaterialPreview::maxShift() const {
	// The full size: the images are loaded at most 512 big, a 1024 texture's shift would be twice
	// the game's (Julius: relief broke apart in the preview).
	if (m_baseSize.isValid() && !m_baseSize.isEmpty()) return reliefMaxShift(std::max(m_baseSize.width(), m_baseSize.height()));
	const QImage& sized = !m_images[Base].isNull() ? m_images[Base] : m_images[Height];
	return reliefMaxShift(sized.isNull() ? 512 : std::max(sized.width(), sized.height()));
}

void MaterialPreview::setSettings(const Settings& settings) {
	if (settings.mode != m_settings.mode) {
		m_meshDirty = true;
		if (settings.mode == Mode::Water) {
			// Look down onto the water, the sun opposite: its glint on the waves is in the picture.
			m_pitch = std::max(m_pitch, 35.f);
			m_lightYaw = m_yaw + 180;
			m_lightPitch = m_pitch + 4;
		}
	}
	m_settings = settings;
	// Water moves; everything else is only drawn when something changes.
	if (settings.mode == Mode::Water && isVisible()) {
		m_clock->start();
	} else {
		m_clock->stop();
	}
	update();
}

void MaterialPreview::showEvent(QShowEvent* event) {
	QOpenGLWidget::showEvent(event);
	if (m_settings.mode == Mode::Water) m_clock->start();
}

void MaterialPreview::hideEvent(QHideEvent* event) {
	QOpenGLWidget::hideEvent(event);
	m_clock->stop();
}

void MaterialPreview::setShape(Shape shape) {
	m_shape = shape;
	m_meshDirty = true;
	update();
}

void MaterialPreview::setLighting(Lighting lighting) {
	m_lighting = lighting;
	update();
}

void MaterialPreview::initializeGL() {
	if (!initializeOpenGLFunctions()) {
		return;
	}
	m_program = std::make_unique<QOpenGLShaderProgram>();
	if (!m_program->addShaderFromSourceCode(QOpenGLShader::Vertex, VS) || !m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, FS) ||
	    !m_program->link()) {
		m_program.reset();
		return;
	}
	glGenVertexArrays(1, &m_vao);
	glGenBuffers(1, &m_vbo);
	glBindVertexArray(m_vao);
	glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
	const GLsizei stride = FLOATS * sizeof(float);
	const int sizes[5] = {3, 3, 3, 3, 2};
	int offset = 0;
	for (GLuint i = 0; i < 5; ++i) {
		glEnableVertexAttribArray(i);
		glVertexAttribPointer(i, sizes[i], GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(static_cast<std::uintptr_t>(offset) * sizeof(float)));
		offset += sizes[i];
	}
	glBindVertexArray(0);
	glGenTextures(SlotCount, m_textures.data());
	m_dirty.fill(true);
	m_meshDirty = true;
	m_ready = true;
}

void MaterialPreview::buildMesh() {
	std::vector<float> data;
	const QVector3D X(1, 0, 0), Y(0, 1, 0), Z(0, 0, 1);
	if (m_shape == Shape::Plane) {
		if (m_settings.mode == Mode::Water) {
			face(data, {0, 0, 0}, Z, Y, X);   // water lies flat
		} else {
			face(data, {0, 0, 0}, X, Y, -Z);   // a wall facing the camera, texture upright
		}
	} else if (m_shape == Shape::Cube) {
		const float h = 0.75f;
		face(data, X * h, X, Y * h, -Z * h);
		face(data, -X * h, -X, -Y * h, -Z * h);
		face(data, Y * h, Y, -X * h, -Z * h);
		face(data, -Y * h, -Y, X * h, -Z * h);
		face(data, Z * h, Z, Y * h, X * h);
		face(data, -Z * h, -Z, Y * h, -X * h);
	} else {
		constexpr int RINGS = 32, SEGMENTS = 64;
		const auto point = [](int ring, int segment, QVector3D& p, QVector3D& t, QVector3D& b) {
			const float theta = float(M_PI) * ring / RINGS;            // 0 at the top
			const float phi = 2.f * float(M_PI) * segment / SEGMENTS;
			p = QVector3D(std::sin(theta) * std::cos(phi), std::sin(theta) * std::sin(phi), std::cos(theta));
			t = QVector3D(-std::sin(phi), std::cos(phi), 0);
			b = QVector3D(std::cos(theta) * std::cos(phi), std::cos(theta) * std::sin(phi), -std::sin(theta));   // down
		};
		for (int r = 0; r < RINGS; ++r) {
			for (int s = 0; s < SEGMENTS; ++s) {
				QVector3D p[4], t[4], b[4];
				const int rs[4][2] = {{r, s}, {r, s + 1}, {r + 1, s + 1}, {r + 1, s}};
				for (int k = 0; k < 4; ++k) {
					point(rs[k][0], rs[k][1], p[k], t[k], b[k]);
				}
				for (const int k : {0, 2, 1, 0, 3, 2}) {
					push(data, p[k], p[k], t[k], b[k], 2.f * rs[k][1] / SEGMENTS, float(rs[k][0]) / RINGS);
				}
			}
		}
	}
	glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
	glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(data.size() * sizeof(float)), data.data(), GL_STATIC_DRAW);
	m_vertexCount = static_cast<GLsizei>(data.size() / FLOATS);
	m_meshDirty = false;
}

void MaterialPreview::upload(Slot slot) {
	m_dirty[slot] = false;
	if (m_images[slot].isNull()) {
		return;
	}
	glBindTexture(GL_TEXTURE_2D, m_textures[slot]);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_images[slot].width(), m_images[slot].height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, m_images[slot].constBits());
	glGenerateMipmap(GL_TEXTURE_2D);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
}

void MaterialPreview::paintGL() {
	glClearColor(0.16f, 0.17f, 0.2f, 1);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	if (!m_program) {
		return;
	}
	if (m_meshDirty) {
		buildMesh();
	}
	for (int slot = 0; slot < SlotCount; ++slot) {
		if (m_dirty[static_cast<std::size_t>(slot)]) upload(static_cast<Slot>(slot));
	}
	const float yaw = qDegreesToRadians(m_yaw), pitch = qDegreesToRadians(m_pitch);
	const QVector3D camera = QVector3D(std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch)) * m_distance;
	QMatrix4x4 projection, view;
	projection.perspective(40.f, float(width()) / float(std::max(1, height())), 0.05f, 50.f);
	view.lookAt(camera, {0, 0, 0}, {0, 0, 1});
	const float ly = qDegreesToRadians(m_lightYaw), lp = qDegreesToRadians(m_lightPitch);
	const QVector3D light(std::cos(lp) * std::cos(ly), std::cos(lp) * std::sin(ly), std::sin(lp));
	QVector3D lightColor, ambient;
	switch (m_lighting) {
		case Lighting::Sun: lightColor = {1.0f, 0.95f, 0.85f}; ambient = {0.32f, 0.36f, 0.45f}; break;
		case Lighting::Indoor: lightColor = {1.0f, 0.78f, 0.55f}; ambient = {0.12f, 0.11f, 0.1f}; break;
		case Lighting::Night: lightColor = {0.35f, 0.45f, 0.75f}; ambient = {0.04f, 0.05f, 0.08f}; break;
		case Lighting::Unlit: lightColor = {1, 1, 1}; ambient = {1, 1, 1}; break;
	}

	glEnable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	const auto& s = m_settings;
	if (s.alphaMode == 1 || s.mode == Mode::Liquid) {
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	} else if (s.alphaMode == 3) {
		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE, GL_ONE);
	} else {
		glDisable(GL_BLEND);
	}
	m_program->bind();
	m_program->setUniformValue("u_viewProj", projection * view);
	m_program->setUniformValue("u_model", QMatrix4x4());
	m_program->setUniformValue("u_camPos", camera);
	m_program->setUniformValue("u_lightDir", light);
	m_program->setUniformValue("u_lightColor", lightColor);
	m_program->setUniformValue("u_ambient", ambient);
	m_program->setUniformValue("u_unlit", m_lighting == Lighting::Unlit ? 1 : 0);
	const char* samplers[SlotCount] = {"u_base", "u_bump", "u_mask", "u_detail", "u_height"};
	const char* flags[SlotCount] = {"u_hasBase", "u_hasBump", "u_hasMask", "u_hasDetail", "u_hasHeight"};
	for (int slot = 0; slot < SlotCount; ++slot) {
		glActiveTexture(GL_TEXTURE0 + slot);
		glBindTexture(GL_TEXTURE_2D, m_textures[static_cast<std::size_t>(slot)]);
		m_program->setUniformValue(samplers[slot], slot);
		m_program->setUniformValue(flags[slot], m_images[static_cast<std::size_t>(slot)].isNull() ? 0 : 1);
	}
	glActiveTexture(GL_TEXTURE0);
	m_program->setUniformValue("u_ssbump", s.ssbump ? 1 : 0);
	m_program->setUniformValue("u_envmap", s.envmap ? 1 : 0);
	m_program->setUniformValue("u_maskMode", s.maskMode);
	m_program->setUniformValue("u_alphaMode", s.alphaMode);
	m_program->setUniformValue("u_selfillum", s.selfillum ? 1 : 0);
	m_program->setUniformValue("u_phong", s.phong ? 1 : 0);
	m_program->setUniformValue("u_detailBlend", s.detailBlend);
	m_program->setUniformValue("u_envTint", s.envTint);
	m_program->setUniformValue("u_selfTint", s.selfillumTint);
	m_program->setUniformValue("u_detailScale", s.detailScale);
	m_program->setUniformValue("u_phongBoost", s.phongBoost);
	m_program->setUniformValue("u_phongExp", std::max(1.f, s.phongExponent));
	m_program->setUniformValue("u_alphaRef", s.alphaRef);
	m_program->setUniformValue("u_mode", static_cast<int>(s.mode));
	m_program->setUniformValue("u_depth", s.parallaxDepth);
	m_program->setUniformValue("u_maxShift", maxShift());
	m_program->setUniformValue("u_plane", 0.5f);   // r_fidelity_parallax_plane (walls)
	m_program->setUniformValue("u_ao", s.parallaxAO);
	m_program->setUniformValue("u_glint", s.waterGlint);
	m_program->setUniformValue("u_reflectTint", s.reflectTint);
	m_program->setUniformValue("u_reflectAmount", s.reflectAmount);
	m_program->setUniformValue("u_fogColor", s.fogColor);
	m_program->setUniformValue("u_liquidColor", s.liquidColor);
	m_program->setUniformValue("u_liquidAlpha", s.liquidAlpha);
	m_program->setUniformValue("u_fill", s.fill);
	m_program->setUniformValue("u_glassColor", s.glassColor);
	m_program->setUniformValue("u_glassAlpha", s.glassAlpha);
	m_program->setUniformValue("u_time", m_time);
	m_program->setUniformValue("u_shapeTop", m_shape == Shape::Plane ? 1.0f : (m_shape == Shape::Cube ? 0.75f : 1.0f));
	glBindVertexArray(m_vao);
	glDrawArrays(GL_TRIANGLES, 0, m_vertexCount);
	glBindVertexArray(0);
	glDisable(GL_BLEND);
	m_program->release();
}

void MaterialPreview::mousePressEvent(QMouseEvent* event) {
	m_last = event->position();
}

void MaterialPreview::mouseMoveEvent(QMouseEvent* event) {
	const QPointF delta = event->position() - m_last;
	m_last = event->position();
	if (event->buttons() & Qt::RightButton) {
		m_lightYaw -= float(delta.x()) * 0.5f;
		m_lightPitch = std::clamp(m_lightPitch + float(delta.y()) * 0.5f, -85.f, 85.f);
	} else if (event->buttons() & Qt::LeftButton) {
		m_yaw -= float(delta.x()) * 0.5f;
		m_pitch = std::clamp(m_pitch + float(delta.y()) * 0.5f, -85.f, 85.f);
	}
	update();
}

void MaterialPreview::wheelEvent(QWheelEvent* event) {
	m_distance = std::clamp(m_distance * std::pow(0.9f, float(event->angleDelta().y()) / 120.f), 1.2f, 10.f);
	update();
}

} // namespace sme::app
