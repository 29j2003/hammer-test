// renderer.cpp - OpenGL scene rendering (see renderer.h).

#include "renderer.h"

#include <QColor>

#include <algorithm>
#include <cmath>

#include <sme/brush.h>
#include <sme/text.h>

namespace sme::app {

namespace {

constexpr int FLAG_WIDTH = 4096;
constexpr int MAX_LIGHTS = 64;
constexpr std::uint8_t FLAG_SELECTED = 1;
constexpr std::uint8_t FLAG_HIDDEN = 2;
constexpr std::uint8_t FLAG_HOVER = 4;

// Vertex: position(3) normal(3) uvw(3: u, v in texels + blend alpha, or rgb for colored geometry) face(1)
constexpr int FLOATS_PER_VERTEX = 10;
// Edge vertex: position(3) color(3) face(1)
constexpr int FLOATS_PER_EDGE = 7;

const char* SCENE_VS = R"(#version 330 core
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec3 a_uvw;
layout(location = 3) in float a_face;
uniform mat4 u_viewProj;
uniform mat4 u_model;
uniform vec2 u_texSize;
uniform usampler2D u_flags;
uniform int u_hideSelected;
uniform int u_onlySelected;
out vec3 v_world;
out vec3 v_normal;
out vec2 v_uv;
out vec3 v_uvw;
flat out uint v_flag;
void main() {
	int id = int(a_face + 0.5);
	uint f = texelFetch(u_flags, ivec2(id % 4096, id / 4096), 0).r;
	v_flag = f;
	vec4 world = u_model * vec4(a_pos, 1.0);
	v_world = world.xyz;
	v_normal = mat3(u_model) * a_normal;
	v_uv = a_uvw.xy / u_texSize;
	v_uvw = a_uvw;
	gl_Position = u_viewProj * world;
	bool selected = (f & 1u) != 0u;
	if ((u_hideSelected == 1 && selected) || (u_onlySelected == 1 && !selected) || (f & 2u) != 0u) {
		gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
	}
}
)";

const char* SCENE_FS = R"(#version 330 core
in vec3 v_world;
in vec3 v_normal;
in vec2 v_uv;
in vec3 v_uvw;
flat in uint v_flag;
uniform sampler2D u_tex;
uniform sampler2D u_tex2;
uniform int u_hasTex2;
uniform int u_useColor;
uniform int u_mode;            // 0 lit, 1 unlit
uniform int u_fullbright;      // tool textures, sky
uniform int u_alphaTest;
uniform float u_opacity;
uniform float u_exposure;
uniform vec3 u_sunDir;
uniform vec3 u_sunColor;
uniform vec3 u_ambient;
uniform int u_lightCount;
uniform vec4 u_lightPos[64];
uniform vec4 u_lightColor[64];
uniform vec4 u_lightSpot[64];
uniform vec4 u_lightSpot2[64];
uniform int u_previewTint;
out vec4 fragColor;

vec3 toLinear(vec3 c) { return pow(c, vec3(2.2)); }
vec3 toGamma(vec3 c) { return pow(c, vec3(1.0 / 2.2)); }

void main() {
	vec4 base;
	if (u_useColor == 1) {
		base = vec4(v_uvw, 1.0);
	} else {
		base = texture(u_tex, v_uv);
		if (u_hasTex2 == 1) {
			vec4 second = texture(u_tex2, v_uv);
			base = mix(base, second, clamp(v_uvw.z / 255.0, 0.0, 1.0));
		}
	}
	if (u_alphaTest == 1 && base.a < 0.5) discard;
	vec3 n = normalize(v_normal);
	vec3 color;
	if (u_mode == 1 || u_fullbright == 1) {
		float shade = 0.78 + 0.22 * dot(n, normalize(vec3(0.35, 0.55, 0.75)));
		color = base.rgb * shade;
	} else {
		vec3 light = u_ambient + u_sunColor * max(dot(n, -u_sunDir), 0.0);
		for (int i = 0; i < u_lightCount; ++i) {
			vec3 toLight = u_lightPos[i].xyz - v_world;
			float d = max(length(toLight), 1.0);
			vec3 l = toLight / d;
			float ndl = max(dot(n, l), 0.0);
			if (ndl <= 0.0) continue;
			// VRAD: premultiplied / (c + l*d + q*d^2); premultiplier in w (c + 100l + 10000q, or 1 for _fifty_percent_distance).
			float c = u_lightColor[i].w, lin = u_lightSpot2[i].z, q = u_lightSpot2[i].w;
			float atten = u_lightPos[i].w / max(c + lin * d + q * d * d, 0.0001);
			atten = min(atten, 4.0);
			if (u_lightSpot[i].w > -1.5) {
				// VRAD light_spot: full inside the inner cone, fringe ^ exponent between the cones, scaled by cos.
				float cosAngle = dot(-l, u_lightSpot[i].xyz);
				float outer = u_lightSpot[i].w, inner = u_lightSpot2[i].x;
				if (cosAngle < outer) continue;
				float spot = cosAngle;
				if (cosAngle < inner) {
					float fringe = clamp((cosAngle - outer) / max(inner - outer, 0.0001), 0.0, 1.0);
					spot *= u_lightSpot2[i].y > 0.0 ? pow(fringe, u_lightSpot2[i].y) : fringe;
				}
				atten *= spot;
			}
			light += u_lightColor[i].rgb * ndl * atten;
		}
		// Soft filmic curve: keeps hot spots near lights from clipping to white.
		vec3 hdr = toLinear(base.rgb) * light * u_exposure;
		color = toGamma(vec3(1.0) - exp(-hdr * 1.25));
	}
	if ((v_flag & 1u) != 0u) {
		color = mix(color, vec3(1.0, 0.45, 0.1), 0.35);
	}
	if ((v_flag & 4u) != 0u) {
		color = mix(color, vec3(0.25, 0.7, 1.0), 0.45);   // face under a dragged material / paint brush
	}
	if (u_previewTint == 1) {
		color = mix(color, vec3(1.0, 0.7, 0.2), 0.25);
	}
	fragColor = vec4(color, base.a * u_opacity);
}
)";

const char* EDGE_VS = R"(#version 330 core
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_color;
layout(location = 2) in float a_face;
uniform mat4 u_viewProj;
uniform mat4 u_model;
uniform usampler2D u_flags;
uniform int u_onlySelected;
uniform int u_hideSelected;
out vec4 v_color;
void main() {
	int id = int(a_face + 0.5);
	uint f = texelFetch(u_flags, ivec2(id % 4096, id / 4096), 0).r;
	bool selected = (f & 1u) != 0u;
	v_color = selected ? vec4(1.0, 0.25, 0.15, 1.0) : vec4(a_color, 1.0);
	gl_Position = u_viewProj * u_model * vec4(a_pos, 1.0);
	if ((u_onlySelected == 1 && !selected) || (u_hideSelected == 1 && selected) || (f & 2u) != 0u) {
		gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
	}
}
)";

const char* LINE_VS = R"(#version 330 core
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec4 a_color;
uniform mat4 u_viewProj;
out vec4 v_color;
void main() {
	v_color = a_color;
	gl_Position = u_viewProj * vec4(a_pos, 1.0);
}
)";

const char* COLOR_FS = R"(#version 330 core
in vec4 v_color;
out vec4 fragColor;
void main() { fragColor = v_color; }
)";

long long solidKey(const SolidRef& ref) {
	return (static_cast<long long>(ref.entityId) << 32) | static_cast<unsigned>(ref.solidId);
}

QVector3D parseColor255(const std::string& text, float* brightness = nullptr) {
	std::vector<double> n;
	for (const auto& part : splitTrimmed(text, ' ')) {
		n.push_back(std::atof(part.c_str()));
	}
	if (brightness) {
		*brightness = n.size() >= 4 ? float(n[3]) : 255.f;  // VRAD: no brightness multiply with three values
	}
	if (n.size() < 3) {
		return {1, 1, 1};
	}
	return {float(n[0] / 255.0), float(n[1] / 255.0), float(n[2] / 255.0)};
}

QVector3D linear(const QVector3D& c) {
	return {std::pow(c.x(), 2.2f), std::pow(c.y(), 2.2f), std::pow(c.z(), 2.2f)};
}

double keyNumber(const Entity& e, const char* key, double fallback) {
	const auto value = e.get(key);
	return value.empty() ? fallback : std::atof(value.c_str());
}

/// Light direction as VRAD reads it (public/map_utils.cpp): yaw from "angle" or angles[1],
/// pitch from the "pitch" key if non-zero, else angles[0]; pitch -90 points straight down.
QVector3D vradDirection(const Entity& e) {
	Vec3 angles;
	(void)parseVec3(e.get("angles", "0 0 0"), angles);
	double yaw = angles.y, pitch = angles.x;
	if (const auto angle = e.get("angle"); !angle.empty()) {
		yaw = std::atof(angle.c_str());
	}
	if (const double p = keyNumber(e, "pitch", 0); p != 0) {
		pitch = p;
	}
	const double pr = pitch * M_PI / 180.0, yr = yaw * M_PI / 180.0;
	return QVector3D(float(std::cos(yr) * std::cos(pr)), float(std::sin(yr) * std::cos(pr)), float(std::sin(pr)));
}

void pushVertex(std::vector<float>& out, const Vec3& p, const Vec3& n, float a, float b, float c, int face) {
	out.insert(out.end(), {float(p.x), float(p.y), float(p.z), float(n.x), float(n.y), float(n.z), a, b, c, float(face)});
}

void pushEdge(std::vector<float>& out, const Vec3& a, const Vec3& b, const QVector3D& color, int face) {
	out.insert(out.end(), {float(a.x), float(a.y), float(a.z), color.x(), color.y(), color.z(), float(face)});
	out.insert(out.end(), {float(b.x), float(b.y), float(b.z), color.x(), color.y(), color.z(), float(face)});
}

bool intersectTriangle(const Ray& ray, const Vec3& a, const Vec3& b, const Vec3& c, double& t) {
	const Vec3 e1 = b - a, e2 = c - a;
	const Vec3 p = cross(ray.direction, e2);
	const double det = dot(e1, p);
	if (std::abs(det) < 1e-9) {
		return false;
	}
	const double inv = 1.0 / det;
	const Vec3 s = ray.origin - a;
	const double u = dot(s, p) * inv;
	if (u < 0 || u > 1) {
		return false;
	}
	const Vec3 q = cross(s, e1);
	const double v = dot(ray.direction, q) * inv;
	if (v < 0 || u + v > 1) {
		return false;
	}
	t = dot(e2, q) * inv;
	return t >= 0;
}

} // namespace

SceneRenderer::SceneRenderer() = default;

SceneRenderer::~SceneRenderer() = default;

bool SceneRenderer::initialize(MaterialCache* materials, QString* error) {
	m_materials = materials;
	if (!initializeOpenGLFunctions()) {
		if (error) {
			*error = QStringLiteral("OpenGL 3.3 core is not available");
		}
		return false;
	}
	const auto build = [&](std::unique_ptr<QOpenGLShaderProgram>& program, const char* vs, const char* fs) {
		program = std::make_unique<QOpenGLShaderProgram>();
		if (!program->addShaderFromSourceCode(QOpenGLShader::Vertex, vs) || !program->addShaderFromSourceCode(QOpenGLShader::Fragment, fs) ||
		    !program->link()) {
			if (error) {
				*error = program->log();
			}
			return false;
		}
		return true;
	};
	if (!build(m_program, SCENE_VS, SCENE_FS) || !build(m_edgeProgram, EDGE_VS, COLOR_FS) || !build(m_lineProgram, LINE_VS, COLOR_FS)) {
		return false;
	}

	glGenVertexArrays(1, &m_vao);
	glGenBuffers(1, &m_vbo);
	glBindVertexArray(m_vao);
	glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
	const GLsizei stride = FLOATS_PER_VERTEX * sizeof(float);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(0));
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(3 * sizeof(float)));
	glEnableVertexAttribArray(2);
	glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(6 * sizeof(float)));
	glEnableVertexAttribArray(3);
	glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(9 * sizeof(float)));

	glGenVertexArrays(1, &m_edgeVao);
	glGenBuffers(1, &m_edgeVbo);
	glBindVertexArray(m_edgeVao);
	glBindBuffer(GL_ARRAY_BUFFER, m_edgeVbo);
	const GLsizei edgeStride = FLOATS_PER_EDGE * sizeof(float);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, edgeStride, reinterpret_cast<void*>(0));
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, edgeStride, reinterpret_cast<void*>(3 * sizeof(float)));
	glEnableVertexAttribArray(2);
	glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, edgeStride, reinterpret_cast<void*>(6 * sizeof(float)));

	glGenVertexArrays(1, &m_lineVao);
	glGenBuffers(1, &m_lineVbo);
	glBindVertexArray(m_lineVao);
	glBindBuffer(GL_ARRAY_BUFFER, m_lineVbo);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(LineVertex), reinterpret_cast<void*>(0));
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(LineVertex), reinterpret_cast<void*>(sizeof(QVector3D)));
	glBindVertexArray(0);

	glGenTextures(1, &m_whiteTexture);
	glBindTexture(GL_TEXTURE_2D, m_whiteTexture);
	const quint32 white = 0xffffffff;
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &white);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	glGenTextures(1, &m_flagTexture);
	m_flags.assign(FLAG_WIDTH, 0);
	ensureFlagTexture();
	m_initialized = true;
	return true;
}

void SceneRenderer::release() {
	if (!m_initialized) {
		return;
	}
	for (auto& tex : m_textures) {
		if (tex.id) glDeleteTextures(1, &tex.id);
		if (tex.id2) glDeleteTextures(1, &tex.id2);
	}
	m_textures.clear();
	glDeleteTextures(1, &m_flagTexture);
	glDeleteTextures(1, &m_whiteTexture);
	glDeleteBuffers(1, &m_vbo);
	glDeleteBuffers(1, &m_edgeVbo);
	glDeleteBuffers(1, &m_lineVbo);
	glDeleteVertexArrays(1, &m_vao);
	glDeleteVertexArrays(1, &m_edgeVao);
	glDeleteVertexArrays(1, &m_lineVao);
	m_program.reset();
	m_edgeProgram.reset();
	m_lineProgram.reset();
	m_initialized = false;
}

void SceneRenderer::ensureFlagTexture() {
	const int rows = std::max<int>(1, (static_cast<int>(m_flags.size()) + FLAG_WIDTH - 1) / FLAG_WIDTH);
	m_flags.resize(static_cast<std::size_t>(rows) * FLAG_WIDTH, 0);
	glBindTexture(GL_TEXTURE_2D, m_flagTexture);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_R8UI, FLAG_WIDTH, rows, 0, GL_RED_INTEGER, GL_UNSIGNED_BYTE, m_flags.data());
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}

void SceneRenderer::rebuild(const MapDocument& doc, const EntityCatalog* catalog) {
	m_doc = &doc;
	std::unordered_map<QString, std::vector<float>> byMaterial;
	std::unordered_map<QString, std::vector<int>> facesByMaterial;
	std::vector<float> entityVertices;
	std::vector<float> edges;
	m_sideFaces.clear();
	m_solidFaces.clear();
	m_entityFaces.clear();
	m_pickItems.clear();
	m_faceToSide.clear();
	m_sceneBounds = {};
	m_triangleCount = 0;
	int faceIndex = 0;

	std::vector<int> faceVertexOffset;   // per face: vertex offset within its material list
	std::vector<QString> faceMaterial;

	const auto addSolid = [&](const Solid& solid, int entityId, const QVector3D& edgeColor) {
		const SolidRef ref{entityId, solid.id};
		const auto geometry = buildBrushGeometry(solid);
		PickItem pick;
		pick.solid = ref;
		pick.entityId = entityId;
		pick.bounds = geometry.bounds;
		m_sceneBounds.add(geometry.bounds);
		for (const auto& face : geometry.faces) {
			const Side& side = solid.sides[static_cast<std::size_t>(face.sideIndex)];
			const QString material = normalizedMaterialName(QString::fromStdString(side.material));
			auto& vertices = byMaterial[material];
			const int id = faceIndex++;
			m_sideFaces[side.id].push_back(id);
			m_solidFaces[solidKey(ref)].push_back(id);
			if (entityId != 0) {
				m_entityFaces[entityId].push_back(id);
			}
			m_faceToSide.emplace_back(ref, side.id);
			faceVertexOffset.push_back(static_cast<int>(vertices.size() / FLOATS_PER_VERTEX));
			faceMaterial.push_back(material);
			facesByMaterial[material].push_back(id);

			if (auto disp = buildDisplacement(side, face)) {
				// Displacement grid; UVs come from the flat base surface like in the engine.
				const int n = disp->size;
				std::vector<Vec3> flat(disp->positions.size());
				std::vector<Vec3> corners(face.vertices.rbegin(), face.vertices.rend());
				// buildDisplacement already rotated to the start corner; recompute flat points from its grid corners.
				const Vec3 c0 = disp->positions[0] - (disp->positions[0] - corners[0]) * 0; // positions include offsets; use base grid below
				(void)c0;
				std::vector<Vec3> normals(disp->positions.size());
				for (int i = 0; i + 1 < n; ++i) {
					for (int j = 0; j + 1 < n; ++j) {
						const int a = i * n + j, b = i * n + j + 1, c = (i + 1) * n + j, d = (i + 1) * n + j + 1;
						for (const auto& tri : {std::array<int, 3>{a, c, b}, std::array<int, 3>{b, c, d}}) {
							const Vec3 tn = cross(disp->positions[tri[1]] - disp->positions[tri[0]], disp->positions[tri[2]] - disp->positions[tri[0]]);
							for (int k : tri) {
								normals[k] += tn;
							}
						}
					}
				}
				const bool flip = [&] {
					Vec3 sum;
					for (const auto& nn : normals) sum += nn;
					return dot(sum, face.normal) < 0;
				}();
				for (int i = 0; i + 1 < n; ++i) {
					for (int j = 0; j + 1 < n; ++j) {
						const int a = i * n + j, b = i * n + j + 1, c = (i + 1) * n + j, d = (i + 1) * n + j + 1;
						for (auto tri : {std::array<int, 3>{a, c, b}, std::array<int, 3>{b, c, d}}) {
							if (flip) {
								std::swap(tri[1], tri[2]);
							}
							for (int k : tri) {
								const Vec3& p = disp->positions[k];
								// Texture coordinates from the undisplaced point (project back onto the face plane).
								const Vec3 base = p - face.normal * dot(p - face.vertices[0], face.normal);
								const auto tc = textureCoordinate(side, base);
								pushVertex(vertices, p, normalize(flip ? -normals[k] : normals[k]), float(tc.u), float(tc.v), disp->alpha[k], id);
								pick.triangles.push_back(p);
							}
							pick.triangleSides.push_back(face.sideIndex);
							++m_triangleCount;
						}
					}
				}
				for (const auto& p : disp->positions) {
					pick.bounds.add(p);
					m_sceneBounds.add(p);
				}
				// Outline of the displacement.
				for (int k = 0; k + 1 < n; ++k) {
					pushEdge(edges, disp->positions[k], disp->positions[k + 1], edgeColor, id);
					pushEdge(edges, disp->positions[(n - 1) * n + k], disp->positions[(n - 1) * n + k + 1], edgeColor, id);
					pushEdge(edges, disp->positions[k * n], disp->positions[(k + 1) * n], edgeColor, id);
					pushEdge(edges, disp->positions[k * n + n - 1], disp->positions[(k + 1) * n + n - 1], edgeColor, id);
				}
				continue;
			}
			for (std::size_t i = 1; i + 1 < face.vertices.size(); ++i) {
				for (const auto* v : {&face.vertices[0], &face.vertices[i], &face.vertices[i + 1]}) {
					const auto tc = textureCoordinate(side, *v);
					pushVertex(vertices, *v, face.normal, float(tc.u), float(tc.v), 0.f, id);
				}
				++m_triangleCount;
			}
			for (std::size_t i = 0; i < face.vertices.size(); ++i) {
				pushEdge(edges, face.vertices[i], face.vertices[(i + 1) % face.vertices.size()], edgeColor, id);
			}
		}
		m_pickItems.push_back(std::move(pick));
	};

	for (const auto& solid : doc.world.solids) {
		addSolid(solid, 0, {0.85f, 0.85f, 0.85f});
	}

	// Entities: brush entities draw their brushes, point entities a colored box.
	m_lights.clear();
	m_hasSun = false;
	m_sunColor = {0, 0, 0};
	m_ambient = {0.25f, 0.25f, 0.28f};
	struct BoxEntity {
		int id;
		Bounds box;
		QVector3D color;
	};
	std::vector<BoxEntity> boxes;
	for (const auto& entity : doc.entities) {
		const auto classname = entity.classname();
		QVector3D color(0.86f, 0.12f, 0.86f);
		if (catalog) {
			if (const auto* definition = catalog->find(classname)) {
				const auto helper = definition->helper("color");
				if (!helper.empty()) {
					color = parseColor255(helper);
				}
			}
		}
		if (!entity.solids.empty()) {
			for (const auto& solid : entity.solids) {
				addSolid(solid, entity.id, color);
			}
			continue;
		}
		if (startsWithI(classname, "light") && !entity.get("_light").empty()) {
			color = parseColor255(entity.get("_light"));   // lights show their own color
		}
		const auto box = edit::entityBounds(entity, catalog);
		boxes.push_back({entity.id, box, color});
		m_sceneBounds.add(box);

		// Lights for the lighting preview.
		if (classname == "light" || classname == "light_spot") {
			float brightness = 200;
			const auto rgb = parseColor255(entity.get("_light", "255 255 255 200"), &brightness);
			Light light;
			const auto origin = entity.origin();
			// Falloff as VRAD computes it (lightmap.cpp): missing keys are 0, all zero means constant.
			double c = keyNumber(entity, "_constant_attn", 0), l = keyNumber(entity, "_linear_attn", 0), q = keyNumber(entity, "_quadratic_attn", 0);
			if (c <= 0 && l <= 0 && q <= 0) {
				c = 1;
			}
			double premultiply = c + 100 * l + 10000 * q;
			const double d50 = keyNumber(entity, "_fifty_percent_distance", 0);
			if (d50 > 0) {
				// Inverse quadratic through 1 at 0, 1/2 at d50 and 1/256 at d0.
				double d0 = keyNumber(entity, "_zero_percent_distance", 0);
				if (d0 < d50) {
					d0 = 2 * d50;
				}
				const double det = d50 * d0 * d0 - d0 * d50 * d50;
				c = 1;
				l = std::abs(det) > 1e-9 ? (1 * d0 * d0 - 255 * d50 * d50) / det : 0;
				q = std::abs(det) > 1e-9 ? (d50 * 255 - d0 * 1) / det : 1 / (d50 * d50);
				premultiply = 1;
			}
			const QVector3D intensity = linear(rgb) * (brightness / 255.f);
			light.position = QVector4D(float(origin.x), float(origin.y), float(origin.z), float(premultiply));
			light.color = QVector4D(intensity, float(c));
			light.spot = QVector4D(0, 0, 0, -2);
			light.spot2 = QVector4D(1, 0, float(l), float(q));
			if (classname == "light_spot") {
				const auto dir = vradDirection(entity);
				const double inner = std::min(90.0, keyNumber(entity, "_inner_cone", 10));
				const double outer = std::min(90.0, std::max(inner, keyNumber(entity, "_cone", inner)));
				light.spot = QVector4D(dir, float(std::cos(outer * M_PI / 180)));
				light.spot2.setX(float(std::cos(inner * M_PI / 180)));
				light.spot2.setY(float(keyNumber(entity, "_exponent", 1)));
			}
			m_lights.push_back(light);
		} else if (classname == "light_environment") {
			float brightness = 200, ambientBrightness = 100;
			const auto rgb = parseColor255(entity.get("_light", "255 255 255 200"), &brightness);
			const auto ambientText = entity.get("_ambient");
			QVector3D ambient = linear(rgb) * (brightness / 255.f) * 0.5f;
			if (!ambientText.empty()) {
				const auto a = parseColor255(ambientText, &ambientBrightness);
				ambient = linear(a) * (ambientBrightness / 255.f);
			}
			m_sunDirection = vradDirection(entity).normalized();
			m_sunColor = linear(rgb) * (brightness / 255.f);
			m_ambient = ambient;
			m_hasSun = true;
		}
	}
	if (!m_hasSun) {
		m_ambient = m_lights.empty() ? QVector3D(0.9f, 0.9f, 0.95f) : QVector3D(0.04f, 0.04f, 0.05f);
		m_sunColor = m_lights.empty() ? QVector3D(0.25f, 0.24f, 0.22f) : QVector3D(0, 0, 0);
		m_sunDirection = QVector3D(-0.4f, -0.3f, -0.85f).normalized();
	}

	for (const auto& b : boxes) {
		const int base = faceIndex;
		const Vec3 lo = b.box.min, hi = b.box.max;
		const Vec3 corners[8] = {{lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, hi.y, lo.z}, {lo.x, hi.y, lo.z},
		                         {lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z}};
		const int quads[6][4] = {{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4}, {2, 3, 7, 6}, {1, 2, 6, 5}, {3, 0, 4, 7}};
		const Vec3 normals[6] = {{0, 0, -1}, {0, 0, 1}, {0, -1, 0}, {0, 1, 0}, {1, 0, 0}, {-1, 0, 0}};
		const int id = faceIndex++;
		m_entityFaces[b.id].push_back(id);
		m_faceToSide.emplace_back(SolidRef{b.id, 0}, 0);
		for (int f = 0; f < 6; ++f) {
			const auto& q = quads[f];
			for (const int k : {q[0], q[1], q[2], q[0], q[2], q[3]}) {
				pushVertex(entityVertices, corners[k], normals[f], b.color.x(), b.color.y(), b.color.z(), id);
			}
			for (int e = 0; e < 4; ++e) {
				pushEdge(edges, corners[q[e]], corners[q[(e + 1) % 4]], b.color, id);
			}
			m_triangleCount += 2;
		}
		(void)base;
		PickItem pick;
		pick.entityId = b.id;
		pick.bounds = b.box;
		m_pickItems.push_back(std::move(pick));
	}

	// Upload: material batches first, entity boxes last.
	std::vector<float> all;
	m_batches.clear();
	std::vector<QString> materials;
	materials.reserve(byMaterial.size());
	for (const auto& [name, vertices] : byMaterial) {
		materials.push_back(name);
	}
	std::sort(materials.begin(), materials.end());
	m_rangesByFace.assign(static_cast<std::size_t>(faceIndex), {0, 0});
	for (const auto& name : materials) {
		const auto& vertices = byMaterial[name];
		Batch batch;
		batch.material = name;
		batch.first = static_cast<GLint>(all.size() / FLOATS_PER_VERTEX);
		batch.count = static_cast<GLsizei>(vertices.size() / FLOATS_PER_VERTEX);
		// Per-face ranges inside this batch (faces were appended in order).
		const auto& faces = facesByMaterial[name];
		for (std::size_t k = 0; k < faces.size(); ++k) {
			const int face = faces[k];
			const int start = faceVertexOffset[static_cast<std::size_t>(face)];
			const int end = k + 1 < faces.size() ? faceVertexOffset[static_cast<std::size_t>(faces[k + 1])] : batch.count;
			m_rangesByFace[static_cast<std::size_t>(face)] = {batch.first + start, end - start};
		}
		all.insert(all.end(), vertices.begin(), vertices.end());
		m_batches.push_back(batch);
		texture(name); // request loading early
	}
	m_entityFirst = static_cast<GLint>(all.size() / FLOATS_PER_VERTEX);
	m_entityCount = static_cast<GLsizei>(entityVertices.size() / FLOATS_PER_VERTEX);
	for (std::size_t i = 0; i < boxes.size(); ++i) {
		const int face = faceIndex - static_cast<int>(boxes.size()) + static_cast<int>(i);
		m_rangesByFace[static_cast<std::size_t>(face)] = {m_entityFirst + static_cast<int>(i) * 36, 36};
	}
	all.insert(all.end(), entityVertices.begin(), entityVertices.end());

	glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
	glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(all.size() * sizeof(float)), all.data(), GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, m_edgeVbo);
	glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(edges.size() * sizeof(float)), edges.data(), GL_STATIC_DRAW);
	m_edgeCount = static_cast<GLsizei>(edges.size() / FLOATS_PER_EDGE);
	m_faceCount = faceIndex;
	m_flags.assign(static_cast<std::size_t>(std::max(faceIndex, 1)), 0);
	ensureFlagTexture();
}

void SceneRenderer::setSelection(const MapDocument& doc, const Selection& selection, const Selection& hidden, int hoverSideId) {
	std::fill(m_flags.begin(), m_flags.end(), 0);
	m_selectedRanges.clear();
	m_hidden = hidden;
	const auto mark = [&](const std::vector<int>& faces, std::uint8_t flag) {
		for (const int face : faces) {
			if (face >= 0 && static_cast<std::size_t>(face) < m_flags.size()) {
				m_flags[static_cast<std::size_t>(face)] |= flag;
				if (flag == FLAG_SELECTED && static_cast<std::size_t>(face) < m_rangesByFace.size()) {
					m_selectedRanges.push_back(m_rangesByFace[static_cast<std::size_t>(face)]);
				}
			}
		}
	};
	const auto markAll = [&](const Selection& sel, std::uint8_t flag) {
		for (const auto& ref : sel.solids) {
			if (const auto it = m_solidFaces.find(solidKey(ref)); it != m_solidFaces.end()) {
				mark(it->second, flag);
			}
		}
		for (const int id : sel.entities) {
			if (const auto it = m_entityFaces.find(id); it != m_entityFaces.end()) {
				mark(it->second, flag);
			}
		}
		for (const auto& face : sel.faces) {
			if (const auto it = m_sideFaces.find(face.sideId); it != m_sideFaces.end()) {
				mark(it->second, flag);
			}
		}
	};
	markAll(selection, FLAG_SELECTED);
	markAll(hidden, FLAG_HIDDEN);
	if (hoverSideId != 0) {
		if (const auto it = m_sideFaces.find(hoverSideId); it != m_sideFaces.end()) {
			mark(it->second, FLAG_HOVER);
		}
	}
	(void)doc;
	glBindTexture(GL_TEXTURE_2D, m_flagTexture);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	const int rows = static_cast<int>(m_flags.size()) / FLAG_WIDTH;
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, FLAG_WIDTH, rows, GL_RED_INTEGER, GL_UNSIGNED_BYTE, m_flags.data());
}

void SceneRenderer::materialReady(const QString& name) {
	if (m_textures.contains(name)) {
		m_dirtyTextures.insert(name);
	}
}

SceneRenderer::GpuTexture& SceneRenderer::texture(const QString& material) {
	auto it = m_textures.find(material);
	if (it == m_textures.end()) {
		it = m_textures.insert(material, GpuTexture{});
		m_dirtyTextures.insert(material);
	}
	if (m_dirtyTextures.contains(material) && m_materials) {
		if (const auto info = m_materials->get(material)) {
			uploadTexture(*it, *info);
			m_dirtyTextures.remove(material);
		}
	}
	return *it;
}

void SceneRenderer::uploadTexture(GpuTexture& tex, const MaterialInfo& info) {
	const auto upload = [this](GLuint& id, const QImage& image) {
		if (!id) {
			glGenTextures(1, &id);
		}
		glBindTexture(GL_TEXTURE_2D, id);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image.width(), image.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, image.constBits());
		glGenerateMipmap(GL_TEXTURE_2D);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		// Anisotropic filtering (GL_EXT_texture_filter_anisotropic, core in 4.6) keeps floors sharp.
		constexpr GLenum MAX_ANISOTROPY = 0x84FF, ANISOTROPY = 0x84FE;
		GLfloat maxAniso = 0;
		glGetFloatv(MAX_ANISOTROPY, &maxAniso);
		if (glGetError() == GL_NO_ERROR && maxAniso > 1) {
			glTexParameterf(GL_TEXTURE_2D, ANISOTROPY, std::min(8.f, maxAniso));
		}
		while (glGetError() != GL_NO_ERROR) {
		}
	};
	const QImage& image = info.image.isNull() ? MaterialCache::checkerboard() : info.image;
	const QImage rgba = image.format() == QImage::Format_RGBA8888 ? image : image.convertToFormat(QImage::Format_RGBA8888);
	upload(tex.id, rgba);
	tex.size = info.image.isNull() ? QSize(64, 64) : info.image.size();
	if (!info.image2.isNull()) {
		upload(tex.id2, info.image2.convertToFormat(QImage::Format_RGBA8888));
	}
	tex.kind = info.kind;
	tex.isTool = info.isTool;
	tex.isSky = info.isSky;
	tex.ready = true;
}

void SceneRenderer::setCommonUniforms(QOpenGLShaderProgram& program, const Params& params, const QMatrix4x4& model) {
	program.setUniformValue("u_viewProj", params.projection * params.view);
	program.setUniformValue("u_model", model);
	program.setUniformValue("u_flags", 2);
	glActiveTexture(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_2D, m_flagTexture);
	glActiveTexture(GL_TEXTURE0);
}

void SceneRenderer::drawBatches(QOpenGLShaderProgram& program, const Params& params, bool translucentPass) {
	for (const auto& batch : m_batches) {
		auto& tex = texture(batch.material);
		const bool translucent = tex.kind == MaterialInfo::Kind::Translucent || tex.kind == MaterialInfo::Kind::Additive;
		if (translucent != translucentPass || (!params.showToolTextures && tex.isTool)) {
			continue;
		}
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, tex.id ? tex.id : m_whiteTexture);
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, tex.id2 ? tex.id2 : m_whiteTexture);
		glActiveTexture(GL_TEXTURE0);
		program.setUniformValue("u_texSize", QVector2D(float(tex.size.width()), float(tex.size.height())));
		program.setUniformValue("u_hasTex2", tex.id2 ? 1 : 0);
		program.setUniformValue("u_alphaTest", tex.kind == MaterialInfo::Kind::AlphaTest ? 1 : 0);
		program.setUniformValue("u_fullbright", (tex.isTool || tex.isSky) ? 1 : 0);
		program.setUniformValue("u_opacity", translucent && tex.isTool ? 0.55f : 1.f);
		if (tex.kind == MaterialInfo::Kind::Additive) {
			glBlendFunc(GL_SRC_ALPHA, GL_ONE);
		} else {
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		}
		glDrawArrays(GL_TRIANGLES, batch.first, batch.count);
		++m_drawCalls;
	}
}

void SceneRenderer::render(const Params& params) {
	if (!m_initialized) {
		return;
	}
	m_drawCalls = 0;
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);

	if (params.mode == Mode::Wireframe || params.mode == Mode::Flat2D) {
		m_edgeProgram->bind();
		setCommonUniforms(*m_edgeProgram, params, QMatrix4x4());
		m_edgeProgram->setUniformValue("u_onlySelected", 0);
		m_edgeProgram->setUniformValue("u_hideSelected", m_hideSelected ? 1 : 0);
		if (params.mode == Mode::Flat2D) {
			glDisable(GL_DEPTH_TEST);
		}
		glBindVertexArray(m_edgeVao);
		glDrawArrays(GL_LINES, 0, m_edgeCount);
		// Selected edges again on top so they are never hidden behind others.
		m_edgeProgram->setUniformValue("u_onlySelected", 1);
		glDisable(GL_DEPTH_TEST);
		glDrawArrays(GL_LINES, 0, m_edgeCount);
		glEnable(GL_DEPTH_TEST);
		m_drawCalls += 2;
		glBindVertexArray(0);
		return;
	}

	m_program->bind();
	setCommonUniforms(*m_program, params, QMatrix4x4());
	m_program->setUniformValue("u_tex", 0);
	m_program->setUniformValue("u_tex2", 1);
	m_program->setUniformValue("u_mode", params.mode == Mode::Lit ? 0 : 1);
	m_program->setUniformValue("u_hideSelected", m_hideSelected ? 1 : 0);
	m_program->setUniformValue("u_onlySelected", 0);
	m_program->setUniformValue("u_previewTint", 0);
	m_program->setUniformValue("u_exposure", 1.0f);
	m_program->setUniformValue("u_sunDir", m_sunDirection);
	m_program->setUniformValue("u_sunColor", m_sunColor);
	m_program->setUniformValue("u_ambient", m_ambient);

	// The 64 lights closest to the camera.
	std::vector<const Light*> lights;
	for (const auto& light : m_lights) {
		lights.push_back(&light);
	}
	if (lights.size() > MAX_LIGHTS) {
		std::partial_sort(lights.begin(), lights.begin() + MAX_LIGHTS, lights.end(), [&](const Light* a, const Light* b) {
			return (a->position.toVector3D() - params.cameraPosition).lengthSquared() < (b->position.toVector3D() - params.cameraPosition).lengthSquared();
		});
		lights.resize(MAX_LIGHTS);
	}
	QVector4D pos[MAX_LIGHTS], color[MAX_LIGHTS], spot[MAX_LIGHTS], spot2[MAX_LIGHTS];
	for (std::size_t i = 0; i < lights.size(); ++i) {
		pos[i] = lights[i]->position;
		color[i] = lights[i]->color;
		spot[i] = lights[i]->spot;
		spot2[i] = lights[i]->spot2;
	}
	m_program->setUniformValue("u_lightCount", static_cast<int>(lights.size()));
	m_program->setUniformValueArray("u_lightPos", pos, MAX_LIGHTS);
	m_program->setUniformValueArray("u_lightColor", color, MAX_LIGHTS);
	m_program->setUniformValueArray("u_lightSpot", spot, MAX_LIGHTS);
	m_program->setUniformValueArray("u_lightSpot2", spot2, MAX_LIGHTS);

	glBindVertexArray(m_vao);
	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);
	glFrontFace(GL_CCW);
	glDisable(GL_BLEND);
	m_program->setUniformValue("u_useColor", 0);
	drawBatches(*m_program, params, false);

	// Entity boxes (flat colored).
	if (m_entityCount > 0) {
		m_program->setUniformValue("u_useColor", 1);
		m_program->setUniformValue("u_hasTex2", 0);
		m_program->setUniformValue("u_alphaTest", 0);
		m_program->setUniformValue("u_fullbright", 1);
		m_program->setUniformValue("u_opacity", 1.f);
		glDrawArrays(GL_TRIANGLES, m_entityFirst, m_entityCount);
		++m_drawCalls;
		m_program->setUniformValue("u_useColor", 0);
	}

	// Translucent materials (water, glass, tool textures like triggers).
	glEnable(GL_BLEND);
	glDepthMask(GL_FALSE);
	drawBatches(*m_program, params, true);
	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
	glDisable(GL_CULL_FACE);

	// Selection outline on top of the surfaces.
	m_edgeProgram->bind();
	setCommonUniforms(*m_edgeProgram, params, QMatrix4x4());
	m_edgeProgram->setUniformValue("u_onlySelected", 1);
	m_edgeProgram->setUniformValue("u_hideSelected", m_hideSelected ? 1 : 0);
	glBindVertexArray(m_edgeVao);
	glDisable(GL_DEPTH_TEST);
	glDrawArrays(GL_LINES, 0, m_edgeCount);
	glEnable(GL_DEPTH_TEST);
	++m_drawCalls;
	glBindVertexArray(0);
}

void SceneRenderer::drawPreview(const Params& params, const QMatrix4x4& model) {
	if (!m_initialized || m_selectedRanges.empty()) {
		return;
	}
	glEnable(GL_DEPTH_TEST);
	if (params.mode == Mode::Wireframe || params.mode == Mode::Flat2D) {
		m_edgeProgram->bind();
		setCommonUniforms(*m_edgeProgram, params, model);
		m_edgeProgram->setUniformValue("u_onlySelected", 1);
		m_edgeProgram->setUniformValue("u_hideSelected", 0);
		glBindVertexArray(m_edgeVao);
		glDisable(GL_DEPTH_TEST);
		glDrawArrays(GL_LINES, 0, m_edgeCount);
		glEnable(GL_DEPTH_TEST);
		glBindVertexArray(0);
		return;
	}
	m_program->bind();
	setCommonUniforms(*m_program, params, model);
	m_program->setUniformValue("u_hideSelected", 0);
	m_program->setUniformValue("u_onlySelected", 1);
	m_program->setUniformValue("u_previewTint", 1);
	m_program->setUniformValue("u_mode", 1);
	glBindVertexArray(m_vao);
	// Draw selected faces material by material.
	for (const auto& batch : m_batches) {
		auto& tex = texture(batch.material);
		glBindTexture(GL_TEXTURE_2D, tex.id ? tex.id : m_whiteTexture);
		m_program->setUniformValue("u_texSize", QVector2D(float(tex.size.width()), float(tex.size.height())));
		m_program->setUniformValue("u_useColor", 0);
		m_program->setUniformValue("u_hasTex2", 0);
		m_program->setUniformValue("u_opacity", 1.f);
		for (const auto& [first, count] : m_selectedRanges) {
			if (first >= batch.first && first < batch.first + batch.count) {
				glDrawArrays(GL_TRIANGLES, first, count);
			}
		}
	}
	m_program->setUniformValue("u_useColor", 1);
	for (const auto& [first, count] : m_selectedRanges) {
		if (first >= m_entityFirst) {
			glDrawArrays(GL_TRIANGLES, first, count);
		}
	}
	m_program->setUniformValue("u_previewTint", 0);
	m_program->setUniformValue("u_onlySelected", 0);
	glBindVertexArray(0);
}

void SceneRenderer::drawLines(const std::vector<LineVertex>& lines, const QMatrix4x4& viewProjection, bool onTop, float width) {
	if (!m_initialized || lines.empty()) {
		return;
	}
	m_lineProgram->bind();
	m_lineProgram->setUniformValue("u_viewProj", viewProjection);
	glBindVertexArray(m_lineVao);
	glBindBuffer(GL_ARRAY_BUFFER, m_lineVbo);
	glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(lines.size() * sizeof(LineVertex)), lines.data(), GL_STREAM_DRAW);
	if (onTop) {
		glDisable(GL_DEPTH_TEST);
	} else {
		glEnable(GL_DEPTH_TEST);
	}
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glLineWidth(width);
	glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(lines.size()));
	glLineWidth(1.f);
	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
	glBindVertexArray(0);
}

std::optional<PickResult> SceneRenderer::pick(const Ray& ray, bool facesOnly) const {
	if (!m_doc) {
		return std::nullopt;
	}
	std::optional<PickResult> best;
	for (const auto& item : m_pickItems) {
		if ((item.solid.solidId != 0 && item.entityId == 0 && m_hidden.contains(item.solid)) || (item.entityId != 0 && m_hidden.containsEntity(item.entityId))) {
			continue;
		}
		const double boxHit = intersect(ray, item.bounds);
		if (boxHit < 0 || (best && boxHit > best->distance)) {
			continue;
		}
		if (item.solid.solidId == 0) {
			if (facesOnly) {
				continue;
			}
			PickResult r;
			r.distance = boxHit;
			r.point = ray.origin + ray.direction * boxHit;
			r.entityId = item.entityId;
			// Normal of the box side that was hit.
			const Vec3 c = item.bounds.center(), half = item.bounds.size() * 0.5, local = r.point - c;
			int axis = 0;
			double bestRatio = -1;
			for (int i = 0; i < 3; ++i) {
				const double ratio = half[i] > 0 ? std::abs(local[i]) / half[i] : 0;
				if (ratio > bestRatio) {
					bestRatio = ratio;
					axis = i;
				}
			}
			r.normal = {};
			r.normal[axis] = local[axis] >= 0 ? 1 : -1;
			best = r;
			continue;
		}
		const auto* solid = m_doc->findSolid(item.solid);
		if (!solid) {
			continue;
		}
		if (!item.triangles.empty()) {
			// Displacements: pick the triangles; other faces of the brush still use the planes.
			for (std::size_t t = 0; t + 2 < item.triangles.size(); t += 3) {
				double dist;
				if (intersectTriangle(ray, item.triangles[t], item.triangles[t + 1], item.triangles[t + 2], dist) && (!best || dist < best->distance)) {
					PickResult r;
					r.distance = dist;
					r.point = ray.origin + ray.direction * dist;
					r.normal = normalize(cross(item.triangles[t + 1] - item.triangles[t], item.triangles[t + 2] - item.triangles[t]));
					r.solid = item.solid;
					r.entityId = item.entityId;
					r.sideId = solid->sides[static_cast<std::size_t>(item.triangleSides[t / 3])].id;
					best = r;
				}
			}
			continue;
		}
		if (const auto hit = intersect(ray, *solid); hit && (!best || hit->distance < best->distance)) {
			PickResult r;
			r.distance = hit->distance;
			r.point = ray.origin + ray.direction * hit->distance;
			r.normal = solid->sides[static_cast<std::size_t>(hit->sideIndex)].plane().normal;
			r.solid = item.solid;
			r.entityId = item.entityId;
			r.sideId = solid->sides[static_cast<std::size_t>(hit->sideIndex)].id;
			best = r;
		}
	}
	return best;
}

} // namespace sme::app
