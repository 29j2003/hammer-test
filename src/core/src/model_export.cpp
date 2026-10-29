// model_export.cpp - A compiled model back into .smd and .qc files (see model_export.h).
//
// Layouts from the Source SDK 2013: studio.h (studiohdr_t, mstudiohitboxset_t, mstudiobbox_t,
// mstudiobone_t), phyfile.h (phyheader_t), vcollide_parse / physics (compactsurfaceheader_t,
// legacysurfaceheader_t) and the IVP compact surface (IVP_Compact_Surface, IVP_Compact_Ledge,
// IVP_Compact_Triangle, IVP_Compact_Poly_Point) as the public format notes describe them (the
// Valve Developer Community; Crowbar reads the same layout). Written for Mapsmith, no code taken.

#include <sme/model_export.h>

#include <sme/animation_import.h>
#include <sme/assets.h>
#include <sme/filesystem.h>
#include <sme/keyvalues.h>
#include <sme/material.h>
#include <sme/studio_model.h>
#include <sme/text.h>
#include <sme/texture.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>

namespace sme {

namespace fs = std::filesystem;

namespace {

constexpr float PI = 3.14159265358979f;
constexpr float METRES_TO_UNITS = 1.f / 0.0254f;   // IVP works in metres, Source in inches

/// Bounds-checked little-endian reads from an untrusted file.
class Bytes {
public:
	explicit Bytes(std::span<const std::byte> data) : m_data(data) {}
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
	[[nodiscard]] std::string str(long long offset, std::size_t limit = 260) const {
		std::string out;
		for (long long i = offset; fits(i, 1) && out.size() < limit; ++i) {
			const char c = static_cast<char>(m_data[static_cast<std::size_t>(i)]);
			if (c == '\0') break;
			out.push_back(c);
		}
		return out;
	}
	[[nodiscard]] std::size_t size() const { return m_data.size(); }

private:
	std::span<const std::byte> m_data;
};

std::string number(float v) {
	if (!std::isfinite(v) || std::abs(v) < 1e-6f) v = 0;
	std::ostringstream out;
	out.precision(6);
	out << std::fixed << v;
	std::string s = out.str();
	while (s.size() > 1 && s.back() == '0') s.pop_back();
	if (!s.empty() && s.back() == '.') s.pop_back();
	return s == "-0" ? "0" : s;
}

/// No quotes or line breaks in a written name.
std::string clean(std::string_view text) {
	std::string out;
	for (const char c : text) {
		if (c != '"' && c != '\n' && c != '\r') out.push_back(c);
	}
	return out;
}

/// A file name part: lower case letters, digits, '_' and '-' (others become '_').
std::string fileStem(std::string_view text) {
	std::string out;
	for (const char c : toLower(text)) {
		out.push_back((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ? c : '_');
	}
	while (!out.empty() && out.back() == '_') out.pop_back();
	return out.empty() ? std::string("part") : out.substr(0, 80);
}

/// `stem` + extension, made unique among the names used so far.
std::string uniqueName(std::set<std::string>& used, const std::string& stem, std::string_view extension) {
	std::string name = stem + std::string(extension);
	for (int n = 2; used.count(name); ++n) name = stem + "_" + std::to_string(n) + std::string(extension);
	used.insert(name);
	return name;
}

AnimVec rotateZ(const AnimVec& v, float angle) {
	const float c = std::cos(angle), s = std::sin(angle);
	return {c * v.x - s * v.y, s * v.x + c * v.y, v.z};
}
AnimQuat multiplyQ(const AnimQuat& p, const AnimQuat& q) {
	return {p.x * q.w + p.y * q.z - p.z * q.y + p.w * q.x, -p.x * q.z + p.y * q.w + p.z * q.x + p.w * q.y,
	        p.x * q.y - p.y * q.x + p.z * q.w + p.w * q.z, -p.x * q.x - p.y * q.y - p.z * q.z + p.w * q.w};
}
/// A root bone's pose turned back for studiomdl (it turns root bones 90 degrees about Z).
std::pair<AnimVec, AnimQuat> turnedBack(AnimVec pos, AnimQuat quat) {
	return {rotateZ(pos, -PI / 2), multiplyQ({0, 0, std::sin(-PI / 4), std::cos(-PI / 4)}, quat)};
}
AnimVec transform(const BoneMatrix& m, const AnimVec& v) {
	return {m.m[0][0] * v.x + m.m[0][1] * v.y + m.m[0][2] * v.z + m.m[0][3], m.m[1][0] * v.x + m.m[1][1] * v.y + m.m[1][2] * v.z + m.m[1][3],
	        m.m[2][0] * v.x + m.m[2][1] * v.y + m.m[2][2] * v.z + m.m[2][3]};
}

/// "nodes" and the rest pose ("skeleton", time 0) of an .smd.
std::string smdSkeleton(const std::vector<StudioBone>& bones) {
	std::string out = "version 1\nnodes\n";
	if (bones.empty()) out += "0 \"root\" -1\n";
	for (std::size_t i = 0; i < bones.size(); ++i) out += std::to_string(i) + " \"" + clean(bones[i].name) + "\" " + std::to_string(bones[i].parent) + "\n";
	out += "end\nskeleton\ntime 0\n";
	if (bones.empty()) out += "0 0 0 0 0 0 0\n";
	for (std::size_t i = 0; i < bones.size(); ++i) {
		auto [pos, quat] = bones[i].parent < 0 ? turnedBack(bones[i].pos, bones[i].quat) : std::pair{bones[i].pos, bones[i].quat};
		const AnimVec rot = quaternionAngles(quat);
		out += std::to_string(i) + " " + number(pos.x) + " " + number(pos.y) + " " + number(pos.z) + " " + number(rot.x) + " " + number(rot.y) + " " +
		       number(rot.z) + "\n";
	}
	out += "end\n";
	return out;
}

/// Bone to model space of every bone at rest.
std::vector<BoneMatrix> restBoneToModel(const std::vector<StudioBone>& bones) {
	std::vector<BoneMatrix> out(bones.size());
	for (std::size_t i = 0; i < bones.size(); ++i) {
		const BoneMatrix local = quaternionMatrix(bones[i].quat, bones[i].pos);
		const int parent = bones[i].parent;
		out[i] = parent >= 0 && std::size_t(parent) < i ? concat(out[std::size_t(parent)], local) : local;
	}
	return out;
}

/// Reads the solids' compact surfaces of a .phy: convex pieces in Source units.
bool readSolidPieces(const Bytes& phy, long long data, long long size, std::vector<std::vector<AnimVec>>& pieces) {
	// The compact surface follows a "VPHY" header (28 bytes) or the legacy header's size field;
	// either way it is marked "IVPS" in its last dummy field.
	long long surface = -1;
	for (const long long candidate : {data + 28, data + 4, data}) {
		if (candidate + 48 <= data + size && phy.fits(candidate, 48) && phy.str(candidate + 44, 4) == "IVPS") {
			surface = candidate;
			break;
		}
	}
	if (surface < 0) return false;
	const long long end = data + size;
	const long long treeRoot = surface + phy.i32(surface + 32);
	long long pointsStart = std::min(end, treeRoot > surface ? treeRoot : end);
	long long ledge = surface + 48;
	for (int n = 0; n < 4096 && ledge + 16 <= pointsStart && phy.fits(ledge, 16); ++n) {
		const long long points = ledge + phy.i32(ledge);
		const auto flags = phy.get<std::uint32_t>(ledge + 8);
		const long long ledgeSize = static_cast<long long>(flags >> 8) * 16;
		const int triangles = phy.get<std::int16_t>(ledge + 12);
		if (ledgeSize <= 0 || triangles <= 0 || !phy.fits(ledge + 16, static_cast<long long>(triangles) * 16)) return !pieces.empty();
		if (points > ledge && points < pointsStart) pointsStart = points;   // the point arrays follow the last ledge
		std::vector<AnimVec> piece;
		for (int t = 0; t < triangles; ++t) {
			const long long tri = ledge + 16 + static_cast<long long>(t) * 16;
			AnimVec corners[3];
			for (int e = 0; e < 3; ++e) {
				const auto index = phy.get<std::uint32_t>(tri + 4 + e * 4) & 0xFFFF;
				const long long p = points + static_cast<long long>(index) * 16;
				if (!phy.fits(p, 12) || p + 12 > end) return false;
				// IVP: x, -z, y in metres -> Source x, y, z in inches.
				corners[e] = {phy.f32(p) * METRES_TO_UNITS, phy.f32(p + 8) * METRES_TO_UNITS, -phy.f32(p + 4) * METRES_TO_UNITS};
			}
			// The swap of y and z mirrors: the corners go the other way round.
			piece.insert(piece.end(), {corners[0], corners[2], corners[1]});
		}
		pieces.push_back(std::move(piece));
		ledge += ledgeSize;
	}
	return !pieces.empty();
}

/// The material name of a texture relative to its $cdmaterials folder: the model's texture name
/// whose resolved path is `resolved`, else its file name.
std::string textureName(const std::string& resolved, const std::map<std::string, std::string>& byResolved) {
	const auto it = byResolved.find(toLower(resolved));
	if (it != byResolved.end()) return it->second;
	return resolved.substr(resolved.find_last_of('/') + 1);
}

/// A path inside the export folder: game paths without "..", absolute parts or drive letters.
bool safeRelative(std::string_view path) {
	if (path.empty() || path.front() == '/' || path.find(':') != std::string_view::npos || path.find('\\') != std::string_view::npos) return false;
	for (const auto& part : splitTrimmed(path, '/')) {
		if (part == ".." || part == ".") return false;
	}
	return true;
}

bool writeBytes(const fs::path& file, std::span<const std::byte> data, std::string* error) {
	std::error_code ec;
	fs::create_directories(file.parent_path(), ec);
	const fs::path temp = file.string() + ".mapsmith-tmp";
	{
		std::ofstream out(temp, std::ios::binary | std::ios::trunc);
		out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
		if (!out) {
			if (error) *error = "cannot write " + displayPath(file);
			return false;
		}
	}
	fs::rename(temp, file, ec);
	if (ec) {
		fs::remove(temp, ec);
		if (error) *error = "cannot write " + displayPath(file);
		return false;
	}
	return true;
}

} // namespace

std::vector<ModelHitboxSet> readHitboxes(std::span<const std::byte> data) {
	std::vector<ModelHitboxSet> out;
	const Bytes mdl(data);
	if (!mdl.fits(0, 408)) return out;
	const int numBones = mdl.i32(156);
	const long long boneIndex = mdl.i32(160);
	const int numSets = mdl.i32(172);
	const long long setIndex = mdl.i32(176);
	if (numSets <= 0 || numSets > 64 || !mdl.fits(setIndex, numSets * 12LL)) return out;
	for (int s = 0; s < numSets; ++s) {
		const long long set = setIndex + s * 12LL;
		ModelHitboxSet hitboxes;
		hitboxes.name = mdl.str(set + mdl.i32(set), 64);
		const int count = mdl.i32(set + 4);
		const long long boxes = set + mdl.i32(set + 8);
		if (count < 0 || count > 4096 || !mdl.fits(boxes, count * 68LL)) continue;
		for (int b = 0; b < count; ++b) {
			const long long box = boxes + b * 68LL;
			const int bone = mdl.i32(box);
			if (bone < 0 || bone >= numBones || !mdl.fits(boneIndex + bone * 216LL, 216)) continue;
			ModelHitbox hitbox;
			const long long boneData = boneIndex + bone * 216LL;
			hitbox.bone = mdl.str(boneData + mdl.i32(boneData), 128);
			hitbox.group = mdl.i32(box + 4);
			hitbox.min = {mdl.f32(box + 8), mdl.f32(box + 12), mdl.f32(box + 16)};
			hitbox.max = {mdl.f32(box + 20), mdl.f32(box + 24), mdl.f32(box + 28)};
			if (const int name = mdl.i32(box + 32); name != 0) hitbox.name = mdl.str(box + name, 64);
			if (!hitbox.bone.empty()) hitboxes.boxes.push_back(std::move(hitbox));
		}
		if (hitboxes.name.empty()) hitboxes.name = "default";
		out.push_back(std::move(hitboxes));
	}
	return out;
}

std::vector<std::vector<int>> readSkinFamilies(std::span<const std::byte> data) {
	const Bytes mdl(data);
	std::vector<std::vector<int>> out;
	if (!mdl.fits(0, 408)) return out;
	const int refs = mdl.i32(220), families = mdl.i32(224);
	const long long index = mdl.i32(228);
	if (refs <= 0 || families <= 0 || refs > 4096 || families > 4096 || !mdl.fits(index, static_cast<long long>(refs) * families * 2)) return out;
	for (int f = 0; f < families; ++f) {
		std::vector<int> row;
		for (int r = 0; r < refs; ++r) row.push_back(mdl.get<std::int16_t>(index + (static_cast<long long>(f) * refs + r) * 2));
		out.push_back(std::move(row));
	}
	return out;
}

std::optional<PhysicsModel> readPhysicsModel(std::span<const std::byte> data, std::string* error) {
	const auto fail = [&](const std::string& message) -> std::optional<PhysicsModel> {
		if (error) *error = message;
		return std::nullopt;
	};
	const Bytes phy(data);
	const int headerSize = phy.i32(0);
	const int solids = phy.i32(8);
	if (!phy.fits(0, 16) || headerSize < 16 || headerSize > 64 || solids <= 0 || solids > 256) return fail("not a physics model (.phy)");
	PhysicsModel model;
	long long offset = headerSize;
	for (int s = 0; s < solids; ++s) {
		const int size = phy.i32(offset);
		if (size <= 0 || !phy.fits(offset + 4, size)) return fail("physics model out of range");
		PhysicsModel::Solid solid;
		solid.index = s;
		if (!readSolidPieces(phy, offset + 4, size, solid.pieces)) return fail("physics model: solid " + std::to_string(s) + " could not be read");
		if (solid.pieces.size() > 1) model.concave = true;
		model.solids.push_back(std::move(solid));
		offset += 4 + static_cast<long long>(size);
	}
	// The text part: solid { "index" "name" "mass" ... }, ragdollconstraint { ... }, editparams { ... }.
	model.text = phy.str(offset, 1 << 20);
	try {
		const KvNode root = parseKeyValues(model.text);
		std::map<int, std::string> names;
		for (const auto& node : root.children) {
			if (!node.block) continue;
			if (iequals(node.key, "solid")) {
				int index = -1;
				try {
					index = std::stoi(node.get("index", "-1"));
				} catch (...) {
				}
				if (index >= 0) names[index] = node.get("name");
			} else if (iequals(node.key, "editparams")) {
				try {
					model.mass = std::stod(node.get("totalmass", "0"));
				} catch (...) {
				}
				if (node.get("concave") == "1") model.concave = true;
			}
		}
		for (auto& solid : model.solids) {
			if (const auto it = names.find(solid.index); it != names.end()) solid.bone = it->second;
		}
		for (const auto& node : root.children) {
			if (!node.block || !iequals(node.key, "ragdollconstraint")) continue;
			int child = -1;
			try {
				child = std::stoi(node.get("child", "-1"));
			} catch (...) {
			}
			const auto it = names.find(child);
			if (it == names.end()) continue;
			for (const char axis : {'x', 'y', 'z'}) {
				PhysicsModel::Constraint c;
				c.bone = it->second;
				c.axis = axis;
				try {
					c.min = std::stof(node.get(std::string(1, axis) + "min", "0"));
					c.max = std::stof(node.get(std::string(1, axis) + "max", "0"));
					c.friction = std::stof(node.get(std::string(1, axis) + "friction", "0"));
				} catch (...) {
					continue;
				}
				model.constraints.push_back(std::move(c));
			}
		}
	} catch (const std::exception&) {
		// A broken text part: the triangles still count.
	}
	if (!std::isfinite(model.mass) || model.mass < 0) model.mass = 0;
	return model;
}

std::string physicsSmd(const PhysicsModel& physics, const std::vector<StudioBone>& bones) {
	std::string out = smdSkeleton(bones);
	out += "triangles\n";
	const bool jointed = physics.solids.size() > 1;
	const auto rest = restBoneToModel(bones);
	for (const auto& solid : physics.solids) {
		int bone = 0;
		if (jointed) {
			const auto it = std::find_if(bones.begin(), bones.end(), [&](const StudioBone& b) { return iequals(b.name, solid.bone); });
			if (it == bones.end()) continue;   // a ragdoll piece on a bone the model lacks
			bone = int(it - bones.begin());
		}
		for (const auto& piece : solid.pieces) {
			for (std::size_t t = 0; t + 2 < piece.size(); t += 3) {
				AnimVec p[3];
				for (int k = 0; k < 3; ++k) {
					AnimVec v = piece[t + std::size_t(k)];
					if (jointed && std::size_t(bone) < rest.size()) v = transform(rest[std::size_t(bone)], v);   // bone space -> model space
					p[k] = rotateZ(v, -PI / 2);
				}
				const AnimVec a{p[1].x - p[0].x, p[1].y - p[0].y, p[1].z - p[0].z}, b{p[2].x - p[0].x, p[2].y - p[0].y, p[2].z - p[0].z};
				AnimVec n{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
				const float length = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
				n = length > 0 ? AnimVec{n.x / length, n.y / length, n.z / length} : AnimVec{0, 0, 1};
				out += "phy\n";
				for (const auto& v : p) {
					out += std::to_string(bone) + " " + number(v.x) + " " + number(v.y) + " " + number(v.z) + " " + number(n.x) + " " + number(n.y) + " " +
					       number(n.z) + " 0 0 1 " + std::to_string(bone) + " 1\n";
				}
			}
		}
	}
	out += "end\n";
	return out;
}

std::string sequenceSmd(const AnimatedModel& model, int sequence) {
	const auto& bones = model.bones();
	std::string out = "version 1\nnodes\n";
	for (std::size_t i = 0; i < bones.size(); ++i) out += std::to_string(i) + " \"" + clean(bones[i].name) + "\" " + std::to_string(bones[i].parent) + "\n";
	out += "end\nskeleton\n";
	const int frames = sequence >= 0 && sequence < int(model.sequences().size()) ? std::clamp(model.sequences()[std::size_t(sequence)].frames, 1, 100000) : 1;
	std::vector<AnimVec> pos;
	std::vector<AnimQuat> quat;
	for (int f = 0; f < frames; ++f) {
		model.localPose(sequence, frames > 1 ? float(f) / float(frames - 1) : 0.f, pos, quat, false);
		out += "time " + std::to_string(f) + "\n";
		for (std::size_t i = 0; i < bones.size() && i < pos.size(); ++i) {
			auto [p, q] = bones[i].parent < 0 ? turnedBack(pos[i], quat[i]) : std::pair{pos[i], quat[i]};
			const AnimVec r = quaternionAngles(q);
			out += std::to_string(i) + " " + number(p.x) + " " + number(p.y) + " " + number(p.z) + " " + number(r.x) + " " + number(r.y) + " " + number(r.z) +
			       "\n";
		}
	}
	out += "end\n";
	return out;
}

std::optional<ModelExport> decompileModel(const GameFileSystem& fs, std::string_view modelPath, const ModelExportOptions& options, std::string* error,
                                          const std::function<bool(int, int)>& progress) {
	const auto fail = [&](const std::string& message) -> std::optional<ModelExport> {
		if (error) *error = message;
		return std::nullopt;
	};
	const std::string path = normalizePath(modelPath);
	if (!endsWithI(path, ".mdl")) return fail("not a .mdl file");
	const std::string stem = path.substr(0, path.size() - 4);
	const auto mdl = fs.read(path);
	if (!mdl) return fail(path + " not found");
	std::string why;
	const auto info = readModelInfo(*mdl, &why);
	if (!info) return fail(path + ": " + why);
	auto animated = AnimatedModel::load(fs, path, &why);
	if (!animated) return fail(path + ": " + why);
	const auto& bones = animated->bones();
	const std::string name = fileStem(stem.substr(stem.find_last_of('/') + 1));

	ModelExport out;
	out.qcFile = name + ".qc";
	std::set<std::string> used{out.qcFile};

	// Texture names -> materials, through the $cdmaterials folders (the first folder that has it).
	std::vector<std::string> folders;
	for (auto folder : info->materialDirs) {
		folder = normalizePath(folder);
		if (!folder.empty() && folder.back() != '/') folder += '/';
		if (std::none_of(folders.begin(), folders.end(), [&](const std::string& f) { return iequals(f, folder); })) folders.push_back(folder);
	}
	if (folders.empty()) folders.emplace_back();
	const auto materialExists = [&](const std::string& material) { return fs.exists("materials/" + material + ".vmt"); };
	std::map<std::string, std::string> byResolved;   // lower-case material -> texture name
	std::vector<std::string> resolvedMaterials;
	for (const auto& texture : info->materials) {
		const std::string t = normalizePath(texture);
		std::string resolved = folders.front() + t;
		for (const auto& folder : folders) {
			if (materialExists(folder + t)) {
				resolved = folder + t;
				break;
			}
		}
		byResolved.emplace(toLower(resolved), t);
		resolvedMaterials.push_back(resolved);
	}

	const auto vvd = fs.read(stem + ".vvd");
	auto vtx = fs.read(stem + ".dx90.vtx");
	if (!vtx) vtx = fs.read(stem + ".vtx");
	if (!vtx) vtx = fs.read(stem + ".dx80.vtx");
	const std::vector<std::byte> empty;
	const ModelFiles files{*mdl, vvd ? *vvd : empty, vtx ? *vtx : empty};

	// Steps for the progress: body group models, sequences, the rest.
	std::vector<int> sequences;
	if (!options.animations) {
		// none
	} else if (options.sequences.empty()) {
		for (std::size_t s = 0; s < animated->sequences().size(); ++s) {
			if (animated->sequences()[s].model == 0) sequences.push_back(int(s));
		}
	} else {
		for (const auto& wanted : options.sequences) {
			const int s = animated->findSequence(wanted);
			if (s >= 0 && std::find(sequences.begin(), sequences.end(), s) == sequences.end()) sequences.push_back(s);
			else if (s < 0) out.notes.push_back("sequence " + wanted + " not found");
		}
	}
	int bodyModels = 0;
	for (const auto& group : info->bodyGroups) bodyModels += int(group.options.size());
	const int total = (options.reference ? bodyModels : 0) + int(sequences.size()) + 2;
	int done = 0;
	const auto step = [&]() { return !progress || progress(++done, total); };

	// Reference .smd files: every model of every body part.
	std::string bodies;
	std::string firstReference;
	if (options.reference) {
		const bool single = info->bodyGroups.size() == 1 && info->bodyGroups.front().options.size() == 1;
		for (std::size_t b = 0; b < info->bodyGroups.size(); ++b) {
			const auto& group = info->bodyGroups[b];
			std::string lines;
			for (std::size_t m = 0; m < group.options.size(); ++m) {
				if (!step()) return fail("stopped");
				auto geometry = buildBodyPartGeometry(files, int(b), int(m), &why, materialExists);
				if (!geometry) return fail(path + ": " + why);
				if (geometry->empty()) {
					lines += "\tblank\n";
					continue;
				}
				for (auto& mesh : geometry->meshes) mesh.material = textureName(mesh.material, byResolved);
				std::string option = group.options[m];
				if (endsWithI(option, ".smd")) option.resize(option.size() - 4);
				const std::string file = uniqueName(used, fileStem(option.empty() ? group.name : option), ".smd");
				out.texts[file] = referenceSmd(*geometry, bones, true);
				if (firstReference.empty()) firstReference = file;
				if (single) {
					bodies += "$body \"" + clean(group.name) + "\" \"" + file + "\"\n";
				} else {
					lines += "\tstudio \"" + file + "\"\n";
				}
			}
			if (!single) bodies += "$bodygroup \"" + clean(group.name) + "\"\n{\n" + lines + "}\n";
		}
	}

	// Animation .smd files.
	std::string sequenceLines;
	for (const int s : sequences) {
		if (!step()) return fail("stopped");
		const auto& seq = animated->sequences()[std::size_t(s)];
		const std::string file = "anims/" + uniqueName(used, fileStem(seq.name), ".smd");
		out.texts[file] = sequenceSmd(*animated, s);
		++out.sequences;
		sequenceLines += "$sequence \"" + clean(seq.name) + "\" \"" + file + "\" fps " + number(seq.fps > 0 ? seq.fps : 30);
		if (seq.looping) sequenceLines += " loop";
		if (seq.delta) sequenceLines += " delta";
		if (!seq.activity.empty()) sequenceLines += " activity \"" + clean(seq.activity) + "\" 1";
		sequenceLines += "\n";
		if (seq.blends > 1) out.notes.push_back("sequence " + seq.name + ": a blend of " + std::to_string(seq.blends) + " poses, only the middle one is written");
	}

	// The physics model.
	std::string collision;
	if (!step()) return fail("stopped");
	if (options.physics) {
		if (const auto phy = fs.read(stem + ".phy")) {
			if (auto physics = readPhysicsModel(*phy, &why)) {
				const std::string file = uniqueName(used, name + "_physics", ".smd");
				out.texts[file] = physicsSmd(*physics, bones);
				const double mass = physics->mass > 0 ? physics->mass : info->mass;
				if (physics->solids.size() > 1) {
					collision += "$collisionjoints \"" + file + "\"\n{\n";
					if (mass > 0) collision += "\t$mass " + number(float(mass)) + "\n";
					for (const auto& c : physics->constraints) {
						collision += "\t$jointconstrain \"" + clean(c.bone) + "\" " + std::string(1, c.axis) + " limit " + number(c.min) + " " + number(c.max) + " " +
						             number(c.friction) + "\n";
					}
					collision += "}\n";
				} else {
					collision += "$collisionmodel \"" + file + "\"\n{\n";
					collision += mass > 0 ? "\t$mass " + number(float(mass)) + "\n" : "\t$automass\n";
					if (physics->concave) collision += "\t$concave\n";
					collision += "}\n";
				}
			} else {
				out.notes.push_back("physics model not read: " + why);
			}
		}
	}

	// Materials and their textures.
	if (!step()) return fail("stopped");
	if (options.materials) {
		for (const auto& material : resolvedMaterials) {
			const std::string vmt = "materials/" + material + ".vmt";
			if (!fs.exists(vmt) || !safeRelative(vmt)) {
				out.notes.push_back("material " + material + " not found in the game");
				continue;
			}
			out.copies[vmt] = vmt;
			const auto parsed = loadMaterial(fs, material);
			if (!parsed) continue;
			for (const auto& [key, value] : parsed->params) {
				if (value.empty() || iequals(value, "env_cubemap") || startsWithI(value, "_rt_")) continue;
				const std::string vtf = "materials/" + normalizePath(value) + ".vtf";
				if (!safeRelative(vtf) || out.copies.count(vtf) || !fs.exists(vtf)) continue;
				out.copies[vtf] = vtf;
				if (options.pictures) out.pictures.push_back(vtf);
			}
		}
	}

	// The .qc.
	std::string qc = "// Taken apart by Mapsmith from " + path + " (Models page, Export for editing).\n";
	qc += "// Compile with the game's studiomdl: studiomdl -game \"<mod folder>\" \"" + out.qcFile + "\"\n";
	std::string modelName = path;
	if (startsWithI(modelName, "models/")) modelName = modelName.substr(7);
	qc += "$modelname \"" + clean(modelName) + "\"\n";
	for (const auto& folder : folders) qc += "$cdmaterials \"" + clean(folder) + "\"\n";
	if (info->staticProp) qc += "$staticprop\n";
	if (!info->surfaceProp.empty()) qc += "$surfaceprop \"" + clean(info->surfaceProp) + "\"\n";
	qc += "\n" + bodies;
	// Skin families: the columns that differ from the first skin.
	const auto skins = readSkinFamilies(*mdl);
	if (skins.size() > 1) {
		std::vector<std::size_t> columns;
		for (std::size_t r = 0; r < skins.front().size(); ++r) {
			if (std::any_of(skins.begin(), skins.end(), [&](const std::vector<int>& row) { return row[r] != skins.front()[r]; })) columns.push_back(r);
		}
		if (!columns.empty()) {
			qc += "\n$texturegroup \"skinfamilies\"\n{\n";
			for (const auto& row : skins) {
				qc += "\t{";
				for (const auto c : columns) {
					const int t = row[c];
					const std::string texture = t >= 0 && t < int(info->materials.size()) ? normalizePath(info->materials[std::size_t(t)]) : "tools/toolsnodraw";
					qc += " \"" + clean(texture) + "\"";
				}
				qc += " }\n";
			}
			qc += "}\n";
		}
	}
	if (!info->keyValues.empty()) qc += "\n$keyvalues\n{\n" + info->keyValues + "\n}\n";
	if (!info->includeModels.empty()) qc += "\n";
	for (auto include : info->includeModels) {
		include = normalizePath(include);
		if (startsWithI(include, "models/")) include = include.substr(7);
		qc += "$includemodel \"" + clean(include) + "\"\n";
	}
	const auto attachments = readAttachments(*mdl);
	if (!attachments.empty()) qc += "\n";
	for (const auto& a : attachments) {
		qc += "$attachment \"" + clean(a.name) + "\" \"" + clean(a.bone) + "\" " + number(a.origin.x) + " " + number(a.origin.y) + " " + number(a.origin.z) +
		      " rotate " + number(a.angles.x) + " " + number(a.angles.y) + " " + number(a.angles.z) + "\n";
	}
	for (const auto& set : readHitboxes(*mdl)) {
		if (set.boxes.empty()) continue;
		qc += "\n$hboxset \"" + clean(set.name) + "\"\n";
		for (const auto& box : set.boxes) {
			qc += "$hbox " + std::to_string(box.group) + " \"" + clean(box.bone) + "\" " + number(box.min.x) + " " + number(box.min.y) + " " + number(box.min.z) + " " +
			      number(box.max.x) + " " + number(box.max.y) + " " + number(box.max.z) + "\n";
		}
	}
	if (sequenceLines.empty() && info->includeModels.empty() && !firstReference.empty()) {
		sequenceLines = "$sequence \"idle\" \"" + firstReference + "\" fps 30\n";   // studiomdl needs one sequence
	}
	if (!sequenceLines.empty()) qc += "\n" + sequenceLines;
	if (!collision.empty()) qc += "\n" + collision;
	out.texts[out.qcFile] = qc;
	out.notes.push_back("not written: LODs, flexes (faces), sequence events, IK rules");
	if (progress) progress(total, total);
	return out;
}

std::optional<std::vector<fs::path>> writeModelExport(const GameFileSystem& gameFs, const ModelExport& exported, const fs::path& folder, std::string* error,
                                                      const std::function<bool(int, int)>& progress) {
	std::vector<fs::path> written;
	const int total = int(exported.texts.size() + exported.copies.size() + exported.pictures.size());
	int done = 0;
	const auto step = [&]() { return !progress || progress(++done, total); };
	const auto stop = [&]() -> std::optional<std::vector<fs::path>> {
		if (error) *error = "stopped";
		return std::nullopt;
	};
	for (const auto& [file, text] : exported.texts) {
		if (!step()) return stop();
		if (!safeRelative(file)) continue;
		const fs::path target = folder / toNativePath(file);
		if (!writeBytes(target, std::as_bytes(std::span(text.data(), text.size())), error)) return std::nullopt;
		written.push_back(target);
	}
	for (const auto& [game, file] : exported.copies) {
		if (!step()) return stop();
		if (!safeRelative(file)) continue;
		const auto data = gameFs.read(game);
		if (!data) continue;
		const fs::path target = folder / toNativePath(file);
		if (!writeBytes(target, *data, error)) return std::nullopt;
		written.push_back(target);
	}
	for (const auto& vtf : exported.pictures) {
		if (!step()) return stop();
		const auto data = gameFs.read(vtf);
		if (!data || !safeRelative(vtf)) continue;
		const auto image = decodeTexture(*data);
		if (!image) continue;
		const auto png = encodePng(*image);
		if (png.empty()) continue;
		const fs::path target = folder / toNativePath(vtf.substr(0, vtf.size() - 4) + ".png");
		if (!writeBytes(target, png, error)) return std::nullopt;
		written.push_back(target);
	}
	return written;
}

fs::path modelExportFolder(const fs::path& modDir, std::string_view modelPath) {
	std::string path = normalizePath(modelPath);
	if (startsWithI(path, "models/")) path = path.substr(7);
	if (endsWithI(path, ".mdl")) path.resize(path.size() - 4);
	std::string safe;
	for (const auto& part : splitTrimmed(path, '/')) {
		if (part.empty() || part == "." || part == "..") continue;
		safe += (safe.empty() ? "" : "/") + fileStem(part);
	}
	return modDir / "modelsrc" / toNativePath(safe.empty() ? std::string("model") : safe);
}

} // namespace sme
