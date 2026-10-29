// renderer.cpp - OpenGL scene rendering (see renderer.h).

#include "renderer.h"

#include <QColor>

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>

#include <sme/brush.h>
#include <sme/decals.h>
#include <sme/studio_model.h>
#include <sme/text.h>

#include "model_cache.h"

namespace sme::app {

namespace {

constexpr int FLAG_WIDTH = 4096;
constexpr int MAX_LIGHTS = 64;
constexpr std::uint8_t FLAG_SELECTED = 1;
constexpr std::uint8_t FLAG_HIDDEN = 2;
constexpr std::uint8_t FLAG_HOVER = 4;
constexpr std::uint8_t FLAG_VOLUME = 8;   // helper volume (parallax_obb): faces only shown while selected
constexpr int LIGHTMAP_UNIT = 16;         // texture unit of the per-face lightmap scales

/// The shader's u_mode: 0 lighting, 1 textured, 2 lightmap density.
int modeUniform(SceneRenderer::Mode mode) {
	return mode == SceneRenderer::Mode::Lit ? 0 : (mode == SceneRenderer::Mode::LightmapDensity ? 2 : 1);
}

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
uniform int u_faceOverride;    // models: one flag slot for the whole instance (-1: per vertex, -2: no flags)
uniform usampler2D u_probeFaces;   // unit 15: the cubemap each face reflects (0 = none)
uniform usampler2D u_lightmapScale;   // unit 16: lightmap scale of each face (0 = no lightmap)
out vec3 v_world;
out vec3 v_normal;
out vec2 v_uv;
out vec3 v_uvw;
flat out uint v_flag;
flat out int v_probe;
flat out float v_lightmap;
void main() {
	int id = u_faceOverride >= 0 ? u_faceOverride : int(a_face + 0.5);
	uint f = u_faceOverride == -2 ? 0u : texelFetch(u_flags, ivec2(id % 4096, id / 4096), 0).r;
	v_flag = f;
	v_probe = u_faceOverride == -2 ? 0 : int(texelFetch(u_probeFaces, ivec2(id % 4096, id / 4096), 0).r);
	v_lightmap = u_faceOverride == -2 ? 0.0 : float(texelFetch(u_lightmapScale, ivec2(id % 4096, id / 4096), 0).r);
	vec4 world = u_model * vec4(a_pos, 1.0);
	v_world = world.xyz;
	v_normal = mat3(u_model) * a_normal;
	v_uv = a_uvw.xy / u_texSize;
	v_uvw = a_uvw;
	gl_Position = u_viewProj * world;
	bool selected = (f & 1u) != 0u;
	if ((u_hideSelected == 1 && selected) || (u_onlySelected == 1 && !selected) || (f & 2u) != 0u || ((f & 8u) != 0u && !selected)) {
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
flat in float v_lightmap;
uniform sampler2D u_tex;
uniform sampler2D u_tex2;
uniform int u_hasTex2;
uniform int u_useColor;
uniform int u_mode;            // 0 lit, 1 unlit, 2 lightmap density
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
uniform vec4 u_flatColor;      // a > 0: one color (models in wireframe and 2D views)
uniform int u_shadows;
uniform sampler2DShadow u_sunShadow;   // unit 3
uniform sampler2DShadow u_atlas;       // unit 4: 4 x 4 tiles for spot and point lights
uniform mat4 u_sunMatrix;
uniform mat4 u_tileMatrix[16];
uniform vec4 u_lightShadow[64];        // x: first tile or -1, y: 1 = point light (six tiles)
uniform float u_sunTexel;
uniform float u_atlasTexel;
uniform int u_skyFace;                 // this batch is a toolsskybox face
uniform int u_hasSky;                  // the six faces of the map's skybox are loaded (units 5..10)
uniform sampler2D u_sky0, u_sky1, u_sky2, u_sky3, u_sky4, u_sky5;   // rt lf bk ft up dn
uniform vec3 u_camPos;
uniform vec4 u_clouds;                 // mapbase-fidelity clouds: amount, scale, coverage, darkness
uniform vec4 u_cloudSun;               // direction to the sun, w 1 = there is one
uniform int u_grade;                   // mapbase-fidelity colour grading (fidelity_grade_ps2x.fxc)
uniform vec4 u_balance;                // rgb: temperature/tint times exposure
uniform vec4 u_tone;                   // contrast, saturation, vibrance, vignette softness
uniform vec4 u_lgg;                    // lift, 1/gamma, gain, vignette radius
uniform vec2 u_vignette;               // strength, aspect (height / width)
uniform vec2 u_viewport;
uniform int u_hasHeight;               // relief: height map on unit 11 (white = top)
uniform sampler2D u_height;
uniform float u_parallaxDepth;
flat in int v_probe;
uniform int u_reflections;             // cubemap reflections on (the view's switch)
uniform int u_probePass;               // drawing into a cubemap: no tint, no reflections
uniform int u_envmap;                  // this material has $envmap
uniform vec3 u_envTint;                // $envmaptint
uniform float u_fresnel;               // $fresnelreflection
uniform int u_envMaskMode;             // 0 none, 1 mask texture (unit 12), 2 base alpha, 3 inverted base alpha
uniform sampler2D u_envMask;
uniform sampler2D u_probeAtlas;        // unit 13: six faces per cubemap, 8 cubemaps per row
uniform sampler2D u_probeData;         // unit 14: origin (w: 0 not drawn yet, 1 drawn, 2 with room box), box min, box max
uniform float u_probeTile;             // pixels per cube face
uniform int u_probeSphere;             // > 0: the preview sphere of cubemap u_probeSphere - 1
out vec4 fragColor;

// Relief like mapbase-fidelity's Fidelity_Parallax: walks into the height map along the view
// (parallax occlusion mapping). The tangent frame comes from the screen derivatives.
vec2 reliefOffset(vec2 uv, vec3 viewTS, float depth, float lod, out float hitHeight) {
	const int STEPS = 24;
	vec2 shift = viewTS.xy / max(viewTS.z, 0.25) * depth;
	float layer = 1.0 / float(STEPS);
	float d = 0.0;
	vec2 current = uv, previous = uv;
	float mapDepth = 1.0 - textureLod(u_height, current, lod).r;
	float previousDepth = 0.0, previousMap = mapDepth;
	for (int i = 0; i < STEPS && d < mapDepth; ++i) {
		previous = current;
		previousDepth = d;
		previousMap = mapDepth;
		current -= shift * layer;
		d += layer;
		mapDepth = 1.0 - textureLod(u_height, current, lod).r;
	}
	float after = mapDepth - d, before = previousMap - previousDepth;
	vec2 hit = mix(current, previous, clamp(after / (after - before), 0.0, 1.0));
	hitHeight = textureLod(u_height, hit, lod).r;
	return hit;
}

vec3 grade(vec3 c) {
	if (u_grade == 0) return c;
	c *= u_balance.rgb;
	c = clamp(c * u_lgg.z + u_lgg.x * (1.0 - c), 0.0, 1.0);
	c = pow(c, vec3(u_lgg.y));
	c = mix(c, c * c * (3.0 - 2.0 * c), u_tone.x);
	float luma = dot(c, vec3(0.299, 0.587, 0.114));
	float chroma = max(c.r, max(c.g, c.b)) - min(c.r, min(c.g, c.b));
	c = mix(vec3(luma), c, u_tone.y * (1.0 + u_tone.z * (1.0 - chroma)));
	vec2 centered = (gl_FragCoord.xy / u_viewport - 0.5) * vec2(1.0 / u_vignette.y, 1.0);
	c *= 1.0 - u_vignette.x * smoothstep(u_lgg.w, u_lgg.w + u_tone.w, length(centered));
	return clamp(c, 0.0, 1.0);
}

// The 2D skybox as the engine maps it (Quake II's sky: rt +X, lf -X, bk +Y, ft -Y, up, dn).
vec3 skyTexture(vec3 v) {
	vec3 a = abs(v);
	vec2 st;
	int face;
	if (a.x >= a.y && a.x >= a.z) {
		face = v.x >= 0.0 ? 0 : 1;
		st = v.x >= 0.0 ? vec2(-v.y, v.z) / a.x : vec2(v.y, v.z) / a.x;
	} else if (a.y >= a.z) {
		face = v.y >= 0.0 ? 2 : 3;
		st = v.y >= 0.0 ? vec2(v.x, v.z) / a.y : vec2(-v.x, v.z) / a.y;
	} else {
		face = v.z >= 0.0 ? 4 : 5;
		st = v.z >= 0.0 ? vec2(-v.y, -v.x) / a.z : vec2(-v.y, v.x) / a.z;
	}
	vec2 uv = vec2((st.x + 1.0) * 0.5, 1.0 - (st.y + 1.0) * 0.5);
	uv = clamp(uv, vec2(0.002), vec2(0.998));   // no seams from the neighbouring edge
	if (face == 0) return texture(u_sky0, uv).rgb;
	if (face == 1) return texture(u_sky1, uv).rgb;
	if (face == 2) return texture(u_sky2, uv).rgb;
	if (face == 3) return texture(u_sky3, uv).rgb;
	if (face == 4) return texture(u_sky4, uv).rgb;
	return texture(u_sky5, uv).rgb;
}

// mapbase-fidelity's drifting clouds (fidelity_sky_ps30.fxc), standing still in the editor.
float skyHash(vec2 p) {
	vec3 p3 = fract(p.xyx * 0.1031);
	p3 += dot(p3, p3.yzx + 33.33);
	return fract((p3.x + p3.y) * p3.z);
}
float valueNoise(vec2 p) {
	vec2 i = floor(p), f = fract(p);
	vec2 u = f * f * (3.0 - 2.0 * f);
	return mix(mix(skyHash(i), skyHash(i + vec2(1, 0)), u.x), mix(skyHash(i + vec2(0, 1)), skyHash(i + vec2(1, 1)), u.x), u.y);
}
float cloudNoise(vec2 p) {
	float v = 0.0, a = 0.5;
	for (int k = 0; k < 4; ++k) {
		v += a * valueNoise(p);
		p = p * 2.03 + vec2(17.1, 9.3);
		a *= 0.5;
	}
	return v;
}
float cloudBase(vec2 p) { return cloudNoise(p) * 0.65 + cloudNoise(p * 0.47 + 31.7) * 0.35; }
float cloudAt(float n, float h) {
	float need = u_clouds.z + h * h * 0.22;
	return clamp((n - need) / 0.22, 0.0, 1.0) * smoothstep(0.0, 0.12, h);
}
vec3 skyClouds(vec3 dir, vec3 color) {
	float fade = smoothstep(-0.01, 0.04, dir.z);
	if (u_clouds.x <= 0.0 || fade <= 0.0) return color;
	float lum = dot(color, vec3(0.299, 0.587, 0.114));
	vec3 grey = mix(color, vec3(lum), 0.8);
	vec3 shadow = grey * (1.0 - 0.75 * u_clouds.w);
	vec3 lit = grey * 1.3 + 0.02;
	bool sun = u_cloudSun.w > 0.5;
	vec2 toSun = u_cloudSun.xy / max(length(u_cloudSun.xy), 0.05) * 0.3;
	vec2 along = dir.xy / max(dir.z, 0.012) * u_clouds.y * 1.6;
	const int STEPS = 6;
	const float THICKNESS = 0.35;
	float stepDepth = min(THICKNESS / max(dir.z, 0.012), 3.0) / float(STEPS) * 12.0;
	float jitter = skyHash(floor(dir.xy * 2048.0)) - 0.5;
	float transmit = 1.0;
	vec3 cloud = vec3(0.0);
	for (int k = 0; k < STEPS; ++k) {
		float h = clamp((float(k) + 0.5 + jitter * 0.8) / float(STEPS), 0.0, 1.0);
		vec2 p = along * (1.0 + h * THICKNESS);
		float detail = valueNoise(p * 6.3) - 0.5;
		float density = cloudAt(cloudBase(p) + detail * 0.1, h);
		if (density > 0.001) {
			float light = sun ? exp(-cloudAt(cloudBase(p + toSun), clamp(h + 0.35 * clamp(u_cloudSun.z, 0.0, 1.0), 0.0, 1.0)) * 2.5) * mix(0.75, 1.0, h)
			                  : mix(0.3, 0.85, h);
			float powder = 1.0 - exp(-density * 3.0);
			vec3 sampleColor = mix(shadow, lit, clamp(light * mix(1.0, 0.75, powder), 0.0, 1.0));
			float alpha = 1.0 - exp(-density * stepDepth);
			cloud += transmit * alpha * sampleColor;
			transmit *= 1.0 - alpha;
			if (transmit < 0.02) break;
		}
	}
	float coverage = 1.0 - transmit;
	if (sun) cloud += grey * pow(clamp(dot(dir, u_cloudSun.xyz), 0.0, 1.0), 12.0) * coverage * transmit * 3.0;
	float haze = smoothstep(0.0, 0.15, dir.z);
	cloud = mix(mix(color, grey, 0.5) * coverage, cloud, haze);
	float amount = fade * u_clouds.x;
	return color * (1.0 - coverage * amount) + cloud * amount;
}

vec3 toLinear(vec3 c) { return pow(c, vec3(2.2)); }

// A cubemap of the map in direction `dir` (sme/cubemaps.h has the same face layout), parallax
// corrected with its room box. Not drawn yet: the sky.
const vec3 CUBE_FORWARD[6] = vec3[6](vec3(1, 0, 0), vec3(-1, 0, 0), vec3(0, 1, 0), vec3(0, -1, 0), vec3(0, 0, 1), vec3(0, 0, -1));
const vec3 CUBE_UP[6] = vec3[6](vec3(0, 0, 1), vec3(0, 0, 1), vec3(0, 0, 1), vec3(0, 0, 1), vec3(0, 1, 0), vec3(0, 1, 0));
vec3 defaultReflection(vec3 dir) { return u_hasSky == 1 ? skyTexture(dir) : vec3(0.42, 0.46, 0.52); }
vec3 probeColor(int probe, vec3 world, vec3 dir, bool boxProject) {
	vec4 origin = texelFetch(u_probeData, ivec2(0, probe), 0);
	if (origin.w < 0.5) return defaultReflection(dir);
	if (boxProject && origin.w > 1.5) {
		vec3 lo = texelFetch(u_probeData, ivec2(1, probe), 0).xyz;
		vec3 hi = texelFetch(u_probeData, ivec2(2, probe), 0).xyz;
		vec3 safe = mix(vec3(1e-6), dir, step(vec3(1e-6), abs(dir)));
		vec3 t = (mix(lo, hi, step(vec3(0.0), dir)) - world) / safe;
		float leave = min(min(t.x, t.y), t.z);
		if (leave > 0.0) {
			vec3 c = world + dir * leave - origin.xyz;
			if (dot(c, c) > 1e-6) dir = normalize(c);
		}
	}
	vec3 a = abs(dir);
	int face = (a.x >= a.y && a.x >= a.z) ? (dir.x >= 0.0 ? 0 : 1) : ((a.y >= a.z) ? (dir.y >= 0.0 ? 2 : 3) : (dir.z >= 0.0 ? 4 : 5));
	vec3 f = CUBE_FORWARD[face], u = CUBE_UP[face], r = cross(f, u);
	vec2 st = (vec2(dot(dir, r), dot(dir, u)) / max(dot(dir, f), 1e-6) + 1.0) * 0.5;
	float inset = 1.0 / u_probeTile;
	st = clamp(st, vec2(inset), vec2(1.0 - inset));
	vec2 tile = vec2(float((probe % 8) * 6 + face), float(probe / 8));
	return textureLod(u_probeAtlas, (tile + st) * u_probeTile / vec2(textureSize(u_probeAtlas, 0)), 0.0).rgb;
}

// 2 x 2 samples with hardware comparison: soft edges.
float pcf(sampler2DShadow map, vec3 c, vec4 rect, float texel) {
	float lit = 0.0;
	for (int x = -1; x <= 1; x += 2) {
		for (int y = -1; y <= 1; y += 2) {
			vec2 uv = clamp(c.xy + vec2(x, y) * 0.75 * texel, rect.xy, rect.zw);
			lit += texture(map, vec3(uv, c.z));
		}
	}
	return lit * 0.25;
}

float sunShadow(vec3 world, vec3 n) {
	vec3 c = (u_sunMatrix * vec4(world + n * 2.0, 1.0)).xyz;
	if (any(lessThan(c, vec3(0.0))) || any(greaterThan(c, vec3(1.0)))) return 1.0;
	return pcf(u_sunShadow, c - vec3(0.0, 0.0, 0.0004), vec4(0.0, 0.0, 1.0, 1.0), u_sunTexel);
}

float lightShadow(int i, vec3 world, vec3 n) {
	vec4 info = u_lightShadow[i];
	if (info.x < 0.0) return 1.0;
	int tile = int(info.x + 0.5);
	if (info.y > 0.5) {
		vec3 d = world - u_lightPos[i].xyz;
		vec3 a = abs(d);
		tile += (a.x >= a.y && a.x >= a.z) ? (d.x > 0.0 ? 0 : 1) : ((a.y >= a.z) ? (d.y > 0.0 ? 2 : 3) : (d.z > 0.0 ? 4 : 5));
	}
	vec4 p = u_tileMatrix[tile] * vec4(world + n * 1.5, 1.0);
	if (p.w <= 0.0) return 1.0;
	vec3 c = p.xyz / p.w;
	if (c.z >= 1.0) return 1.0;
	float tx = float(tile % 4) * 0.25, ty = float(tile / 4) * 0.25;
	vec4 rect = vec4(tx, ty, tx + 0.25, ty + 0.25) + vec4(1.0, 1.0, -1.0, -1.0) * u_atlasTexel;
	return pcf(u_atlas, c - vec3(0.0, 0.0, 0.0002), rect, u_atlasTexel);
}
vec3 toGamma(vec3 c) { return pow(c, vec3(1.0 / 2.2)); }

void main() {
	if (u_probeSphere > 0) {
		// Preview sphere of a selected env_cubemap: only its reflection, as Source 2 shows it.
		vec3 V = normalize(u_camPos - v_world);
		fragColor = vec4(probeColor(u_probeSphere - 1, v_world, reflect(-V, normalize(v_normal)), false), 1.0);
		return;
	}
	vec4 base;
	vec2 uv = v_uv;
	float relief = 1.0;            // height at the visible point (1 = top)
	vec3 reliefNormal = vec3(0.0); // set: lighting uses this normal
	if (u_hasHeight == 1 && u_flatColor.a <= 0.0) {
		vec3 N = normalize(v_normal);
		vec3 dp1 = dFdx(v_world), dp2 = dFdy(v_world);
		vec2 duv1 = dFdx(v_uv), duv2 = dFdy(v_uv);
		vec3 dp2perp = cross(dp2, N), dp1perp = cross(N, dp1);
		vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
		vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;
		float scale = inversesqrt(max(max(dot(T, T), dot(B, B)), 1e-12));
		T *= scale;
		B *= scale;
		vec3 toEye = u_camPos - v_world;
		float distance = length(toEye);
		vec3 V = toEye / max(distance, 0.001);
		vec3 viewTS = vec3(dot(V, normalize(T)), dot(V, normalize(B)), dot(V, N));
		// Fades out in the distance, as $parallaxfade does (1000 units).
		float depth = u_parallaxDepth * (1.0 - smoothstep(600.0, 1000.0, distance));
		if (depth > 0.0001 && viewTS.z > 0.0) {
			vec2 size = vec2(textureSize(u_height, 0));
			float lod = max(0.0, 0.5 * log2(max(dot(duv1 * size, duv1 * size), dot(duv2 * size, duv2 * size))));
			uv = reliefOffset(v_uv, viewTS, depth, lod, relief);
			vec2 texel = 1.0 / size * exp2(lod);
			float hx = textureLod(u_height, uv + vec2(texel.x, 0.0), lod).r - textureLod(u_height, uv - vec2(texel.x, 0.0), lod).r;
			float hy = textureLod(u_height, uv + vec2(0.0, texel.y), lod).r - textureLod(u_height, uv - vec2(0.0, texel.y), lod).r;
			vec3 nTS = normalize(vec3(-depth * hx / (2.0 * texel.x), -depth * hy / (2.0 * texel.y), 1.0));
			reliefNormal = normalize(normalize(T) * nTS.x + normalize(B) * nTS.y + N * nTS.z);
		}
	}
	if (u_useColor == 1) {
		base = vec4(v_uvw, 1.0);
	} else {
		base = texture(u_tex, uv);
		if (u_hasTex2 == 1) {
			vec4 second = texture(u_tex2, uv);
			base = mix(base, second, clamp(v_uvw.z / 255.0, 0.0, 1.0));
		}
	}
	if (u_alphaTest == 1 && base.a < 0.5) discard;
	if (u_flatColor.a > 0.0) {
		vec3 lineColor = (v_flag & 1u) != 0u ? vec3(1.0, 0.25, 0.15) : u_flatColor.rgb;
		fragColor = vec4(lineColor, 1.0);
		return;
	}
	vec3 n = normalize(v_normal);
	vec3 color;
	if (u_mode == 2 && u_useColor == 0) {
		// Lightmap density: the colour says the lightmap scale (red = fine and costly, green = 16,
		// blue = coarse), the checker shows one luxel. Faces without a lightmap stay grey.
		float luma = dot(base.rgb, vec3(0.3, 0.59, 0.11));
		if (v_lightmap <= 0.0 || u_skyFace == 1) {
			color = vec3(0.22 + 0.18 * luma);
		} else {
			const vec3 stops[6] = vec3[6](vec3(0.95, 0.15, 0.15), vec3(1.0, 0.6, 0.1), vec3(0.3, 0.85, 0.3),
			                              vec3(0.2, 0.8, 0.85), vec3(0.25, 0.4, 1.0), vec3(0.6, 0.3, 0.9));
			float x = clamp(log2(v_lightmap) - 2.0, 0.0, 5.0);   // 4 -> 0, 8, 16, 32, 64, 128 -> 5
			int i = min(int(x), 4);
			color = mix(stops[i], stops[i + 1], x - float(i));
			// Luxels lie on the face's plane: project onto the two axes it faces least.
			vec3 a = abs(n);
			vec2 p = a.z >= a.x && a.z >= a.y ? v_world.xy : (a.x >= a.y ? v_world.yz : v_world.xz);
			vec2 cell = floor(p / v_lightmap);
			float checker = mod(cell.x + cell.y, 2.0);
			color *= (0.78 + 0.22 * checker) * (0.85 + 0.3 * luma);
		}
		if ((v_flag & 1u) != 0u) color = mix(color, vec3(1.0, 0.45, 0.1), 0.35);
		fragColor = vec4(color, base.a * u_opacity);
		return;
	}
	if (u_skyFace == 1 && u_hasSky == 1) {
		// Sky faces show the map's sky (and the mod's clouds), seen through them as in the game.
		vec3 dir = normalize(v_world - u_camPos);
		color = skyClouds(dir, skyTexture(dir));
		color = grade(color);
		if ((v_flag & 1u) != 0u && u_probePass == 0) color = mix(color, vec3(1.0, 0.45, 0.1), 0.35);
		fragColor = vec4(color, 1.0);
		return;
	}
	if (u_mode == 1 || u_fullbright == 1) {
		float shade = 0.78 + 0.22 * dot(n, normalize(vec3(0.35, 0.55, 0.75)));
		color = base.rgb * shade;
	} else {
		if (dot(reliefNormal, reliefNormal) > 0.0) n = reliefNormal;
		float sunLit = max(dot(n, -u_sunDir), 0.0);
		if (u_shadows == 1 && sunLit > 0.0) sunLit *= sunShadow(v_world, n);
		vec3 light = u_ambient + u_sunColor * sunLit;
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
			if (u_shadows == 1) atten *= lightShadow(i, v_world, n);
			light += u_lightColor[i].rgb * ndl * atten;
		}
		// Soft filmic curve: keeps hot spots near lights from clipping to white.
		light *= 1.0 - (1.0 - relief) * 0.25;   // deep parts darker (the shader's default $parallaxao)
		vec3 hdr = toLinear(base.rgb) * light * u_exposure;
		if (u_envmap == 1 && u_reflections == 1 && u_probePass == 0) {
			// $envmap: the cubemap in the reflected direction, added on top as the engine does.
			vec3 V = normalize(u_camPos - v_world);
			vec3 R = reflect(-V, n);
			vec3 env = v_probe > 0 ? probeColor(v_probe - 1, v_world, R, true) : defaultReflection(R);
			vec3 mask = vec3(1.0);
			if (u_envMaskMode == 1) mask = texture(u_envMask, uv).rgb;
			else if (u_envMaskMode == 2) mask = vec3(base.a);
			else if (u_envMaskMode == 3) mask = vec3(1.0 - base.a);
			float fresnel = mix(pow(1.0 - clamp(dot(n, V), 0.0, 1.0), 5.0), 1.0, u_fresnel);
			hdr += toLinear(env) * u_envTint * mask * fresnel * u_exposure;
		}
		color = grade(toGamma(vec3(1.0) - exp(-hdr * 1.25)));
	}
	if ((v_flag & 1u) != 0u && u_probePass == 0) {
		color = mix(color, vec3(1.0, 0.45, 0.1), 0.35);
	}
	if ((v_flag & 4u) != 0u && u_probePass == 0) {
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
uniform int u_noFlags;         // live previews: drawn as they are
out vec4 v_color;
void main() {
	int id = int(a_face + 0.5);
	uint f = u_noFlags == 1 ? 1u : texelFetch(u_flags, ivec2(id % 4096, id / 4096), 0).r;
	bool selected = (f & 1u) != 0u;
	v_color = selected ? vec4(1.0, 0.25, 0.15, 1.0) : vec4(a_color, 1.0);
	gl_Position = u_viewProj * u_model * vec4(a_pos, 1.0);
	if ((u_onlySelected == 1 && !selected) || (u_hideSelected == 1 && selected) || (f & 2u) != 0u) {
		gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
	}
}
)";

// Editor sprites: one instance per entity, the quad corners come from gl_VertexID.
const char* SPRITE_VS = R"(#version 330 core
layout(location = 0) in vec4 a_center;     // xyz, size
layout(location = 1) in vec3 a_color;
layout(location = 2) in float a_face;
uniform mat4 u_viewProj;
uniform vec3 u_right;
uniform vec3 u_up;
uniform usampler2D u_flags;
uniform int u_hideSelected;
out vec2 v_uv;
out vec3 v_color;
flat out uint v_flag;
void main() {
	vec2 corner = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1));
	v_uv = vec2(corner.x, 1.0 - corner.y);
	vec3 p = a_center.xyz + (u_right * (corner.x - 0.5) + u_up * (corner.y - 0.5)) * a_center.w;
	int id = int(a_face + 0.5);
	uint f = texelFetch(u_flags, ivec2(id % 4096, id / 4096), 0).r;
	v_flag = f;
	v_color = a_color;
	gl_Position = u_viewProj * vec4(p, 1.0);
	if ((f & 2u) != 0u || (u_hideSelected == 1 && (f & 1u) != 0u)) {
		gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
	}
}
)";

const char* SPRITE_FS = R"(#version 330 core
in vec2 v_uv;
in vec3 v_color;
flat in uint v_flag;
uniform sampler2D u_tex;
uniform int u_textured;
uniform int u_additive;        // black background: brightness is the alpha
out vec4 fragColor;
void main() {
	vec4 c = u_textured == 1 ? texture(u_tex, v_uv) : vec4(v_color, 1.0);
	if (u_additive == 1) {
		c.a = max(c.r, max(c.g, c.b));
	}
	if (c.a < 0.4) {
		discard;
	}
	if ((v_flag & 1u) != 0u) {
		c.rgb = mix(c.rgb, vec3(1.0, 0.25, 0.15), 0.45);
	}
	fragColor = vec4(c.rgb, 1.0);
}
)";

// Full-screen quad for the text overlay (painted on the CPU, premultiplied alpha).
const char* OVERLAY_VS = R"(#version 330 core
out vec2 v_uv;
void main() {
	vec2 p = vec2((gl_VertexID & 1) * 2.0, (gl_VertexID >> 1) * 2.0);
	v_uv = vec2(p.x * 0.5, 1.0 - p.y * 0.5);
	gl_Position = vec4(p - 1.0, 0.0, 1.0);
}
)";

// Pixel for pixel (texelFetch): with a fractional display scale (125 %, 150 %) a sampled texture
// one pixel off the view's size dropped single rows, cutting the bottoms off letters.
const char* OVERLAY_FS = R"(#version 330 core
in vec2 v_uv;
uniform sampler2D u_image;
out vec4 fragColor;
void main() {
	ivec2 size = textureSize(u_image, 0);
	ivec2 pixel = ivec2(gl_FragCoord.xy);
	pixel.y = size.y - 1 - pixel.y;
	if (pixel.x >= size.x || pixel.y < 0) discard;
	fragColor = texelFetch(u_image, pixel, 0);
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

/// Vertex floats of a displacement face (two triangles per cell, normals averaged per vertex,
/// texture coordinates from the flat base like the engine) and its picking triangles. Returns
/// the number of triangles.
std::size_t displacementTriangles(const Side& side, const FaceGeometry& face, const DisplacementMesh& disp, int id, std::vector<float>& vertices,
                                  std::vector<Vec3>* pickTriangles, std::vector<int>* pickSides) {
	const int n = disp.size;
	std::vector<Vec3> normals(disp.positions.size());
	for (int i = 0; i + 1 < n; ++i) {
		for (int j = 0; j + 1 < n; ++j) {
			const int a = i * n + j, b = i * n + j + 1, c = (i + 1) * n + j, d = (i + 1) * n + j + 1;
			for (const auto& tri : {std::array<int, 3>{a, c, b}, std::array<int, 3>{b, c, d}}) {
				const Vec3 tn = cross(disp.positions[tri[1]] - disp.positions[tri[0]], disp.positions[tri[2]] - disp.positions[tri[0]]);
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
	std::size_t count = 0;
	for (int i = 0; i + 1 < n; ++i) {
		for (int j = 0; j + 1 < n; ++j) {
			const int a = i * n + j, b = i * n + j + 1, c = (i + 1) * n + j, d = (i + 1) * n + j + 1;
			for (auto tri : {std::array<int, 3>{a, c, b}, std::array<int, 3>{b, c, d}}) {
				if (flip) {
					std::swap(tri[1], tri[2]);
				}
				for (int k : tri) {
					const Vec3& p = disp.positions[k];
					// Texture coordinates from the undisplaced point (project back onto the face plane).
					const Vec3 base = p - face.normal * dot(p - face.vertices[0], face.normal);
					const auto tc = textureCoordinate(side, base);
					pushVertex(vertices, p, normalize(flip ? -normals[k] : normals[k]), float(tc.u), float(tc.v), disp.alpha[k], id);
					if (pickTriangles) pickTriangles->push_back(p);
				}
				if (pickSides) pickSides->push_back(face.sideIndex);
				++count;
			}
		}
	}
	return count;
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
	if (!build(m_program, SCENE_VS, SCENE_FS) || !build(m_edgeProgram, EDGE_VS, COLOR_FS) || !build(m_lineProgram, LINE_VS, COLOR_FS) ||
	    !build(m_overlayProgram, OVERLAY_VS, OVERLAY_FS) || !build(m_spriteProgram, SPRITE_VS, SPRITE_FS)) {
		return false;
	}
	if (!initShadows(error)) {
		return false;
	}
	initCubemaps();

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

	// Preview brushes use the same layouts.
	for (auto [vao, vbo, edgesLayout] : {std::tuple{&m_previewVao, &m_previewVbo, false}, std::tuple{&m_previewEdgeVao, &m_previewEdgeVbo, true}}) {
		glGenVertexArrays(1, vao);
		glGenBuffers(1, vbo);
		glBindVertexArray(*vao);
		glBindBuffer(GL_ARRAY_BUFFER, *vbo);
		if (edgesLayout) {
			glEnableVertexAttribArray(0);
			glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, edgeStride, reinterpret_cast<void*>(0));
			glEnableVertexAttribArray(1);
			glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, edgeStride, reinterpret_cast<void*>(3 * sizeof(float)));
			glEnableVertexAttribArray(2);
			glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, edgeStride, reinterpret_cast<void*>(6 * sizeof(float)));
		} else {
			for (GLuint i = 0; i < 4; ++i) {
				glEnableVertexAttribArray(i);
			}
			glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(0));
			glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(3 * sizeof(float)));
			glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(6 * sizeof(float)));
			glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(9 * sizeof(float)));
		}
	}

	glGenVertexArrays(1, &m_lineVao);
	glGenBuffers(1, &m_lineVbo);
	glBindVertexArray(m_lineVao);
	glBindBuffer(GL_ARRAY_BUFFER, m_lineVbo);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(LineVertex), reinterpret_cast<void*>(0));
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(LineVertex), reinterpret_cast<void*>(sizeof(QVector3D)));
	glBindVertexArray(0);

	glGenVertexArrays(1, &m_emptyVao);
	glGenVertexArrays(1, &m_spriteVao);
	glGenBuffers(1, &m_spriteVbo);

	glGenTextures(1, &m_whiteTexture);
	glBindTexture(GL_TEXTURE_2D, m_whiteTexture);
	const quint32 white = 0xffffffff;
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &white);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	glGenTextures(1, &m_flagTexture);
	glGenTextures(1, &m_lightmapTexture);
	uploadLightmapScales();
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
		if (tex.height) glDeleteTextures(1, &tex.height);
		if (tex.envMask) glDeleteTextures(1, &tex.envMask);
	}
	m_textures.clear();
	for (auto& model : m_gpuModels) {
		glDeleteBuffers(1, &model.vbo);
		glDeleteBuffers(1, &model.ibo);
		glDeleteVertexArrays(1, &model.vao);
	}
	m_gpuModels.clear();
	glDeleteTextures(1, &m_flagTexture);
	glDeleteTextures(1, &m_lightmapTexture);
	glDeleteTextures(1, &m_whiteTexture);
	glDeleteBuffers(1, &m_vbo);
	glDeleteBuffers(1, &m_edgeVbo);
	glDeleteBuffers(1, &m_lineVbo);
	glDeleteBuffers(1, &m_spriteVbo);
	glDeleteVertexArrays(1, &m_spriteVao);
	glDeleteVertexArrays(1, &m_emptyVao);
	glDeleteBuffers(1, &m_previewVbo);
	glDeleteBuffers(1, &m_previewEdgeVbo);
	glDeleteVertexArrays(1, &m_previewVao);
	glDeleteVertexArrays(1, &m_previewEdgeVao);
	glDeleteVertexArrays(1, &m_vao);
	glDeleteVertexArrays(1, &m_edgeVao);
	glDeleteVertexArrays(1, &m_lineVao);
	releaseShadows();
	releaseCubemaps();
	m_program.reset();
	m_edgeProgram.reset();
	m_lineProgram.reset();
	m_overlayProgram.reset();
	m_spriteProgram.reset();
	if (m_overlayTexture) {
		glDeleteTextures(1, &m_overlayTexture);
		m_overlayTexture = 0;
	}
	m_initialized = false;
}

void SceneRenderer::ensureFlagTexture() {
	const int rows = std::max<int>(1, (static_cast<int>(m_flags.size()) + FLAG_WIDTH - 1) / FLAG_WIDTH);
	m_flags.resize(static_cast<std::size_t>(rows) * FLAG_WIDTH, 0);
	glBindTexture(GL_TEXTURE_2D, m_flagTexture);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_R8UI, FLAG_WIDTH, rows, 0, GL_RED_INTEGER, GL_UNSIGNED_BYTE, m_flags.data());
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}

void SceneRenderer::uploadLightmapScales() {
	const int rows = std::max<int>(1, (static_cast<int>(m_faceLightmap.size()) + FLAG_WIDTH - 1) / FLAG_WIDTH);
	m_faceLightmap.resize(static_cast<std::size_t>(rows) * FLAG_WIDTH, 0);
	glBindTexture(GL_TEXTURE_2D, m_lightmapTexture);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_R8UI, FLAG_WIDTH, rows, 0, GL_RED_INTEGER, GL_UNSIGNED_BYTE, m_faceLightmap.data());
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}

void SceneRenderer::rebuild(const MapDocument& doc, const EntityCatalog* catalog) {
	m_skyName = QString::fromStdString(doc.world.get("skyname")).trimmed();
	m_doc = &doc;
	std::unordered_map<QString, std::vector<float>> byMaterial;
	std::unordered_map<QString, std::vector<int>> facesByMaterial;
	std::vector<float> entityVertices;
	std::vector<float> edges;
	m_sideFaces.clear();
	m_solidFaces.clear();
	m_entityFaces.clear();
	m_pickItems.clear();
	m_instanceSolids.clear();
	m_instanceBounds.clear();
	m_faceToSide.clear();
	m_faceLightmap.clear();
	m_sceneBounds = {};
	m_triangleCount = 0;
	m_modelInstances.clear();
	m_shadowsDirty = true;
	int faceIndex = 0;

	// Brush shapes from the last build, and the material names in the form the batches use: both
	// would otherwise be worked out again for every face of the map on every edit.
	std::unordered_multimap<std::size_t, CachedBrush> brushes;
	brushes.reserve(m_brushCache.size());
	const auto geometryOf = [&](const Solid& solid) -> std::shared_ptr<const BrushGeometry> {
		std::vector<std::array<Vec3, 3>> planes;
		planes.reserve(solid.sides.size());
		std::size_t hash = solid.sides.size();
		for (const auto& side : solid.sides) {
			planes.push_back(side.planePoints);
			for (const auto& p : side.planePoints) {
				for (const double c : {p.x, p.y, p.z}) hash = hash * 1099511628211ull ^ std::hash<double>{}(c);
			}
		}
		for (const auto* cache : {&brushes, &m_brushCache}) {
			const auto [first, last] = cache->equal_range(hash);
			for (auto it = first; it != last; ++it) {
				if (it->second.planes == planes) {
					if (cache != &brushes) brushes.emplace(hash, it->second);
					return it->second.geometry;
				}
			}
		}
		auto geometry = std::make_shared<const BrushGeometry>(buildBrushGeometry(solid));
		brushes.emplace(hash, CachedBrush{std::move(planes), geometry});
		return geometry;
	};
	std::unordered_map<std::string, QString> materialNames;
	const auto materialOf = [&materialNames](const std::string& name) -> const QString& {
		auto it = materialNames.find(name);
		if (it == materialNames.end()) it = materialNames.emplace(name, normalizedMaterialName(QString::fromStdString(name))).first;
		return it->second;
	};

	std::vector<int> faceVertexOffset;   // per face: vertex offset within its material list
	std::vector<QString> faceMaterial;
	// Per face: its side id (0 for models and instance brushes) and center, for the cubemap it
	// reflects; side id -1 = reflects nothing (entity boxes, decals).
	std::vector<std::pair<int, Vec3>> faceSides;
	const auto noteFace = [&faceSides](int id, int sideId, const Vec3& center) {
		if (static_cast<std::size_t>(id) >= faceSides.size()) faceSides.resize(static_cast<std::size_t>(id) + 1, {-1, Vec3{}});
		faceSides[static_cast<std::size_t>(id)] = {sideId, center};
	};

	// `detached`: a brush that is not in the document (instance content); it has no side ids of
	// the map and picking it selects its entity.
	const auto addSolid = [&](const Solid& solid, int entityId, const QVector3D& edgeColor, const Solid* detached = nullptr) {
		const SolidRef ref{entityId, solid.id};
		const auto shape = geometryOf(solid);
		const BrushGeometry& geometry = *shape;
		PickItem pick;
		pick.solid = ref;
		pick.entityId = entityId;
		pick.detached = detached;
		pick.bounds = geometry.bounds;
		m_sceneBounds.add(geometry.bounds);
		for (const auto& face : geometry.faces) {
			const Side& side = solid.sides[static_cast<std::size_t>(face.sideIndex)];
			const QString& material = materialOf(side.material);
			auto& vertices = byMaterial[material];
			const int id = faceIndex++;
			{
				Vec3 center;
				for (const auto& v : face.vertices) center += v;
				noteFace(id, detached ? 0 : side.id, face.vertices.empty() ? center : center / double(face.vertices.size()));
			}
			if (!detached) m_sideFaces[side.id].push_back(id);
			m_solidFaces[solidKey(ref)].push_back(id);
			if (entityId != 0) {
				m_entityFaces[entityId].push_back(id);
			}
			m_faceToSide.emplace_back(ref, side.id);
			// Lightmap scale for the density view; tool faces (nodraw, sky, clips ...) have no lightmap.
			if (m_faceLightmap.size() <= static_cast<std::size_t>(id)) m_faceLightmap.resize(static_cast<std::size_t>(id) + 1, 0);
			if (!material.startsWith(QLatin1String("tools/"), Qt::CaseInsensitive)) {
				m_faceLightmap[static_cast<std::size_t>(id)] = static_cast<std::uint8_t>(std::clamp(side.lightmapScale, 1, 255));
			}
			faceVertexOffset.push_back(static_cast<int>(vertices.size() / FLOATS_PER_VERTEX));
			faceMaterial.push_back(material);
			facesByMaterial[material].push_back(id);

			if (auto disp = buildDisplacement(side, face)) {
				// Displacement grid; UVs come from the flat base surface like in the engine.
				const int n = disp->size;
				m_triangleCount += displacementTriangles(side, face, *disp, id, vertices, &pick.triangles, &pick.triangleSides);
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
		QString sprite;   ///< Editor sprite material (drawn instead of the box in 3D)
	};
	std::vector<BoxEntity> boxes;
	for (const auto& entity : doc.entities) {
		const auto classname = entity.classname();
		// func_instance: the instance's content, read-only, with tinted edges; the entity itself
		// still gets its box below.
		if (m_instanceProvider && isInstanceEntity(entity)) {
			if (const auto* preview = m_instanceProvider(entity)) {
				const QVector3D tint(0.35f, 0.8f, 0.9f);
				for (const auto& solid : preview->solids) {
					m_instanceSolids.push_back(solid);
					addSolid(m_instanceSolids.back(), entity.id, tint, &m_instanceSolids.back());
				}
				for (const auto& [pointBox, pointClass] : preview->pointBoxes) {
					boxes.push_back({entity.id, pointBox, tint, {}});
				}
				m_instanceBounds[entity.id] = preview->bounds;
				m_sceneBounds.add(preview->bounds);
			}
		}
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
		m_sceneBounds.add(box);
		// Loaded model: drawn with its own triangles; picked by its (turned) box.
		const auto modelPath = QString::fromStdString(edit::entityModelKey(entity, catalog));
		const auto geometry = m_models && !modelPath.isEmpty() ? m_models->get(modelPath) : nullptr;
		if (geometry && !geometry->empty()) {
			Vec3 angles;
			(void)parseVec3(entity.get("angles", "0 0 0"), angles);
			const Mat3 r = edit::anglesToMatrix(angles);
			const Vec3 o = entity.origin();
			ModelInstance instance;
			instance.path = modelPath;
			instance.transform = QMatrix4x4(float(r.m[0][0]), float(r.m[0][1]), float(r.m[0][2]), float(o.x), float(r.m[1][0]), float(r.m[1][1]),
			                                float(r.m[1][2]), float(o.y), float(r.m[2][0]), float(r.m[2][1]), float(r.m[2][2]), float(o.z), 0, 0, 0, 1);
			instance.face = faceIndex++;
			if (edit::isSeeThroughHelper(entity)) instance.opacity = 0.4f;   // stands for an NPC, is none
			noteFace(instance.face, 0, o);
			m_entityFaces[entity.id].push_back(instance.face);
			m_faceToSide.emplace_back(SolidRef{entity.id, 0}, 0);
			m_modelInstances.push_back(std::move(instance));
			m_triangleCount += geometry->triangleCount();
			PickItem pick;
			pick.entityId = entity.id;
			pick.bounds = box;
			m_pickItems.push_back(std::move(pick));
		} else {
			boxes.push_back({entity.id, box, color, QString::fromStdString(edit::entitySprite(entity, catalog))});
		}

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
	m_brushCache = std::move(brushes);   // only the brushes of this build stay
	if (!m_hasSun) {
		m_ambient = m_lights.empty() ? QVector3D(0.9f, 0.9f, 0.95f) : QVector3D(0.04f, 0.04f, 0.05f);
		m_sunColor = m_lights.empty() ? QVector3D(0.25f, 0.24f, 0.22f) : QVector3D(0, 0, 0);
		m_sunDirection = QVector3D(-0.4f, -0.3f, -0.85f).normalized();
	}

	// Infodecals and overlays: textured polygons on the brushes, drawn blended after them, a
	// little in front so they never flicker with the surface. Their faces belong to the entity
	// (selection, hiding) and their box stays for picking.
	std::unordered_map<QString, std::vector<float>> decalsByMaterial;
	std::unordered_map<QString, std::vector<int>> decalFacesByMaterial;
	m_decalPolygons = 0;
	m_overlayPolygons = 0;
	m_decalsWaiting.clear();
	{
		const bool any = std::any_of(doc.entities.begin(), doc.entities.end(), [](const Entity& e) {
			return iequals(e.classname(), "infodecal") || iequals(e.classname(), "info_overlay");
		});
		const std::vector<SurfaceFace> surfaces = any ? collectSurfaceFaces(doc) : std::vector<SurfaceFace>{};
		for (const auto& entity : doc.entities) {
			if (!any) break;
			const bool decal = iequals(entity.classname(), "infodecal");
			if (!decal && !iequals(entity.classname(), "info_overlay")) continue;
			const QString material = normalizedMaterialName(QString::fromStdString(entity.get(decal ? "texture" : "material")));
			if (material.isEmpty()) continue;
			std::vector<SurfacePatch> patches;
			if (decal) {
				// Texture size x $decalscale, as the engine sizes it; a stand-in until the material is loaded.
				double width = 64, height = 64;
				if (const auto info = m_materials ? m_materials->get(material) : nullptr) {
					if (!info->image.isNull()) {
						width = info->image.width() * double(info->decalScale);
						height = info->image.height() * double(info->decalScale);
					}
				} else {
					m_decalsWaiting.insert(material);
				}
				patches = decalPatches(surfaces, entity.origin(), width, height);				m_decalPolygons += static_cast<int>(patches.size());
			} else if (const auto overlay = readOverlay(entity)) {
				patches = overlayPatches(surfaces, *overlay);
				m_overlayPolygons += static_cast<int>(patches.size());
			}
			if (patches.empty()) continue;
			auto& vertices = decalsByMaterial[material];
			const int id = faceIndex++;
			m_entityFaces[entity.id].push_back(id);
			m_faceToSide.emplace_back(SolidRef{entity.id, 0}, 0);
			faceVertexOffset.push_back(static_cast<int>(vertices.size() / FLOATS_PER_VERTEX));
			faceMaterial.push_back(material);
			decalFacesByMaterial[material].push_back(id);
			PickItem pick;
			pick.entityId = entity.id;
			for (const auto& patch : patches) {
				const Vec3 lift = patch.normal * 0.15;
				for (std::size_t i = 1; i + 1 < patch.vertices.size(); ++i) {
					for (const std::size_t k : {std::size_t{0}, i, i + 1}) {
						pushVertex(vertices, patch.vertices[k] + lift, patch.normal, float(patch.uv[k][0]), float(patch.uv[k][1]), 0.f, id);
					}
					++m_triangleCount;
				}
				for (std::size_t i = 0; i < patch.vertices.size(); ++i) {
					pushEdge(edges, patch.vertices[i] + lift, patch.vertices[(i + 1) % patch.vertices.size()] + lift, QVector3D(0.95f, 0.75f, 0.3f), id);
					pick.bounds.add(patch.vertices[i] + lift * 2);
					pick.bounds.add(patch.vertices[i]);
				}
			}
			// Clicking the drawn decal or overlay selects it, as clicking its box does.
			m_pickItems.push_back(std::move(pick));
		}
	}

	// Room boxes of cubemaps (parallax_obb) would cover whole rooms: only their outline unless selected.
	m_volumeFaces.clear();
	for (const auto& entity : doc.entities) {
		if (iequals(entity.classname(), "parallax_obb")) {
			if (const auto it = m_entityFaces.find(entity.id); it != m_entityFaces.end()) {
				m_volumeFaces.insert(m_volumeFaces.end(), it->second.begin(), it->second.end());
			}
		}
	}
	// Entities with an editor sprite go last: the 3D views draw their sprites instead of these boxes.
	std::stable_partition(boxes.begin(), boxes.end(), [](const BoxEntity& b) { return b.sprite.isEmpty(); });
	std::vector<float> spriteData;
	m_spriteBatches.clear();
	{
		std::vector<std::size_t> order;
		for (std::size_t i = 0; i < boxes.size(); ++i) {
			if (!boxes[i].sprite.isEmpty()) order.push_back(i);
		}
		std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return boxes[a].sprite < boxes[b].sprite; });
		const int firstSpriteFace = faceIndex + static_cast<int>(boxes.size() - order.size());
		const std::size_t firstSprite = boxes.size() - order.size();
		for (const std::size_t i : order) {
			const auto& b = boxes[i];
			const Vec3 c = b.box.center(), size = b.box.size();
			// Hammer's sprites are 16 units; bigger boxes get a bigger sprite (up to 32).
			const double extent = std::clamp(std::max(size.x, size.y), 16.0, 32.0);
			const int face = firstSpriteFace + static_cast<int>(i - firstSprite);
			if (m_spriteBatches.empty() || m_spriteBatches.back().material != b.sprite) {
				m_spriteBatches.push_back({b.sprite, static_cast<GLint>(spriteData.size() / 8), 0});
				texture(b.sprite);   // request loading early
			}
			++m_spriteBatches.back().count;
			for (const float v : {float(c.x), float(c.y), float(c.z), float(extent), b.color.x(), b.color.y(), b.color.z(), float(face)}) {
				spriteData.push_back(v);
			}
		}
		m_spriteBoxVertices = static_cast<GLsizei>(order.size() * 36);
	}
	glBindVertexArray(m_spriteVao);
	glBindBuffer(GL_ARRAY_BUFFER, m_spriteVbo);
	glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(spriteData.size() * sizeof(float)), spriteData.data(), GL_STATIC_DRAW);
	glBindVertexArray(0);

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
	// Decal and overlay batches after the brushes they lie on.
	std::vector<QString> decalMaterials;
	for (const auto& [name, vertices] : decalsByMaterial) decalMaterials.push_back(name);
	std::sort(decalMaterials.begin(), decalMaterials.end());
	for (const auto& name : decalMaterials) {
		const auto& vertices = decalsByMaterial[name];
		Batch batch;
		batch.material = name;
		batch.decal = true;
		batch.first = static_cast<GLint>(all.size() / FLOATS_PER_VERTEX);
		batch.count = static_cast<GLsizei>(vertices.size() / FLOATS_PER_VERTEX);
		const auto& faces = decalFacesByMaterial[name];
		for (std::size_t k = 0; k < faces.size(); ++k) {
			const int face = faces[k];
			const int start = faceVertexOffset[static_cast<std::size_t>(face)];
			const int end = k + 1 < faces.size() ? faceVertexOffset[static_cast<std::size_t>(faces[k + 1])] : batch.count;
			m_rangesByFace[static_cast<std::size_t>(face)] = {batch.first + start, end - start};
		}
		all.insert(all.end(), vertices.begin(), vertices.end());
		m_batches.push_back(batch);
		texture(name);
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
	m_faceLightmap.resize(static_cast<std::size_t>(std::max(faceIndex, 1)), 0);
	uploadLightmapScales();
	faceSides.resize(static_cast<std::size_t>(faceIndex), {-1, Vec3{}});
	updateProbes(doc, faceSides);
}

bool SceneRenderer::updateDisplacement(int sideId, const Side& side, const FaceGeometry& face, const DisplacementMesh& mesh) {
	const auto it = m_sideFaces.find(sideId);
	if (!m_initialized || it == m_sideFaces.end()) return false;
	bool updated = false;
	for (const int id : it->second) {
		const auto [first, count] = m_rangesByFace[static_cast<std::size_t>(id)];
		std::vector<float> vertices;
		vertices.reserve(static_cast<std::size_t>(count) * FLOATS_PER_VERTEX);
		std::vector<Vec3> triangles;
		displacementTriangles(side, face, mesh, id, vertices, &triangles, nullptr);
		if (static_cast<int>(vertices.size() / FLOATS_PER_VERTEX) != count) return false;   // not drawn as this displacement
		glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
		glBufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptr>(first) * FLOATS_PER_VERTEX * sizeof(float),
		                static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data());
		// Picking follows the new surface: replace this face's triangles in its brush's list.
		const SolidRef ref = m_faceToSide[static_cast<std::size_t>(id)].first;
		for (auto& pick : m_pickItems) {
			if (pick.detached || !(pick.solid == ref)) continue;
			std::size_t next = 0;
			for (std::size_t t = 0; t < pick.triangleSides.size() && next < triangles.size(); ++t) {
				if (pick.triangleSides[t] != face.sideIndex) continue;
				for (int k = 0; k < 3; ++k) pick.triangles[t * 3 + k] = triangles[next++];
			}
			for (const auto& p : mesh.positions) pick.bounds.add(p);
		}
		updated = true;
	}
	m_shadowsDirty = true;
	return updated;
}

void SceneRenderer::setSelection(const MapDocument& doc, const Selection& selection, const Selection& hidden, int hoverSideId) {
	m_selectedProbes.clear();
	for (const int id : selection.entities) {
		for (std::size_t i = 0; i < m_probes.size(); ++i) {
			if (m_probes[i].entityId == id) m_selectedProbes.push_back(static_cast<int>(i));
		}
	}
	std::fill(m_flags.begin(), m_flags.end(), 0);
	for (const int face : m_volumeFaces) {
		if (face >= 0 && static_cast<std::size_t>(face) < m_flags.size()) m_flags[static_cast<std::size_t>(face)] = FLAG_VOLUME;
	}
	m_selectedRanges.clear();
	m_hidden = hidden;
	const auto mark = [&](const std::vector<int>& faces, std::uint8_t flag) {
		for (const int face : faces) {
			if (face >= 0 && static_cast<std::size_t>(face) < m_flags.size()) {
				m_flags[static_cast<std::size_t>(face)] |= flag;
				if (flag == FLAG_SELECTED && static_cast<std::size_t>(face) < m_rangesByFace.size() && m_rangesByFace[static_cast<std::size_t>(face)].second > 0) {
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
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
}

bool SceneRenderer::materialReady(const QString& name) {
	if (m_textures.contains(name)) {
		m_dirtyTextures.insert(name);
	}
	return m_decalsWaiting.contains(name);
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
	if (!info.height.isNull()) {
		upload(tex.height, info.height.convertToFormat(QImage::Format_RGBA8888));
	} else if (tex.height) {
		glDeleteTextures(1, &tex.height);
		tex.height = 0;
	}
	tex.parallaxDepth = info.height.isNull() ? 0.f : info.parallaxDepth;
	tex.envmap = info.envmap;
	tex.envTint = QVector3D(info.envmapTint[0], info.envmapTint[1], info.envmapTint[2]);
	tex.fresnel = info.fresnel;
	tex.envMaskMode = 0;
	if (info.envmapMask == MaterialInfo::EnvmapMask::Texture && !info.envmapMaskImage.isNull()) {
		upload(tex.envMask, info.envmapMaskImage.convertToFormat(QImage::Format_RGBA8888));
		tex.envMaskMode = 1;
	} else if (info.envmapMask == MaterialInfo::EnvmapMask::BaseAlpha) {
		tex.envMaskMode = 2;
	} else if (info.envmapMask == MaterialInfo::EnvmapMask::InvertedBaseAlpha) {
		tex.envMaskMode = 3;
	}
	tex.kind = info.kind;
	tex.isTool = info.isTool;
	tex.isSky = info.isSky;
	tex.found = info.found && !info.image.isNull();
	tex.ready = true;
}

void SceneRenderer::setCommonUniforms(QOpenGLShaderProgram& program, const Params& params, const QMatrix4x4& model) {
	program.setUniformValue("u_viewProj", params.projection * params.view);
	program.setUniformValue("u_model", model);
	program.setUniformValue("u_flags", 2);
	if (&program == m_program.get()) {
		program.setUniformValue("u_skyFace", 0);
		program.setUniformValue("u_hasHeight", 0);
		program.setUniformValue("u_envmap", 0);
		program.setUniformValue("u_probeSphere", 0);
		bindProbes(program);
		program.setUniformValue("u_lightmapScale", LIGHTMAP_UNIT);
		glActiveTexture(GL_TEXTURE0 + LIGHTMAP_UNIT);
		glBindTexture(GL_TEXTURE_2D, m_lightmapTexture);
	}
	glActiveTexture(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_2D, m_flagTexture);
	glActiveTexture(GL_TEXTURE0);
}

void SceneRenderer::bindSky(QOpenGLShaderProgram& program, const Params& params) {
	// The six faces of the map's skybox ("skybox/<skyname>rt" ...), in the shader's order.
	static const char* SUFFIXES[6] = {"rt", "lf", "bk", "ft", "up", "dn"};
	bool all = !m_skyName.isEmpty();
	for (int i = 0; i < 6 && all; ++i) {
		auto& tex = texture(QStringLiteral("skybox/%1%2").arg(m_skyName, QLatin1String(SUFFIXES[i])));
		all = tex.found && tex.id != 0;
		glActiveTexture(GL_TEXTURE5 + GLenum(i));
		glBindTexture(GL_TEXTURE_2D, tex.id ? tex.id : m_whiteTexture);
		program.setUniformValue(QStringLiteral("u_sky%1").arg(i).toLatin1().constData(), 5 + i);
	}
	glActiveTexture(GL_TEXTURE0);
	program.setUniformValue("u_hasSky", all ? 1 : 0);
	program.setUniformValue("u_camPos", params.cameraPosition);
	program.setUniformValue("u_clouds", QVector4D(m_skyLook.clouds, std::max(m_skyLook.scale, 0.05f), m_skyLook.coverage, m_skyLook.darkness));
	program.setUniformValue("u_cloudSun", QVector4D(-m_sunDirection, m_hasSun ? 1.f : 0.f));
	// Colour grading only in the lighting preview (the game grades the lit picture).
	const bool grading = m_grade.on && params.mode == Mode::Lit && !m_probesDrawing;   // the cubemaps keep the plain picture
	program.setUniformValue("u_grade", grading ? 1 : 0);
	if (grading) {
		const auto& g = m_grade;
		const float temperature = std::clamp(g.temperature, -1.f, 1.f), tint = std::clamp(g.tint, -1.f, 1.f);
		const float exposure = std::max(0.f, g.exposure);
		program.setUniformValue("u_balance", QVector4D((1 + 0.10f * temperature - 0.03f * tint) * exposure, (1 + 0.08f * tint) * exposure,
		                                               (1 - 0.10f * temperature - 0.03f * tint) * exposure, 0));
		program.setUniformValue("u_tone", QVector4D(g.contrast, std::max(0.f, g.saturation), g.vibrance, 0.45f));
		program.setUniformValue("u_lgg", QVector4D(g.lift, 1.f / std::max(0.1f, g.gamma), g.gain, 0.55f));
		GLint viewport[4] = {0, 0, 1, 1};
		glGetIntegerv(GL_VIEWPORT, viewport);
		program.setUniformValue("u_viewport", QVector2D(float(std::max(1, viewport[2])), float(std::max(1, viewport[3]))));
		program.setUniformValue("u_vignette", QVector2D(std::clamp(g.vignette, 0.f, 1.f), float(std::max(1, viewport[3])) / float(std::max(1, viewport[2]))));
	}
}

void SceneRenderer::drawBatches(QOpenGLShaderProgram& program, const Params& params, bool translucentPass) {
	for (const auto& batch : m_batches) {
		auto& tex = texture(batch.material);
		const bool translucent = tex.kind == MaterialInfo::Kind::Translucent || tex.kind == MaterialInfo::Kind::Additive || batch.decal;
		// Inside a cubemap tool textures are invisible as in the game, except the sky.
		if (translucent != translucentPass || (!params.showToolTextures && tex.isTool && !(m_probesDrawing && tex.isSky))) {
			continue;
		}
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, tex.id ? tex.id : m_whiteTexture);
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, tex.id2 ? tex.id2 : m_whiteTexture);
		glActiveTexture(GL_TEXTURE0);
		// Decals and overlays come with texture coordinates 0..1.
		program.setUniformValue("u_texSize", batch.decal ? QVector2D(1, 1) : QVector2D(float(tex.size.width()), float(tex.size.height())));
		program.setUniformValue("u_hasTex2", tex.id2 ? 1 : 0);
		program.setUniformValue("u_alphaTest", tex.kind == MaterialInfo::Kind::AlphaTest ? 1 : 0);
		program.setUniformValue("u_fullbright", (tex.isTool || tex.isSky) ? 1 : 0);
		program.setUniformValue("u_skyFace", tex.isSky ? 1 : 0);
		// Relief (mapbase-fidelity) in the lighting preview.
		const bool relief = tex.height != 0 && params.mode == Mode::Lit && !translucent;
		program.setUniformValue("u_hasHeight", relief ? 1 : 0);
		if (relief) {
			glActiveTexture(GL_TEXTURE11);
			glBindTexture(GL_TEXTURE_2D, tex.height);
			glActiveTexture(GL_TEXTURE0);
			program.setUniformValue("u_height", 11);
			program.setUniformValue("u_parallaxDepth", tex.parallaxDepth);
		}
		program.setUniformValue("u_opacity", translucent && tex.isTool ? 0.55f : 1.f);
		bindEnvmap(program, tex, params.reflections && params.mode == Mode::Lit);
		if (tex.kind == MaterialInfo::Kind::Additive) {
			glBlendFunc(GL_SRC_ALPHA, GL_ONE);
		} else {
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		}
		glDrawArrays(GL_TRIANGLES, batch.first, batch.count);
		++m_drawCalls;
	}
	program.setUniformValue("u_skyFace", 0);
	program.setUniformValue("u_hasHeight", 0);
	program.setUniformValue("u_envmap", 0);
}

void SceneRenderer::bindEnvmap(QOpenGLShaderProgram& program, const GpuTexture& tex, bool reflections) {
	const bool on = tex.envmap && reflections && !m_probesDrawing;
	program.setUniformValue("u_envmap", on ? 1 : 0);
	if (!on) return;
	program.setUniformValue("u_envTint", tex.envTint);
	program.setUniformValue("u_fresnel", tex.fresnel);
	program.setUniformValue("u_envMaskMode", tex.envMaskMode);
	if (tex.envMaskMode == 1) {
		glActiveTexture(GL_TEXTURE12);
		glBindTexture(GL_TEXTURE_2D, tex.envMask);
		glActiveTexture(GL_TEXTURE0);
		program.setUniformValue("u_envMask", 12);
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
		if (!m_modelInstances.empty()) {
			m_program->bind();
			setCommonUniforms(*m_program, params, QMatrix4x4());
			m_program->setUniformValue("u_hideSelected", m_hideSelected ? 1 : 0);
			m_program->setUniformValue("u_onlySelected", 0);
			if (params.mode == Mode::Flat2D) {
				glDisable(GL_DEPTH_TEST);
			}
			drawModels(*m_program, true, nullptr);
			glEnable(GL_DEPTH_TEST);
		}
		return;
	}

	// Cubemaps that wait are drawn first (a few faces per frame); they bind their own framebuffer.
	if (params.mode == Mode::Lit && params.reflections) {
		buildProbeFaces(6);
	}
	drawSurfaces(params, false);
	drawProbeSpheres(params);

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

void SceneRenderer::drawSurfaces(const Params& params, bool probePass) {
	m_program->bind();
	setCommonUniforms(*m_program, params, QMatrix4x4());
	m_program->setUniformValue("u_tex", 0);
	m_program->setUniformValue("u_tex2", 1);
	m_program->setUniformValue("u_mode", modeUniform(params.mode));
	m_program->setUniformValue("u_hideSelected", m_hideSelected && !probePass ? 1 : 0);
	m_program->setUniformValue("u_onlySelected", 0);
	m_program->setUniformValue("u_previewTint", 0);
	m_program->setUniformValue("u_probePass", probePass ? 1 : 0);
	m_program->setUniformValue("u_reflections", params.reflections ? 1 : 0);
	m_modelReflections = params.reflections && params.mode == Mode::Lit && !probePass;
	m_program->setUniformValue("u_exposure", 1.0f);
	m_program->setUniformValue("u_sunDir", m_sunDirection);
	m_program->setUniformValue("u_sunColor", m_sunColor);
	m_program->setUniformValue("u_ambient", m_ambient);
	bindSky(*m_program, params);

	// The 64 lights closest to the camera.
	std::vector<const Light*> lights;
	for (const auto& light : m_lights) {
		lights.push_back(&light);
	}
	// Nearest first (the nearest ones also get shadows).
	const auto nearer = [&](const Light* a, const Light* b) {
		return (a->position.toVector3D() - params.cameraPosition).lengthSquared() < (b->position.toVector3D() - params.cameraPosition).lengthSquared();
	};
	if (lights.size() > MAX_LIGHTS) {
		std::partial_sort(lights.begin(), lights.begin() + MAX_LIGHTS, lights.end(), nearer);
		lights.resize(MAX_LIGHTS);
	} else {
		std::sort(lights.begin(), lights.end(), nearer);
	}
	// Shadow maps first (only redrawn when something changed); they rebind their own framebuffer.
	QVector4D shadowInfo[MAX_LIGHTS];
	const bool shadows = params.shadows && params.mode == Mode::Lit;
	if (shadows) {
		updateShadows(lights, shadowInfo);
		m_program->bind();
	} else {
		m_shadowCasterCount = 0;
	}
	m_program->setUniformValue("u_shadows", shadows && m_hasShadowMaps ? 1 : 0);
	if (shadows) {
		m_program->setUniformValue("u_sunMatrix", m_sunMatrix);
		m_program->setUniformValueArray("u_tileMatrix", m_tileMatrices, 16);
		m_program->setUniformValueArray("u_lightShadow", shadowInfo, MAX_LIGHTS);
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
	m_program->setUniformValue("u_faceOverride", -1);
	m_program->setUniformValue("u_flatColor", QVector4D(0, 0, 0, 0));
	drawBatches(*m_program, params, false);
	drawModels(*m_program, false, nullptr);
	glBindVertexArray(m_vao);
	glEnable(GL_CULL_FACE);

	// Entity boxes (flat colored); not inside cubemaps.
	if (m_entityCount > 0 && !probePass) {
		m_program->setUniformValue("u_useColor", 1);
		m_program->setUniformValue("u_hasTex2", 0);
		m_program->setUniformValue("u_alphaTest", 0);
		m_program->setUniformValue("u_fullbright", 1);
		m_program->setUniformValue("u_opacity", 1.f);
		glDrawArrays(GL_TRIANGLES, m_entityFirst, m_entityCount - m_spriteBoxVertices);
		++m_drawCalls;
		m_program->setUniformValue("u_useColor", 0);
	}
	if (!probePass) drawSprites(params);
	m_program->bind();
	glBindVertexArray(m_vao);

	// Translucent materials (water, glass, tool textures like triggers).
	glEnable(GL_BLEND);
	glDepthMask(GL_FALSE);
	drawBatches(*m_program, params, true);
	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
	glDisable(GL_CULL_FACE);
	m_program->setUniformValue("u_probePass", 0);
}

void SceneRenderer::drawSprites(const Params& params) {
	if (m_spriteBatches.empty()) {
		return;
	}
	m_spriteProgram->bind();
	m_spriteProgram->setUniformValue("u_viewProj", params.projection * params.view);
	const QMatrix4x4& v = params.view;
	m_spriteProgram->setUniformValue("u_right", QVector3D(v(0, 0), v(0, 1), v(0, 2)));
	m_spriteProgram->setUniformValue("u_up", QVector3D(v(1, 0), v(1, 1), v(1, 2)));
	m_spriteProgram->setUniformValue("u_flags", 2);
	m_spriteProgram->setUniformValue("u_tex", 0);
	m_spriteProgram->setUniformValue("u_hideSelected", m_hideSelected ? 1 : 0);
	glDisable(GL_CULL_FACE);
	glBindVertexArray(m_spriteVao);
	glBindBuffer(GL_ARRAY_BUFFER, m_spriteVbo);
	constexpr GLsizei stride = 8 * sizeof(float);
	for (GLuint i = 0; i < 3; ++i) {
		glEnableVertexAttribArray(i);
		glVertexAttribDivisor(i, 1);
	}
	for (const auto& batch : m_spriteBatches) {
		const auto& tex = texture(batch.material);
		const bool textured = tex.ready && tex.found;
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, textured ? tex.id : m_whiteTexture);
		m_spriteProgram->setUniformValue("u_textured", textured ? 1 : 0);
		m_spriteProgram->setUniformValue("u_additive", tex.kind == MaterialInfo::Kind::Additive ? 1 : 0);
		const auto offset = static_cast<std::uintptr_t>(batch.first) * stride;
		glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offset));
		glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offset + 4 * sizeof(float)));
		glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offset + 7 * sizeof(float)));
		glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, batch.count);
		++m_drawCalls;
	}
	glBindVertexArray(0);
}

void SceneRenderer::drawGhostModel(const Params& params, const QString& path, const QMatrix4x4& transform, float opacity) {
	if (!m_initialized) {
		return;
	}
	auto* model = gpuModel(path);
	if (!model) {
		return;
	}
	const bool flat = params.mode == Mode::Wireframe || params.mode == Mode::Flat2D;
	glEnable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	m_program->bind();
	setCommonUniforms(*m_program, params, transform);
	m_program->setUniformValue("u_hideSelected", 0);
	m_program->setUniformValue("u_onlySelected", 0);
	m_program->setUniformValue("u_previewTint", 1);
	m_program->setUniformValue("u_faceOverride", -1);
	m_program->setUniformValue("u_useColor", 0);
	m_program->setUniformValue("u_hasTex2", 0);
	m_program->setUniformValue("u_fullbright", 0);
	m_program->setUniformValue("u_opacity", opacity);
	m_program->setUniformValue("u_texSize", QVector2D(1, 1));
	m_program->setUniformValue("u_mode", 1);
	m_program->setUniformValue("u_flatColor", flat ? QVector4D(0.85f, 0.75f, 1.0f, 1) : QVector4D(0, 0, 0, 0));
	if (flat) {
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	}
	const bool blend = opacity < 1.f;
	if (blend) {
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}
	glBindVertexArray(model->vao);
	for (const auto& mesh : model->meshes) {
		auto& tex = texture(mesh.material);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, tex.id ? tex.id : m_whiteTexture);
		m_program->setUniformValue("u_alphaTest", tex.kind == MaterialInfo::Kind::AlphaTest ? 1 : 0);
		glDrawElements(GL_TRIANGLES, mesh.count, GL_UNSIGNED_INT, reinterpret_cast<void*>(static_cast<std::uintptr_t>(mesh.first) * sizeof(std::uint32_t)));
		++m_drawCalls;
	}
	glBindVertexArray(0);
	if (blend) {
		glDisable(GL_BLEND);
		m_program->setUniformValue("u_opacity", 1.f);
	}
	if (flat) {
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	}
	m_program->setUniformValue("u_previewTint", 0);
	m_program->setUniformValue("u_model", QMatrix4x4());
	m_program->setUniformValue("u_flatColor", QVector4D(0, 0, 0, 0));
	m_program->setUniformValue("u_alphaTest", 0);
}

void SceneRenderer::drawPreview(const Params& params, const QMatrix4x4& model, bool brushes) {
	if (!m_initialized) {
		return;
	}
	glEnable(GL_DEPTH_TEST);
	if (!m_modelInstances.empty()) {
		const bool flat = params.mode == Mode::Wireframe || params.mode == Mode::Flat2D;
		m_program->bind();
		setCommonUniforms(*m_program, params, QMatrix4x4());
		m_program->setUniformValue("u_hideSelected", 0);
		m_program->setUniformValue("u_onlySelected", 1);
		m_program->setUniformValue("u_previewTint", 1);
		m_program->setUniformValue("u_mode", 1);
		drawModels(*m_program, flat, &model);
		m_program->setUniformValue("u_previewTint", 0);
		m_program->setUniformValue("u_onlySelected", 0);
	}
	if (m_selectedRanges.empty()) {
		return;
	}
	if (!brushes) {
		// Only the point entity boxes (they follow the matrix; brushes come from setPreviewSolids).
		m_program->bind();
		setCommonUniforms(*m_program, params, model);
		m_program->setUniformValue("u_hideSelected", 0);
		m_program->setUniformValue("u_onlySelected", 1);
		m_program->setUniformValue("u_previewTint", 1);
		m_program->setUniformValue("u_faceOverride", -1);
		m_program->setUniformValue("u_flatColor", QVector4D(0, 0, 0, 0));
		m_program->setUniformValue("u_useColor", 1);
		m_program->setUniformValue("u_mode", 1);
		glBindVertexArray(m_vao);
		for (const auto& [first, count] : m_selectedRanges) {
			if (first >= m_entityFirst) {
				glDrawArrays(GL_TRIANGLES, first, count);
			}
		}
		m_program->setUniformValue("u_previewTint", 0);
		m_program->setUniformValue("u_onlySelected", 0);
		m_program->setUniformValue("u_useColor", 0);
		glBindVertexArray(0);
		return;
	}
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
	m_program->setUniformValue("u_faceOverride", -1);
	m_program->setUniformValue("u_flatColor", QVector4D(0, 0, 0, 0));
	glBindVertexArray(m_vao);
	// Draw selected faces material by material.
	for (const auto& batch : m_batches) {
		auto& tex = texture(batch.material);
		glBindTexture(GL_TEXTURE_2D, tex.id ? tex.id : m_whiteTexture);
		m_program->setUniformValue("u_texSize", batch.decal ? QVector2D(1, 1) : QVector2D(float(tex.size.width()), float(tex.size.height())));
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

void SceneRenderer::setPreviewSolids(const std::vector<Solid>& solids) {
	m_previewBatches.clear();
	m_previewEdgeCount = 0;
	if (!m_initialized) {
		return;
	}
	std::map<QString, std::vector<float>> byMaterial;
	std::vector<float> edges;
	const QVector3D edgeColor(1.0f, 0.65f, 0.2f);
	for (const auto& solid : solids) {
		const auto geometry = buildBrushGeometry(solid);
		for (const auto& face : geometry.faces) {
			const Side& side = solid.sides[static_cast<std::size_t>(face.sideIndex)];
			auto& vertices = byMaterial[normalizedMaterialName(QString::fromStdString(side.material))];
			for (std::size_t i = 1; i + 1 < face.vertices.size(); ++i) {
				for (const auto* v : {&face.vertices[0], &face.vertices[i], &face.vertices[i + 1]}) {
					const auto tc = textureCoordinate(side, *v);
					pushVertex(vertices, *v, face.normal, float(tc.u), float(tc.v), 0.f, 0);
				}
			}
			for (std::size_t i = 0; i < face.vertices.size(); ++i) {
				pushEdge(edges, face.vertices[i], face.vertices[(i + 1) % face.vertices.size()], edgeColor, 0);
			}
		}
	}
	std::vector<float> all;
	for (auto& [material, vertices] : byMaterial) {
		Batch batch;
		batch.material = material;
		batch.first = static_cast<GLint>(all.size() / FLOATS_PER_VERTEX);
		batch.count = static_cast<GLsizei>(vertices.size() / FLOATS_PER_VERTEX);
		all.insert(all.end(), vertices.begin(), vertices.end());
		m_previewBatches.push_back(batch);
		texture(material);
	}
	glBindBuffer(GL_ARRAY_BUFFER, m_previewVbo);
	glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(all.size() * sizeof(float)), all.data(), GL_DYNAMIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, m_previewEdgeVbo);
	glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(edges.size() * sizeof(float)), edges.data(), GL_DYNAMIC_DRAW);
	m_previewEdgeCount = static_cast<GLsizei>(edges.size() / FLOATS_PER_EDGE);
}

void SceneRenderer::drawPreviewSolids(const Params& params) {
	if (!m_initialized || m_previewEdgeCount == 0) {
		return;
	}
	glEnable(GL_DEPTH_TEST);
	if (params.mode != Mode::Wireframe && params.mode != Mode::Flat2D) {
		m_program->bind();
		setCommonUniforms(*m_program, params, QMatrix4x4());
		m_program->setUniformValue("u_hideSelected", 0);
		m_program->setUniformValue("u_onlySelected", 0);
		m_program->setUniformValue("u_faceOverride", -2);
		m_program->setUniformValue("u_flatColor", QVector4D(0, 0, 0, 0));
		m_program->setUniformValue("u_previewTint", 1);
		m_program->setUniformValue("u_useColor", 0);
		m_program->setUniformValue("u_hasTex2", 0);
		m_program->setUniformValue("u_alphaTest", 0);
		m_program->setUniformValue("u_fullbright", 0);
		m_program->setUniformValue("u_opacity", 1.f);
		m_program->setUniformValue("u_mode", modeUniform(params.mode));
		glBindVertexArray(m_previewVao);
		for (const auto& batch : m_previewBatches) {
			auto& tex = texture(batch.material);
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, tex.id ? tex.id : m_whiteTexture);
			m_program->setUniformValue("u_texSize", QVector2D(float(tex.size.width()), float(tex.size.height())));
			glDrawArrays(GL_TRIANGLES, batch.first, batch.count);
		}
		m_program->setUniformValue("u_previewTint", 0);
		m_program->setUniformValue("u_faceOverride", -1);
	}
	m_edgeProgram->bind();
	setCommonUniforms(*m_edgeProgram, params, QMatrix4x4());
	m_edgeProgram->setUniformValue("u_noFlags", 1);
	m_edgeProgram->setUniformValue("u_onlySelected", 0);
	m_edgeProgram->setUniformValue("u_hideSelected", 0);
	glBindVertexArray(m_previewEdgeVao);
	glDisable(GL_DEPTH_TEST);
	glDrawArrays(GL_LINES, 0, m_previewEdgeCount);
	glEnable(GL_DEPTH_TEST);
	m_edgeProgram->setUniformValue("u_noFlags", 0);
	glBindVertexArray(0);
}

SceneRenderer::GpuModel* SceneRenderer::gpuModel(const QString& path) {
	if (auto it = m_gpuModels.find(path); it != m_gpuModels.end()) {
		return &*it;
	}
	const auto geometry = m_models ? m_models->get(path) : nullptr;
	if (!geometry) {
		return nullptr;
	}
	// Same vertex layout as the brushes: position, normal, uv (0..1, texture size 1) and face 0.
	std::vector<float> vertices;
	vertices.reserve(geometry->vertices.size() * FLOATS_PER_VERTEX);
	for (const auto& v : geometry->vertices) {
		vertices.insert(vertices.end(), {v.position[0], v.position[1], v.position[2], v.normal[0], v.normal[1], v.normal[2], v.uv[0], v.uv[1], 0.f, 0.f});
	}
	GpuModel model;
	glGenVertexArrays(1, &model.vao);
	glGenBuffers(1, &model.vbo);
	glGenBuffers(1, &model.ibo);
	glBindVertexArray(model.vao);
	glBindBuffer(GL_ARRAY_BUFFER, model.vbo);
	glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data(), GL_STATIC_DRAW);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, model.ibo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(geometry->indices.size() * sizeof(std::uint32_t)), geometry->indices.data(), GL_STATIC_DRAW);
	const GLsizei stride = FLOATS_PER_VERTEX * sizeof(float);
	for (GLuint i = 0; i < 4; ++i) {
		glEnableVertexAttribArray(i);
	}
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(0));
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(3 * sizeof(float)));
	glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(6 * sizeof(float)));
	glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(9 * sizeof(float)));
	glBindVertexArray(0);
	for (const auto& mesh : geometry->meshes) {
		model.meshes.push_back({normalizedMaterialName(QString::fromStdString(mesh.material)), mesh.firstIndex, static_cast<GLsizei>(mesh.indexCount)});
	}
	return &*m_gpuModels.insert(path, std::move(model));
}

void SceneRenderer::drawModels(QOpenGLShaderProgram& program, bool flat, const QMatrix4x4* preview) {
	if (m_modelInstances.empty()) {
		return;
	}
	glDisable(GL_CULL_FACE);   // models keep the winding of their files
	program.setUniformValue("u_useColor", 0);
	program.setUniformValue("u_hasTex2", 0);
	program.setUniformValue("u_fullbright", 0);
	program.setUniformValue("u_opacity", 1.f);
	program.setUniformValue("u_texSize", QVector2D(1, 1));
	program.setUniformValue("u_alphaTest", 0);
	program.setUniformValue("u_flatColor", flat ? QVector4D(0.85f, 0.75f, 1.0f, 1) : QVector4D(0, 0, 0, 0));
	if (flat) {
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	}
	// Solid models first, then the see-through ones blended over them (no depth writes).
	for (const bool seeThrough : {false, true}) {
		if (seeThrough && !flat) {
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glDepthMask(GL_FALSE);
		}
		for (const auto& instance : m_modelInstances) {
			if ((instance.opacity < 1.f) != seeThrough) continue;
			if (preview && !(m_flags[static_cast<std::size_t>(instance.face)] & FLAG_SELECTED)) {
				continue;
			}
			auto* model = gpuModel(instance.path);
			if (!model) {
				continue;
			}
			program.setUniformValue("u_model", preview ? *preview * instance.transform : instance.transform);
			program.setUniformValue("u_faceOverride", instance.face);
			program.setUniformValue("u_opacity", flat ? 1.f : instance.opacity);
			glBindVertexArray(model->vao);
			for (const auto& mesh : model->meshes) {
				if (!flat) {
					auto& tex = texture(mesh.material);
					glActiveTexture(GL_TEXTURE0);
					glBindTexture(GL_TEXTURE_2D, tex.id ? tex.id : m_whiteTexture);
					program.setUniformValue("u_alphaTest", tex.kind == MaterialInfo::Kind::AlphaTest ? 1 : 0);
					bindEnvmap(program, tex, m_modelReflections);
				}
				glDrawElements(GL_TRIANGLES, mesh.count, GL_UNSIGNED_INT, reinterpret_cast<void*>(static_cast<std::uintptr_t>(mesh.first) * sizeof(std::uint32_t)));
				++m_drawCalls;
			}
		}
		if (seeThrough && !flat) {
			glDepthMask(GL_TRUE);
			glDisable(GL_BLEND);
		}
	}
	program.setUniformValue("u_opacity", 1.f);
	if (flat) {
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	}
	glBindVertexArray(0);
	program.setUniformValue("u_model", QMatrix4x4());
	program.setUniformValue("u_faceOverride", -1);
	program.setUniformValue("u_flatColor", QVector4D(0, 0, 0, 0));
	program.setUniformValue("u_alphaTest", 0);
	program.setUniformValue("u_envmap", 0);
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

void SceneRenderer::drawOverlay(const QImage& image, bool changed) {
	if (!m_initialized || image.isNull()) {
		return;
	}
	glActiveTexture(GL_TEXTURE0);
	if (!m_overlayTexture) {
		glGenTextures(1, &m_overlayTexture);
		changed = true;
	}
	glBindTexture(GL_TEXTURE_2D, m_overlayTexture);
	if (changed) {
		glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		const QImage rgba = image.format() == QImage::Format_RGBA8888_Premultiplied ? image : image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
		if (rgba.size() != m_overlaySize) {
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, rgba.width(), rgba.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.constBits());
			m_overlaySize = rgba.size();
		} else {
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, rgba.width(), rgba.height(), GL_RGBA, GL_UNSIGNED_BYTE, rgba.constBits());
		}
	}
	m_overlayProgram->bind();
	m_overlayProgram->setUniformValue("u_image", 0);
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	glBindVertexArray(m_emptyVao);   // the quad comes from gl_VertexID
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glBindVertexArray(0);
	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
}

void SceneRenderer::endFrame() {
	if (!m_initialized) {
		return;
	}
	// QPainter's glyph cache uploads rows padded to 4 bytes; an alignment of 1 left behind by
	// the flag texture turns all text into smudge on some drivers (and makes it vanish on others).
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glUseProgram(0);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_BLEND);
	glDepthMask(GL_TRUE);
	glLineWidth(1.f);
}

Bounds SceneRenderer::instanceBounds(int entityId) const {
	const auto it = m_instanceBounds.find(entityId);
	return it == m_instanceBounds.end() ? Bounds{} : it->second;
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
		if (item.detached && facesOnly) {
			continue;   // instance content has no faces of the map
		}
		const auto* solid = item.detached ? item.detached : m_doc->findSolid(item.solid);
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
