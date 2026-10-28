// renderer_shadows.cpp - Real-time shadows for the lighting preview (see renderer.h).
//
// The scene does not move while editing, so the depth maps are only drawn again when the
// geometry, the lights or the set of nearest shadow-casting lights change - looking around
// costs nothing extra. Entity boxes do not cast shadows (a light's own box would hide it).

#include "renderer.h"

#include <algorithm>
#include <cmath>

namespace sme::app {

namespace {

constexpr int SUN_SIZE = 4096;
constexpr int ATLAS_SIZE = 4096;
constexpr int TILE_SIZE = 1024;            // 4 x 4 tiles
constexpr int MAX_SPOT_SHADOWS = 4;        // one tile each
constexpr int MAX_POINT_SHADOWS = 2;       // six tiles each (cube sides)

const char* DEPTH_VS = R"(#version 330 core
layout(location = 0) in vec3 a_pos;
uniform mat4 u_viewProj;
uniform mat4 u_model;
void main() { gl_Position = u_viewProj * u_model * vec4(a_pos, 1.0); }
)";

const char* DEPTH_FS = R"(#version 330 core
void main() {}
)";

/// Maps clip space of a light into one tile of the atlas (or the whole sun map).
QMatrix4x4 toTexture(int tile) {
	QMatrix4x4 m;
	if (tile < 0) {
		m.translate(0.5f, 0.5f, 0.5f);
		m.scale(0.5f, 0.5f, 0.5f);
		return m;
	}
	const float tx = float(tile % 4), ty = float(tile / 4);
	m.translate(tx * 0.25f + 0.125f, ty * 0.25f + 0.125f, 0.5f);
	m.scale(0.125f, 0.125f, 0.5f);
	return m;
}

/// How far a light reaches (where it has faded to about 2 % of its brightness).
float lightRange(const QVector4D& position, const QVector4D& color, const QVector4D& spot2) {
	const double numerator = position.w(), c = color.w(), l = spot2.z(), q = spot2.w();
	const double target = numerator / 0.02;
	double range = 8192;
	if (q > 1e-9) {
		range = (-l + std::sqrt(std::max(0.0, l * l - 4 * q * (c - target)))) / (2 * q);
	} else if (l > 1e-9) {
		range = (target - c) / l;
	}
	return float(std::clamp(range, 256.0, 8192.0));
}

GLuint makeDepthTexture(QOpenGLFunctions_3_3_Core& gl, int size) {
	GLuint id = 0;
	gl.glGenTextures(1, &id);
	gl.glBindTexture(GL_TEXTURE_2D, id);
	gl.glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, size, size, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
	gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
	gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
	return id;
}

} // namespace

bool SceneRenderer::initShadows(QString* error) {
	m_depthProgram = std::make_unique<QOpenGLShaderProgram>();
	if (!m_depthProgram->addShaderFromSourceCode(QOpenGLShader::Vertex, DEPTH_VS) ||
	    !m_depthProgram->addShaderFromSourceCode(QOpenGLShader::Fragment, DEPTH_FS) || !m_depthProgram->link()) {
		if (error) *error = m_depthProgram->log();
		return false;
	}
	GLint previousFbo = 0;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFbo);
	for (auto [fbo, depth, size] : {std::tuple{&m_sunFbo, &m_sunDepth, SUN_SIZE}, std::tuple{&m_atlasFbo, &m_atlasDepth, ATLAS_SIZE}}) {
		*depth = makeDepthTexture(*this, size);
		glGenFramebuffers(1, fbo);
		glBindFramebuffer(GL_FRAMEBUFFER, *fbo);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, *depth, 0);
		glDrawBuffer(GL_NONE);
		glReadBuffer(GL_NONE);
		m_hasShadowMaps = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
	}
	glBindFramebuffer(GL_FRAMEBUFFER, GLuint(previousFbo));   // the widget's own framebuffer, not 0
	// Shadow maps stay on units 3 and 4; the scene shader's shadow samplers point there for good
	// (two sampler types on one unit would be an error even when unused).
	glActiveTexture(GL_TEXTURE3);
	glBindTexture(GL_TEXTURE_2D, m_sunDepth);
	glActiveTexture(GL_TEXTURE4);
	glBindTexture(GL_TEXTURE_2D, m_atlasDepth);
	glActiveTexture(GL_TEXTURE0);
	m_program->bind();
	m_program->setUniformValue("u_sunShadow", 3);
	m_program->setUniformValue("u_atlas", 4);
	m_program->setUniformValue("u_sunTexel", 1.f / SUN_SIZE);
	m_program->setUniformValue("u_atlasTexel", 1.f / ATLAS_SIZE);
	m_program->release();
	m_shadowsDirty = true;
	return true;
}

void SceneRenderer::releaseShadows() {
	for (GLuint* fbo : {&m_sunFbo, &m_atlasFbo}) {
		if (*fbo) glDeleteFramebuffers(1, fbo);
		*fbo = 0;
	}
	for (GLuint* tex : {&m_sunDepth, &m_atlasDepth}) {
		if (*tex) glDeleteTextures(1, tex);
		*tex = 0;
	}
	m_depthProgram.reset();
	m_hasShadowMaps = false;
}

void SceneRenderer::drawDepth(const QMatrix4x4& viewProjection) {
	m_depthProgram->setUniformValue("u_viewProj", viewProjection);
	m_depthProgram->setUniformValue("u_model", QMatrix4x4());
	glBindVertexArray(m_vao);
	for (const auto& batch : m_batches) {
		const auto& tex = texture(batch.material);
		// Glass, water and tool textures (sky, triggers, nodraw) let the light through.
		if (batch.decal || tex.isTool || tex.isSky || tex.kind == MaterialInfo::Kind::Translucent || tex.kind == MaterialInfo::Kind::Additive) {
			continue;
		}
		glDrawArrays(GL_TRIANGLES, batch.first, batch.count);
	}
	for (const auto& instance : m_modelInstances) {
		if (auto* model = gpuModel(instance.path)) {
			m_depthProgram->setUniformValue("u_model", instance.transform);
			glBindVertexArray(model->vao);
			for (const auto& mesh : model->meshes) {
				glDrawElements(GL_TRIANGLES, mesh.count, GL_UNSIGNED_INT, reinterpret_cast<void*>(static_cast<std::uintptr_t>(mesh.first) * sizeof(std::uint32_t)));
			}
		}
	}
	glBindVertexArray(0);
}

void SceneRenderer::updateShadows(const std::vector<const Light*>& lights, QVector4D* shadowInfo) {
	for (std::size_t i = 0; i < lights.size(); ++i) {
		shadowInfo[i] = QVector4D(-1, 0, 0, 0);
	}
	if (!m_hasShadowMaps) {
		m_shadowCasterCount = 0;
		return;
	}
	// Casters: the nearest spot and point lights (lights is sorted nearest first when it was cut).
	std::vector<const Light*> casters;
	int spots = 0, points = 0;
	for (const auto* light : lights) {
		const bool point = light->spot.w() < -1.5f;
		if (point ? points < MAX_POINT_SHADOWS : spots < MAX_SPOT_SHADOWS) {
			casters.push_back(light);
			(point ? points : spots)++;
		}
	}
	if (casters != m_shadowLights || m_shadowSunDirection != m_sunDirection) {
		m_shadowsDirty = true;
	}
	int tile = 0;
	std::vector<std::pair<const Light*, int>> tiles;   // light -> first tile
	for (const auto* light : casters) {
		tiles.emplace_back(light, tile);
		tile += light->spot.w() < -1.5f ? 6 : 1;
	}
	for (std::size_t i = 0; i < lights.size(); ++i) {
		for (const auto& [light, first] : tiles) {
			if (light == lights[i]) {
				shadowInfo[i] = QVector4D(float(first), light->spot.w() < -1.5f ? 1.f : 0.f, 0, 0);
			}
		}
	}
	m_shadowCasterCount = static_cast<int>(casters.size()) + (m_hasSun ? 1 : 0);
	if (!m_shadowsDirty) {
		return;
	}
	m_shadowsDirty = false;
	m_shadowLights = casters;
	m_shadowSunDirection = m_sunDirection;

	GLint previousFbo = 0, viewport[4];
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFbo);
	glGetIntegerv(GL_VIEWPORT, viewport);
	m_depthProgram->bind();
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	glDisable(GL_CULL_FACE);
	glDisable(GL_BLEND);
	glEnable(GL_POLYGON_OFFSET_FILL);
	glPolygonOffset(2.f, 4.f);

	// Sun: orthographic over the whole map.
	glBindFramebuffer(GL_FRAMEBUFFER, m_sunFbo);
	glViewport(0, 0, SUN_SIZE, SUN_SIZE);
	glClear(GL_DEPTH_BUFFER_BIT);
	if (m_hasSun && !m_sceneBounds.empty()) {
		const QVector3D center = toQt(m_sceneBounds.center());
		const float radius = float(std::max(64.0, length(m_sceneBounds.size()) * 0.5));
		const QVector3D up = std::abs(m_sunDirection.z()) > 0.95f ? QVector3D(1, 0, 0) : QVector3D(0, 0, 1);
		QMatrix4x4 view;
		view.lookAt(center - m_sunDirection * radius * 2, center, up);
		QMatrix4x4 projection;
		projection.ortho(-radius, radius, -radius, radius, radius * 0.5f, radius * 3.5f);
		drawDepth(projection * view);
		m_sunMatrix = toTexture(-1) * projection * view;
	}

	// Atlas: one tile per spot light, six per point light.
	glBindFramebuffer(GL_FRAMEBUFFER, m_atlasFbo);
	glViewport(0, 0, ATLAS_SIZE, ATLAS_SIZE);
	glClear(GL_DEPTH_BUFFER_BIT);
	glEnable(GL_SCISSOR_TEST);
	const auto renderTile = [&](int index, const QMatrix4x4& viewProjection) {
		const int x = (index % 4) * TILE_SIZE, y = (index / 4) * TILE_SIZE;
		glViewport(x, y, TILE_SIZE, TILE_SIZE);
		glScissor(x, y, TILE_SIZE, TILE_SIZE);
		drawDepth(viewProjection);
		m_tileMatrices[index] = toTexture(index) * viewProjection;
	};
	for (const auto& [light, first] : tiles) {
		const QVector3D position = light->position.toVector3D();
		const float range = lightRange(light->position, light->color, light->spot2);
		if (light->spot.w() < -1.5f) {
			// Cube sides +X -X +Y -Y +Z -Z (the shader picks the side by the largest axis).
			const QVector3D dirs[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
			const QVector3D ups[6] = {{0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {1, 0, 0}, {1, 0, 0}};
			QMatrix4x4 projection;
			projection.perspective(90.f, 1.f, 2.f, range);
			for (int face = 0; face < 6; ++face) {
				QMatrix4x4 view;
				view.lookAt(position, position + dirs[face], ups[face]);
				renderTile(first + face, projection * view);
			}
		} else {
			const QVector3D dir = light->spot.toVector3D();
			const float outer = std::acos(std::clamp(light->spot.w(), -1.f, 1.f)) * 180.f / float(M_PI);
			QMatrix4x4 projection;
			projection.perspective(std::min(170.f, outer * 2 + 10), 1.f, 2.f, range);
			QMatrix4x4 view;
			view.lookAt(position, position + dir, std::abs(dir.z()) > 0.95f ? QVector3D(1, 0, 0) : QVector3D(0, 0, 1));
			renderTile(first, projection * view);
		}
	}
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_POLYGON_OFFSET_FILL);
	glBindFramebuffer(GL_FRAMEBUFFER, GLuint(previousFbo));
	glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
	glDepthFunc(GL_LEQUAL);
}

} // namespace sme::app
