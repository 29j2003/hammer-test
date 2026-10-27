// editing.h - Editor operations on a map document (move, rotate, resize, copy, delete,
// create blocks, place entities, apply materials, tie brushes to entities).
//
// Each operation records an undoable Change; the caller pushes it to the History.
// They live in the core (not the UI) so they can be tested without a window.

#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <sme/entity_catalog.h>
#include <sme/history.h>
#include <sme/map.h>
#include <sme/shapes.h>

namespace sme {

/// A face, addressed by the side's id (stable across edits).
struct FaceRef {
	SolidRef solid;
	int sideId = 0;
	constexpr bool operator==(const FaceRef&) const = default;
};

struct Selection {
	std::vector<SolidRef> solids;   ///< World brushes
	std::vector<int> entities;      ///< Point and brush entities (a brush entity moves as a whole)
	std::vector<FaceRef> faces;     ///< Face mode

	[[nodiscard]] bool empty() const { return solids.empty() && entities.empty() && faces.empty(); }
	[[nodiscard]] bool hasObjects() const { return !solids.empty() || !entities.empty(); }
	void clear() { solids.clear(); entities.clear(); faces.clear(); }
	[[nodiscard]] bool contains(const SolidRef& ref) const;
	[[nodiscard]] bool containsEntity(int entityId) const;
	[[nodiscard]] bool contains(const FaceRef& ref) const;
	void toggle(const SolidRef& ref);
	void toggleEntity(int entityId);
	void toggle(const FaceRef& ref);
	/// Drops references to objects that no longer exist (after undo, delete, ...).
	void prune(const MapDocument& doc);
};

namespace edit {

/// Box of a point entity: FGD size() helper, else 16 units around the origin.
[[nodiscard]] Bounds entityBounds(const Entity& entity, const EntityCatalog* catalog);
/// Model a point entity shows: its "model" key (.mdl) or the FGD's studio("...") model; empty if none.
[[nodiscard]] std::string entityModel(const Entity& entity, const EntityCatalog* catalog);
/// Box around everything selected (objects, or the faces' brushes in face mode).
[[nodiscard]] Bounds selectionBounds(const MapDocument& doc, const Selection& selection, const EntityCatalog* catalog);

/// Source angles "pitch yaw roll" <-> rotation matrix (Source convention: yaw around Z, pitch around Y, roll around X).
[[nodiscard]] Mat3 anglesToMatrix(const Vec3& pitchYawRoll);
[[nodiscard]] Vec3 matrixToAngles(const Mat3& m);

[[nodiscard]] Change translate(MapDocument& doc, const Selection& selection, const Vec3& delta, bool textureLock);
[[nodiscard]] Change rotate(MapDocument& doc, const Selection& selection, const Mat3& rotation, const Vec3& center, bool textureLock);
/// Resizes the selection from box `from` to box `to`; brushes that would become invalid are left alone.
[[nodiscard]] Change scale(MapDocument& doc, const Selection& selection, const Bounds& from, const Bounds& to);
[[nodiscard]] Change remove(MapDocument& doc, const Selection& selection);

/// Copies the selection, moved by `offset`; returns the change and the selection of the copies.
[[nodiscard]] std::pair<Change, Selection> duplicate(MapDocument& doc, const Selection& selection, const Vec3& offset);

[[nodiscard]] std::pair<Change, SolidRef> createBlock(MapDocument& doc, const Bounds& box, const std::string& material);

/// Editor sprite of a class (FGD iconsprite("editor/info_target.vmt")) as a material name; empty if none.
[[nodiscard]] std::string classSprite(const EntityClass& cls);
/// Sprite shown for an entity: its class sprite, or for sprite() / iconsprite() without a file
/// the entity's own "model" (env_sprite, env_glow). Empty if it has none.
[[nodiscard]] std::string entitySprite(const Entity& entity, const EntityCatalog* catalog);

/// Places a point entity; keys with FGD defaults are filled in like Hammer does.
/// `keys` are set after the FGD defaults (e.g. the model of a dropped prop).
[[nodiscard]] std::pair<Change, int> placeEntity(MapDocument& doc, const std::string& classname, const Vec3& origin, const EntityClass* definition,
                                                 const std::vector<std::pair<std::string, std::string>>& keys = {});

/// Material on the selected faces (face mode) or on every face of the selected brushes/brush entities.
[[nodiscard]] Change applyMaterial(MapDocument& doc, const Selection& selection, const std::string& material);

/// Runs `modify` on every selected face (texture scale, shift, alignment, ...).
template<typename F>
[[nodiscard]] Change modifyFaces(MapDocument& doc, const std::vector<FaceRef>& faces, const std::string& name, F&& modify) {
	Transaction t(doc, name);
	for (const auto& face : faces) {
		t.touchSolid(face.solid);
		if (auto* solid = doc.findSolid(face.solid)) {
			for (auto& side : solid->sides) {
				if (side.id == face.sideId) {
					modify(side);
				}
			}
		}
	}
	return t.finish();
}

/// All faces (world and brush entities) that use the material (case-insensitive).
[[nodiscard]] std::vector<FaceRef> facesWithMaterial(const MapDocument& doc, const std::string& material);
/// The face plus every face in the same plane that touches it, directly or through others
/// ("the whole wall"), across all brushes.
[[nodiscard]] std::vector<FaceRef> coplanarConnectedFaces(const MapDocument& doc, const FaceRef& seed);

/// Moves selected world brushes into a new brush entity (Hammer: "Tie to Entity").
[[nodiscard]] std::pair<Change, int> tieToEntity(MapDocument& doc, const Selection& selection, const std::string& classname, const EntityClass* definition);
/// Moves the brushes of selected brush entities back into the world and deletes those entities.
[[nodiscard]] Change moveToWorld(MapDocument& doc, const Selection& selection);

/// Sets (or with an empty optional removes) a key on several entities (0 = world).
[[nodiscard]] Change setKeyValue(MapDocument& doc, const std::vector<int>& entityIds, const std::string& key, const std::optional<std::string>& value);
// --- Shapes and brush tools (csg.h, shapes.h) -------------------------------------------

/// Tool material a new brush entity of this class usually gets (trigger_* -> tools/toolstrigger,
/// func_areaportal -> tools/toolsareaportal, ...); empty when it takes the normal material.
[[nodiscard]] std::string brushEntityMaterial(std::string_view classname);
/// A new brush entity made of a shape fitted into `box` (entity tool with a brush class armed),
/// one undo step "Create <class>". Returns the entity id (0 if the shape does not fit).
[[nodiscard]] std::pair<Change, int> createBrushEntity(MapDocument& doc, const Bounds& box, const ShapeOptions& options, const std::string& classname,
                                                       const EntityClass* definition, const std::string& material);

/// Brushes of a shape fitted into `box` (block tool). Undo name "Create <shape>".
[[nodiscard]] std::pair<Change, std::vector<SolidRef>> createShape(MapDocument& doc, const Bounds& box, const ShapeOptions& options, const std::string& material);
/// Removes `remove` and adds `add` in the same entity as the first removed brush (world if none).
[[nodiscard]] std::pair<Change, std::vector<SolidRef>> replaceSolids(MapDocument& doc, const std::vector<SolidRef>& remove, std::vector<Solid> add,
                                                                     const std::string& name);
/// Hollow every selected brush (walls of `thickness`, negative = outside). Selects the walls.
[[nodiscard]] std::pair<Change, Selection> hollow(MapDocument& doc, const Selection& selection, double thickness);
/// Carve: the selected brushes cut themselves out of every other brush they touch.
[[nodiscard]] Change carve(MapDocument& doc, const Selection& selection);
/// Merge the selected world brushes into one convex brush; `exact` false if gaps were filled.
struct MergeOutcome {
	Change change;
	SolidRef merged;
	bool exact = false;
};
[[nodiscard]] std::optional<MergeOutcome> merge(MapDocument& doc, const Selection& selection);
/// Rounds (segments > 1) or bevels the edge between two faces of one brush.
[[nodiscard]] std::optional<Change> roundEdge(MapDocument& doc, const FaceRef& a, const FaceRef& b, double radius, int segments);
/// Clip the selected brushes with a plane: keep the part behind it, in front of it or both.
enum class ClipKeep { Back, Front, Both };
[[nodiscard]] std::pair<Change, Selection> clip(MapDocument& doc, const Selection& selection, const Plane& plane, ClipKeep keep);

/// Vertex tool: moves the corners at `corners` of the given brushes by `delta` (brushes stay convex).
struct VertexMoveOutcome {
	Change change;
	bool cornersLost = false;   ///< A brush folded in and lost corners
	bool refused = false;       ///< A brush would have become invalid (flat): nothing changed
};
[[nodiscard]] VertexMoveOutcome moveVertices(MapDocument& doc, const std::vector<SolidRef>& solids, const std::vector<Vec3>& corners, const Vec3& delta);

/// Texture justification of faces (Hammer's face edit sheet). With `treatAsOne` the faces are
/// handled as one surface: coplanar faces share the first face's axes and one texture spans all.
enum class Justify { Fit, Left, Right, Top, Bottom, Center };
[[nodiscard]] Change justifyFaces(MapDocument& doc, const std::vector<FaceRef>& faces, Justify mode, bool treatAsOne,
                                  const std::function<std::pair<int, int>(const std::string&)>& textureSize);

/// Replaces the outputs of one entity.
[[nodiscard]] Change setConnections(MapDocument& doc, int entityId, const std::vector<Connection>& connections);

} // namespace edit
} // namespace sme
