// anim_edit.cpp - Keyframe editing of a model's animations (see anim_edit.h).

#include <sme/anim_edit.h>

#include <sme/animation_import.h>
#include <sme/keyvalues.h>
#include <sme/model_export.h>
#include <sme/text.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace sme {

namespace {

constexpr std::size_t MAX_UNDO = 200;
constexpr std::size_t MAX_PROJECT_BYTES = 64u << 20;
constexpr std::size_t MAX_NAME = 256;

AnimQuat qmul(const AnimQuat& p, const AnimQuat& q) {
	// Hamilton product p * q: q first, then p (QuaternionMult).
	return {p.x * q.w + p.y * q.z - p.z * q.y + p.w * q.x, -p.x * q.z + p.y * q.w + p.z * q.x + p.w * q.y,
	        p.x * q.y - p.y * q.x + p.z * q.w + p.w * q.z, -p.x * q.x - p.y * q.y - p.z * q.z + p.w * q.w};
}

AnimQuat qconj(const AnimQuat& q) {
	return {-q.x, -q.y, -q.z, q.w};
}

AnimQuat qnorm(const AnimQuat& q) {
	const float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
	if (!(len > 1e-8f) || !std::isfinite(len)) return {};
	return {q.x / len, q.y / len, q.z / len, q.w / len};
}

/// Spherical interpolation the short way round (keys can be far apart, unlike frames).
AnimQuat qslerp(const AnimQuat& a, AnimQuat b, float t) {
	float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
	if (d < 0) {
		b = {-b.x, -b.y, -b.z, -b.w};
		d = -d;
	}
	float wa = 1 - t, wb = t;
	if (d < 0.9995f) {
		const float angle = std::acos(std::clamp(d, -1.f, 1.f));
		const float s = std::sin(angle);
		wa = std::sin((1 - t) * angle) / s;
		wb = std::sin(t * angle) / s;
	}
	return qnorm({a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb});
}

AnimVec qrotate(const AnimQuat& q, const AnimVec& v) {
	const AnimQuat r = qmul(qmul(q, {v.x, v.y, v.z, 0}), qconj(q));
	return {r.x, r.y, r.z};
}

AnimVec add(const AnimVec& a, const AnimVec& b) {
	return {a.x + b.x, a.y + b.y, a.z + b.z};
}
AnimVec sub(const AnimVec& a, const AnimVec& b) {
	return {a.x - b.x, a.y - b.y, a.z - b.z};
}

bool finite(const AnimQuat& q) {
	return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w);
}
bool finite(const AnimVec& v) {
	return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

/// Rotation part of a matrix as a quaternion (MatrixQuaternion).
AnimQuat matrixQuat(const float m[3][4]) {
	const float trace = m[0][0] + m[1][1] + m[2][2];
	AnimQuat q;
	if (trace > 0) {
		const float s = std::sqrt(trace + 1.f) * 2.f;
		q = {(m[2][1] - m[1][2]) / s, (m[0][2] - m[2][0]) / s, (m[1][0] - m[0][1]) / s, 0.25f * s};
	} else if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) {
		const float s = std::sqrt(1.f + m[0][0] - m[1][1] - m[2][2]) * 2.f;
		q = {0.25f * s, (m[0][1] + m[1][0]) / s, (m[0][2] + m[2][0]) / s, (m[2][1] - m[1][2]) / s};
	} else if (m[1][1] > m[2][2]) {
		const float s = std::sqrt(1.f + m[1][1] - m[0][0] - m[2][2]) * 2.f;
		q = {(m[0][1] + m[1][0]) / s, 0.25f * s, (m[1][2] + m[2][1]) / s, (m[0][2] - m[2][0]) / s};
	} else {
		const float s = std::sqrt(1.f + m[2][2] - m[0][0] - m[1][1]) * 2.f;
		q = {(m[0][2] + m[2][0]) / s, (m[1][2] + m[2][1]) / s, 0.25f * s, (m[1][0] - m[0][1]) / s};
	}
	return qnorm(q);
}

/// The model-space rotation of every bone (parents before children, as studiomdl orders them).
std::vector<AnimQuat> modelRotations(const std::vector<StudioBone>& bones, const std::vector<AnimQuat>& local) {
	std::vector<AnimQuat> out(bones.size());
	for (std::size_t i = 0; i < bones.size() && i < local.size(); ++i) {
		const int parent = bones[i].parent;
		out[i] = parent >= 0 && std::size_t(parent) < i ? qnorm(qmul(out[std::size_t(parent)], local[i])) : local[i];
	}
	return out;
}

std::string number(float v) {
	if (!std::isfinite(v)) v = 0;
	if (std::fabs(v) < 1e-7f) v = 0;
	char buffer[32];
	const auto result = std::to_chars(buffer, buffer + sizeof buffer, v, std::chars_format::general, 7);
	return std::string(buffer, result.ptr);
}

bool parseFloat(std::string_view text, float& out) {
	text = trim(text);
	if (text.empty()) return false;
	const auto result = std::from_chars(text.data(), text.data() + text.size(), out);
	return result.ec == std::errc() && std::isfinite(out);
}

bool parseInt(std::string_view text, int& out) {
	text = trim(text);
	if (!text.empty() && text.front() == '+') text.remove_prefix(1);
	const auto result = std::from_chars(text.data(), text.data() + text.size(), out);
	return result.ec == std::errc() && result.ptr == text.data() + text.size();
}

std::vector<std::string_view> words(std::string_view text) {
	std::vector<std::string_view> out;
	std::size_t i = 0;
	while (i < text.size()) {
		while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
		const std::size_t start = i;
		while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i]))) ++i;
		if (i > start) out.push_back(text.substr(start, i - start));
	}
	return out;
}

/// A name from a file: printable, no quotes or control characters, limited length.
std::string cleanName(std::string_view text) {
	std::string out;
	for (const char c : text) {
		const auto u = static_cast<unsigned char>(c);
		if (u < 32 || u == 127 || c == '"' || c == '{' || c == '}') continue;
		out.push_back(c);
		if (out.size() >= MAX_NAME) break;
	}
	return std::string(trim(out));
}

AnimVec scale(const AnimVec& a, float s) {
	return {a.x * s, a.y * s, a.z * s};
}
float dot(const AnimVec& a, const AnimVec& b) {
	return a.x * b.x + a.y * b.y + a.z * b.z;
}
AnimVec cross(const AnimVec& a, const AnimVec& b) {
	return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
float length(const AnimVec& a) {
	return std::sqrt(dot(a, a));
}
AnimVec normalized(const AnimVec& a) {
	const float l = length(a);
	return l > 1e-6f ? scale(a, 1.f / l) : AnimVec{};
}

/// The shortest rotation that turns direction a into direction b.
AnimQuat rotationBetween(const AnimVec& from, const AnimVec& to) {
	const AnimVec a = normalized(from), b = normalized(to);
	const float d = dot(a, b);
	if (d > 0.999999f) return {};
	if (d < -0.999999f) {
		// Half a turn about any axis across a.
		AnimVec axis = cross(a, {1, 0, 0});
		if (length(axis) < 1e-3f) axis = cross(a, {0, 1, 0});
		axis = normalized(axis);
		return {axis.x, axis.y, axis.z, 0};
	}
	const AnimVec c = cross(a, b);
	return qnorm({c.x, c.y, c.z, 1.f + d});
}

AnimVec translation(const BoneMatrix& m) {
	return {m.m[0][3], m.m[1][3], m.m[2][3]};
}

AnimQuat axisAngle(const AnimVec& axis, float radians) {
	const AnimVec a = normalized(axis);
	if (length(a) < 0.5f || !std::isfinite(radians)) return {};
	const float s = std::sin(radians * 0.5f);
	return qnorm({a.x * s, a.y * s, a.z * s, std::cos(radians * 0.5f)});
}

constexpr float DEG = 3.14159265f / 180.f;

bool isSideToken(std::string_view token) {
	return token == "L" || token == "R" || token == "l" || token == "r" || startsWithI(token, "left") || startsWithI(token, "right");
}

} // namespace

// --- Interpolation ---------------------------------------------------------------------------

float easeKey(KeyInterp interp, float t) {
	t = std::clamp(t, 0.f, 1.f);
	switch (interp) {
	case KeyInterp::Linear: return t;
	case KeyInterp::Step: return t >= 1.f ? 1.f : 0.f;
	case KeyInterp::EaseIn: return t * t;
	case KeyInterp::EaseOut: return 1.f - (1.f - t) * (1.f - t);
	case KeyInterp::Smooth: break;
	}
	return t * t * (3.f - 2.f * t);
}

const char* keyInterpWord(KeyInterp interp) {
	switch (interp) {
	case KeyInterp::Linear: return "linear";
	case KeyInterp::Step: return "step";
	case KeyInterp::EaseIn: return "easein";
	case KeyInterp::EaseOut: return "easeout";
	case KeyInterp::Smooth: break;
	}
	return "smooth";
}

KeyInterp keyInterpFromWord(std::string_view word) {
	for (const KeyInterp k : {KeyInterp::Linear, KeyInterp::Step, KeyInterp::EaseIn, KeyInterp::EaseOut}) {
		if (iequals(word, keyInterpWord(k))) return k;
	}
	return KeyInterp::Smooth;
}

// --- Bone names ------------------------------------------------------------------------------

std::string mirroredBoneName(std::string_view name) {
	// Tokens between delimiters: "ValveBiped.Bip01_L_Hand" -> ValveBiped, Bip01, L, Hand.
	std::size_t start = 0;
	for (std::size_t i = 0; i <= name.size(); ++i) {
		const bool delimiter = i == name.size() || name[i] == '.' || name[i] == '_' || name[i] == ' ' || name[i] == '-' || name[i] == ':';
		if (!delimiter) continue;
		const std::string_view token = name.substr(start, i - start);
		if (!token.empty() && isSideToken(token)) {
			std::string replacement;
			if (token.size() == 1) {
				const char c = token[0];
				replacement = c == 'L' ? "R" : c == 'R' ? "L" : c == 'l' ? "r" : "l";
			} else {
				const bool left = startsWithI(token, "left");
				const std::size_t len = left ? 4 : 5;
				std::string word(token.substr(0, len));
				const bool upper = std::isupper(static_cast<unsigned char>(word[1]));
				const bool capital = std::isupper(static_cast<unsigned char>(word[0]));
				std::string other = left ? "right" : "left";
				if (upper) std::transform(other.begin(), other.end(), other.begin(), [](unsigned char ch) { return char(std::toupper(ch)); });
				else if (capital) other[0] = char(std::toupper(static_cast<unsigned char>(other[0])));
				// "Leftover" is no side: only whole words or a capital after ("LeftArm").
				const std::string_view rest = token.substr(len);
				if (!rest.empty() && !std::isupper(static_cast<unsigned char>(rest[0])) && !std::isdigit(static_cast<unsigned char>(rest[0]))) {
					start = i + 1;
					continue;
				}
				replacement = other + std::string(rest);
			}
			return std::string(name.substr(0, start)) + replacement + std::string(name.substr(i));
		}
		start = i + 1;
	}
	return {};
}

std::string boneGroup(std::string_view boneName) {
	const std::string lower = toLower(boneName);
	const std::string mirrored = mirroredBoneName(boneName);
	std::string side;
	if (!mirrored.empty()) {
		// Which side this one is: the token that was replaced.
		std::size_t i = 0;
		while (i < boneName.size() && i < mirrored.size() && boneName[i] == mirrored[i]) ++i;
		const char c = i < boneName.size() ? char(std::tolower(static_cast<unsigned char>(boneName[i]))) : 0;
		side = c == 'l' ? "Left " : "Right ";
	}
	const auto has = [&](std::initializer_list<const char*> parts) {
		return std::any_of(parts.begin(), parts.end(), [&](const char* p) { return lower.find(p) != std::string::npos; });
	};
	if (has({"finger", "thumb", "index", "middle", "ring", "pinky", "digit"})) return side + "fingers";
	if (has({"hand", "wrist", "palm"})) return side + "hand";
	if (has({"arm", "clavicle", "shoulder", "elbow", "bicep", "ulna", "trapezius"})) return side + "arm";
	if (has({"thigh", "calf", "foot", "toe", "leg", "knee", "shin"})) return side + "leg";
	if (has({"spine", "pelvis", "neck", "head", "chest", "hips", "jaw", "eye"})) return "Spine and head";
	if (has({"weapon", "gun", "bolt", "clip", "mag", "slide", "trigger", "barrel", "hammer", "muzzle", "pump", "stock", "sight", "grenade", "bullet", "shell"}))
		return "Weapon";
	if (!side.empty()) return side + "side";
	return {};
}

// --- AnimEditor ------------------------------------------------------------------------------

AnimEditor::AnimEditor(const AnimatedModel& model, AnimClip clip) : m_model(&model), m_clip(std::move(clip)) {
	m_clip.frames = std::clamp(m_clip.frames, 1, ANIM_CLIP_MAX_FRAMES);
	if (!(m_clip.fps > 0.f) || !std::isfinite(m_clip.fps)) m_clip.fps = 30;
	m_clip.editFrom = std::clamp(m_clip.editFrom, 0, m_clip.frames - 1);
	if (!std::isfinite(m_clip.baseStart) || !std::isfinite(m_clip.baseEnd)) m_clip.baseStart = 0, m_clip.baseEnd = 1;
	m_clip.baseStart = std::clamp(m_clip.baseStart, 0.f, 1.f);
	m_clip.baseEnd = std::clamp(m_clip.baseEnd, m_clip.baseStart, 1.f);
	if (m_clip.mode == AnimClip::Mode::Pose) m_clip.editFrom = 0;
	m_base = m_clip.baseSequence.empty() ? -1 : model.findSequence(m_clip.baseSequence);
	findChains();
	m_hands = findHands(model.bones());
	// Keys of bones the model lacks stay in the clip (saved again), but play no part.
	for (auto& [bone, keys] : m_clip.tracks) {
		std::erase_if(keys, [&](const BoneKey& k) { return k.frame < m_clip.editFrom || k.frame >= m_clip.frames; });
		std::sort(keys.begin(), keys.end(), [](const BoneKey& a, const BoneKey& b) { return a.frame < b.frame; });
		keys.erase(std::unique(keys.begin(), keys.end(), [](const BoneKey& a, const BoneKey& b) { return a.frame == b.frame; }), keys.end());
	}
}

int AnimEditor::boneIndex(std::string_view name) const {
	const auto& b = bones();
	for (std::size_t i = 0; i < b.size(); ++i) {
		if (iequals(b[i].name, name)) return int(i);
	}
	return -1;
}

std::vector<BoneKey>* AnimEditor::track(int bone, bool create) {
	if (bone < 0 || bone >= int(bones().size())) return nullptr;
	const std::string& name = bones()[std::size_t(bone)].name;
	auto it = m_clip.tracks.find(name);
	if (it == m_clip.tracks.end()) {
		// Same bone spelled differently in a file.
		for (auto i = m_clip.tracks.begin(); i != m_clip.tracks.end(); ++i) {
			if (iequals(i->first, name)) return &i->second;
		}
		if (!create) return nullptr;
		it = m_clip.tracks.emplace(name, std::vector<BoneKey>{}).first;
	}
	return &it->second;
}

const std::vector<BoneKey>* AnimEditor::track(int bone) const {
	return const_cast<AnimEditor*>(this)->track(bone, false);
}

std::vector<int> AnimEditor::orAll(const std::vector<int>& list) const {
	if (!list.empty()) return list;
	std::vector<int> all(bones().size());
	for (std::size_t i = 0; i < all.size(); ++i) all[i] = int(i);
	return all;
}

void AnimEditor::basePose(float frame, std::vector<AnimVec>& pos, std::vector<AnimQuat>& quat) const {
	if (m_base < 0) {
		m_model->localPose(-1, 0.f, pos, quat, false);
		return;
	}
	float cycle = 0;
	if (m_clip.mode != AnimClip::Mode::New) {
		const float t = m_clip.frames > 1 ? std::clamp(frame, 0.f, float(m_clip.frames - 1)) / float(m_clip.frames - 1) : 0.f;
		cycle = m_clip.baseStart + t * (m_clip.baseEnd - m_clip.baseStart);
	} else {
		const int baseFrames = m_model->sequences()[std::size_t(m_base)].frames;
		cycle = baseFrames > 1 ? float(std::clamp(m_clip.baseFrame, 0, baseFrames - 1)) / float(baseFrames - 1) : 0.f;
	}
	m_model->localPose(m_base, cycle, pos, quat, false);
}

std::pair<AnimQuat, AnimVec> AnimEditor::offset(int bone, float frame) const {
	const auto* keys = track(bone);
	if (keys && !keys->empty() && m_clip.mode == AnimClip::Mode::Pose) return {keys->front().rot, keys->front().pos};   // one change for all frames
	if (!keys || keys->empty() || frame < float(m_clip.editFrom)) return {{}, {}};
	const auto& k = *keys;
	if (frame <= float(k.front().frame)) return {k.front().rot, k.front().pos};
	if (frame >= float(k.back().frame)) return {k.back().rot, k.back().pos};
	const auto next = std::upper_bound(k.begin(), k.end(), frame, [](float f, const BoneKey& key) { return f < float(key.frame); });
	const BoneKey& b = *next;
	const BoneKey& a = *(next - 1);
	const float t = easeKey(a.interp, (frame - float(a.frame)) / float(std::max(1, b.frame - a.frame)));
	return {qslerp(a.rot, b.rot, t), {a.pos.x + (b.pos.x - a.pos.x) * t, a.pos.y + (b.pos.y - a.pos.y) * t, a.pos.z + (b.pos.z - a.pos.z) * t}};
}

void AnimEditor::pose(float frame, std::vector<AnimVec>& pos, std::vector<AnimQuat>& quat) const {
	basePose(frame, pos, quat);
	for (std::size_t i = 0; i < pos.size() && i < quat.size(); ++i) {
		if (!track(int(i))) continue;
		const auto [r, p] = offset(int(i), frame);
		quat[i] = qnorm(qmul(quat[i], r));
		pos[i] = add(pos[i], p);
	}
}

std::vector<BoneMatrix> AnimEditor::boneToModel(float frame) const {
	std::vector<AnimVec> pos;
	std::vector<AnimQuat> quat;
	pose(frame, pos, quat);
	const auto& own = bones();
	std::vector<BoneMatrix> world(own.size());
	for (std::size_t i = 0; i < own.size() && i < pos.size(); ++i) {
		const BoneMatrix local = quaternionMatrix(quat[i], pos[i]);
		world[i] = own[i].parent >= 0 && std::size_t(own[i].parent) < i ? concat(world[std::size_t(own[i].parent)], local) : local;
	}
	return world;
}

std::vector<BoneMatrix> AnimEditor::skinning(float frame) const {
	auto world = boneToModel(frame);
	const auto& own = bones();
	for (std::size_t i = 0; i < own.size() && i < world.size(); ++i) world[i] = concat(world[i], own[i].poseToBone);
	return world;
}

bool AnimEditor::hasKey(int bone, int frame) const {
	const auto* keys = track(bone);
	return keys && std::any_of(keys->begin(), keys->end(), [frame](const BoneKey& k) { return k.frame == frame; });
}

std::vector<int> AnimEditor::keyFrames(const std::vector<int>& list) const {
	std::vector<int> out;
	const auto addTrack = [&out](const std::vector<BoneKey>& keys) {
		for (const auto& k : keys) out.push_back(k.frame);
	};
	if (list.empty()) {
		for (const auto& [name, keys] : m_clip.tracks) addTrack(keys);
	} else {
		for (const int bone : list) {
			if (const auto* keys = track(bone)) addTrack(*keys);
		}
	}
	std::sort(out.begin(), out.end());
	out.erase(std::unique(out.begin(), out.end()), out.end());
	return out;
}

int AnimEditor::keyCount() const {
	int count = 0;
	for (const auto& [name, keys] : m_clip.tracks) count += int(keys.size());
	return count;
}

void AnimEditor::changed() {
	++m_revision;
	if (m_changeDepth > 0) return;
	m_undo.push_back(m_clip);
	if (m_undo.size() > MAX_UNDO) m_undo.erase(m_undo.begin());
	m_redo.clear();
}

void AnimEditor::putKey(int bone, BoneKey key) {
	key.frame = m_clip.mode == AnimClip::Mode::Pose ? 0 : std::clamp(key.frame, m_clip.editFrom, m_clip.frames - 1);
	key.rot = finite(key.rot) ? qnorm(key.rot) : AnimQuat{};
	if (!finite(key.pos)) key.pos = {};
	auto* keys = track(bone, true);
	if (!keys) return;
	// A bone's first key: an anchor (no change) at the edit-from frame, so the motion blends
	// from the original into the change instead of jumping there from the start.
	if (keys->empty() && key.frame > m_clip.editFrom && m_clip.mode != AnimClip::Mode::Pose) keys->push_back({m_clip.editFrom, {}, {}, KeyInterp::Smooth});
	const auto it = std::lower_bound(keys->begin(), keys->end(), key.frame, [](const BoneKey& k, int f) { return k.frame < f; });
	if (it != keys->end() && it->frame == key.frame) {
		it->rot = key.rot;
		it->pos = key.pos;
		return;
	}
	if (keyCount() >= ANIM_CLIP_MAX_KEYS) return;
	keys->insert(it, key);
}

void AnimEditor::keyBones(const std::vector<int>& list, int frame) {
	if (list.empty()) return;
	changed();
	for (const int bone : list) {
		const auto [r, p] = offset(bone, float(frame));
		putKey(bone, {frame, r, p, KeyInterp::Smooth});
	}
}

void AnimEditor::setKey(int bone, int frame, const AnimQuat& rot, const AnimVec& pos) {
	if (bone < 0 || bone >= int(bones().size())) return;
	changed();
	putKey(bone, {frame, rot, pos, KeyInterp::Smooth});
}

void AnimEditor::rotateBone(int bone, int frame, const AnimQuat& modelRotation) {
	if (bone < 0 || bone >= int(bones().size()) || !finite(modelRotation)) return;
	std::vector<AnimVec> pos, basePos;
	std::vector<AnimQuat> quat, baseQuat;
	pose(float(frame), pos, quat);
	basePose(float(frame), basePos, baseQuat);
	if (std::size_t(bone) >= quat.size() || std::size_t(bone) >= baseQuat.size()) return;
	const auto model = modelRotations(bones(), quat);
	const int parent = bones()[std::size_t(bone)].parent;
	const AnimQuat p = parent >= 0 && parent < bone ? model[std::size_t(parent)] : AnimQuat{};
	// Model space M = P * L; wanted R * M, so the new local rotation is P^-1 * R * P * L, and the
	// offset on the base B is B^-1 * that.
	const AnimQuat local = qnorm(qmul(qmul(qmul(qconj(p), qnorm(modelRotation)), p), quat[std::size_t(bone)]));
	const AnimQuat off = qnorm(qmul(qconj(baseQuat[std::size_t(bone)]), local));
	changed();
	putKey(bone, {frame, off, offset(bone, float(frame)).second, KeyInterp::Smooth});
}

void AnimEditor::moveBone(int bone, int frame, const AnimVec& modelOffset) {
	if (bone < 0 || bone >= int(bones().size()) || !finite(modelOffset)) return;
	std::vector<AnimVec> pos;
	std::vector<AnimQuat> quat;
	pose(float(frame), pos, quat);
	const auto model = modelRotations(bones(), quat);
	const int parent = bones()[std::size_t(bone)].parent;
	const AnimQuat p = parent >= 0 && parent < bone ? model[std::size_t(parent)] : AnimQuat{};
	const auto [r, current] = offset(bone, float(frame));
	changed();
	putKey(bone, {frame, r, add(current, qrotate(qconj(p), modelOffset)), KeyInterp::Smooth});
}

bool AnimEditor::deleteKeys(const std::vector<int>& list, int frame) {
	const auto targets = orAll(list);
	if (std::none_of(targets.begin(), targets.end(), [&](int b) { return hasKey(b, frame); })) return false;
	changed();
	for (const int bone : targets) {
		if (auto* keys = track(bone, false)) std::erase_if(*keys, [frame](const BoneKey& k) { return k.frame == frame; });
	}
	std::erase_if(m_clip.tracks, [](const auto& entry) { return entry.second.empty(); });
	return true;
}

bool AnimEditor::moveKeys(const std::vector<int>& list, int from, int to, bool copy) {
	to = std::clamp(to, m_clip.editFrom, m_clip.frames - 1);
	if (from == to) return false;
	const auto targets = orAll(list);
	if (std::none_of(targets.begin(), targets.end(), [&](int b) { return hasKey(b, from); })) return false;
	changed();
	for (const int bone : targets) {
		auto* keys = track(bone, false);
		if (!keys) continue;
		const auto it = std::find_if(keys->begin(), keys->end(), [from](const BoneKey& k) { return k.frame == from; });
		if (it == keys->end()) continue;
		BoneKey key = *it;
		if (!copy) keys->erase(it);
		std::erase_if(*keys, [to](const BoneKey& k) { return k.frame == to; });
		key.frame = to;
		keys->insert(std::lower_bound(keys->begin(), keys->end(), to, [](const BoneKey& k, int f) { return k.frame < f; }), key);
	}
	return true;
}

void AnimEditor::setInterp(const std::vector<int>& list, int frame, KeyInterp interp) {
	const auto targets = orAll(list);
	bool any = false;
	for (const int bone : targets) {
		const auto* keys = track(bone);
		if (keys && std::any_of(keys->begin(), keys->end(), [&](const BoneKey& k) { return k.frame == frame && k.interp != interp; })) any = true;
	}
	if (!any) return;
	changed();
	for (const int bone : targets) {
		if (auto* keys = track(bone, false)) {
			for (auto& k : *keys) {
				if (k.frame == frame) k.interp = interp;
			}
		}
	}
}

void AnimEditor::resetBones(const std::vector<int>& list, int frame) {
	if (list.empty()) return;
	changed();
	for (const int bone : list) putKey(bone, {frame, {}, {}, KeyInterp::Smooth});
}

void AnimEditor::clearBones(const std::vector<int>& list) {
	if (std::none_of(list.begin(), list.end(), [&](int b) { return track(b) != nullptr; })) return;
	changed();
	for (const int bone : list) {
		if (bone < 0 || bone >= int(bones().size())) continue;
		const std::string& name = bones()[std::size_t(bone)].name;
		std::erase_if(m_clip.tracks, [&](const auto& entry) { return iequals(entry.first, name); });
	}
}

ClipPose AnimEditor::copyPose(const std::vector<int>& list, int frame) const {
	ClipPose out;
	for (const int bone : orAll(list)) {
		if (bone < 0 || bone >= int(bones().size())) continue;
		out.bones[bones()[std::size_t(bone)].name] = offset(bone, float(frame));
	}
	return out;
}

void AnimEditor::pastePose(const ClipPose& pose, int frame) {
	if (pose.bones.empty()) return;
	changed();
	for (const auto& [name, value] : pose.bones) {
		const int bone = boneIndex(name);
		if (bone >= 0) putKey(bone, {frame, value.first, value.second, KeyInterp::Smooth});
	}
}

int AnimEditor::mirrorPose(const std::vector<int>& list, int frame) {
	const auto& own = bones();
	const auto targets = orAll(list);
	if (own.empty() || targets.empty()) return 0;
	std::vector<AnimVec> pos, basePos;
	std::vector<AnimQuat> quat, baseQuat;
	pose(float(frame), pos, quat);
	basePose(float(frame), basePos, baseQuat);
	if (pos.size() != own.size() || basePos.size() != own.size()) return 0;
	// Model-space matrices now and at rest.
	std::vector<BoneMatrix> now(own.size()), rest(own.size());
	for (std::size_t i = 0; i < own.size(); ++i) {
		const int parent = own[i].parent;
		const BoneMatrix local = quaternionMatrix(quat[i], pos[i]);
		now[i] = parent >= 0 && std::size_t(parent) < i ? concat(now[std::size_t(parent)], local) : local;
		rest[i] = invertRigid(own[i].poseToBone);
	}
	// Target of bone b from its partner p: rotation S * Rp * Rp_rest^T * S * Rb_rest (S flips Y),
	// so a bone at rest stays at rest; position S * tp.
	std::vector<bool> wanted(own.size(), false);
	for (const int b : targets) {
		if (b >= 0 && b < int(own.size())) wanted[std::size_t(b)] = true;
	}
	std::vector<BoneMatrix> target(own.size());
	for (std::size_t b = 0; b < own.size(); ++b) {
		if (!wanted[b]) continue;
		int p = int(b);
		if (const std::string other = mirroredBoneName(own[b].name); !other.empty()) {
			if (const int found = boneIndex(other); found >= 0) p = found;
		}
		const auto& rp = now[std::size_t(p)].m;
		const auto& rpRest = rest[std::size_t(p)].m;
		const auto& rbRest = rest[b].m;
		float a[3][3], c[3][3];
		// a = S * Rp * Rp_rest^T * S
		for (int r = 0; r < 3; ++r) {
			for (int col = 0; col < 3; ++col) {
				float v = 0;
				for (int k = 0; k < 3; ++k) v += rp[r][k] * rpRest[col][k];
				a[r][col] = v * (r == 1 ? -1.f : 1.f) * (col == 1 ? -1.f : 1.f);
			}
		}
		for (int r = 0; r < 3; ++r) {
			for (int col = 0; col < 3; ++col) {
				float v = 0;
				for (int k = 0; k < 3; ++k) v += a[r][k] * rbRest[k][col];
				c[r][col] = v;
			}
		}
		BoneMatrix t;
		for (int r = 0; r < 3; ++r) {
			for (int col = 0; col < 3; ++col) t.m[r][col] = c[r][col];
		}
		t.m[0][3] = rp[0][3];
		t.m[1][3] = -rp[1][3];
		t.m[2][3] = rp[2][3];
		target[b] = t;
	}
	// Back to offsets, parents first (their new placement moves the children).
	changed();
	int count = 0;
	std::vector<BoneMatrix> placed = now;
	for (std::size_t b = 0; b < own.size(); ++b) {
		const int parent = own[b].parent;
		if (wanted[b]) {
			const BoneMatrix local = parent >= 0 && std::size_t(parent) < b ? concat(invertRigid(placed[std::size_t(parent)]), target[b]) : target[b];
			const AnimQuat localQuat = matrixQuat(local.m);
			const AnimVec localPos{local.m[0][3], local.m[1][3], local.m[2][3]};
			const AnimQuat off = qnorm(qmul(qconj(baseQuat[b]), localQuat));
			const AnimVec offPos = sub(localPos, basePos[b]);
			const auto [oldRot, oldPos] = offset(int(b), float(frame));
			const float dot = std::fabs(oldRot.x * off.x + oldRot.y * off.y + oldRot.z * off.z + oldRot.w * off.w);
			const AnimVec moved = sub(offPos, oldPos);
			if (dot < 0.99999f || std::fabs(moved.x) + std::fabs(moved.y) + std::fabs(moved.z) > 1e-3f) ++count;
			putKey(int(b), {frame, off, offPos, KeyInterp::Smooth});
			placed[b] = parent >= 0 && std::size_t(parent) < b ? concat(placed[std::size_t(parent)], local) : local;
		} else {
			placed[b] = parent >= 0 && std::size_t(parent) < b ? concat(placed[std::size_t(parent)], quaternionMatrix(quat[b], pos[b])) : now[b];
		}
	}
	return count;
}

void AnimEditor::setFrameCount(int frames) {
	frames = std::clamp(frames, 1, ANIM_CLIP_MAX_FRAMES);
	if (frames == m_clip.frames || m_clip.mode == AnimClip::Mode::Pose) return;   // a layer has no length of its own
	changed();
	const int old = m_clip.frames;
	for (auto& [name, keys] : m_clip.tracks) {
		std::vector<BoneKey> moved;
		for (BoneKey k : keys) {
			k.frame = old > 1 ? int(std::lround(double(k.frame) * double(frames - 1) / double(old - 1))) : k.frame;
			k.frame = std::clamp(k.frame, 0, frames - 1);
			if (!moved.empty() && moved.back().frame == k.frame) moved.back() = k;   // the later one wins
			else moved.push_back(k);
		}
		keys = std::move(moved);
	}
	if (old > 1) m_clip.editFrom = int(std::lround(double(m_clip.editFrom) * double(frames - 1) / double(old - 1)));
	m_clip.editFrom = std::clamp(m_clip.editFrom, 0, frames - 1);
	m_clip.frames = frames;
}

bool AnimEditor::trim(int first, int last) {
	first = std::clamp(first, 0, m_clip.frames - 1);
	last = std::clamp(last, first, m_clip.frames - 1);
	if ((first == 0 && last == m_clip.frames - 1) || m_clip.mode == AnimClip::Mode::Pose) return false;
	changed();
	const int old = m_clip.frames;
	if (m_clip.mode == AnimClip::Mode::Tweak && old > 1) {
		const float span = m_clip.baseEnd - m_clip.baseStart;
		const float start = m_clip.baseStart + span * float(first) / float(old - 1);
		const float end = m_clip.baseStart + span * float(last) / float(old - 1);
		m_clip.baseStart = start;
		m_clip.baseEnd = end;
	} else if (m_clip.mode == AnimClip::Mode::New) {
		m_clip.baseFrame = std::max(0, m_clip.baseFrame);
	}
	for (auto& [name, keys] : m_clip.tracks) {
		std::erase_if(keys, [&](const BoneKey& k) { return k.frame < first || k.frame > last; });
		for (auto& k : keys) k.frame -= first;
	}
	std::erase_if(m_clip.tracks, [](const auto& entry) { return entry.second.empty(); });
	m_clip.frames = last - first + 1;
	m_clip.editFrom = std::clamp(m_clip.editFrom - first, 0, m_clip.frames - 1);
	return true;
}

void AnimEditor::setEditFrom(int frame) {
	frame = std::clamp(frame, 0, m_clip.frames - 1);
	if (frame == m_clip.editFrom || m_clip.mode == AnimClip::Mode::Pose) return;
	changed();
	m_clip.editFrom = frame;
	for (auto& [name, keys] : m_clip.tracks) std::erase_if(keys, [frame](const BoneKey& k) { return k.frame < frame; });
	std::erase_if(m_clip.tracks, [](const auto& entry) { return entry.second.empty(); });
}

// --- Hands and the pose layer ----------------------------------------------------------------

void AnimEditor::setPreviewSequence(const std::string& sequence) {
	const int index = sequence.empty() ? -1 : m_model->findSequence(sequence);
	m_clip.baseSequence = index >= 0 ? m_model->sequences()[std::size_t(index)].name : std::string();
	m_base = index;
	if (m_clip.mode == AnimClip::Mode::Pose) {
		const StudioSequence* seq = index >= 0 ? &m_model->sequences()[std::size_t(index)] : nullptr;
		m_clip.frames = seq ? std::clamp(seq->frames, 1, ANIM_CLIP_MAX_FRAMES) : 1;
		m_clip.fps = seq && seq->fps > 0 ? seq->fps : 30.f;
		m_clip.loop = seq && seq->looping;
		m_clip.baseStart = 0;
		m_clip.baseEnd = 1;
	}
	++m_revision;
}

void AnimEditor::setHandShape(int hand, const HandShape& input, int frame) {
	if (hand < 0 || hand >= int(m_hands.size())) return;
	const HandRig& rig = m_hands[std::size_t(hand)];
	HandShape shape = input;
	shape.curl = std::isfinite(shape.curl) ? std::clamp(shape.curl, -0.25f, 1.f) : 0.f;
	shape.spread = std::isfinite(shape.spread) ? std::clamp(shape.spread, -1.f, 1.f) : 0.f;
	for (auto it = shape.fingers.begin(); it != shape.fingers.end();) {
		if (!std::isfinite(it->second) || std::fabs(it->second) < 1e-4f) {
			it = shape.fingers.erase(it);
		} else {
			it->second = std::clamp(it->second, -1.f, 1.f);
			++it;
		}
	}
	changed();
	// Bend per segment: fingers 70/95/70 degrees, the thumb less (it bends across the palm).
	static constexpr float FINGER_BEND[4] = {70, 95, 70, 60};
	static constexpr float THUMB_BEND[4] = {25, 40, 40, 30};
	for (std::size_t f = 0; f < rig.fingers.size(); ++f) {
		const FingerChain& finger = rig.fingers[f];
		if (finger.segments.empty()) continue;
		const std::string& rootName = bones()[std::size_t(finger.segments.front())].name;
		float curl = shape.curl;
		for (const auto& [name, extra] : shape.fingers) {
			if (iequals(name, rootName)) curl += extra;
		}
		curl = std::clamp(curl, -0.4f, 1.2f);
		for (std::size_t k = 0; k < finger.segments.size(); ++k) {
			const int bone = finger.segments[k];
			const float degrees = (finger.thumb ? THUMB_BEND : FINGER_BEND)[std::min<std::size_t>(k, 3)];
			AnimQuat q = axisAngle(rig.curlAxes[f][k], curl * degrees * DEG);
			if (k == 0 && rig.spreadSign[f] != 0.f) {
				const float spread = shape.spread * rig.spreadSign[f] * (finger.thumb ? 20.f : 12.f) * DEG;
				q = qnorm(qmul(axisAngle(rig.spreadAxes[f], spread), q));
			}
			putKey(bone, {frame, q, offset(bone, float(frame)).second, KeyInterp::Smooth});
		}
	}
	const std::string& handName = bones()[std::size_t(rig.bone)].name;
	if (shape.curl == 0.f && shape.spread == 0.f && shape.fingers.empty()) m_clip.hands.erase(handName);
	else m_clip.hands[handName] = shape;
}

HandShape AnimEditor::handShape(int hand) const {
	if (hand < 0 || hand >= int(m_hands.size())) return {};
	const std::string& name = bones()[std::size_t(m_hands[std::size_t(hand)].bone)].name;
	for (const auto& [bone, shape] : m_clip.hands) {
		if (iequals(bone, name)) return shape;
	}
	return {};
}

std::vector<HandRig> findHands(const std::vector<StudioBone>& bones) {
	std::vector<HandRig> out;
	if (bones.empty()) return out;
	std::vector<std::string> lower(bones.size());
	for (std::size_t i = 0; i < bones.size(); ++i) lower[i] = toLower(bones[i].name);
	const auto has = [](const std::string& text, std::initializer_list<const char*> parts) {
		return std::any_of(parts.begin(), parts.end(), [&](const char* p) { return text.find(p) != std::string::npos; });
	};
	const auto fingerish = [&](std::size_t b) { return has(lower[b], {"finger", "thumb", "index", "middle", "ring", "pinky", "pinkie", "digit"}); };
	// Rest pose in model space.
	std::vector<AnimVec> at(bones.size());
	std::vector<AnimQuat> rot(bones.size());
	for (std::size_t i = 0; i < bones.size(); ++i) {
		const BoneMatrix world = invertRigid(bones[i].poseToBone);
		at[i] = translation(world);
		rot[i] = matrixQuat(world.m);
	}
	std::vector<std::vector<int>> children(bones.size());
	for (std::size_t i = 0; i < bones.size(); ++i) {
		if (bones[i].parent >= 0 && std::size_t(bones[i].parent) < bones.size()) children[std::size_t(bones[i].parent)].push_back(int(i));
	}
	for (std::size_t h = 0; h < bones.size(); ++h) {
		const std::string& name = lower[h];
		if (!(has(name, {"hand", "wrist"}) && name.find("handle") == std::string::npos) || fingerish(h)) continue;
		HandRig rig;
		rig.bone = int(h);
		const std::string group = boneGroup(bones[h].name);
		rig.label = startsWithI(group, "left") ? "Left hand" : startsWithI(group, "right") ? "Right hand" : "Hand";
		// Finger roots: finger bones below the hand (palm/metacarpal bones in between are passed).
		std::vector<int> roots;
		std::vector<std::pair<int, int>> stack;   // bone, depth
		for (const int c : children[h]) stack.push_back({c, 1});
		while (!stack.empty()) {
			const auto [b, depth] = stack.back();
			stack.pop_back();
			if (fingerish(std::size_t(b))) {
				roots.push_back(b);
				continue;
			}
			if (depth < 3 && !has(lower[std::size_t(b)], {"hand", "wrist"})) {
				for (const int c : children[std::size_t(b)]) stack.push_back({c, depth + 1});
			}
		}
		for (const int r : roots) {
			FingerChain finger;
			int b = r;
			while (b >= 0 && finger.segments.size() < 4) {
				finger.segments.push_back(b);
				int next = -1;
				for (const int c : children[std::size_t(b)]) {
					if (fingerish(std::size_t(c))) {
						next = c;
						break;
					}
				}
				b = next;
			}
			const std::string& rn = lower[std::size_t(r)];
			// ValveBiped: Finger0 is the thumb, Finger1..4 index to pinky.
			finger.thumb = rn.find("thumb") != std::string::npos || (rn.size() >= 7 && rn.compare(rn.size() - 7, 7, "finger0") == 0);
			rig.fingers.push_back(std::move(finger));
		}
		std::sort(rig.fingers.begin(), rig.fingers.end(), [&](const FingerChain& a, const FingerChain& b) {
			if (a.thumb != b.thumb) return a.thumb;
			return lower[std::size_t(a.segments.front())] < lower[std::size_t(b.segments.front())];
		});
		if (rig.fingers.size() < 2) continue;
		std::vector<std::size_t> plain;
		for (std::size_t f = 0; f < rig.fingers.size(); ++f) {
			if (!rig.fingers[f].thumb) plain.push_back(f);
		}
		static constexpr const char* NAMES[4] = {"Index", "Middle", "Ring", "Pinky"};
		for (std::size_t i = 0; i < plain.size(); ++i) {
			const std::string& rn = lower[std::size_t(rig.fingers[plain[i]].segments.front())];
			std::string label;
			for (const char* n : {"index", "middle", "ring", "pinky"}) {
				if (rn.find(n) != std::string::npos) label = std::string(1, char(std::toupper(static_cast<unsigned char>(n[0])))) + (n + 1);
			}
			if (label.empty()) label = plain.size() == 4 ? NAMES[i] : "Finger " + std::to_string(i + 1);
			rig.fingers[plain[i]].label = label;
		}
		for (auto& finger : rig.fingers) {
			if (finger.thumb) finger.label = "Thumb";
		}
		// The palm's normal (towards where the fingers curl).
		const AnimVec handAt = at[h];
		AnimVec normal;
		if (plain.size() >= 2) {
			const AnimVec first = sub(at[std::size_t(rig.fingers[plain.front()].segments.front())], handAt);
			const AnimVec last = sub(at[std::size_t(rig.fingers[plain.back()].segments.front())], handAt);
			normal = normalized(cross(first, last));
		}
		if (length(normal) < 0.5f) {
			const AnimVec along = normalized(sub(at[std::size_t(rig.fingers.back().segments.front())], handAt));
			normal = normalized(cross(along, sub(at[std::size_t(rig.fingers.front().segments.front())], handAt)));
		}
		if (length(normal) < 0.5f) continue;
		// Which side is the palm: rest poses hold the fingers a little bent towards it, and the
		// thumb's root sits in front of the palm.
		float score = 0;
		for (const std::size_t f : plain) {
			const auto& seg = rig.fingers[f].segments;
			if (seg.size() < 3) continue;
			const AnimVec d0 = normalized(sub(at[std::size_t(seg[1])], at[std::size_t(seg[0])]));
			const AnimVec tip = sub(at[std::size_t(seg.back())], at[std::size_t(seg[0])]);
			const AnimVec dev = sub(tip, scale(d0, dot(tip, d0)));
			score += dot(dev, normal) / std::max(1e-3f, length(tip));
		}
		for (const auto& finger : rig.fingers) {
			if (finger.thumb) score += 0.5f * dot(normalized(sub(at[std::size_t(finger.segments.front())], handAt)), normal);
		}
		if (score < 0) normal = scale(normal, -1.f);
		// Axes per segment, in the bone's own axes.
		AnimVec centre;
		for (const std::size_t f : plain) centre = add(centre, at[std::size_t(rig.fingers[f].segments.front())]);
		if (!plain.empty()) centre = scale(centre, 1.f / float(plain.size()));
		const AnimVec indexAt = plain.empty() ? handAt : at[std::size_t(rig.fingers[plain.front()].segments.front())];
		for (const auto& finger : rig.fingers) {
			std::vector<AnimVec> axes;
			AnimVec dir = normalized(sub(at[std::size_t(finger.segments.front())], handAt));
			for (std::size_t k = 0; k < finger.segments.size(); ++k) {
				const std::size_t b = std::size_t(finger.segments[k]);
				if (k + 1 < finger.segments.size()) {
					const AnimVec next = sub(at[std::size_t(finger.segments[k + 1])], at[b]);
					if (length(next) > 1e-3f) dir = normalized(next);
				}
				axes.push_back(qrotate(qconj(rot[b]), normalized(cross(dir, normal))));
			}
			rig.curlAxes.push_back(std::move(axes));
			const std::size_t root = std::size_t(finger.segments.front());
			rig.spreadAxes.push_back(qrotate(qconj(rot[root]), normal));
			// Spread turns each finger away from the middle (the thumb away from the index finger).
			const AnimVec rootDir = finger.segments.size() > 1 ? normalized(sub(at[std::size_t(finger.segments[1])], at[root])) : normalized(sub(at[root], handAt));
			const AnimVec moves = cross(normal, rootDir);
			const AnimVec away = finger.thumb ? sub(at[root], indexAt) : sub(at[root], centre);
			const float d = dot(moves, away);
			rig.spreadSign.push_back(length(away) < 1e-3f || std::fabs(d) < 1e-4f ? 0.f : (d > 0 ? 1.f : -1.f));
		}
		out.push_back(std::move(rig));
	}
	return out;
}

void applyPoseLayer(const AnimClip& pose, const std::vector<StudioBone>& bones, std::vector<std::vector<std::pair<AnimVec, AnimQuat>>>& frames) {
	std::vector<std::optional<std::pair<AnimQuat, AnimVec>>> layer(bones.size());
	for (const auto& [name, keys] : pose.tracks) {
		if (keys.empty()) continue;
		for (std::size_t i = 0; i < bones.size(); ++i) {
			if (!iequals(bones[i].name, name)) continue;
			const AnimQuat r = finite(keys.front().rot) ? qnorm(keys.front().rot) : AnimQuat{};
			layer[i] = std::pair<AnimQuat, AnimVec>{r, finite(keys.front().pos) ? keys.front().pos : AnimVec{}};
		}
	}
	for (auto& frame : frames) {
		for (std::size_t i = 0; i < frame.size() && i < layer.size(); ++i) {
			if (layer[i]) frame[i] = {add(frame[i].first, layer[i]->second), qnorm(qmul(frame[i].second, layer[i]->first))};
		}
	}
}

std::map<std::string, SequenceReplacement> poseLayerReplacements(const AnimatedModel& model, const AnimClip& pose, const std::vector<AnimClip>& tweaks) {
	std::map<std::string, SequenceReplacement> out;
	const auto& bones = model.bones();
	const bool layered = std::any_of(pose.tracks.begin(), pose.tracks.end(), [](const auto& t) { return !t.second.empty(); });
	std::vector<AnimVec> pos;
	std::vector<AnimQuat> quat;
	const auto& sequences = model.sequences();
	for (std::size_t s = 0; s < sequences.size(); ++s) {
		const StudioSequence& seq = sequences[s];
		if (seq.model != 0 || seq.delta) continue;   // borrowed from include models, or adds onto others
		const AnimClip* tweak = nullptr;
		for (const auto& t : tweaks) {
			if (t.mode == AnimClip::Mode::Tweak && iequals(t.baseSequence, seq.name) && !t.tracks.empty()) tweak = &t;
		}
		if (!layered && !tweak) continue;
		std::vector<std::vector<std::pair<AnimVec, AnimQuat>>> baked;
		SequenceReplacement replacement;
		if (tweak) {
			const AnimEditor editor(model, *tweak);
			baked = editor.bake();
			replacement.fps = editor.clip().fps;
			replacement.loop = editor.clip().loop ? 1 : 0;
		} else {
			const int frames = std::clamp(seq.frames, 1, ANIM_CLIP_MAX_FRAMES);
			baked.resize(static_cast<std::size_t>(frames));
			for (int f = 0; f < frames; ++f) {
				model.localPose(int(s), frames > 1 ? float(f) / float(frames - 1) : 0.f, pos, quat, false);
				auto& frame = baked[std::size_t(f)];
				frame.resize(std::min({pos.size(), quat.size(), bones.size()}));
				for (std::size_t i = 0; i < frame.size(); ++i) frame[i] = {pos[i], quat[i]};
			}
		}
		if (layered) applyPoseLayer(pose, bones, baked);
		replacement.smd = poseFramesSmd(bones, baked);
		out[toLower(seq.name)] = std::move(replacement);
	}
	return out;
}

// --- IK --------------------------------------------------------------------------------------

void AnimEditor::findChains() {
	m_chains.clear();
	const auto& own = bones();
	if (own.empty()) return;
	// Rest positions in model space (for the segment lengths).
	std::vector<AnimVec> rest(own.size());
	for (std::size_t i = 0; i < own.size(); ++i) rest[i] = translation(invertRigid(own[i].poseToBone));
	const auto lower = [&](int b) { return toLower(own[std::size_t(b)].name); };
	// A twist or helper bone between two joints is skipped on the way up.
	const auto upFrom = [&](int b) {
		int p = own[std::size_t(b)].parent;
		while (p >= 0 && std::size_t(p) < own.size() && (lower(p).find("twist") != std::string::npos || lower(p).find("helper") != std::string::npos)) {
			p = own[std::size_t(p)].parent;
		}
		return p;
	};
	for (std::size_t b = 0; b < own.size(); ++b) {
		const std::string name = lower(int(b));
		const bool hand = (name.find("hand") != std::string::npos && name.find("handle") == std::string::npos) || name.find("wrist") != std::string::npos;
		const bool foot = name.find("foot") != std::string::npos || name.find("ankle") != std::string::npos;
		if (!hand && !foot) continue;
		if (name.find("finger") != std::string::npos || name.find("thumb") != std::string::npos || name.find("toe") != std::string::npos ||
		    name.find("twist") != std::string::npos || name.find("ik") == 0 || name.find("_ik") != std::string::npos || name.find("attach") != std::string::npos) {
			continue;
		}
		const int mid = upFrom(int(b));
		const int root = mid >= 0 ? upFrom(mid) : -1;
		if (mid < 0 || root < 0) continue;
		const float l1 = length(sub(rest[std::size_t(mid)], rest[std::size_t(root)]));
		const float l2 = length(sub(rest[b], rest[std::size_t(mid)]));
		if (l1 < 0.5f || l2 < 0.5f) continue;
		IkChain chain{root, mid, int(b), {}};
		const std::string group = boneGroup(own[b].name);   // "Right hand", "Left leg"
		const std::string side = startsWithI(group, "left") ? "Left " : startsWithI(group, "right") ? "Right " : "";
		chain.label = side + (hand ? "hand" : "foot");
		if (!chain.label.empty()) chain.label[0] = char(std::toupper(static_cast<unsigned char>(chain.label[0])));
		// One chain per end bone; a second hand bone on the same arm (e.g. a "hand" helper) is left out.
		if (std::none_of(m_chains.begin(), m_chains.end(), [&](const IkChain& c) { return c.root == root && c.mid == mid; })) m_chains.push_back(chain);
	}
}

int AnimEditor::chainOf(int bone) const {
	for (std::size_t i = 0; i < m_chains.size(); ++i) {
		if (m_chains[i].end == bone) return int(i);
	}
	for (std::size_t i = 0; i < m_chains.size(); ++i) {
		if (m_chains[i].mid == bone || m_chains[i].root == bone) return int(i);
	}
	return -1;
}

bool AnimEditor::placeEnd(int chainIndex, int frame, const AnimVec& target) {
	if (chainIndex < 0 || chainIndex >= int(m_chains.size()) || !finite(target)) return false;
	const IkChain chain = m_chains[std::size_t(chainIndex)];
	auto world = boneToModel(float(frame));
	if (std::size_t(std::max({chain.root, chain.mid, chain.end})) >= world.size()) return false;
	const AnimVec a = translation(world[std::size_t(chain.root)]);
	const AnimVec b = translation(world[std::size_t(chain.mid)]);
	const AnimVec c = translation(world[std::size_t(chain.end)]);
	const AnimQuat endBefore = matrixQuat(world[std::size_t(chain.end)].m);
	const float l1 = length(sub(b, a)), l2 = length(sub(c, b));
	if (l1 < 1e-3f || l2 < 1e-3f) return false;
	AnimVec toTarget = sub(target, a);
	float d = length(toTarget);
	const float minReach = std::fabs(l1 - l2) + 1e-3f, maxReach = l1 + l2 - 1e-3f;
	AnimVec u = d > 1e-5f ? scale(toTarget, 1.f / d) : normalized(sub(c, a));
	d = std::clamp(d, minReach, maxReach);
	// The bend direction (where the elbow/knee points) from the current pose.
	const AnimVec ac = normalized(sub(c, a));
	AnimVec pole = sub(sub(b, a), scale(ac, dot(sub(b, a), ac)));
	if (length(pole) < 1e-3f) pole = {0, 0, -1};   // a straight limb: bend downwards
	AnimVec side = sub(pole, scale(u, dot(pole, u)));
	if (length(side) < 1e-4f) side = sub(AnimVec{1, 0, 0}, scale(u, u.x));
	side = normalized(side);
	const float cosA = std::clamp((l1 * l1 + d * d - l2 * l2) / (2.f * l1 * d), -1.f, 1.f);
	const float sinA = std::sqrt(std::max(0.f, 1.f - cosA * cosA));
	const AnimVec newB = add(a, scale(add(scale(u, cosA), scale(side, sinA)), l1));
	const AnimVec newC = add(a, scale(u, d));

	beginChange();
	const int revision = m_revision;
	rotateBone(chain.root, frame, rotationBetween(sub(b, a), sub(newB, a)));
	world = boneToModel(float(frame));
	const AnimVec bNow = translation(world[std::size_t(chain.mid)]), cNow = translation(world[std::size_t(chain.end)]);
	rotateBone(chain.mid, frame, rotationBetween(sub(cNow, bNow), sub(newC, bNow)));
	// The end keeps its orientation in model space (a hand holding a grip).
	world = boneToModel(float(frame));
	const AnimQuat endNow = matrixQuat(world[std::size_t(chain.end)].m);
	rotateBone(chain.end, frame, qnorm(qmul(endBefore, qconj(endNow))));
	if (!hasKey(chain.end, frame)) keyBones({chain.end}, frame);
	endChange();
	return m_revision != revision;
}

std::vector<AnimVec> AnimEditor::motionPath(int bone, int maxPoints) const {
	std::vector<AnimVec> out;
	if (bone < 0 || bone >= int(bones().size()) || maxPoints < 2) return out;
	const int frames = m_clip.frames;
	const int count = std::min(frames, maxPoints);
	out.reserve(std::size_t(count));
	for (int i = 0; i < count; ++i) {
		const float frame = count > 1 ? float(i) * float(frames - 1) / float(count - 1) : 0.f;
		out.push_back(translation(boneToModel(frame)[std::size_t(bone)]));
	}
	return out;
}

void AnimEditor::setFps(float fps) {
	if (!(fps > 0.f) || !std::isfinite(fps)) return;
	fps = std::clamp(fps, 1.f, 1000.f);
	if (fps == m_clip.fps) return;
	changed();
	m_clip.fps = fps;
}

void AnimEditor::setLoop(bool loop) {
	if (loop == m_clip.loop) return;
	changed();
	m_clip.loop = loop;
}

void AnimEditor::setName(std::string name) {
	name = sequenceNameFromFile(name);
	if (name == m_clip.name) return;
	changed();
	m_clip.name = std::move(name);
}

void AnimEditor::setActivity(std::string activity) {
	activity = cleanName(activity);
	if (activity == m_clip.activity) return;
	changed();
	m_clip.activity = std::move(activity);
}

void AnimEditor::beginChange() {
	if (m_changeDepth++ == 0) {
		m_changeStart = m_clip;
		m_undo.push_back(m_clip);
		if (m_undo.size() > MAX_UNDO) m_undo.erase(m_undo.begin());
		m_redo.clear();
	}
}

void AnimEditor::endChange() {
	if (m_changeDepth == 0) return;
	if (--m_changeDepth == 0) {
		// Nothing changed in the end: no undo step.
		if (m_changeStart && *m_changeStart == m_clip && !m_undo.empty()) m_undo.pop_back();
		m_changeStart.reset();
	}
}

void AnimEditor::revertChange() {
	if (m_changeDepth > 0 && m_changeStart) {
		m_clip = *m_changeStart;
		++m_revision;
	}
}

bool AnimEditor::undo() {
	if (m_undo.empty() || m_changeDepth > 0) return false;
	m_redo.push_back(std::move(m_clip));
	m_clip = std::move(m_undo.back());
	m_undo.pop_back();
	++m_revision;
	return true;
}

bool AnimEditor::redo() {
	if (m_redo.empty() || m_changeDepth > 0) return false;
	m_undo.push_back(std::move(m_clip));
	m_clip = std::move(m_redo.back());
	m_redo.pop_back();
	++m_revision;
	return true;
}

std::vector<std::vector<std::pair<AnimVec, AnimQuat>>> AnimEditor::bake() const {
	std::vector<std::vector<std::pair<AnimVec, AnimQuat>>> out(static_cast<std::size_t>(m_clip.frames));
	std::vector<AnimVec> pos;
	std::vector<AnimQuat> quat;
	for (int f = 0; f < m_clip.frames; ++f) {
		pose(float(f), pos, quat);
		auto& frame = out[std::size_t(f)];
		frame.resize(std::min(pos.size(), quat.size()));
		for (std::size_t i = 0; i < frame.size(); ++i) frame[i] = {pos[i], quat[i]};
	}
	return out;
}

std::string AnimEditor::smd() const {
	return poseFramesSmd(bones(), bake());
}

StudioSequence AnimEditor::sequenceInfo() const {
	StudioSequence seq;
	seq.name = m_clip.name.empty() ? std::string("new_animation") : m_clip.name;
	seq.activity = m_clip.activity;
	seq.looping = m_clip.loop;
	seq.fps = m_clip.fps;
	seq.frames = m_clip.frames;
	if (m_clip.mode == AnimClip::Mode::Tweak && m_base >= 0) {
		const auto& base = m_model->sequences()[std::size_t(m_base)];
		seq.delta = base.delta;
		seq.layer = base.layer;
	}
	return seq;
}

// --- Project files ---------------------------------------------------------------------------

std::string writeAnimClip(const AnimClip& clip) {
	KvNode root;
	KvNode main = KvNode::makeBlock("smanim");
	main.children.push_back(KvNode::makeValue("version", "1"));
	main.children.push_back(KvNode::makeValue("model", cleanName(clip.model)));
	main.children.push_back(KvNode::makeValue("name", cleanName(clip.name)));
	main.children.push_back(KvNode::makeValue("mode", clip.mode == AnimClip::Mode::Tweak ? "tweak" : clip.mode == AnimClip::Mode::Pose ? "pose" : "new"));
	main.children.push_back(KvNode::makeValue("base", cleanName(clip.baseSequence)));
	main.children.push_back(KvNode::makeValue("baseframe", std::to_string(clip.baseFrame)));
	main.children.push_back(KvNode::makeValue("frames", std::to_string(clip.frames)));
	if (clip.editFrom > 0) main.children.push_back(KvNode::makeValue("editfrom", std::to_string(clip.editFrom)));
	if (clip.baseStart > 0.f || clip.baseEnd < 1.f) {
		main.children.push_back(KvNode::makeValue("basestart", number(clip.baseStart)));
		main.children.push_back(KvNode::makeValue("baseend", number(clip.baseEnd)));
	}
	main.children.push_back(KvNode::makeValue("fps", number(clip.fps)));
	main.children.push_back(KvNode::makeValue("loop", clip.loop ? "1" : "0"));
	main.children.push_back(KvNode::makeValue("activity", cleanName(clip.activity)));
	for (const auto& [name, keys] : clip.tracks) {
		if (keys.empty()) continue;
		KvNode bone = KvNode::makeBlock("bone");
		bone.children.push_back(KvNode::makeValue("name", cleanName(name)));
		for (const auto& k : keys) {
			// frame interpolation, rotation x y z w, position x y z
			bone.children.push_back(KvNode::makeValue("key", std::to_string(k.frame) + " " + keyInterpWord(k.interp) + " " + number(k.rot.x) + " " +
			                                                    number(k.rot.y) + " " + number(k.rot.z) + " " + number(k.rot.w) + " " + number(k.pos.x) + " " +
			                                                    number(k.pos.y) + " " + number(k.pos.z)));
		}
		main.children.push_back(std::move(bone));
	}
	for (const auto& [name, shape] : clip.hands) {
		KvNode hand = KvNode::makeBlock("hand");
		hand.children.push_back(KvNode::makeValue("bone", cleanName(name)));
		hand.children.push_back(KvNode::makeValue("curl", number(shape.curl)));
		hand.children.push_back(KvNode::makeValue("spread", number(shape.spread)));
		for (const auto& [finger, extra] : shape.fingers) hand.children.push_back(KvNode::makeValue("finger", cleanName(finger) + " " + number(extra)));
		main.children.push_back(std::move(hand));
	}
	root.children.push_back(std::move(main));
	return "// Mapsmith animation project: keys on top of the base animation (Animation editor)\n" + writeKeyValues(root);
}

std::optional<AnimClip> readAnimClip(std::string_view text, std::string* error) {
	const auto fail = [&](const std::string& message) -> std::optional<AnimClip> {
		if (error) *error = message;
		return std::nullopt;
	};
	if (text.size() > MAX_PROJECT_BYTES) return fail("the file is too big");
	KvNode root;
	try {
		root = parseKeyValues(text);
	} catch (const std::exception& e) {
		return fail(e.what());
	}
	const KvNode* main = root.find("smanim");
	if (!main || !main->block) return fail("not a Mapsmith animation project");
	AnimClip clip;
	clip.model = normalizePath(cleanName(main->get("model")));
	clip.name = cleanName(main->get("name"));
	if (!clip.name.empty()) clip.name = sequenceNameFromFile(clip.name);
	clip.mode = iequals(main->get("mode"), "new") ? AnimClip::Mode::New : iequals(main->get("mode"), "pose") ? AnimClip::Mode::Pose : AnimClip::Mode::Tweak;
	clip.baseSequence = cleanName(main->get("base"));
	int number = 0;
	if (parseInt(main->get("baseframe"), number)) clip.baseFrame = std::clamp(number, 0, ANIM_CLIP_MAX_FRAMES);
	if (!parseInt(main->get("frames"), number) || number < 1) return fail("no length (frames)");
	clip.frames = std::min(number, ANIM_CLIP_MAX_FRAMES);
	if (parseInt(main->get("editfrom"), number)) clip.editFrom = std::clamp(number, 0, clip.frames - 1);
	float range = 0;
	if (parseFloat(main->get("basestart"), range)) clip.baseStart = std::clamp(range, 0.f, 1.f);
	if (parseFloat(main->get("baseend"), range)) clip.baseEnd = std::clamp(range, clip.baseStart, 1.f);
	float fps = 30;
	if (parseFloat(main->get("fps"), fps) && fps > 0) clip.fps = std::clamp(fps, 1.f, 1000.f);
	clip.loop = main->get("loop") == "1";
	clip.activity = cleanName(main->get("activity"));
	int keyCount = 0;
	for (const auto& node : main->children) {
		if (!node.block || !iequals(node.key, "hand") || clip.hands.size() >= 16) continue;
		const std::string bone = cleanName(node.get("bone"));
		if (bone.empty()) continue;
		HandShape shape;
		float v = 0;
		if (parseFloat(node.get("curl"), v)) shape.curl = std::clamp(v, -0.25f, 1.f);
		if (parseFloat(node.get("spread"), v)) shape.spread = std::clamp(v, -1.f, 1.f);
		for (const auto& child : node.children) {
			if (child.block || !iequals(child.key, "finger") || shape.fingers.size() >= 8) continue;
			const auto parts = words(child.value);
			if (parts.size() == 2 && parseFloat(parts[1], v)) shape.fingers[cleanName(parts[0])] = std::clamp(v, -1.f, 1.f);
		}
		clip.hands[bone] = shape;
	}
	for (const auto& node : main->children) {
		if (!node.block || !iequals(node.key, "bone")) continue;
		const std::string name = cleanName(node.get("name"));
		if (name.empty()) continue;
		auto& keys = clip.tracks[name];
		for (const auto& child : node.children) {
			if (child.block || !iequals(child.key, "key")) continue;
			if (++keyCount > ANIM_CLIP_MAX_KEYS) return fail("too many keys");
			const auto parts = words(child.value);
			if (parts.size() != 9) continue;
			BoneKey key;
			float v[7];
			if (!parseInt(parts[0], key.frame) || key.frame < 0 || key.frame >= clip.frames) continue;
			key.interp = keyInterpFromWord(parts[1]);
			bool ok = true;
			for (int i = 0; i < 7 && ok; ++i) ok = parseFloat(parts[std::size_t(i) + 2], v[i]) && std::fabs(v[i]) < 1e6f;
			if (!ok) continue;
			key.rot = qnorm({v[0], v[1], v[2], v[3]});
			key.pos = {v[4], v[5], v[6]};
			keys.push_back(key);
		}
		std::sort(keys.begin(), keys.end(), [](const BoneKey& a, const BoneKey& b) { return a.frame < b.frame; });
		keys.erase(std::unique(keys.begin(), keys.end(), [](const BoneKey& a, const BoneKey& b) { return a.frame == b.frame; }), keys.end());
		if (keys.empty()) clip.tracks.erase(name);
	}
	return clip;
}

std::string animClipPath(std::string_view model, std::string_view name) {
	std::string path = normalizePath(model);
	if (startsWithI(path, "models/")) path = path.substr(7);
	if (endsWithI(path, ".mdl")) path.resize(path.size() - 4);
	std::string safe;
	for (const char c : path) {
		const auto u = static_cast<unsigned char>(c);
		if (std::isalnum(u) || c == '_' || c == '-' || c == '/' || c == '.') safe.push_back(char(std::tolower(u)));
		else safe.push_back('_');
	}
	// No way out of modelsrc ("..", empty parts).
	std::string cleaned;
	for (const auto& part : splitTrimmed(safe, '/')) {
		if (part.find_first_not_of('.') == std::string::npos) continue;   // "", ".", "..", "..." (Windows drops trailing dots)
		if (!cleaned.empty()) cleaned += '/';
		cleaned += part;
	}
	if (cleaned.empty()) cleaned = "model";
	return "modelsrc/" + cleaned + "/animations/" + sequenceNameFromFile(name) + ".smanim";
}

// --- Compiling -------------------------------------------------------------------------------

namespace {

/// Tokens of one .qc line: raw text (with its quotes) and the value.
struct QcToken {
	std::string raw, value;
};

std::vector<QcToken> qcTokens(std::string_view line) {
	std::vector<QcToken> out;
	std::size_t i = 0;
	while (i < line.size()) {
		while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
		if (i >= line.size()) break;
		if (line[i] == '/' && i + 1 < line.size() && line[i + 1] == '/') break;
		const std::size_t start = i;
		if (line[i] == '"') {
			const std::size_t end = line.find('"', i + 1);
			const std::size_t stop = end == std::string_view::npos ? line.size() : end + 1;
			out.push_back({std::string(line.substr(start, stop - start)), std::string(line.substr(start + 1, (end == std::string_view::npos ? line.size() : end) - start - 1))});
			i = stop;
		} else if (line[i] == '{' || line[i] == '}') {
			out.push_back({std::string(1, line[i]), std::string(1, line[i])});
			++i;
		} else {
			while (i < line.size() && !std::isspace(static_cast<unsigned char>(line[i])) && line[i] != '{' && line[i] != '}' && line[i] != '"') ++i;
			out.push_back({std::string(line.substr(start, i - start)), std::string(line.substr(start, i - start))});
		}
	}
	return out;
}

} // namespace

std::optional<ReplacedSequence> replaceSequenceInQc(std::string_view qc, std::string_view sequence, float fps, bool loop) {
	std::string out;
	out.reserve(qc.size() + 32);
	std::optional<std::string> file;
	std::size_t at = 0;
	while (at <= qc.size()) {
		std::size_t end = qc.find('\n', at);
		if (end == std::string_view::npos) end = qc.size();
		std::string_view line = qc.substr(at, end - at);
		const bool cr = !line.empty() && line.back() == '\r';
		if (cr) line.remove_suffix(1);
		auto tokens = qcTokens(line);
		if (!file && tokens.size() >= 3 && iequals(tokens[0].value, "$sequence") && iequals(tokens[1].value, sequence) && tokens[2].value != "{") {
			file = tokens[2].value;
			std::string rebuilt = "$sequence " + tokens[0 + 1].raw + " " + tokens[2].raw + " fps " + number(std::clamp(fps, 1.f, 1000.f));
			if (loop) rebuilt += " loop";
			bool afterBrace = false;
			for (std::size_t i = 3; i < tokens.size(); ++i) {
				if (tokens[i].value == "{") afterBrace = true;
				if (!afterBrace) {
					if (iequals(tokens[i].value, "fps") && i + 1 < tokens.size()) {
						++i;
						continue;
					}
					if (iequals(tokens[i].value, "loop")) continue;
				}
				rebuilt += " " + tokens[i].raw;
			}
			// Leading white space of the line stays.
			const std::size_t indent = line.find_first_not_of(" \t");
			out += std::string(line.substr(0, indent == std::string_view::npos ? 0 : indent)) + rebuilt;
		} else {
			out += line;
		}
		if (cr) out += '\r';
		if (end < qc.size()) out += '\n';
		at = end + 1;
	}
	if (!file || file->empty()) return std::nullopt;
	return ReplacedSequence{std::move(out), *file};
}

namespace {

std::optional<int> intAt(std::span<const std::byte> data, std::size_t offset) {
	if (data.size() < offset + 4) return std::nullopt;
	std::int32_t v;
	std::memcpy(&v, data.data() + offset, 4);
	return int(v);
}

bool idIs(std::span<const std::byte> data, std::size_t offset, const char* id) {
	return data.size() >= offset + 4 && std::memcmp(data.data() + offset, id, 4) == 0;
}

} // namespace

std::optional<int> mdlChecksum(std::span<const std::byte> mdl) {
	if (!idIs(mdl, 0, "IDST")) return std::nullopt;
	return intAt(mdl, 8);   // studiohdr_t: id, version, checksum
}

std::optional<int> vvdChecksum(std::span<const std::byte> vvd) {
	if (!idIs(vvd, 0, "IDSV")) return std::nullopt;
	return intAt(vvd, 8);   // vertexFileHeader_t: id, version, checksum
}

std::optional<int> vtxChecksum(std::span<const std::byte> vtx) {
	// FileHeader_t (packed): version 7, vertCacheSize, two shorts, maxBonesPerVert, checkSum.
	const auto version = intAt(vtx, 0);
	if (!version || *version != 7) return std::nullopt;
	return intAt(vtx, 16);
}

std::optional<int> phyChecksum(std::span<const std::byte> phy) {
	// phyheader_t: size (16), id, solidCount, checkSum.
	const auto size = intAt(phy, 0);
	if (!size || *size != 16) return std::nullopt;
	return intAt(phy, 12);
}

bool ModelChecksums::consistent() const {
	if (!mdl || !vvd || !dx90) return false;
	for (const auto& other : {vvd, dx90, dx80, sw, phy}) {
		if (other && *other != *mdl) return false;
	}
	return true;
}

} // namespace sme
