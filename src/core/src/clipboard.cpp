// clipboard.cpp - Copying and pasting map objects as VMF text (see clipboard.h).

#include <sme/clipboard.h>

#include <sme/brush.h>
#include <sme/text.h>

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>

namespace sme::edit {

namespace {

constexpr std::string_view NAME_KEY = "targetname";

/// Keys that hold no entity names, even when their value happens to equal one.
bool keepsValue(std::string_view key) {
	return iequals(key, "classname") || iequals(key, "origin") || iequals(key, "angles") || iequals(key, "model") || iequals(key, "id");
}

/// Every object of a document, for the selection and the bounds of what was pasted.
Selection everything(const MapDocument& doc) {
	Selection all;
	for (const auto& solid : doc.world.solids) {
		all.solids.push_back({0, solid.id});
	}
	for (const auto& entity : doc.entities) {
		all.entities.push_back(entity.id);
	}
	return all;
}

std::optional<MapDocument> parseCopied(std::string_view text, std::string* error) {
	try {
		auto doc = MapDocument::parse(text);
		if (doc.world.solids.empty() && doc.entities.empty()) {
			if (error) *error = "nothing to paste";
			return std::nullopt;
		}
		return doc;
	} catch (const std::exception& e) {
		if (error) *error = e.what();
		return std::nullopt;
	}
}

/// "door" -> "door_2", "door_3", ... (the first number not taken); "door_2" -> "door_3".
std::string uniqueName(const std::string& name, const std::set<std::string>& taken) {
	std::string stem = name;
	int number = 2;
	if (const auto underscore = name.rfind('_'); underscore != std::string::npos && underscore + 1 < name.size() &&
	                                               std::all_of(name.begin() + long(underscore) + 1, name.end(), [](char c) { return c >= '0' && c <= '9'; })) {
		stem = name.substr(0, underscore);
		number = std::stoi(name.substr(underscore + 1)) + 1;
	}
	for (;; ++number) {
		std::string candidate = stem + "_" + std::to_string(number);
		if (!taken.contains(toLower(candidate))) return candidate;
	}
}

} // namespace

std::string copyObjects(const MapDocument& doc, const Selection& selection) {
	if (!selection.hasObjects()) {
		return {};
	}
	MapDocument copy;
	copy.headerBlocks.resize(1);   // versioninfo only
	for (const auto& ref : selection.solids) {
		if (selection.containsEntity(ref.entityId) && ref.entityId != 0) continue;   // goes with its entity
		if (const auto* solid = doc.findSolid(ref)) {
			copy.world.solids.push_back(*solid);
		}
	}
	for (const int id : selection.entities) {
		if (id == 0) continue;
		if (const auto* entity = doc.findEntity(id)) {
			copy.entities.push_back(*entity);
		}
	}
	return copy.serialize();
}

bool looksLikeMapObjects(std::string_view text) {
	// Cheap check first: VMF text starts with one of its blocks.
	std::size_t start = text.find_first_not_of(" \t\r\n");
	if (start == std::string_view::npos) return false;
	const auto head = toLower(std::string(text.substr(start, 16)));
	if (!head.starts_with("versioninfo") && !head.starts_with("visgroups") && !head.starts_with("viewsettings") && !head.starts_with("world") &&
	    !head.starts_with("entity")) {
		return false;
	}
	return parseCopied(text, nullptr).has_value();
}

std::optional<Bounds> copiedBounds(std::string_view text, const EntityCatalog* catalog) {
	const auto copied = parseCopied(text, nullptr);
	if (!copied) return std::nullopt;
	const Bounds box = selectionBounds(*copied, everything(*copied), catalog);
	if (box.empty()) return std::nullopt;
	return box;
}

std::optional<std::pair<Change, Selection>> pasteObjects(MapDocument& doc, std::string_view text, const PasteOptions& options,
                                                         const EntityCatalog* catalog, std::string* error) {
	const auto copied = parseCopied(text, error);
	if (!copied) return std::nullopt;
	const int copies = std::clamp(options.copies, 1, 1000);
	const Bounds box = selectionBounds(*copied, everything(*copied), catalog);
	const Vec3 originalCenter = box.empty() ? Vec3{} : box.center();
	const Vec3 base = options.center ? *options.center - originalCenter : Vec3{};
	// With a center the first copy lands there; without, copy i is offset i times.
	const int firstStep = options.center ? 0 : 1;

	// Names already in the map (lower case), to make the pasted ones unique.
	std::set<std::string> taken;
	for (const auto& entity : doc.entities) {
		if (const auto name = entity.get(NAME_KEY); !name.empty()) taken.insert(toLower(name));
	}

	Transaction t(doc, copies > 1 ? "Paste " + std::to_string(copies) + " copies" : std::string("Paste"));
	Selection pasted;
	for (int i = 0; i < copies; ++i) {
		const double step = double(i + firstStep);
		const Vec3 move = base + options.offset * step;
		Mat3 turn = Mat3::rotation(0, options.rotation.x * step) * Mat3::rotation(1, options.rotation.y * step) * Mat3::rotation(2, options.rotation.z * step);
		const bool rotates = options.rotation.x != 0 || options.rotation.y != 0 || options.rotation.z != 0;
		const Vec3 pivot = originalCenter + move;
		const auto place = [&](Solid& solid) {
			translateSolid(solid, move, options.textureLock);
			if (rotates && step != 0) rotateSolid(solid, turn, pivot, options.textureLock);
		};

		// New names for this copy: prefix, then a free number when the name is taken.
		std::map<std::string, std::string> renamed;   // lower-case old name -> new name
		if (options.uniqueNames || !options.namePrefix.empty()) {
			for (const auto& entity : copied->entities) {
				const auto name = entity.get(NAME_KEY);
				if (name.empty() || renamed.contains(toLower(name))) continue;
				std::string next = options.namePrefix + name;
				if (options.uniqueNames && taken.contains(toLower(next))) next = uniqueName(next, taken);
				renamed[toLower(name)] = next;
			}
			for (const auto& [old, next] : renamed) taken.insert(toLower(next));
		}

		for (const auto& source : copied->world.solids) {
			Solid solid = cloneSolid(doc, source);
			place(solid);
			const SolidRef ref{0, solid.id};
			t.touchSolid(ref);
			doc.world.solids.push_back(std::move(solid));
			pasted.solids.push_back(ref);
		}
		for (const auto& source : copied->entities) {
			Entity entity = source;
			entity.id = doc.newEntityId();
			t.touchEntity(entity.id);
			for (auto& solid : entity.solids) {
				solid = cloneSolid(doc, solid);
				place(solid);
			}
			if (entity.hasOrigin()) {
				Vec3 origin = entity.origin() + move;
				if (rotates && step != 0) origin = turn * (origin - pivot) + pivot;
				entity.setOrigin(origin);
			}
			if (rotates && step != 0 && entity.solids.empty()) {
				Vec3 angles;
				if (!parseVec3(entity.get("angles", "0 0 0"), angles)) angles = {};
				entity.set("angles", formatVec3(matrixToAngles(turn * anglesToMatrix(angles))));
			}
			if (!renamed.empty()) {
				// The copy's own name and every reference to a renamed name inside the pasted objects.
				for (auto& [key, value] : entity.keyValues) {
					if (keepsValue(key)) continue;
					if (const auto it = renamed.find(toLower(value)); it != renamed.end()) value = it->second;
				}
				for (auto& connection : entity.connections) {
					if (const auto it = renamed.find(toLower(connection.target)); it != renamed.end()) connection.target = it->second;
				}
			}
			pasted.entities.push_back(entity.id);
			doc.entities.push_back(std::move(entity));
		}
	}
	return std::pair{t.finish(), std::move(pasted)};
}

} // namespace sme::edit
