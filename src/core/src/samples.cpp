// samples.cpp - The sample files Mapsmith ships (see samples.h).

#include <sme/samples.h>

#include <sme/animation_import.h>
#include <sme/particles.h>
#include <sme/sound_edit.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <functional>
#include <numbers>
#include <sstream>
#include <system_error>

namespace sme {

namespace fs = std::filesystem;

namespace {

constexpr double TAU = 2 * std::numbers::pi;

/// A small fixed random generator (xorshift): the same samples every time.
class Random {
public:
	explicit Random(std::uint32_t seed) : m_state(seed ? seed : 1) {}
	double next() {   // 0..1
		m_state ^= m_state << 13;
		m_state ^= m_state >> 17;
		m_state ^= m_state << 5;
		return double(m_state) / 4294967296.0;
	}
	double signedNext() { return next() * 2 - 1; }

private:
	std::uint32_t m_state;
};

/// A stable value 0..1 for a grid cell (tile colours).
double cellHash(int x, int y, int salt) {
	std::uint32_t h = std::uint32_t(x) * 73856093u ^ std::uint32_t(y) * 19349663u ^ std::uint32_t(salt) * 83492791u;
	h ^= h >> 13;
	h *= 0x5bd1e995u;
	h ^= h >> 15;
	return double(h & 0xffffff) / double(0xffffff);
}

DecodedAudio toAudio(int rate, int channels, const std::vector<double>& samples) {
	DecodedAudio audio;
	audio.sampleRate = rate;
	audio.channels = channels;
	audio.format = "PCM 16 bit";
	audio.samples.reserve(samples.size());
	for (const double v : samples) audio.samples.push_back(std::int16_t(std::lround(std::clamp(v, -1.0, 1.0) * 32000.0)));
	return audio;
}

/// Noise low-passed around a loop of `count` samples (the filter runs twice around, so the end
/// meets the start without a jump).
std::vector<double> loopedNoise(std::size_t count, double smoothing, std::uint32_t seed) {
	Random random(seed);
	std::vector<double> raw(count);
	for (auto& v : raw) v = random.signedNext();
	std::vector<double> out(count);
	double state = 0;
	for (int pass = 0; pass < 2; ++pass) {
		for (std::size_t i = 0; i < count; ++i) {
			state += (raw[i] - state) * smoothing;
			out[i] = state;
		}
	}
	double peak = 1e-9;
	for (const double v : out) peak = std::max(peak, std::abs(v));
	for (auto& v : out) v /= peak;
	return out;
}

ImageRgba blankImage(int width, int height) {
	ImageRgba image;
	image.width = std::uint16_t(width);
	image.height = std::uint16_t(height);
	image.pixels.assign(std::size_t(width) * std::size_t(height) * 4, std::byte(255));
	return image;
}

void setPixel(ImageRgba& image, int x, int y, double r, double g, double b, double a = 1.0) {
	const std::size_t at = (std::size_t(y) * image.width + std::size_t(x)) * 4;
	const auto byte = [](double v) { return std::byte(std::uint8_t(std::lround(std::clamp(v, 0.0, 1.0) * 255))); };
	image.pixels[at] = byte(r);
	image.pixels[at + 1] = byte(g);
	image.pixels[at + 2] = byte(b);
	image.pixels[at + 3] = byte(a);
}

std::string num(double v) {
	if (!std::isfinite(v) || std::abs(v) < 1e-7) v = 0;
	std::ostringstream out;
	out.precision(6);
	out << std::fixed << v;
	std::string s = out.str();
	while (s.size() > 1 && s.back() == '0') s.pop_back();
	if (!s.empty() && s.back() == '.') s.pop_back();
	return s == "-0" ? "0" : s;
}

std::vector<std::byte> bytesOf(std::string_view text) {
	std::vector<std::byte> out(text.size());
	std::transform(text.begin(), text.end(), out.begin(), [](char c) { return std::byte(static_cast<unsigned char>(c)); });
	return out;
}

/// Height of the tiles at a pixel: 1 on a tile (softly bevelled), 0 in the grout.
double tileHeight(int x, int y) {
	constexpr int TILE = 64, GROUT = 3;
	const int u = ((x % TILE) + TILE) % TILE, v = ((y % TILE) + TILE) % TILE;
	const int edge = std::min({u, v, TILE - 1 - u, TILE - 1 - v});
	if (edge < GROUT) return 0;
	return std::min(1.0, 0.35 + 0.65 * double(edge - GROUT) / 4.0);
}

// --- Meshes for the .smd sign ---------------------------------------------------------------------

struct Vec {
	double x, y, z;
};

/// Six faces of a box as .smd triangles on one bone; the big faces (front/back, -y/+y) get the
/// whole picture, the others a thin strip of its edge.
void addBox(std::string& out, const std::string& material, Vec a, Vec b, int bone) {
	const auto corner = [&](int i) { return Vec{(i & 1) ? b.x : a.x, (i & 2) ? b.y : a.y, (i & 4) ? b.z : a.z}; };
	struct Face {
		int c[4];
		Vec n;
		bool big;
	};
	// Corners counter-clockwise seen from outside (Source: front faces counter-clockwise).
	const Face faces[] = {
		{{0, 1, 5, 4}, {0, -1, 0}, true},    // -y (front)
		{{3, 2, 6, 7}, {0, 1, 0}, true},     // +y (back)
		{{2, 0, 4, 6}, {-1, 0, 0}, false},   // -x
		{{1, 3, 7, 5}, {1, 0, 0}, false},    // +x
		{{4, 5, 7, 6}, {0, 0, 1}, false},    // +z
		{{2, 3, 1, 0}, {0, 0, -1}, false},   // -z
	};
	const double uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
	for (const auto& face : faces) {
		const auto vertex = [&](int k) {
			const Vec p = corner(face.c[k]);
			const double u = face.big ? uv[k][0] : 0.02 + 0.04 * uv[k][0];
			const double v = face.big ? uv[k][1] : uv[k][1];
			return std::to_string(bone) + " " + num(p.x) + " " + num(p.y) + " " + num(p.z) + " " + num(face.n.x) + " " + num(face.n.y) + " " +
			       num(face.n.z) + " " + num(u) + " " + num(v) + " 1 " + std::to_string(bone) + " 1\n";
		};
		for (const auto& tri : {std::array{0, 1, 2}, std::array{0, 2, 3}}) {
			out += material + "\n";
			for (const int k : tri) out += vertex(k);
		}
	}
}

constexpr double SIGN_HINGE = 56;   // height of the board's hinge (bone "board") above the bracket's bone

std::string signNodes() {
	return "version 1\nnodes\n0 \"bracket\" -1\n1 \"board\" 0\nend\n";
}

std::string signFrame(int frame, double boardRoll, double boardYaw) {
	return "time " + std::to_string(frame) + "\n0 0 0 0 0 0 0\n1 0 0 " + num(SIGN_HINGE) + " " + num(boardRoll) + " 0 " + num(boardYaw) + "\n";
}

/// A relative animation of a few HL2 bones: a frame lists each bone with an offset (0 0 0) and
/// rotation change in radians.
std::string humanAnimation(const char* comment, const std::vector<std::string>& bones, int frames,
                           const std::function<std::array<double, 3>(std::size_t bone, double t)>& angles) {
	std::string out = "version 1\n// mapsmith: relative - only these bones move, by these angles, on top of the model's rest pose.\n";
	out += std::string("// ") + comment + "\nnodes\n";
	for (std::size_t b = 0; b < bones.size(); ++b) out += std::to_string(b) + " \"" + bones[b] + "\" -1\n";
	out += "end\nskeleton\n";
	for (int f = 0; f < frames; ++f) {
		out += "time " + std::to_string(f) + "\n";
		const double t = double(f) / double(frames);   // 0..1 over the loop
		for (std::size_t b = 0; b < bones.size(); ++b) {
			const auto a = angles(b, t);
			out += std::to_string(b) + " 0 0 0 " + num(a[0]) + " " + num(a[1]) + " " + num(a[2]) + "\n";
		}
	}
	out += "end\n";
	return out;
}

PcfFunction function(std::string name, PcfValues values = {}) {
	return {std::move(name), std::move(values)};
}

} // namespace

// --- Sounds -----------------------------------------------------------------------------------------

DecodedAudio sampleHumLoop() {
	constexpr int RATE = 22050;
	constexpr std::size_t COUNT = RATE * 4;
	const auto wind = loopedNoise(COUNT, 0.02, 7);
	std::vector<double> s(COUNT);
	for (std::size_t i = 0; i < COUNT; ++i) {
		const double t = double(i) / RATE;
		// 60 Hz mains hum with harmonics (whole cycles in 4 s, so the loop is seamless) and a
		// wind that swells once per loop.
		const double hum = 0.30 * std::sin(TAU * 60 * t) + 0.12 * std::sin(TAU * 120 * t + 0.4) + 0.06 * std::sin(TAU * 180 * t + 1.1);
		const double swell = 0.6 + 0.4 * std::sin(TAU * 0.25 * t);
		s[i] = 0.75 * (hum + 0.35 * swell * wind[i]);
	}
	auto audio = toAudio(RATE, 1, s);
	audio.loopStart = 0;
	return audio;
}

DecodedAudio sampleWindLoop() {
	constexpr int RATE = 22050;
	constexpr std::size_t COUNT = RATE * 3;
	const auto low = loopedNoise(COUNT, 0.015, 11);
	const auto high = loopedNoise(COUNT, 0.2, 13);
	std::vector<double> s(COUNT);
	for (std::size_t i = 0; i < COUNT; ++i) {
		const double t = double(i) / RATE;
		const double gust = 0.5 + 0.5 * std::sin(TAU * t / 3.0 - std::numbers::pi / 2);
		s[i] = 0.6 * (0.7 * low[i] + 0.25 * gust * high[i]);
	}
	return toAudio(RATE, 1, s);
}

DecodedAudio sampleClick() {
	constexpr int RATE = 44100;
	constexpr std::size_t COUNT = RATE * 3 / 10;
	Random random(3);
	std::vector<double> s(COUNT);
	for (std::size_t i = 0; i < COUNT; ++i) {
		const double t = double(i) / RATE;
		const double snap = random.signedNext() * std::exp(-t * 400);                  // the latch
		const double body = std::sin(TAU * 1800 * t) * std::exp(-t * 60) * 0.5;        // the metal rings
		const double thud = std::sin(TAU * 140 * t) * std::exp(-t * 35) * 0.6;         // the housing
		s[i] = 0.8 * (snap + body + thud);
	}
	return toAudio(RATE, 1, s);
}

DecodedAudio sampleChime() {
	constexpr int RATE = 44100;
	constexpr std::size_t FRAMES = RATE * 5 / 2;
	std::vector<double> s(FRAMES * 2);
	const struct {
		double start, freq, pan;
	} notes[] = {{0.0, 659.25, 0.35}, {0.6, 523.25, 0.65}};   // E5, then C5
	for (std::size_t i = 0; i < FRAMES; ++i) {
		const double t = double(i) / RATE;
		double left = 0, right = 0;
		for (const auto& note : notes) {
			const double d = t - note.start;
			if (d < 0) continue;
			// A bell: a few inharmonic partials, the higher ones dying faster.
			const double v = std::sin(TAU * note.freq * d) * std::exp(-d * 2.2) + 0.45 * std::sin(TAU * note.freq * 2.76 * d) * std::exp(-d * 4.5) +
			                 0.2 * std::sin(TAU * note.freq * 5.4 * d) * std::exp(-d * 9.0);
			const double attack = std::min(1.0, d * 400);
			left += v * attack * (1 - note.pan);
			right += v * attack * note.pan;
		}
		s[i * 2] = 0.55 * left;
		s[i * 2 + 1] = 0.55 * right;
	}
	return toAudio(RATE, 2, s);
}

// --- Pictures ---------------------------------------------------------------------------------------

ImageRgba sampleTiles() {
	constexpr int SIZE = 512, TILE = 64;
	ImageRgba image = blankImage(SIZE, SIZE);
	Random random(21);
	for (int y = 0; y < SIZE; ++y) {
		for (int x = 0; x < SIZE; ++x) {
			const double grain = 0.06 * random.signedNext();
			if (tileHeight(x, y) == 0) {
				setPixel(image, x, y, 0.42 + grain, 0.40 + grain, 0.37 + grain);   // grout
				continue;
			}
			// Each tile its own shade of terracotta, a little lighter towards its middle.
			const double shade = cellHash(x / TILE, y / TILE, 1);
			const double r = 0.62 + 0.14 * shade, g = 0.34 + 0.08 * shade, b = 0.24 + 0.05 * shade;
			const double light = 0.04 * (tileHeight(x, y) - 0.7);
			setPixel(image, x, y, r + grain + light, g + grain * 0.8 + light, b + grain * 0.6 + light);
		}
	}
	return image;
}

ImageRgba sampleTilesNormal() {
	constexpr int SIZE = 512;
	ImageRgba image = blankImage(SIZE, SIZE);
	const auto h = [](int x, int y) { return tileHeight((x + SIZE) % SIZE, (y + SIZE) % SIZE); };
	for (int y = 0; y < SIZE; ++y) {
		for (int x = 0; x < SIZE; ++x) {
			const double dx = (h(x + 1, y) - h(x - 1, y)) * 2.0;
			const double dy = (h(x, y + 1) - h(x, y - 1)) * 2.0;
			double nx = -dx, ny = dy, nz = 1;   // green up (rows go down the picture)
			const double len = std::sqrt(nx * nx + ny * ny + nz * nz);
			nx /= len;
			ny /= len;
			nz /= len;
			setPixel(image, x, y, nx * 0.5 + 0.5, ny * 0.5 + 0.5, nz * 0.5 + 0.5);
		}
	}
	return image;
}

ImageRgba sampleGrate() {
	constexpr int SIZE = 256, CELL = 32, BAR = 7;
	ImageRgba image = blankImage(SIZE, SIZE);
	Random random(31);
	for (int y = 0; y < SIZE; ++y) {
		for (int x = 0; x < SIZE; ++x) {
			const int u = x % CELL, v = y % CELL;
			const bool frame = x < 8 || y < 8 || x >= SIZE - 8 || y >= SIZE - 8;
			const bool bar = u < BAR || v < BAR || frame;
			if (!bar) {
				setPixel(image, x, y, 0, 0, 0, 0);   // a hole: see-through
				continue;
			}
			// Dark steel with rust patches.
			const double rust = std::clamp(cellHash(x / 16, y / 16, 5) * 1.4 - 0.5, 0.0, 1.0);
			const double grain = 0.08 * random.signedNext();
			const double edge = (u == 0 || v == 0) ? 0.08 : 0.0;
			setPixel(image, x, y, 0.30 + 0.30 * rust + grain + edge, 0.30 + 0.10 * rust + grain + edge, 0.32 - 0.05 * rust + grain + edge);
		}
	}
	return image;
}

// --- Crate (.obj) -------------------------------------------------------------------------------------

std::string sampleCrateObj() {
	// 0.8 m cube standing on the ground (about 31 Source units), Y up as Blender exports it.
	constexpr double S = 0.4, H = 0.8;
	std::string out = "# Wooden crate - a Mapsmith sample (File > Import into the mod > Model ...)\nmtllib crate.mtl\no crate\n";
	const double px[] = {-S, S, S, -S, -S, S, S, -S}, py[] = {0, 0, 0, 0, H, H, H, H}, pz[] = {S, S, -S, -S, S, S, -S, -S};
	for (int i = 0; i < 8; ++i) out += "v " + num(px[i]) + " " + num(py[i]) + " " + num(pz[i]) + "\n";
	out += "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n";
	out += "vn 0 0 1\nvn 1 0 0\nvn 0 0 -1\nvn -1 0 0\nvn 0 1 0\nvn 0 -1 0\n";
	out += "usemtl crate_wood\n";
	// Faces counter-clockwise from outside: front, right, back, left, top, bottom.
	const int faces[6][4] = {{1, 2, 6, 5}, {2, 3, 7, 6}, {3, 4, 8, 7}, {4, 1, 5, 8}, {5, 6, 7, 8}, {4, 3, 2, 1}};
	for (int f = 0; f < 6; ++f) {
		out += "f";
		for (int k = 0; k < 4; ++k) out += " " + std::to_string(faces[f][k]) + "/" + std::to_string(k + 1) + "/" + std::to_string(f + 1);
		out += "\n";
	}
	return out;
}

std::string sampleCrateMtl() {
	return "# Material of the sample crate\nnewmtl crate_wood\nKd 0.6 0.45 0.3\nmap_Kd crate.png\n";
}

ImageRgba sampleCrateTexture() {
	constexpr int SIZE = 256, BORDER = 26;
	ImageRgba image = blankImage(SIZE, SIZE);
	Random random(41);
	for (int y = 0; y < SIZE; ++y) {
		for (int x = 0; x < SIZE; ++x) {
			const bool border = x < BORDER || y < BORDER || x >= SIZE - BORDER || y >= SIZE - BORDER;
			// Planks run across, the frame boards along the edges; grain stripes and dark gaps.
			const int plank = border ? 0 : (y - BORDER) / 34;
			const bool gap = !border && (y - BORDER) % 34 < 2;
			const bool seam = x == BORDER || y == BORDER || x == SIZE - BORDER - 1 || y == SIZE - BORDER - 1;
			const double along = border && (x < BORDER || x >= SIZE - BORDER) ? double(y) : double(x);
			const double grain = 0.05 * std::sin(along * 0.35 + cellHash(plank, 0, 2) * 20) + 0.04 * random.signedNext();
			const double shade = border ? 0.92 : 0.82 + 0.14 * cellHash(plank, 1, 3);
			double r = 0.62 * shade + grain, g = 0.44 * shade + grain * 0.8, b = 0.26 * shade + grain * 0.5;
			if (gap || seam) {
				r *= 0.45;
				g *= 0.45;
				b *= 0.45;
			}
			// Nails at the frame's corners.
			for (const int cx : {BORDER / 2, SIZE - BORDER / 2}) {
				for (const int cy : {BORDER / 2, SIZE - BORDER / 2}) {
					if ((x - cx) * (x - cx) + (y - cy) * (y - cy) < 12) r = g = b = 0.25;
				}
			}
			setPixel(image, x, y, r, g, b);
		}
	}
	return image;
}

// --- Hanging sign (rigged .smd) -----------------------------------------------------------------------

std::string sampleSignSmd() {
	std::string out = "// Hanging sign - a Mapsmith sample: a rigged reference .smd (bones \"bracket\" and \"board\").\n";
	out += "// Import it on the Models page (Kind: dynamic prop), then add hanging_sign_swing.smd on the Animations page.\n";
	out += signNodes() + "skeleton\n" + signFrame(0, 0, 0) + "end\ntriangles\n";
	addBox(out, "hanging_sign", {-20, -1.5, SIGN_HINGE + 1}, {20, 1.5, SIGN_HINGE + 4}, 0);   // the bracket
	addBox(out, "hanging_sign", {-1, -1, SIGN_HINGE - 1}, {1, 1, SIGN_HINGE + 1}, 1);          // the hinge pin
	addBox(out, "hanging_sign", {-16, -1, SIGN_HINGE - 26}, {16, 1, SIGN_HINGE - 2}, 1);       // the board
	out += "end\n";
	return out;
}

std::string sampleSignSwingSmd() {
	constexpr int FRAMES = 48;   // 2 seconds at 24 fps, a loop
	std::string out = "// The sign swings in the wind - animation for hanging_sign.smd (same bones).\n";
	out += signNodes() + "skeleton\n";
	for (int f = 0; f < FRAMES; ++f) out += signFrame(f, 0.3 * std::sin(TAU * f / FRAMES), 0);
	out += "end\n";
	return out;
}

std::string sampleSignSpinSmd() {
	constexpr int FRAMES = 60;
	std::string out = "// The board turns once around its hinge - animation for hanging_sign.smd (same bones).\n";
	out += signNodes() + "skeleton\n";
	for (int f = 0; f < FRAMES; ++f) out += signFrame(f, 0, TAU * f / FRAMES);
	out += "end\n";
	return out;
}

ImageRgba sampleSignTexture() {
	constexpr int SIZE = 256;
	ImageRgba image = blankImage(SIZE, SIZE);
	Random random(51);
	for (int y = 0; y < SIZE; ++y) {
		for (int x = 0; x < SIZE; ++x) {
			const double grain = 0.04 * random.signedNext();
			// Painted board: a dark green field, a cream frame and a cream arrow pointing right.
			const bool frame = x < 18 || y < 18 || x >= SIZE - 18 || y >= SIZE - 18;
			const bool inner = x >= 24 && y >= 24 && x < SIZE - 24 && y < SIZE - 24;
			const int ax = x - 60, ay = y - SIZE / 2;
			const bool shaft = ax >= 0 && ax < 90 && std::abs(ay) < 14;
			const bool head = ax >= 90 && ax < 140 && std::abs(ay) < (140 - ax);
			const bool cream = frame || (inner && (shaft || head));
			if (cream) setPixel(image, x, y, 0.90 + grain, 0.85 + grain, 0.70 + grain);
			else setPixel(image, x, y, 0.10 + grain, 0.32 + grain, 0.20 + grain);
		}
	}
	return image;
}

// --- HL2 people --------------------------------------------------------------------------------------

std::string sampleHumanNodSmd() {
	return humanAnimation("A nod: the head and neck bend forward and back twice (loop, 30 fps).",
	                      {"ValveBiped.Bip01_Neck1", "ValveBiped.Bip01_Head1"}, 40, [](std::size_t bone, double t) {
		                      const double bend = std::sin(TAU * 2 * t) * (bone == 0 ? 0.12 : 0.3);
		                      return std::array<double, 3>{0, 0, bend};
	                      });
}

std::string sampleHumanWaveSmd() {
	return humanAnimation("A wave: the right arm goes up and the hand waves (30 fps).",
	                      {"ValveBiped.Bip01_R_UpperArm", "ValveBiped.Bip01_R_Forearm", "ValveBiped.Bip01_R_Hand"}, 60, [](std::size_t bone, double t) {
		                      // Raise during the first quarter, wave, lower during the last quarter.
		                      const double up = std::clamp(std::min(t, 1 - t) * 4, 0.0, 1.0);
		                      const double raise = up * up * (3 - 2 * up);
		                      const double wave = std::sin(TAU * 3 * t) * raise;
		                      if (bone == 0) return std::array<double, 3>{0, -1.2 * raise, -0.4 * raise};
		                      if (bone == 1) return std::array<double, 3>{0, 0, -1.1 * raise + 0.35 * wave};
		                      return std::array<double, 3>{0.4 * wave, 0, 0};
	                      });
}

// --- Particles ---------------------------------------------------------------------------------------

std::vector<std::byte> sampleParticleFile() {
	PcfSystem sparks;
	sparks.name = "mapsmith_sample_sparks";
	sparks.properties = {{"material", std::string("effects/spark.vmt")},
	                     {"max_particles", 200},
	                     {"initial_particles", 0},
	                     {"radius", 2.0f},
	                     {"color", PcfColor{255, 200, 120, 255}}};
	sparks.emitters = {function("emit_continuously", {{"emission_rate", 40.0f}, {"emission_duration", 0.0f}, {"emission_start_time", 0.0f}})};
	sparks.initializers = {
		function("Position Within Sphere Random",
	             {{"distance_min", 0.0f},
	              {"distance_max", 2.0f},
	              {"speed_min", 60.0f},
	              {"speed_max", 160.0f},
	              {"speed_in_local_coordinate_system_min", Vec3{-30, -30, 80}},
	              {"speed_in_local_coordinate_system_max", Vec3{30, 30, 160}}}),
		function("Lifetime Random", {{"lifetime_min", 0.4f}, {"lifetime_max", 0.9f}}),
		function("Color Random", {{"color1", PcfColor{255, 230, 160, 255}}, {"color2", PcfColor{255, 150, 60, 255}}}),
		function("Radius Random", {{"radius_min", 0.8f}, {"radius_max", 1.8f}}),
	};
	sparks.operators = {
		function("Movement Basic", {{"gravity", Vec3{0, 0, -500}}, {"drag", 0.02f}}),
		function("Alpha Fade Out Random", {{"fade_out_time_min", 0.3f}, {"fade_out_time_max", 0.6f}}),
		function("Lifespan Decay"),
	};
	sparks.renderers = {function("render_animated_sprites", {{"animation rate", 1.0f}})};

	PcfSystem smoke;
	smoke.name = "mapsmith_sample_smoke";
	smoke.properties = {{"material", std::string("particle/particle_smokegrenade.vmt")},
	                    {"max_particles", 60},
	                    {"initial_particles", 0},
	                    {"radius", 12.0f},
	                    {"color", PcfColor{180, 180, 180, 255}}};
	smoke.emitters = {function("emit_continuously", {{"emission_rate", 6.0f}, {"emission_duration", 0.0f}, {"emission_start_time", 0.0f}})};
	smoke.initializers = {
		function("Position Within Sphere Random",
	             {{"distance_min", 0.0f},
	              {"distance_max", 6.0f},
	              {"speed_min", 0.0f},
	              {"speed_max", 6.0f},
	              {"speed_in_local_coordinate_system_min", Vec3{-4, -4, 24}},
	              {"speed_in_local_coordinate_system_max", Vec3{4, 4, 40}}}),
		function("Lifetime Random", {{"lifetime_min", 3.0f}, {"lifetime_max", 5.0f}}),
		function("Color Random", {{"color1", PcfColor{150, 150, 150, 255}}, {"color2", PcfColor{200, 200, 200, 255}}}),
		function("Radius Random", {{"radius_min", 10.0f}, {"radius_max", 16.0f}}),
		function("Rotation Random", {{"rotation_initial", 0.0f}, {"rotation_offset_min", 0.0f}, {"rotation_offset_max", 360.0f}}),
		function("Alpha Random", {{"alpha_min", 80}, {"alpha_max", 140}}),
	};
	smoke.operators = {
		function("Movement Basic", {{"gravity", Vec3{0, 0, 6}}, {"drag", 0.05f}}),
		function("Radius Scale", {{"radius_start_scale", 0.6f}, {"radius_end_scale", 2.2f}}),
		function("Alpha Fade In Random", {{"fade_in_time_min", 0.2f}, {"fade_in_time_max", 0.4f}}),
		function("Alpha Fade Out Random", {{"fade_out_time_min", 0.4f}, {"fade_out_time_max", 0.7f}}),
		function("Lifespan Decay"),
	};
	smoke.renderers = {function("render_animated_sprites", {{"animation rate", 0.5f}})};
	return writeParticleFile({sparks, smoke});
}

// --- The folder ----------------------------------------------------------------------------------------

std::vector<SampleFile> makeSamples() {
	std::vector<SampleFile> out;
	const auto add = [&out](std::string path, std::vector<std::byte> data) { out.push_back({std::move(path), std::move(data)}); };
	add("sounds/hum_loop.wav", encodeWav(sampleHumLoop()));
	add("sounds/door_chime.flac", encodeFlac(sampleChime()));
	add("pictures/terracotta_tiles.png", encodePng(sampleTiles()));
	add("pictures/terracotta_tiles_normal.png", encodePng(sampleTilesNormal()));
	add("pictures/rusty_grate.png", encodePng(sampleGrate()));
	add("models/crate/crate.obj", bytesOf(sampleCrateObj()));
	add("models/crate/crate.mtl", bytesOf(sampleCrateMtl()));
	add("models/crate/crate.png", encodePng(sampleCrateTexture()));
	add("models/hanging_sign/hanging_sign.smd", bytesOf(sampleSignSmd()));
	add("models/hanging_sign/hanging_sign.png", encodePng(sampleSignTexture()));
	add("animations/hanging_sign_swing.smd", bytesOf(sampleSignSwingSmd()));
	add("animations/hanging_sign_spin.smd", bytesOf(sampleSignSpinSmd()));
	add("animations/hl2_human_nod.smd", bytesOf(sampleHumanNodSmd()));
	add("animations/hl2_human_wave.smd", bytesOf(sampleHumanWaveSmd()));
	add("particles/mapsmith_samples.pcf", sampleParticleFile());
	add("README.txt", bytesOf(samplesReadme()));
	return out;
}

std::vector<SampleFile> sampleMediaSources() {
	return {{"click.wav", encodeWav(sampleClick())}, {"wind_loop.wav", encodeWav(sampleWindLoop())}};
}

std::string samplesReadme() {
	return R"(Mapsmith samples
================

Files to try Mapsmith's importers with. Mapsmith made all of them itself (synthesised sounds, drawn
pictures, modelled meshes) - use them in your own mods as you like.

sounds/       Sounds page > Import ...
  hum_loop.wav          a 4 s seamless loop (hum and wind) - tick "Loops" for an ambient_generic
  wind_loop.ogg         a 3 s wind loop (Ogg Vorbis: the import turns it into a WAV)
  click.mp3             a short click, one shot (MP3)
  door_chime.flac       a two-note chime, stereo (FLAC)

pictures/     Materials page > Import ...
  terracotta_tiles.png          floor tiles; terracotta_tiles_normal.png next to it becomes its bump map
  rusty_grate.png               a grate with see-through holes (alpha: the material gets $alphatest)

models/       Models page > Import ...
  crate/crate.obj               a wooden crate (with crate.mtl and crate.png) - Units: metres, Y is up
  hanging_sign/hanging_sign.smd a shop sign with two bones - Kind: dynamic prop; Units: inches, Y is up: off

animations/   Animations page > Add animation ...
  hanging_sign_swing.smd        the sign swings - for the hanging_sign model imported above
  hanging_sign_spin.smd         the board turns once around its hinge - same model
  hl2_human_nod.smd             a nod for any of Half-Life 2's people (e.g. models/humans/group01/male_07.mdl)
  hl2_human_wave.smd            a wave with the right arm - same

  The hl2_human files move only a few bones, relative to the model's rest pose ("// mapsmith:
  relative" in their header). Mapsmith completes them for the model you pick: the other bones keep
  their rest pose. How exactly the arm moves depends on the model's bones - they are a test, not a
  finished animation.

particles/    Particles page > Import ...
  mapsmith_samples.pcf          two systems: mapsmith_sample_sparks (falling sparks) and
                                mapsmith_sample_smoke (a slow smoke column); place them with an
                                info_particle_system (Start active) after the import
)";
}

bool writeSamples(const fs::path& dir, const fs::path& mediaDir, std::string* error) {
	const auto write = [&](const fs::path& path, const std::vector<std::byte>& data) {
		std::error_code ec;
		fs::create_directories(path.parent_path(), ec);
		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		file.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
		if (!file) {
			if (error) *error = "cannot write " + path.string();
			return false;
		}
		return true;
	};
	for (const auto& sample : makeSamples()) {
		if (!write(dir / fs::path(sample.path), sample.data)) return false;
	}
	std::error_code ec;
	if (!mediaDir.empty() && fs::is_directory(mediaDir, ec)) {
		for (const auto& entry : fs::directory_iterator(mediaDir, ec)) {
			const auto ext = entry.path().extension().string();
			if (ext != ".mp3" && ext != ".ogg") continue;
			fs::create_directories(dir / SAMPLE_SOUNDS, ec);
			fs::copy_file(entry.path(), dir / SAMPLE_SOUNDS / entry.path().filename(), fs::copy_options::overwrite_existing, ec);
			if (ec) {
				if (error) *error = "cannot copy " + entry.path().string();
				return false;
			}
		}
	}
	return true;
}

} // namespace sme
