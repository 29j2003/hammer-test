// renderer_cubemaps.cpp - Cubemap reflections for the lighting preview (see renderer.h).
//
// Every env_cubemap is drawn from its origin into six small pictures, kept in one atlas texture
// (8 cubemaps per row, 6 faces each, the face layout of sme/cubemaps.h). The scene does not move
// while editing, so the pictures are only drawn again a moment after the map changed (or with
// Map > Build cubemaps), a few faces per frame so the view stays smooth. Each face reflects the
// cubemap VBSP would give it; the scene shader in renderer.cpp reads the atlas, box-projected
// when the cubemap has a room box (Mapbase parallax_obb). Without any env_cubemap the sky is
// reflected, as the game does with its default cubemap.

#include "renderer.h"

#include <algorithm>
#include <cmath>

namespace sme::app {

namespace {

constexpr int PROBE_SIZE = 128;          // pixels per cube face
constexpr int PROBES_PER_ROW = 8;        // the shader uses the same number
constexpr int MAX_PROBES = 256;          // atlas 6144 x 4096 at most
constexpr int PROBE_FACE_WIDTH = 4096;   // probe-per-face texture: same layout as the face flags
constexpr int DEBOUNCE_MS = 300;         // after the last change of the map

// Unit sphere in the scene's vertex layout (position, normal, uvw, face) for the preview spheres.
std::vector<float> sphereVertices(float radius) {
	constexpr int RINGS = 12, SEGMENTS = 20;
	std::vector<float> out;
	const auto point = [&](int ring, int segment) {
		const double theta = M_PI * ring / RINGS, phi = 2 * M_PI * segment / SEGMENTS;
		return QVector3D(float(std::sin(theta) * std::cos(phi)), float(std::sin(theta) * std::sin(phi)), float(std::cos(theta)));
	};
	const auto push = [&](const QVector3D& n) {
		out.insert(out.end(), {n.x() * radius, n.y() * radius, n.z() * radius, n.x(), n.y(), n.z(), 0.f, 0.f, 0.f, 0.f});
	};
	for (int r = 0; r < RINGS; ++r) {
		for (int s = 0; s < SEGMENTS; ++s) {
			const QVector3D a = point(r, s), b = point(r + 1, s), c = point(r + 1, s + 1), d = point(r, s + 1);
			push(a), push(b), push(c);
			push(a), push(c), push(d);
		}
	}
	return out;
}

} // namespace

bool SceneRenderer::initCubemaps() {
	glGenTextures(1, &m_probeAtlas);
	glBindTexture(GL_TEXTURE_2D, m_probeAtlas);
	const quint32 grey = 0xff806a5a;
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &grey);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	m_probeAtlasRows = 0;

	glGenTextures(1, &m_probeData);
	glGenTextures(1, &m_probeFaceTexture);
	m_probes.clear();
	m_probeOfFace.assign(1, 0);
	m_probeEverBuilt.clear();
	m_probeFacesDone.clear();
	uploadProbeData();
	uploadProbeFaces();

	// The face being drawn: its own colour texture and depth, copied into the atlas afterwards.
	glGenTextures(1, &m_probeColor);
	glBindTexture(GL_TEXTURE_2D, m_probeColor);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, PROBE_SIZE, PROBE_SIZE, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glGenRenderbuffers(1, &m_probeDepth);
	glBindRenderbuffer(GL_RENDERBUFFER, m_probeDepth);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, PROBE_SIZE, PROBE_SIZE);
	GLint previousFbo = 0;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFbo);
	glGenFramebuffers(1, &m_probeFbo);
	glBindFramebuffer(GL_FRAMEBUFFER, m_probeFbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_probeColor, 0);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_probeDepth);
	const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
	glBindFramebuffer(GL_FRAMEBUFFER, GLuint(previousFbo));
	glBindTexture(GL_TEXTURE_2D, 0);
	if (!complete) {
		glDeleteFramebuffers(1, &m_probeFbo);
		m_probeFbo = 0;   // no cubemaps then: reflections show the sky
	}

	const auto sphere = sphereVertices(10.f);
	m_sphereVertices = static_cast<GLsizei>(sphere.size() / 10);
	glGenVertexArrays(1, &m_sphereVao);
	glGenBuffers(1, &m_sphereVbo);
	glBindVertexArray(m_sphereVao);
	glBindBuffer(GL_ARRAY_BUFFER, m_sphereVbo);
	glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(sphere.size() * sizeof(float)), sphere.data(), GL_STATIC_DRAW);
	const GLsizei stride = 10 * sizeof(float);
	for (GLuint i = 0; i < 4; ++i) glEnableVertexAttribArray(i);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(0));
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(3 * sizeof(float)));
	glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(6 * sizeof(float)));
	glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(9 * sizeof(float)));
	glBindVertexArray(0);
	return complete;
}

void SceneRenderer::releaseCubemaps() {
	for (GLuint* tex : {&m_probeAtlas, &m_probeData, &m_probeFaceTexture, &m_probeColor}) {
		if (*tex) glDeleteTextures(1, tex);
		*tex = 0;
	}
	if (m_probeDepth) glDeleteRenderbuffers(1, &m_probeDepth);
	if (m_probeFbo) glDeleteFramebuffers(1, &m_probeFbo);
	if (m_sphereVbo) glDeleteBuffers(1, &m_sphereVbo);
	if (m_sphereVao) glDeleteVertexArrays(1, &m_sphereVao);
	m_probeDepth = m_probeFbo = m_sphereVbo = m_sphereVao = 0;
	m_probes.clear();
}

void SceneRenderer::uploadProbeData() {
	// Per cubemap three texels: origin (w: 0 not drawn yet, 1 drawn, 2 drawn with a room box),
	// room box min and max.
	const int count = std::max<int>(1, static_cast<int>(m_probes.size()));
	std::vector<float> data(static_cast<std::size_t>(count) * 12, 0.f);
	for (std::size_t i = 0; i < m_probes.size(); ++i) {
		const auto& probe = m_probes[i];
		float* row = data.data() + i * 12;
		row[0] = float(probe.origin.x);
		row[1] = float(probe.origin.y);
		row[2] = float(probe.origin.z);
		row[3] = i < m_probeEverBuilt.size() && m_probeEverBuilt[i] ? (probe.box ? 2.f : 1.f) : 0.f;
		if (probe.box) {
			for (int k = 0; k < 3; ++k) {
				row[4 + k] = float(probe.box->min[k]);
				row[8 + k] = float(probe.box->max[k]);
			}
		}
	}
	glBindTexture(GL_TEXTURE_2D, m_probeData);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 3, count, 0, GL_RGBA, GL_FLOAT, data.data());
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glBindTexture(GL_TEXTURE_2D, 0);
}

void SceneRenderer::uploadProbeFaces() {
	const int rows = std::max<int>(1, (static_cast<int>(m_probeOfFace.size()) + PROBE_FACE_WIDTH - 1) / PROBE_FACE_WIDTH);
	std::vector<std::uint16_t> texels(static_cast<std::size_t>(rows) * PROBE_FACE_WIDTH, 0);
	std::copy(m_probeOfFace.begin(), m_probeOfFace.end(), texels.begin());
	glBindTexture(GL_TEXTURE_2D, m_probeFaceTexture);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_R16UI, PROBE_FACE_WIDTH, rows, 0, GL_RED_INTEGER, GL_UNSIGNED_SHORT, texels.data());
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glBindTexture(GL_TEXTURE_2D, 0);
}

void SceneRenderer::updateProbes(const MapDocument& doc, const std::vector<std::pair<int, Vec3>>& faceSides) {
	auto probes = collectCubemaps(doc);
	if (probes.size() > MAX_PROBES) probes.resize(MAX_PROBES);
	// Same cubemaps in the same places: the old pictures stay until the new ones are drawn.
	bool same = probes.size() == m_probes.size();
	for (std::size_t i = 0; same && i < probes.size(); ++i) same = probes[i].origin == m_probes[i].origin;
	m_probes = std::move(probes);
	const int count = static_cast<int>(m_probes.size());

	m_probeOfSide.clear();
	m_probeOfFace.assign(std::max<std::size_t>(1, faceSides.size()), 0);
	for (std::size_t face = 0; face < faceSides.size(); ++face) {
		const auto& [sideId, center] = faceSides[face];
		if (sideId < 0) continue;
		const int probe = probeForFace(m_probes, sideId, center);
		m_probeOfFace[face] = static_cast<std::uint16_t>(probe + 1);
		if (sideId > 0 && probe >= 0) m_probeOfSide[sideId] = probe;
	}
	uploadProbeFaces();

	if (!same) {
		m_probeEverBuilt.assign(static_cast<std::size_t>(count), 0);
		const int rows = (count + PROBES_PER_ROW - 1) / PROBES_PER_ROW;
		GLint maxSize = 0;
		glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxSize);
		glBindTexture(GL_TEXTURE_2D, m_probeAtlas);
		if (rows > 0 && maxSize >= PROBES_PER_ROW * 6 * PROBE_SIZE && maxSize >= rows * PROBE_SIZE) {
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, PROBES_PER_ROW * 6 * PROBE_SIZE, rows * PROBE_SIZE, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
			m_probeAtlasRows = rows;
		} else {
			const quint32 grey = 0xff806a5a;
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &grey);
			m_probeAtlasRows = 0;
		}
		glBindTexture(GL_TEXTURE_2D, 0);
	}
	m_probeFacesDone.assign(static_cast<std::size_t>(count), 0);
	m_probesDirty = count > 0 && m_probeAtlasRows > 0 && m_probeFbo != 0;
	m_probesChanged.restart();
	uploadProbeData();
}

void SceneRenderer::buildCubemaps() {
	if (m_probes.empty() || m_probeAtlasRows == 0 || !m_probeFbo) return;
	m_probeFacesDone.assign(m_probes.size(), 0);
	m_probesDirty = true;
	m_probesNow = true;
}

int SceneRenderer::builtCubemapCount() const {
	return static_cast<int>(std::count(m_probeEverBuilt.begin(), m_probeEverBuilt.end(), 1));
}

int SceneRenderer::cubemapDelayMs() const {
	if (!m_probesDirty || m_probesNow || !m_probesChanged.isValid()) return 0;
	return static_cast<int>(std::max<qint64>(0, DEBOUNCE_MS - m_probesChanged.elapsed())) + 1;
}

int SceneRenderer::cubemapOfSide(int sideId) const {
	const auto it = m_probeOfSide.find(sideId);
	return it == m_probeOfSide.end() ? -1 : it->second;
}

void SceneRenderer::buildProbeFaces(int faces) {
	if (!m_probesDirty) return;
	if (!m_probesNow && m_probesChanged.elapsed() < DEBOUNCE_MS) return;   // the map is still changing
	GLint previousFbo = 0, viewport[4];
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFbo);
	glGetIntegerv(GL_VIEWPORT, viewport);
	GLfloat clear[4];
	glGetFloatv(GL_COLOR_CLEAR_VALUE, clear);
	glBindFramebuffer(GL_FRAMEBUFFER, m_probeFbo);
	glViewport(0, 0, PROBE_SIZE, PROBE_SIZE);
	m_probesDrawing = true;
	bool finished = false;
	int drawn = 0;
	for (std::size_t i = 0; i < m_probes.size() && drawn < faces; ++i) {
		for (int face = 0; face < 6 && drawn < faces; ++face) {
			if (m_probeFacesDone[i] & (1u << face)) continue;
			const Vec3 o = m_probes[i].origin, f = cubeFaceForward(face), u = cubeFaceUp(face);
			Params params;
			params.cameraPosition = QVector3D(float(o.x), float(o.y), float(o.z));
			params.view.lookAt(params.cameraPosition, params.cameraPosition + QVector3D(float(f.x), float(f.y), float(f.z)),
			                   QVector3D(float(u.x), float(u.y), float(u.z)));
			params.projection.perspective(90.f, 1.f, 1.f, 32768.f);
			params.mode = Mode::Lit;
			params.shadows = false;
			params.reflections = false;
			params.showToolTextures = false;   // as in the game; the sky still shows (drawBatches)
			glClearColor(0.16f, 0.17f, 0.19f, 1.f);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			glEnable(GL_DEPTH_TEST);
			glDepthFunc(GL_LEQUAL);
			drawSurfaces(params, true);
			const int tile = static_cast<int>(i % PROBES_PER_ROW) * 6 + face, row = static_cast<int>(i / PROBES_PER_ROW);
			glBindTexture(GL_TEXTURE_2D, m_probeAtlas);
			glCopyTexSubImage2D(GL_TEXTURE_2D, 0, tile * PROBE_SIZE, row * PROBE_SIZE, 0, 0, PROBE_SIZE, PROBE_SIZE);
			glBindTexture(GL_TEXTURE_2D, 0);
			m_probeFacesDone[i] |= std::uint8_t(1u << face);
			++drawn;
			if (m_probeFacesDone[i] == 0x3f && !m_probeEverBuilt[i]) {
				m_probeEverBuilt[i] = 1;
				finished = true;
			}
		}
	}
	m_probesDrawing = false;
	glBindFramebuffer(GL_FRAMEBUFFER, GLuint(previousFbo));
	glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
	glClearColor(clear[0], clear[1], clear[2], clear[3]);
	if (finished) uploadProbeData();
	if (std::all_of(m_probeFacesDone.begin(), m_probeFacesDone.end(), [](std::uint8_t done) { return done == 0x3f; })) {
		m_probesDirty = false;
		m_probesNow = false;
	}
}

void SceneRenderer::bindProbes(QOpenGLShaderProgram& program) {
	glActiveTexture(GL_TEXTURE13);
	glBindTexture(GL_TEXTURE_2D, m_probeAtlas);
	glActiveTexture(GL_TEXTURE14);
	glBindTexture(GL_TEXTURE_2D, m_probeData);
	glActiveTexture(GL_TEXTURE15);
	glBindTexture(GL_TEXTURE_2D, m_probeFaceTexture);
	glActiveTexture(GL_TEXTURE0);
	program.setUniformValue("u_probeAtlas", 13);
	program.setUniformValue("u_probeData", 14);
	program.setUniformValue("u_probeFaces", 15);
	program.setUniformValue("u_probeTile", float(PROBE_SIZE));
}

void SceneRenderer::drawProbeSpheres(const Params& params) {
	if (m_selectedProbes.empty() || params.mode != Mode::Lit || !params.reflections || !m_sphereVao) return;
	m_program->bind();
	glBindVertexArray(m_sphereVao);
	glEnable(GL_DEPTH_TEST);
	m_program->setUniformValue("u_faceOverride", -2);
	for (const int probe : m_selectedProbes) {
		if (probe < 0 || probe >= static_cast<int>(m_probes.size())) continue;
		const Vec3 o = m_probes[static_cast<std::size_t>(probe)].origin;
		QMatrix4x4 model;
		model.translate(float(o.x), float(o.y), float(o.z));
		m_program->setUniformValue("u_model", model);
		m_program->setUniformValue("u_probeSphere", probe + 1);
		glDrawArrays(GL_TRIANGLES, 0, m_sphereVertices);
		++m_drawCalls;
	}
	m_program->setUniformValue("u_probeSphere", 0);
	m_program->setUniformValue("u_model", QMatrix4x4());
	m_program->setUniformValue("u_faceOverride", -1);
	glBindVertexArray(0);
}

} // namespace sme::app
