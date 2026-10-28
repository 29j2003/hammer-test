// animation_import.cpp - New animations for an existing model (see animation_import.h).

#include <sme/animation_import.h>

#include <sme/keyvalues.h>
#include <sme/text.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <sstream>

namespace sme {

namespace {

constexpr float PI = 3.14159265358979f;

/// studiomdl's default rotation of .smd data: 90 degrees about Z.
AnimVec rotateZ(const AnimVec& v, float angle) {
	const float c = std::cos(angle), s = std::sin(angle);
	return {c * v.x - s * v.y, s * v.x + c * v.y, v.z};
}
AnimQuat zQuat(float angle) {
	return {0, 0, std::sin(angle * 0.5f), std::cos(angle * 0.5f)};
}
AnimQuat multiplyQ(const AnimQuat& p, const AnimQuat& q) {
	return {p.x * q.w + p.y * q.z - p.z * q.y + p.w * q.x, -p.x * q.z + p.y * q.w + p.z * q.x + p.w * q.y,
	        p.x * q.y - p.y * q.x + p.z * q.w + p.w * q.z, -p.x * q.x - p.y * q.y - p.z * q.z + p.w * q.w};
}

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

/// A line split into words; "quoted strings" stay one word.
std::vector<std::string> words(std::string_view line) {
	std::vector<std::string> out;
	std::size_t i = 0;
	while (i < line.size()) {
		while (i < line.size() && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r')) ++i;
		if (i >= line.size()) break;
		if (line[i] == '"') {
			const std::size_t end = line.find('"', i + 1);
			out.emplace_back(line.substr(i + 1, (end == std::string_view::npos ? line.size() : end) - i - 1));
			i = end == std::string_view::npos ? line.size() : end + 1;
		} else {
			std::size_t end = i;
			while (end < line.size() && line[end] != ' ' && line[end] != '\t' && line[end] != '\r') ++end;
			out.emplace_back(line.substr(i, end - i));
			i = end;
		}
	}
	return out;
}

bool toInt(const std::string& s, int& out) {
	try {
		std::size_t used = 0;
		out = std::stoi(s, &used);
		return used == s.size();
	} catch (...) {
		return false;
	}
}
bool toFloat(const std::string& s, float& out) {
	try {
		std::size_t used = 0;
		out = std::stof(s, &used);
		return used == s.size() && std::isfinite(out);
	} catch (...) {
		return false;
	}
}

/// Source's quote-free bone and material names: no quotes or line breaks in a written file.
std::string clean(std::string_view text) {
	std::string out;
	for (const char c : text) {
		if (c != '"' && c != '\n' && c != '\r') out.push_back(c);
	}
	return out;
}

/// The angles ($attachment rotate, degrees: pitch yaw roll) of a 3x4 matrix, as MatrixAngles.
AnimVec matrixAngles(const BoneMatrix& m) {
	const float forward[3] = {m.m[0][0], m.m[1][0], m.m[2][0]};
	const float left[3] = {m.m[0][1], m.m[1][1], m.m[2][1]};
	const float up2 = m.m[2][2];
	const float xyDist = std::sqrt(forward[0] * forward[0] + forward[1] * forward[1]);
	AnimVec angles;   // pitch, yaw, roll
	if (xyDist > 0.001f) {
		angles.y = std::atan2(forward[1], forward[0]);
		angles.x = std::atan2(-forward[2], xyDist);
		angles.z = std::atan2(left[2], up2);
	} else {
		angles.y = std::atan2(-left[0], left[1]);
		angles.x = std::atan2(-forward[2], xyDist);
		angles.z = 0;
	}
	return angles;
}

} // namespace

std::optional<SmdAnimation> parseSmd(std::string_view text, std::string* error) {
	const auto fail = [&](const std::string& message, int line) -> std::optional<SmdAnimation> {
		if (error) *error = "line " + std::to_string(line) + ": " + message;
		return std::nullopt;
	};
	SmdAnimation smd;
	enum class Section { None, Nodes, Skeleton, Other } section = Section::None;
	std::vector<std::pair<AnimVec, AnimVec>> current;
	bool inFrame = false;
	int lineNumber = 0;
	bool sawVersion = false;
	std::size_t start = 0;
	while (start <= text.size()) {
		std::size_t end = text.find('\n', start);
		if (end == std::string_view::npos) end = text.size();
		const std::string_view raw = text.substr(start, end - start);
		start = end + 1;
		++lineNumber;
		const std::string_view line = trim(raw);
		if (line.empty() || line.starts_with("//") || line.starts_with("#") || line.starts_with(";")) {
			if (section == Section::None && smd.nodes.empty() && line.find(RELATIVE_SMD_MARKER) != std::string_view::npos) smd.relative = true;
			if (end == text.size()) break;
			continue;
		}
		const auto w = words(line);
		if (section == Section::None) {
			if (w[0] == "version") sawVersion = true;
			else if (w[0] == "nodes") section = Section::Nodes;
			else if (w[0] == "skeleton") section = Section::Skeleton;
			else if (w[0] == "triangles" || w[0] == "vertexanimation") section = Section::Other;
		} else if (w[0] == "end") {
			if (section == Section::Skeleton && inFrame) smd.frames.push_back(current);
			section = Section::None;
			inFrame = false;
		} else if (section == Section::Nodes) {
			int id = 0, parent = -1;
			if (w.size() < 3 || !toInt(w[0], id) || !toInt(w[2], parent)) return fail("a node is \"<id> \\\"<name>\\\" <parent>\"", lineNumber);
			if (id != int(smd.nodes.size())) return fail("nodes must be numbered 0, 1, 2 ...", lineNumber);
			if (parent < -1 || parent >= id) return fail("a node's parent must come before it", lineNumber);
			if (smd.nodes.size() >= 1024) return fail("too many nodes", lineNumber);
			smd.nodes.push_back({w[1], parent});
		} else if (section == Section::Skeleton) {
			if (w[0] == "time") {
				if (inFrame) smd.frames.push_back(current);
				if (smd.frames.size() >= 100000) return fail("too many frames", lineNumber);
				// A frame starts from the one before (bones not listed keep their values).
				if (current.size() != smd.nodes.size()) current.assign(smd.nodes.size(), {});
				inFrame = true;
				continue;
			}
			int id = 0;
			float v[6];
			if (!inFrame || w.size() < 7 || !toInt(w[0], id)) return fail("a bone line is \"<id> x y z rx ry rz\" after \"time\"", lineNumber);
			for (int k = 0; k < 6; ++k) {
				if (!toFloat(w[std::size_t(k + 1)], v[k])) return fail("not a number: " + w[std::size_t(k + 1)], lineNumber);
			}
			if (id < 0 || id >= int(smd.nodes.size())) return fail("bone " + std::to_string(id) + " is not in the nodes", lineNumber);
			current[std::size_t(id)] = {{v[0], v[1], v[2]}, {v[3], v[4], v[5]}};
		}
		if (end == text.size()) break;
	}
	if (!sawVersion && smd.nodes.empty()) return fail("not an .smd file", 1);
	if (smd.nodes.empty()) return fail("the file has no bones (nodes)", lineNumber);
	if (smd.frames.empty()) return fail("the file has no frames (skeleton)", lineNumber);
	return smd;
}

int SkeletonMatch::matched() const {
	return int(std::count_if(toModel.begin(), toModel.end(), [](int b) { return b >= 0; }));
}

SkeletonMatch matchSkeleton(const SmdAnimation& smd, const std::vector<StudioBone>& bones) {
	SkeletonMatch match;
	std::vector<bool> used(bones.size(), false);
	for (const auto& node : smd.nodes) {
		int found = -1;
		for (std::size_t b = 0; b < bones.size(); ++b) {
			if (iequals(bones[b].name, node.name)) {
				found = int(b);
				used[b] = true;
				break;
			}
		}
		match.toModel.push_back(found);
		if (found < 0) match.notInModel.push_back(node.name);
	}
	for (std::size_t b = 0; b < bones.size(); ++b) {
		if (!used[b]) match.notInSmd.push_back(bones[b].name);
	}
	return match;
}

AnimVec quaternionAngles(const AnimQuat& q) {
	const AnimVec qa = matrixAngles(quaternionMatrix(q, {}));   // pitch, yaw, roll
	return {qa.z, qa.x, qa.y};                                  // RadianEuler: roll, pitch, yaw
}

bool animationNeedsCompleting(const SmdAnimation& smd, const std::vector<StudioBone>& bones) {
	return smd.relative || !matchSkeleton(smd, bones).notInSmd.empty();
}

std::string completedAnimationSmd(const SmdAnimation& smd, const std::vector<StudioBone>& bones) {
	const SkeletonMatch match = matchSkeleton(smd, bones);
	std::vector<int> fromSmd(bones.size(), -1);   // model bone -> .smd node
	for (std::size_t n = 0; n < match.toModel.size(); ++n) {
		if (match.toModel[n] >= 0 && fromSmd[std::size_t(match.toModel[n])] < 0) fromSmd[std::size_t(match.toModel[n])] = int(n);
	}
	// A bone in the .smd's own space (root bones turned back for studiomdl, like referenceSmd).
	const auto smdSpace = [&](std::size_t b, AnimVec pos, AnimQuat quat) {
		if (bones[b].parent < 0) {
			pos = rotateZ(pos, -PI / 2);
			quat = multiplyQ(zQuat(-PI / 2), quat);
		}
		return std::pair{pos, quaternionAngles(quat)};
	};
	std::string out = "version 1\n// Completed by Mapsmith for the model's skeleton: every bone in every frame.\nnodes\n";
	for (std::size_t b = 0; b < bones.size(); ++b) out += std::to_string(b) + " \"" + clean(bones[b].name) + "\" " + std::to_string(bones[b].parent) + "\n";
	out += "end\nskeleton\n";
	for (std::size_t f = 0; f < smd.frames.size(); ++f) {
		out += "time " + std::to_string(f) + "\n";
		const auto& frame = smd.frames[f];
		for (std::size_t b = 0; b < bones.size(); ++b) {
			const int n = fromSmd[b];
			std::pair<AnimVec, AnimVec> value;
			if (n >= 0 && std::size_t(n) < frame.size() && !smd.relative) {
				value = frame[std::size_t(n)];   // whole pose from the file, already in its space
			} else if (n >= 0 && std::size_t(n) < frame.size()) {
				const auto& [offset, angles] = frame[std::size_t(n)];
				const AnimVec pos{bones[b].pos.x + offset.x, bones[b].pos.y + offset.y, bones[b].pos.z + offset.z};
				value = smdSpace(b, pos, multiplyQ(bones[b].quat, angleQuaternion(angles)));
			} else {
				value = smdSpace(b, bones[b].pos, bones[b].quat);   // not in the file: the rest pose
			}
			const auto& [pos, rot] = value;
			out += std::to_string(b) + " " + number(pos.x) + " " + number(pos.y) + " " + number(pos.z) + " " + number(rot.x) + " " + number(rot.y) + " " +
			       number(rot.z) + "\n";
		}
	}
	out += "end\n";
	return out;
}

int addPreviewSequence(AnimatedModel& model, const SmdAnimation& smd, const ImportedAnimation& options) {
	const auto& bones = model.bones();
	const SkeletonMatch match = matchSkeleton(smd, bones);
	std::vector<std::vector<std::pair<AnimVec, AnimQuat>>> frames;
	for (const auto& frame : smd.frames) {
		std::vector<std::pair<AnimVec, AnimQuat>> pose;
		for (const auto& bone : bones) pose.push_back({bone.pos, bone.quat});
		for (std::size_t n = 0; n < smd.nodes.size() && n < frame.size(); ++n) {
			const int target = match.toModel[n];
			if (target < 0) continue;
			AnimVec pos = frame[n].first;
			AnimQuat quat = angleQuaternion(frame[n].second);
			if (smd.nodes[n].parent < 0) {
				// studiomdl turns the .smd's root bones by 90 degrees about Z.
				pos = rotateZ(pos, PI / 2);
				quat = multiplyQ(zQuat(PI / 2), quat);
			}
			pose[std::size_t(target)] = {pos, quat};
		}
		frames.push_back(std::move(pose));
	}
	StudioSequence seq;
	seq.name = options.name;
	seq.activity = options.activity;
	seq.fps = options.fps > 0 ? options.fps : 30;
	seq.looping = options.loop;
	return model.addKeyframedSequence(std::move(seq), std::move(frames));
}

std::string animationModelPath(std::string_view originalModel) {
	std::string path = normalizePath(originalModel);
	if (startsWithI(path, "models/")) path = path.substr(7);
	if (startsWithI(path, "mapsmith/")) path = path.substr(9);   // already ours: the same file again
	return "models/mapsmith/" + path;
}

std::string animationSourceFolder(std::string_view originalModel) {
	std::string path = animationModelPath(originalModel).substr(16);   // after "models/mapsmith/"
	if (endsWithI(path, ".mdl")) path = path.substr(0, path.size() - 4);
	return "modelsrc/mapsmith/" + path;
}

std::vector<ModelAttachment> readAttachments(std::span<const std::byte> mdl) {
	std::vector<ModelAttachment> out;
	const auto fits = [&](long long offset, long long size) {
		return offset >= 0 && size >= 0 && static_cast<unsigned long long>(offset + size) <= mdl.size();
	};
	const auto i32 = [&](long long offset) {
		std::int32_t v = 0;
		if (fits(offset, 4)) std::memcpy(&v, mdl.data() + offset, 4);
		return v;
	};
	const auto f32 = [&](long long offset) {
		float v = 0;
		if (fits(offset, 4)) std::memcpy(&v, mdl.data() + offset, 4);
		return std::isfinite(v) ? v : 0.f;
	};
	const auto str = [&](long long offset) {
		std::string s;
		for (long long i = offset; fits(i, 1) && s.size() < 128; ++i) {
			const char c = static_cast<char>(mdl[std::size_t(i)]);
			if (c == '\0') break;
			s.push_back(c);
		}
		return s;
	};
	if (!fits(0, 408)) return out;
	const int numBones = i32(156);
	const long long boneIndex = i32(160);
	const int count = i32(240);
	const long long index = i32(244);
	if (count <= 0 || count > 512 || !fits(index, count * 92LL)) return out;
	for (int i = 0; i < count; ++i) {
		const long long a = index + i * 92LL;
		ModelAttachment attachment;
		attachment.name = str(a + i32(a));
		const int bone = i32(a + 8);
		if (bone >= 0 && bone < numBones && fits(boneIndex + bone * 216LL, 216)) {
			const long long b = boneIndex + bone * 216LL;
			attachment.bone = str(b + i32(b));
		}
		BoneMatrix local;
		for (int r = 0; r < 3; ++r) {
			for (int c = 0; c < 4; ++c) local.m[r][c] = f32(a + 12 + (r * 4 + c) * 4);
		}
		attachment.origin = {local.m[0][3], local.m[1][3], local.m[2][3]};
		const AnimVec angles = matrixAngles(local);
		attachment.angles = {angles.x * 180.f / PI, angles.y * 180.f / PI, angles.z * 180.f / PI};
		if (!attachment.name.empty() && !attachment.bone.empty()) out.push_back(std::move(attachment));
	}
	return out;
}

std::string referenceSmd(const ModelGeometry& geometry, const std::vector<StudioBone>& bones) {
	std::string out = "version 1\nnodes\n";
	if (bones.empty()) {
		out += "0 \"root\" -1\n";
	}
	for (std::size_t i = 0; i < bones.size(); ++i) out += std::to_string(i) + " \"" + clean(bones[i].name) + "\" " + std::to_string(bones[i].parent) + "\n";
	out += "end\nskeleton\ntime 0\n";
	if (bones.empty()) out += "0 0 0 0 0 0 0\n";
	for (std::size_t i = 0; i < bones.size(); ++i) {
		AnimVec pos = bones[i].pos;
		AnimQuat quat = bones[i].quat;
		if (bones[i].parent < 0) {
			// Turned back: studiomdl turns root bones by 90 degrees about Z again.
			pos = rotateZ(pos, -PI / 2);
			quat = multiplyQ(zQuat(-PI / 2), quat);
		}
		const AnimVec rot = quaternionAngles(quat);
		out += std::to_string(i) + " " + number(pos.x) + " " + number(pos.y) + " " + number(pos.z) + " " + number(rot.x) + " " + number(rot.y) + " " +
		       number(rot.z) + "\n";
	}
	out += "end\ntriangles\n";
	for (const auto& mesh : geometry.meshes) {
		const std::string material = clean(mesh.material.substr(mesh.material.find_last_of('/') + 1));
		for (std::uint32_t t = 0; t + 2 < mesh.indexCount; t += 3) {
			out += material + "\n";
			for (int k = 0; k < 3; ++k) {
				const std::uint32_t index = geometry.indices[mesh.firstIndex + t + std::uint32_t(k)];
				const ModelVertex& v = geometry.vertices[index];
				const AnimVec p = rotateZ({v.position[0], v.position[1], v.position[2]}, -PI / 2);
				const AnimVec n = rotateZ({v.normal[0], v.normal[1], v.normal[2]}, -PI / 2);
				int parent = 0;
				std::string links;
				int linkCount = 0;
				if (index < geometry.weights.size()) {
					const auto& w = geometry.weights[index];
					for (int b = 0; b < w.count; ++b) {
						if (w.bone[b] >= std::max<std::size_t>(1, bones.size()) || !(w.weight[b] > 0)) continue;
						if (linkCount == 0) parent = w.bone[b];
						links += " " + std::to_string(w.bone[b]) + " " + number(w.weight[b]);
						++linkCount;
					}
				}
				out += std::to_string(parent) + " " + number(p.x) + " " + number(p.y) + " " + number(p.z) + " " + number(n.x) + " " + number(n.y) + " " +
				       number(n.z) + " " + number(v.uv[0]) + " " + number(1.f - v.uv[1]);
				out += linkCount > 0 ? " " + std::to_string(linkCount) + links : std::string(" 0");
				out += "\n";
			}
		}
	}
	out += "end\n";
	return out;
}

std::string animationModelQc(std::string_view originalModel, const ModelGeometry& geometry, const ModelInfo& info,
                             const std::vector<ModelAttachment>& attachments, const std::vector<ImportedAnimation>& animations) {
	const std::string original = normalizePath(originalModel);
	const std::string model = animationModelPath(original).substr(7);   // $modelname is below models/
	std::string qc;
	qc += "// Made by Mapsmith: " + original + " with new animations (Animations page, Add animation).\n";
	qc += "// Everything of the original stays through $includemodel; written again on every change.\n";
	qc += "$modelname \"" + model + "\"\n";
	// Material folders: the model's own, then those its meshes were found in.
	std::vector<std::string> folders;
	const auto addFolder = [&folders](std::string folder) {
		folder = normalizePath(folder);
		if (!folder.empty() && folder.back() != '/') folder += '/';
		if (!folder.empty() && std::none_of(folders.begin(), folders.end(), [&](const std::string& f) { return iequals(f, folder); })) folders.push_back(folder);
	};
	for (const auto& folder : info.materialDirs) addFolder(folder);
	for (const auto& mesh : geometry.meshes) {
		const auto slash = mesh.material.find_last_of('/');
		if (slash != std::string::npos) addFolder(mesh.material.substr(0, slash));
	}
	for (const auto& folder : folders) qc += "$cdmaterials \"" + clean(folder) + "\"\n";
	if (!info.surfaceProp.empty()) qc += "$surfaceprop \"" + clean(info.surfaceProp) + "\"\n";
	qc += "$body \"body\" \"reference.smd\"\n";
	std::string include = original;
	if (startsWithI(include, "models/")) include = include.substr(7);
	qc += "$includemodel \"" + clean(include) + "\"\n";
	for (const auto& a : attachments) {
		qc += "$attachment \"" + clean(a.name) + "\" \"" + clean(a.bone) + "\" " + number(a.origin.x) + " " + number(a.origin.y) + " " + number(a.origin.z) +
		      " rotate " + number(a.angles.x) + " " + number(a.angles.y) + " " + number(a.angles.z) + "\n";
	}
	for (const auto& anim : animations) {
		qc += "$sequence \"" + clean(anim.name) + "\" \"" + clean(anim.smdFile) + "\" fps " + number(anim.fps > 0 ? anim.fps : 30);
		if (anim.loop) qc += " loop";
		if (!anim.activity.empty()) qc += " activity \"" + clean(anim.activity) + "\" 1";
		qc += "\n";
	}
	if (!info.keyValues.empty()) {
		qc += "$keyvalues\n{\n" + info.keyValues + "\n}\n";
	}
	return qc;
}

std::vector<ImportedAnimation> readAnimationList(std::string_view text) {
	std::vector<ImportedAnimation> out;
	try {
		for (const auto& block : parseKeyValues(text).children) {
			for (const auto& entry : block.children) {
				if (!entry.block) continue;
				ImportedAnimation anim;
				anim.name = entry.key;
				anim.smdFile = entry.get("smd", entry.key + ".smd");
				float fps = 30;
				anim.fps = toFloat(entry.get("fps", "30"), fps) && fps > 0 ? fps : 30.f;
				anim.loop = entry.get("loop", "0") == "1";
				anim.activity = entry.get("activity");
				if (!anim.name.empty()) out.push_back(std::move(anim));
			}
		}
	} catch (const std::exception&) {
	}
	return out;
}

std::string writeAnimationList(const std::vector<ImportedAnimation>& animations) {
	std::string out = "// Animations Mapsmith added to this model (Animations page). Edit with care.\n\"animations\"\n{\n";
	for (const auto& anim : animations) {
		out += "\t\"" + clean(anim.name) + "\"\n\t{\n";
		out += "\t\t\"smd\" \"" + clean(anim.smdFile) + "\"\n";
		out += "\t\t\"fps\" \"" + number(anim.fps) + "\"\n";
		out += "\t\t\"loop\" \"" + std::string(anim.loop ? "1" : "0") + "\"\n";
		if (!anim.activity.empty()) out += "\t\t\"activity\" \"" + clean(anim.activity) + "\"\n";
		out += "\t}\n";
	}
	out += "}\n";
	return out;
}

std::string sequenceNameFromFile(std::string_view fileName) {
	std::string name(fileName);
	const auto slash = name.find_last_of("/\\");
	if (slash != std::string::npos) name = name.substr(slash + 1);
	if (const auto dot = name.find_last_of('.'); dot != std::string::npos && dot > 0) name = name.substr(0, dot);
	std::string out;
	for (const char c : name) {
		const auto u = static_cast<unsigned char>(c);
		if (std::isalnum(u)) out.push_back(char(std::tolower(u)));
		else if (!out.empty() && out.back() != '_') out.push_back('_');
	}
	while (!out.empty() && out.back() == '_') out.pop_back();
	return out.empty() ? "new_animation" : out;
}

} // namespace sme
