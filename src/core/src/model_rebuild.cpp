// model_rebuild.cpp - Rebuilding a game model with changes into the mod (see model_rebuild.h).

#include <sme/model_rebuild.h>

#include <sme/filesystem.h>
#include <sme/keyvalues.h>
#include <sme/text.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace sme {

namespace fs = std::filesystem;

namespace {

constexpr std::string_view BACKUP = ".mapsmith-backup";
constexpr std::string_view RECORD_KIND = "model rebuild";
constexpr std::string_view CHANGES_FILE = "changes.txt";

bool writeFile(const fs::path& path, std::string_view text, std::string* error) {
	std::error_code ec;
	fs::create_directories(path.parent_path(), ec);
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	if (out) out.write(text.data(), std::streamsize(text.size()));
	if (!out) {
		if (error) *error = "cannot write " + displayPath(path);
		return false;
	}
	return true;
}

std::string number(float v) {
	char buffer[32];
	const auto result = std::to_chars(buffer, buffer + sizeof buffer, v);
	return std::string(buffer, result.ptr);
}

float toFloat(std::string_view text, float fallback) {
	float v = fallback;
	const auto result = std::from_chars(text.data(), text.data() + text.size(), v);
	return result.ec == std::errc() && std::isfinite(v) ? v : fallback;
}

/// "models/weapons/v_smg1.mdl" -> "weapons/v_smg1"; empty for anything that is not a model path.
std::string modelStem(std::string_view modelPath) {
	std::string path = normalizePath(modelPath);
	if (!isSafeGamePath(path) || !startsWithI(path, "models/") || !endsWithI(path, ".mdl") || path.size() <= 11) return {};
	return path.substr(7, path.size() - 11);
}

/// A file name for a sequence's replacement frames.
std::string smdFileName(const std::string& sequence) {
	std::string out;
	for (const char c : sequence) out.push_back(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' ? c : '_');
	if (out.empty()) out = "sequence";
	return "edits/" + out + ".smd";
}

} // namespace

void ModelRebuildChanges::merge(const ModelRebuildChanges& newer) {
	for (const auto& [key, list] : newer.events) events[key] = list;
	for (const auto& [key, r] : newer.replacements) replacements[key] = r;
	if (newer.effects) effects = newer.effects;
}

namespace {

KvNode eventsNode(const std::vector<ModelEvent>& list) {
	KvNode events = KvNode::makeBlock("events");
	for (const auto& e : list) {
		KvNode event = KvNode::makeBlock("event");
		event.set("id", e.id());
		event.set("cycle", number(e.cycle));
		event.set("options", cleanEventOptions(e.options));
		events.children.push_back(std::move(event));
	}
	return events;
}

std::vector<ModelEvent> readEvents(const KvNode& events) {
	std::vector<ModelEvent> list;
	for (const auto& node : events.children) {
		if (!node.block || !iequals(node.key, "event") || list.size() >= 1024) continue;
		ModelEvent e;
		if (!setEventId(e, node.get("id"))) continue;
		e.cycle = std::clamp(toFloat(node.get("cycle"), 0.f), 0.f, 1.f);
		e.options = cleanEventOptions(node.get("options"));
		list.push_back(std::move(e));
	}
	return list;
}

/// A word of a keyvalue written into the .qc: no quotes, braces, spaces or control characters.
std::string kvWord(std::string_view text) {
	std::string out;
	for (const char c : trim(text)) {
		const auto u = static_cast<unsigned char>(c);
		if (u < 33 || u == 127 || c == '"' || c == '{' || c == '}' || c == '\\') continue;
		out += c;
	}
	return out.substr(0, 128);
}

} // namespace

std::string writeModelChanges(const StoredModelChanges& stored, std::map<std::string, std::string>* smdFiles) {
	KvNode root;
	KvNode block = KvNode::makeBlock("ModelRebuild");
	block.set("model", stored.model);
	block.set("base", stored.base);
	std::vector<std::string> names;
	for (const auto& [key, unused] : stored.changes.events) names.push_back(key);
	for (const auto& [key, unused] : stored.changes.replacements) {
		if (std::find(names.begin(), names.end(), key) == names.end()) names.push_back(key);
	}
	std::sort(names.begin(), names.end());
	for (const auto& name : names) {
		KvNode seq = KvNode::makeBlock("sequence");
		seq.set("name", name);
		if (const auto it = stored.changes.events.find(name); it != stored.changes.events.end()) seq.children.push_back(eventsNode(it->second));
		if (const auto it = stored.changes.replacements.find(name); it != stored.changes.replacements.end()) {
			const auto& r = it->second;
			const std::string file = smdFileName(name);
			if (smdFiles) (*smdFiles)[file] = r.smd;
			KvNode frames = KvNode::makeBlock("frames");
			frames.set("smd", file);
			if (r.fps > 0) frames.set("fps", number(r.fps));
			if (r.loop >= 0) frames.set("loop", std::to_string(r.loop));
			if (!r.activity.empty()) frames.set("activity", r.activity);
			seq.children.push_back(std::move(frames));
		}
		block.children.push_back(std::move(seq));
	}
	if (const auto& effects = stored.changes.effects) {
		// Effects (Models/Weapons page, Effects): added on top of each sequence's events.
		KvNode node = KvNode::makeBlock("effects");
		for (const auto& [name, list] : effects->events) {
			KvNode seq = KvNode::makeBlock("sequence");
			seq.set("name", name);
			seq.children.push_back(eventsNode(list));
			node.children.push_back(std::move(seq));
		}
		for (const auto& p : effects->particles) {
			KvNode particle = KvNode::makeBlock("particle");
			particle.set("name", kvWord(p.name));
			particle.set("attachment_type", kvWord(p.attachType));
			particle.set("attachment_point", kvWord(p.attachment));
			node.children.push_back(std::move(particle));
		}
		block.children.push_back(std::move(node));
	}
	root.children.push_back(std::move(block));
	return "// What Mapsmith changed in this model (Weapons page, animation editor, effects). Rebuilt from the original each time.\n" + writeKeyValues(root);
}

std::optional<StoredModelChanges> readModelChanges(std::string_view text, const std::function<std::string(std::string_view)>& readSmd) {
	KvNode root;
	try {
		root = parseKeyValues(text);
	} catch (const std::exception&) {
		return std::nullopt;
	}
	const KvNode* block = root.find("ModelRebuild");
	if (!block || !block->block) return std::nullopt;
	StoredModelChanges out;
	out.model = normalizePath(block->get("model"));
	out.base = iequals(block->get("base"), "mod") ? "mod" : "game";
	if (const KvNode* effects = block->find("effects"); effects && effects->block) {
		AddedModelEffects added;
		for (const auto& node : effects->children) {
			if (!node.block) continue;
			if (iequals(node.key, "sequence")) {
				const std::string name = toLower(node.get("name"));
				const KvNode* events = node.find("events");
				if (name.empty() || !events || !events->block || added.events.size() >= 1024) continue;
				added.events[name] = readEvents(*events);
			} else if (iequals(node.key, "particle") && added.particles.size() < 256) {
				ModelParticle p;
				p.name = kvWord(node.get("name"));
				p.attachType = kvWord(node.get("attachment_type", "follow_attachment"));
				p.attachment = kvWord(node.get("attachment_point"));
				if (!p.name.empty()) added.particles.push_back(std::move(p));
			}
		}
		out.changes.effects = std::move(added);
	}
	for (const auto& seq : block->children) {
		if (!seq.block || !iequals(seq.key, "sequence")) continue;
		const std::string name = toLower(seq.get("name"));
		if (name.empty()) continue;
		if (const KvNode* events = seq.find("events"); events && events->block) out.changes.events[name] = readEvents(*events);
		if (const KvNode* frames = seq.find("frames"); frames && frames->block) {
			const std::string file = normalizePath(frames->get("smd"));
			if (!isSafeGamePath(file)) continue;
			SequenceReplacement r;
			r.smd = readSmd ? readSmd(file) : std::string();
			if (r.smd.empty()) continue;
			r.fps = std::max(0.f, toFloat(frames->get("fps"), 0.f));
			r.loop = std::clamp(static_cast<int>(toFloat(frames->get("loop", "-1"), -1.f)), -1, 1);
			r.activity = frames->get("activity");
			out.changes.replacements[name] = std::move(r);
		}
	}
	return out;
}

fs::path modelRebuildFolder(const fs::path& modDir, std::string_view modelPath) {
	const std::string stem = modelStem(modelPath);
	if (stem.empty()) return {};
	return modDir / "modelsrc" / "mapsmith_rebuild" / toNativePath(stem);
}

std::optional<StoredModelChanges> loadModelChanges(const fs::path& modDir, std::string_view modelPath) {
	const fs::path folder = modelRebuildFolder(modDir, modelPath);
	if (folder.empty()) return std::nullopt;
	const auto text = readFileText(folder / CHANGES_FILE);
	if (!text) return std::nullopt;
	return readModelChanges(*text, [&folder](std::string_view file) { return readFileText(folder / toNativePath(file)).value_or(std::string()); });
}

std::vector<fs::path> modelCompiledFiles(const fs::path& modDir, std::string_view modelPath) {
	const std::string stem = modelStem(modelPath);
	if (stem.empty()) return {};
	std::vector<fs::path> out;
	for (const char* extension : {".mdl", ".vvd", ".dx90.vtx", ".dx80.vtx", ".sw.vtx", ".phy", ".ani"}) {
		out.push_back(modDir / "models" / toNativePath(stem + extension));
	}
	return out;
}

std::optional<ImportRecord> modelRebuildRecord(const fs::path& modDir, std::string_view modelPath) {
	const std::string model = normalizePath(modelPath);
	for (const auto& record : loadImports(modDir)) {
		if (record.kind == RECORD_KIND && iequals(record.asset, model)) return record;
	}
	return std::nullopt;
}

GameFileSystem fileSystemWithout(const GameFileSystem& source, const fs::path& folder, const fs::path& front) {
	GameFileSystem out;
	std::error_code ec;
	if (!front.empty() && fs::is_directory(front, ec)) out.addDirectory(front, {"game", "mod"}, "in front");
	const fs::path skip = fs::weakly_canonical(folder, ec);
	for (const auto& mount : source.mounts()) {
		std::error_code mountEc;
		fs::path real = fs::weakly_canonical(mount.path, mountEc);
		if (mountEc) real = mount.path.lexically_normal();
		const auto rel = real.lexically_relative(skip);
		if (!skip.empty() && !rel.empty() && *rel.begin() != "..") continue;   // Inside the skipped folder
		if (mount.kind == Mount::Kind::Vpk) {
			out.addVpk(mount.path, mount.pathIds, mount.source);
		} else {
			out.addDirectory(mount.path, mount.pathIds, mount.source);
		}
	}
	return out;
}

std::optional<ModelRebuildPlan> prepareModelRebuild(const GameFileSystem& gameFs, const fs::path& modDir, std::string_view modelPath,
                                                    const ModelRebuildChanges& changes, std::string* error, const std::function<bool(int, int)>& progress) {
	const auto fail = [&](const std::string& message) -> std::optional<ModelRebuildPlan> {
		if (error) *error = message;
		return std::nullopt;
	};
	const std::string model = normalizePath(modelPath);
	const fs::path folder = modelRebuildFolder(modDir, model);
	if (folder.empty()) return fail("not a model path: " + std::string(modelPath));

	ModelRebuildPlan plan;
	plan.model = model;
	plan.folder = folder;
	const auto earlier = modelRebuildRecord(modDir, model);
	plan.earlierRebuild = earlier.has_value();
	StoredModelChanges stored;
	if (auto loaded = loadModelChanges(modDir, model)) stored = std::move(*loaded);
	stored.model = model;
	std::error_code ec;
	const auto compiled = modelCompiledFiles(modDir, model);
	const fs::path baseDir = folder / "base";
	if (!earlier) {
		// First rebuild: the mod's own copy (if it has one) is the base, kept in base/ and as backups.
		const bool modHasOwn = fs::exists(compiled.front(), ec);
		stored.base = modHasOwn ? "mod" : "game";
		fs::remove_all(baseDir, ec);
		if (modHasOwn) {
			for (const auto& file : compiled) {
				if (!fs::exists(file, ec)) continue;
				const fs::path inBase = baseDir / "models" / fs::relative(file, modDir / "models", ec);
				fs::create_directories(inBase.parent_path(), ec);
				fs::copy_file(file, inBase, fs::copy_options::overwrite_existing, ec);
				if (ec) return fail("cannot copy " + displayPath(file) + " to " + displayPath(inBase));
			}
		}
	}
	stored.changes.merge(changes);

	// The model as the base has it: the game without the mod's folder, with base/ in front.
	const GameFileSystem source = fileSystemWithout(gameFs, modDir, stored.base == "mod" ? baseDir : fs::path());
	ModelExportOptions options;
	options.materials = false;
	options.pictures = false;
	options.events = stored.changes.events;
	options.replacements = stored.changes.replacements;
	options.effects = stored.changes.effects;
	std::string why;
	auto exported = decompileModel(source, model, options, &why, progress);
	if (!exported) return fail(why);
	plan.notes = exported->notes;
	if (!writeModelExport(source, *exported, folder, &why)) return fail(why);
	std::map<std::string, std::string> smdFiles;
	const std::string changesText = writeModelChanges(stored, &smdFiles);
	for (const auto& [file, text] : smdFiles) {
		if (!writeFile(folder / toNativePath(file), text, error)) return std::nullopt;
	}
	if (!writeFile(folder / CHANGES_FILE, changesText, error)) return std::nullopt;
	plan.qc = folder / toNativePath(exported->qcFile);

	// The import record: the earlier one, or backups of the mod's own compiled files.
	if (earlier) {
		plan.record = *earlier;
	} else {
		plan.record.kind = std::string(RECORD_KIND);
		plan.record.asset = model;
		for (const auto& file : compiled) {
			if (!fs::exists(file, ec)) continue;
			const fs::path backup = file.string() + std::string(BACKUP);
			if (!fs::exists(backup, ec)) {
				fs::copy_file(file, backup, fs::copy_options::overwrite_existing, ec);
				if (ec) return fail("cannot back up " + displayPath(file));
			}
			plan.record.replaced.push_back(file);
		}
	}
	plan.arguments = {"-game", displayPath(modDir), "-nop4", "-verbose", displayPath(plan.qc)};
	return plan;
}

bool finishModelRebuild(ModelRebuildPlan& plan, const fs::path& modDir, std::string* error) {
	std::error_code ec;
	const auto compiled = modelCompiledFiles(modDir, plan.model);
	if (compiled.empty() || !fs::exists(compiled.front(), ec)) {
		if (error) *error = "studiomdl did not write " + displayPath(compiled.empty() ? fs::path() : compiled.front());
		if (!plan.earlierRebuild) {
			// Put the mod's own files back and drop the backups (nothing was rebuilt).
			for (const auto& file : plan.record.replaced) {
				const fs::path backup = file.string() + std::string(BACKUP);
				if (fs::exists(backup, ec)) fs::rename(backup, file, ec);
			}
		}
		return false;
	}
	const ImportRecord before = plan.record;
	for (const auto& file : compiled) {
		if (!fs::exists(file, ec)) continue;
		const bool known = std::find(plan.record.created.begin(), plan.record.created.end(), file) != plan.record.created.end() ||
		                   std::find(plan.record.replaced.begin(), plan.record.replaced.end(), file) != plan.record.replaced.end();
		if (!known) plan.record.created.push_back(file);
	}
	if (plan.earlierRebuild) forgetImport(modDir, before);
	if (!rememberImport(modDir, plan.record)) {
		if (error) *error = "cannot write " + displayPath(importLogFile(modDir));
		return false;
	}
	plan.earlierRebuild = true;
	return true;
}

bool restoreModel(const fs::path& modDir, std::string_view modelPath, std::string* error) {
	const auto record = modelRebuildRecord(modDir, modelPath);
	const fs::path folder = modelRebuildFolder(modDir, modelPath);
	std::error_code ec;
	if (!folder.empty()) {
		fs::remove(folder / CHANGES_FILE, ec);
		fs::remove_all(folder / "base", ec);
		fs::remove_all(folder / "edits", ec);
	}
	if (!record) return true;
	if (!undoImport(*record, error)) return false;
	return forgetImport(modDir, *record);
}

} // namespace sme
