// studio_anim.cpp - Skeletal animation of Source 1 models (see studio_anim.h).

#include <sme/studio_anim.h>

#include <sme/filesystem.h>
#include <sme/text.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>

namespace sme {

namespace {

// studiohdr_t
constexpr long long HDR_SIZE = 408;
constexpr long long HDR_NUM_BONES = 156, HDR_BONE_INDEX = 160;
constexpr long long HDR_NUM_ANIMS = 180, HDR_ANIM_INDEX = 184;
constexpr long long HDR_NUM_SEQS = 188, HDR_SEQ_INDEX = 192;
constexpr long long HDR_NUM_INCLUDES = 336, HDR_INCLUDE_INDEX = 340;
constexpr long long HDR_ANIMBLOCK_NAME = 348, HDR_NUM_ANIMBLOCKS = 352, HDR_ANIMBLOCK_INDEX = 356;
constexpr long long BONE_SIZE = 216;      // mstudiobone_t
constexpr long long ANIMDESC_SIZE = 100;  // mstudioanimdesc_t
constexpr long long SEQ_SIZE = 212;       // mstudioseqdesc_t
constexpr int MAX_BONES = 256;
constexpr int MAX_ANIMS = 1 << 15;

// Flags of mstudioanim_t and of animations/sequences.
constexpr int ANIM_RAWPOS = 0x01, ANIM_RAWROT = 0x02, ANIM_ANIMPOS = 0x04, ANIM_ANIMROT = 0x08, ANIM_DELTA = 0x10, ANIM_RAWROT2 = 0x20;
constexpr int STUDIO_LOOPING = 0x0001, STUDIO_DELTA = 0x0004;

/// Bounds-checked little-endian reads from a file.
class Bytes {
public:
	explicit Bytes(const std::vector<std::byte>& data) : m_data(data) {}
	[[nodiscard]] bool fits(long long offset, long long size) const {
		return offset >= 0 && size >= 0 && static_cast<unsigned long long>(offset) <= m_data.size() &&
		       static_cast<unsigned long long>(size) <= m_data.size() - static_cast<std::size_t>(offset);
	}
	template <typename T>
	[[nodiscard]] T get(long long offset) const {
		T v{};
		if (fits(offset, sizeof(T))) std::memcpy(&v, m_data.data() + offset, sizeof(T));
		return v;
	}
	[[nodiscard]] int i32(long long offset) const { return get<std::int32_t>(offset); }
	[[nodiscard]] float f32(long long offset) const {
		const float v = get<float>(offset);
		return std::isfinite(v) ? v : 0.f;
	}
	[[nodiscard]] AnimVec vec(long long offset) const { return {f32(offset), f32(offset + 4), f32(offset + 8)}; }
	[[nodiscard]] std::string str(long long offset) const {
		std::string out;
		for (long long i = offset; fits(i, 1) && out.size() < 260; ++i) {
			const char c = static_cast<char>(m_data[static_cast<std::size_t>(i)]);
			if (c == '\0') break;
			out.push_back(c);
		}
		return out;
	}

private:
	const std::vector<std::byte>& m_data;
};

/// IEEE half float (Vector48's components).
float halfToFloat(std::uint16_t h) {
	const int sign = (h >> 15) & 1, exponent = (h >> 10) & 0x1f, mantissa = h & 0x3ff;
	float v;
	if (exponent == 0) v = std::ldexp(float(mantissa), -24);
	else if (exponent == 31) v = 0.f;   // inf/NaN: not a usable position
	else v = std::ldexp(float(mantissa | 0x400), exponent - 25);
	return sign ? -v : v;
}

AnimQuat normalized(AnimQuat q) {
	const float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
	if (!(len > 1e-8f) || !std::isfinite(len)) return {};
	return {q.x / len, q.y / len, q.z / len, q.w / len};
}

AnimQuat quaternion48(const Bytes& b, long long at) {
	const auto x = b.get<std::uint16_t>(at), y = b.get<std::uint16_t>(at + 2), zw = b.get<std::uint16_t>(at + 4);
	AnimQuat q;
	q.x = (int(x) - 32768) * (1.f / 32768.f);
	q.y = (int(y) - 32768) * (1.f / 32768.f);
	q.z = (int(zw & 0x7fff) - 16384) * (1.f / 16384.f);
	q.w = std::sqrt(std::max(0.f, 1.f - q.x * q.x - q.y * q.y - q.z * q.z));
	if (zw & 0x8000) q.w = -q.w;
	return q;
}

AnimQuat quaternion64(const Bytes& b, long long at) {
	const auto bits = b.get<std::uint64_t>(at);
	const auto part = [&](int shift) { return int((bits >> shift) & 0x1fffff); };
	AnimQuat q;
	q.x = (part(0) - 1048576) * (1.f / 1048576.5f);
	q.y = (part(21) - 1048576) * (1.f / 1048576.5f);
	q.z = (part(42) - 1048576) * (1.f / 1048576.5f);
	q.w = std::sqrt(std::max(0.f, 1.f - q.x * q.x - q.y * q.y - q.z * q.z));
	if ((bits >> 63) & 1) q.w = -q.w;
	return q;
}

/// ExtractAnimValue: the run-length encoded value of `frame` (0 without data). The stream is a
/// list of runs {valid, total} followed by `valid` values; after the last one the last value repeats.
float extractValue(const Bytes& b, long long stream, int frame, float scale) {
	if (stream <= 0) return 0.f;
	int k = frame;
	long long at = stream;
	for (int guard = 0; guard < 4096; ++guard) {
		if (!b.fits(at, 2)) return 0.f;
		const int valid = b.get<std::uint8_t>(at), total = b.get<std::uint8_t>(at + 1);
		if (total == 0) return 0.f;
		if (total > k) {
			const int index = valid > k ? k + 1 : valid;
			return float(b.get<std::int16_t>(at + 2 * index)) * scale;
		}
		k -= total;
		at += 2 * (valid + 1);
	}
	return 0.f;
}

AnimQuat slerp(const AnimQuat& a, AnimQuat b, float t) {
	// Nearest way round, then a normalized blend (frames are close together).
	if (a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w < 0) b = {-b.x, -b.y, -b.z, -b.w};
	return normalized({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t});
}

AnimQuat multiply(const AnimQuat& p, const AnimQuat& q) {
	return {p.x * q.w + p.y * q.z - p.z * q.y + p.w * q.x, -p.x * q.z + p.y * q.w + p.z * q.x + p.w * q.y,
	        p.x * q.y - p.y * q.x + p.z * q.w + p.w * q.z, -p.x * q.x - p.y * q.y - p.z * q.z + p.w * q.w};
}

std::string lowerName(std::string_view name) {
	return toLower(name);
}

} // namespace

AnimQuat angleQuaternion(const AnimVec& angles) {
	const float sy = std::sin(angles.z * 0.5f), cy = std::cos(angles.z * 0.5f);
	const float sp = std::sin(angles.y * 0.5f), cp = std::cos(angles.y * 0.5f);
	const float sr = std::sin(angles.x * 0.5f), cr = std::cos(angles.x * 0.5f);
	const float srXcp = sr * cp, crXsp = cr * sp, crXcp = cr * cp, srXsp = sr * sp;
	return {srXcp * cy - crXsp * sy, crXsp * cy + srXcp * sy, crXcp * sy - srXsp * cy, crXcp * cy + srXsp * sy};
}

BoneMatrix quaternionMatrix(const AnimQuat& q, const AnimVec& pos) {
	BoneMatrix m;
	m.m[0][0] = 1.f - 2.f * q.y * q.y - 2.f * q.z * q.z;
	m.m[1][0] = 2.f * q.x * q.y + 2.f * q.w * q.z;
	m.m[2][0] = 2.f * q.x * q.z - 2.f * q.w * q.y;
	m.m[0][1] = 2.f * q.x * q.y - 2.f * q.w * q.z;
	m.m[1][1] = 1.f - 2.f * q.x * q.x - 2.f * q.z * q.z;
	m.m[2][1] = 2.f * q.y * q.z + 2.f * q.w * q.x;
	m.m[0][2] = 2.f * q.x * q.z + 2.f * q.w * q.y;
	m.m[1][2] = 2.f * q.y * q.z - 2.f * q.w * q.x;
	m.m[2][2] = 1.f - 2.f * q.x * q.x - 2.f * q.y * q.y;
	m.m[0][3] = pos.x;
	m.m[1][3] = pos.y;
	m.m[2][3] = pos.z;
	return m;
}

BoneMatrix concat(const BoneMatrix& a, const BoneMatrix& b) {
	BoneMatrix out;
	for (int r = 0; r < 3; ++r) {
		for (int c = 0; c < 4; ++c) {
			float v = a.m[r][0] * b.m[0][c] + a.m[r][1] * b.m[1][c] + a.m[r][2] * b.m[2][c];
			if (c == 3) v += a.m[r][3];
			out.m[r][c] = v;
		}
	}
	return out;
}

BoneMatrix invertRigid(const BoneMatrix& m) {
	BoneMatrix out;
	for (int r = 0; r < 3; ++r) {
		for (int c = 0; c < 3; ++c) out.m[r][c] = m.m[c][r];
		out.m[r][3] = -(m.m[0][r] * m.m[0][3] + m.m[1][r] * m.m[1][3] + m.m[2][r] * m.m[2][3]);
	}
	return out;
}

AnimVec transformPoint(const BoneMatrix& m, const AnimVec& p) {
	return {m.m[0][0] * p.x + m.m[0][1] * p.y + m.m[0][2] * p.z + m.m[0][3], m.m[1][0] * p.x + m.m[1][1] * p.y + m.m[1][2] * p.z + m.m[1][3],
	        m.m[2][0] * p.x + m.m[2][1] * p.y + m.m[2][2] * p.z + m.m[2][3]};
}

std::optional<AnimatedModel::File> AnimatedModel::parse(FileData data, std::string* error) {
	const auto fail = [&](const std::string& message) -> std::optional<File> {
		if (error) *error = data.path.empty() ? message : data.path + ": " + message;
		return std::nullopt;
	};
	File file;
	file.path = std::move(data.path);
	file.mdl = std::move(data.mdl);
	file.ani = std::move(data.ani);
	const Bytes mdl(file.mdl);
	if (!mdl.fits(0, HDR_SIZE) || std::memcmp(file.mdl.data(), "IDST", 4) != 0) return fail("not a Source model (.mdl)");
	const int version = mdl.i32(4);
	if (version < 44 || version > 49) return fail("unsupported model version " + std::to_string(version));

	const int numBones = mdl.i32(HDR_NUM_BONES);
	const long long boneIndex = mdl.i32(HDR_BONE_INDEX);
	if (numBones < 0 || numBones > MAX_BONES || !mdl.fits(boneIndex, numBones * BONE_SIZE)) return fail("bones out of range");
	for (int i = 0; i < numBones; ++i) {
		const long long b = boneIndex + i * BONE_SIZE;
		StudioBone bone;
		bone.name = mdl.str(b + mdl.i32(b));
		bone.parent = mdl.i32(b + 4);
		if (bone.parent < -1 || bone.parent >= i) bone.parent = -1;   // parents come first
		bone.pos = mdl.vec(b + 32);
		bone.quat = normalized({mdl.f32(b + 44), mdl.f32(b + 48), mdl.f32(b + 52), mdl.f32(b + 56)});
		bone.rot = mdl.vec(b + 60);
		bone.posScale = mdl.vec(b + 72);
		bone.rotScale = mdl.vec(b + 84);
		for (int r = 0; r < 3; ++r) {
			for (int c = 0; c < 4; ++c) bone.poseToBone.m[r][c] = mdl.f32(b + 96 + (r * 4 + c) * 4);
		}
		file.bones.push_back(std::move(bone));
	}

	const int numAnims = mdl.i32(HDR_NUM_ANIMS);
	const long long animIndex = mdl.i32(HDR_ANIM_INDEX);
	if (numAnims < 0 || numAnims > MAX_ANIMS || !mdl.fits(animIndex, numAnims * ANIMDESC_SIZE)) return fail("animations out of range");
	for (int i = 0; i < numAnims; ++i) {
		const long long a = animIndex + i * ANIMDESC_SIZE;
		AnimDesc anim;
		anim.offset = a;
		anim.fps = mdl.f32(a + 8);
		anim.flags = mdl.i32(a + 12);
		anim.frames = std::clamp(mdl.i32(a + 16), 1, 1 << 16);
		anim.block = mdl.i32(a + 52);
		anim.index = mdl.i32(a + 56);
		anim.sectionIndex = mdl.i32(a + 80);
		anim.sectionFrames = std::max(0, mdl.i32(a + 84));
		file.anims.push_back(anim);
	}

	const int numBlocks = mdl.i32(HDR_NUM_ANIMBLOCKS);
	const long long blockIndex = mdl.i32(HDR_ANIMBLOCK_INDEX);
	if (numBlocks > 0 && numBlocks < 65536 && mdl.fits(blockIndex, numBlocks * 8LL)) {
		for (int i = 0; i < numBlocks; ++i) file.aniBlocks.emplace_back(mdl.i32(blockIndex + i * 8), mdl.i32(blockIndex + i * 8 + 4));
	}
	return file;
}

std::optional<AnimatedModel> AnimatedModel::fromFiles(std::vector<FileData> files, std::string* error) {
	if (files.empty()) {
		if (error) *error = "no model";
		return std::nullopt;
	}
	AnimatedModel model;
	for (auto& data : files) {
		auto file = parse(std::move(data), error);
		if (!file) {
			if (model.m_files.empty()) return std::nullopt;   // the model itself must be readable
			continue;                                          // a broken include only loses its sequences
		}
		model.m_files.push_back(std::move(*file));
	}
	// Bones of every file mapped onto the model's by name (as the engine's virtual model does).
	const auto& own = model.m_files.front().bones;
	for (auto& file : model.m_files) {
		file.toModel.assign(file.bones.size(), -1);
		for (std::size_t b = 0; b < file.bones.size(); ++b) {
			for (std::size_t m = 0; m < own.size(); ++m) {
				if (iequals(file.bones[b].name, own[m].name)) {
					file.toModel[b] = int(m);
					break;
				}
			}
		}
	}
	// Sequences: the model's own, then each include's that are new (by name).
	std::set<std::string> seen;
	for (std::size_t f = 0; f < model.m_files.size(); ++f) {
		const auto& file = model.m_files[f];
		const Bytes mdl(file.mdl);
		const int numSeqs = mdl.i32(HDR_NUM_SEQS);
		const long long seqIndex = mdl.i32(HDR_SEQ_INDEX);
		if (numSeqs <= 0 || numSeqs > MAX_ANIMS || !mdl.fits(seqIndex, numSeqs * SEQ_SIZE)) continue;
		for (int i = 0; i < numSeqs; ++i) {
			const long long s = seqIndex + i * SEQ_SIZE;
			StudioSequence seq;
			seq.name = mdl.str(s + mdl.i32(s + 4));
			if (seq.name.empty() || !seen.insert(lowerName(seq.name)).second) continue;
			seq.activity = mdl.str(s + mdl.i32(s + 8));
			const int flags = mdl.i32(s + 12);
			seq.looping = (flags & STUDIO_LOOPING) != 0;
			seq.delta = (flags & STUDIO_DELTA) != 0;
			seq.model = int(f);
			seq.animation = mdl.get<std::int16_t>(s + mdl.i32(s + 60));   // the first blend of animindexindex
			if (seq.animation >= 0 && seq.animation < int(file.anims.size())) {
				const auto& anim = file.anims[std::size_t(seq.animation)];
				seq.fps = anim.fps > 0 ? anim.fps : 30.f;
				seq.frames = anim.frames;
				seq.delta = seq.delta || (anim.flags & STUDIO_DELTA) != 0;
			} else {
				seq.animation = -1;
			}
			model.m_sequences.push_back(std::move(seq));
		}
	}
	return model;
}

std::optional<AnimatedModel> AnimatedModel::load(const GameFileSystem& fs, std::string_view modelPath, std::string* error) {
	std::vector<FileData> files;
	std::set<std::string> visited;
	// Breadth first: the model, its includes, theirs (the engine follows includes of includes).
	std::vector<std::string> queue{normalizePath(modelPath)};
	for (std::size_t q = 0; q < queue.size() && files.size() < 32; ++q) {
		const std::string path = queue[q];
		if (!visited.insert(toLower(path)).second) continue;
		auto mdl = fs.read(path);
		if (!mdl) {
			if (q == 0) {
				if (error) *error = path + " not found";
				return std::nullopt;
			}
			continue;
		}
		FileData data;
		data.path = path;
		data.mdl = std::move(*mdl);
		const Bytes bytes(data.mdl);
		if (bytes.fits(0, HDR_SIZE)) {
			// The .ani: its name is in the header ("models/x.ani"); without one, next to the model.
			std::string ani = bytes.i32(HDR_NUM_ANIMBLOCKS) > 0 ? normalizePath(bytes.str(bytes.i32(HDR_ANIMBLOCK_NAME))) : std::string();
			if (!ani.empty()) {
				if (!startsWithI(ani, "models/")) ani = "models/" + ani;
				if (auto aniData = fs.read(ani)) data.ani = std::move(*aniData);
				else if (auto beside = fs.read(path.substr(0, path.size() - 4) + ".ani")) data.ani = std::move(*beside);
			}
			const int includes = bytes.i32(HDR_NUM_INCLUDES);
			const long long includeIndex = bytes.i32(HDR_INCLUDE_INDEX);
			if (includes > 0 && includes < 64 && bytes.fits(includeIndex, includes * 8LL) && q < 8) {
				for (int i = 0; i < includes; ++i) {
					const long long g = includeIndex + i * 8;
					std::string include = normalizePath(bytes.str(g + bytes.i32(g + 4)));
					if (include.empty()) continue;
					if (!startsWithI(include, "models/")) include = "models/" + include;
					queue.push_back(std::move(include));
				}
			}
		}
		files.push_back(std::move(data));
	}
	return fromFiles(std::move(files), error);
}

int AnimatedModel::addKeyframedSequence(StudioSequence sequence, std::vector<std::vector<std::pair<AnimVec, AnimQuat>>> frames) {
	sequence.model = -1;
	sequence.animation = int(m_keyframed.size());
	sequence.frames = std::max<int>(1, int(frames.size()));
	m_keyframed.push_back(std::move(frames));
	// A new name replaces an earlier preview of the same name.
	for (std::size_t i = 0; i < m_sequences.size(); ++i) {
		if (m_sequences[i].model < 0 && iequals(m_sequences[i].name, sequence.name)) {
			m_sequences[i] = std::move(sequence);
			return int(i);
		}
	}
	m_sequences.push_back(std::move(sequence));
	return int(m_sequences.size()) - 1;
}

int AnimatedModel::findSequence(std::string_view name) const {
	for (std::size_t i = 0; i < m_sequences.size(); ++i) {
		if (iequals(m_sequences[i].name, name)) return int(i);
	}
	return -1;
}

std::vector<std::string> AnimatedModel::files() const {
	std::vector<std::string> out;
	for (const auto& file : m_files) out.push_back(file.path);
	return out;
}

AnimatedModel::AnimData AnimatedModel::animData(const File& file, const AnimDesc& anim, int frame) const {
	// mstudioanimdesc_t::pAnim: long animations are cut into sections; the last frame of a long
	// one has a section of its own.
	int block = anim.block;
	long long index = anim.index;
	int local = std::clamp(frame, 0, anim.frames - 1);
	const Bytes mdl(file.mdl);
	if (anim.sectionFrames > 0) {
		int section;
		if (anim.frames > anim.sectionFrames && local == anim.frames - 1) {
			local = 0;
			section = anim.frames / anim.sectionFrames + 1;
		} else {
			section = local / anim.sectionFrames;
			local -= section * anim.sectionFrames;
		}
		const long long entry = anim.offset + anim.sectionIndex + section * 8LL;
		if (!mdl.fits(entry, 8)) return {};
		block = mdl.i32(entry);
		index = mdl.i32(entry + 4);
	}
	if (block < 0) return {};
	if (block == 0) return {&file.mdl, anim.offset + index, local};
	if (block >= int(file.aniBlocks.size()) || file.ani.empty()) return {};
	const auto [start, end] = file.aniBlocks[std::size_t(block)];
	if (start < 0 || end < start || index < 0 || start + index >= end) return {};
	return {&file.ani, start + index, local};
}

void AnimatedModel::applyFrame(const File& file, const AnimDesc& anim, int frame, std::vector<AnimVec>& pos, std::vector<AnimQuat>& quat,
                               std::vector<bool>* touched) const {
	const AnimData data = animData(file, anim, frame);
	if (!data.data) return;
	const Bytes b(*data.data);
	long long at = data.offset;
	for (int guard = 0; guard < MAX_BONES * 2 && b.fits(at, 4); ++guard) {
		const int boneIndex = b.get<std::uint8_t>(at);
		const int flags = b.get<std::uint8_t>(at + 1);
		const int next = b.get<std::int16_t>(at + 2);
		if (boneIndex == 255) break;
		const int target = boneIndex < int(file.toModel.size()) ? file.toModel[std::size_t(boneIndex)] : -1;
		if (target >= 0 && target < int(pos.size())) {
			const StudioBone& bone = file.bones[std::size_t(boneIndex)];
			const bool delta = (flags & ANIM_DELTA) != 0;
			const long long dataStart = at + 4;
			// Rotation (CalcBoneQuaternion).
			AnimQuat q;
			if (flags & ANIM_RAWROT) {
				q = quaternion48(b, dataStart);
			} else if (flags & ANIM_RAWROT2) {
				q = quaternion64(b, dataStart);
			} else if (flags & ANIM_ANIMROT) {
				AnimVec angle;
				const float* scale = &bone.rotScale.x;
				float* out = &angle.x;
				for (int j = 0; j < 3; ++j) {
					const int offset = b.get<std::int16_t>(dataStart + j * 2);
					out[j] = offset > 0 ? extractValue(b, dataStart + offset, data.frame, scale[j]) : 0.f;
				}
				if (!delta) angle = {angle.x + bone.rot.x, angle.y + bone.rot.y, angle.z + bone.rot.z};
				q = angleQuaternion(angle);
			} else {
				q = delta ? AnimQuat{} : bone.quat;
			}
			// Position (CalcBonePosition); it follows the raw rotation (6 or 8 bytes) or the rotation offsets.
			AnimVec p;
			if (flags & ANIM_RAWPOS) {
				const long long posAt = dataStart + ((flags & ANIM_RAWROT) ? 6 : 0) + ((flags & ANIM_RAWROT2) ? 8 : 0);
				p = {halfToFloat(b.get<std::uint16_t>(posAt)), halfToFloat(b.get<std::uint16_t>(posAt + 2)), halfToFloat(b.get<std::uint16_t>(posAt + 4))};
			} else if (flags & ANIM_ANIMPOS) {
				const long long values = dataStart + ((flags & ANIM_ANIMROT) ? 6 : 0);
				const float* scale = &bone.posScale.x;
				float* out = &p.x;
				for (int j = 0; j < 3; ++j) {
					const int offset = b.get<std::int16_t>(values + j * 2);
					out[j] = offset > 0 ? extractValue(b, values + offset, data.frame, scale[j]) : 0.f;
				}
				if (!delta) p = {p.x + bone.pos.x, p.y + bone.pos.y, p.z + bone.pos.z};
			} else {
				p = delta ? AnimVec{} : bone.pos;
			}
			if (delta) {
				// A layer: onto the pose underneath.
				quat[std::size_t(target)] = normalized(multiply(quat[std::size_t(target)], q));
				pos[std::size_t(target)] = {pos[std::size_t(target)].x + p.x, pos[std::size_t(target)].y + p.y, pos[std::size_t(target)].z + p.z};
			} else {
				quat[std::size_t(target)] = normalized(q);
				pos[std::size_t(target)] = p;
			}
			if (touched) (*touched)[std::size_t(target)] = true;
		}
		if (next <= 0) break;
		at += next;
	}
}

void AnimatedModel::localPose(int sequence, float cycle, std::vector<AnimVec>& pos, std::vector<AnimQuat>& quat) const {
	const auto& own = bones();
	pos.resize(own.size());
	quat.resize(own.size());
	for (std::size_t i = 0; i < own.size(); ++i) {
		pos[i] = own[i].pos;
		quat[i] = own[i].quat;
	}
	if (sequence < 0 || sequence >= int(m_sequences.size())) return;
	const StudioSequence& seq = m_sequences[std::size_t(sequence)];
	if (!std::isfinite(cycle)) cycle = 0;
	if (seq.model < 0) {
		// Plain frames (an import being previewed), blended like the compiled ones.
		if (seq.animation < 0 || seq.animation >= int(m_keyframed.size())) return;
		const auto& frames = m_keyframed[std::size_t(seq.animation)];
		if (frames.empty()) return;
		const float frameF = std::clamp(cycle, 0.f, 1.f) * float(frames.size() - 1);
		const auto f0 = std::size_t(frameF);
		const std::size_t f1 = std::min(f0 + 1, frames.size() - 1);
		const float s = frameF - float(f0);
		for (std::size_t i = 0; i < pos.size() && i < frames[f0].size() && i < frames[f1].size(); ++i) {
			const auto& a = frames[f0][i];
			const auto& b = frames[f1][i];
			pos[i] = {a.first.x + (b.first.x - a.first.x) * s, a.first.y + (b.first.y - a.first.y) * s, a.first.z + (b.first.z - a.first.z) * s};
			quat[i] = slerp(a.second, b.second, s);
		}
		return;
	}
	const File& file = m_files[std::size_t(seq.model)];
	if (seq.animation < 0 || seq.animation >= int(file.anims.size())) return;
	const AnimDesc& anim = file.anims[std::size_t(seq.animation)];
	// The include's own rest pose first for the bones it has (CalcVirtualAnimation), then the data.
	if (seq.model != 0 && !seq.delta) {
		for (std::size_t b = 0; b < file.bones.size(); ++b) {
			const int target = file.toModel[b];
			if (target < 0) continue;
			pos[std::size_t(target)] = file.bones[b].pos;
			quat[std::size_t(target)] = file.bones[b].quat;
		}
	}
	const float frameF = std::clamp(cycle, 0.f, 1.f) * float(anim.frames - 1);
	const int frame = int(frameF);
	const float s = frameF - float(frame);
	auto pos0 = pos, pos1 = pos;
	auto quat0 = quat, quat1 = quat;
	applyFrame(file, anim, frame, pos0, quat0, nullptr);
	if (s > 0.f && frame + 1 < anim.frames) {
		applyFrame(file, anim, frame + 1, pos1, quat1, nullptr);
		for (std::size_t i = 0; i < pos.size(); ++i) {
			pos[i] = {pos0[i].x + (pos1[i].x - pos0[i].x) * s, pos0[i].y + (pos1[i].y - pos0[i].y) * s, pos0[i].z + (pos1[i].z - pos0[i].z) * s};
			quat[i] = slerp(quat0[i], quat1[i], s);
		}
	} else {
		pos = std::move(pos0);
		quat = std::move(quat0);
	}
}

std::vector<BoneMatrix> AnimatedModel::boneToModel(int sequence, float cycle) const {
	std::vector<AnimVec> pos;
	std::vector<AnimQuat> quat;
	localPose(sequence, cycle, pos, quat);
	const auto& own = bones();
	std::vector<BoneMatrix> world(own.size());
	for (std::size_t i = 0; i < own.size(); ++i) {
		const BoneMatrix local = quaternionMatrix(quat[i], pos[i]);
		world[i] = own[i].parent >= 0 ? concat(world[std::size_t(own[i].parent)], local) : local;
	}
	return world;
}

std::vector<BoneMatrix> AnimatedModel::skinning(int sequence, float cycle) const {
	auto world = boneToModel(sequence, cycle);
	const auto& own = bones();
	for (std::size_t i = 0; i < own.size(); ++i) world[i] = concat(world[i], own[i].poseToBone);
	return world;
}

void skinVertices(const ModelGeometry& geometry, const std::vector<BoneMatrix>& skinning, std::vector<ModelVertex>& out) {
	out = geometry.vertices;
	if (skinning.empty() || geometry.weights.size() != geometry.vertices.size()) return;
	for (std::size_t i = 0; i < out.size(); ++i) {
		const ModelVertexWeights& w = geometry.weights[i];
		if (w.count == 0) continue;
		const ModelVertex& rest = geometry.vertices[i];
		float p[3] = {0, 0, 0}, n[3] = {0, 0, 0}, total = 0;
		for (int k = 0; k < w.count; ++k) {
			if (w.bone[k] >= skinning.size() || !(w.weight[k] > 0.f)) continue;
			const BoneMatrix& m = skinning[w.bone[k]];
			for (int r = 0; r < 3; ++r) {
				p[r] += w.weight[k] * (m.m[r][0] * rest.position[0] + m.m[r][1] * rest.position[1] + m.m[r][2] * rest.position[2] + m.m[r][3]);
				n[r] += w.weight[k] * (m.m[r][0] * rest.normal[0] + m.m[r][1] * rest.normal[1] + m.m[r][2] * rest.normal[2]);
			}
			total += w.weight[k];
		}
		if (!(total > 1e-6f)) continue;
		const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
		for (int r = 0; r < 3; ++r) {
			out[i].position[r] = p[r] / total;
			out[i].normal[r] = len > 1e-6f ? n[r] / len : rest.normal[r];
		}
	}
}

} // namespace sme
