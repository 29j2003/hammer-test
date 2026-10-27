// renderer.h - Draws the map with OpenGL 3.3: textured brushes batched per material,
// displacements, entity boxes, wireframe edges, selection highlight, lighting preview,
// grid and helper lines (gizmos, previews).
//
// Geometry is rebuilt only when the document changes. Selection and "hide while dragging"
// are per-face flags in a small texture, so clicking never rebuilds the scene.

#pragma once

#include <QHash>
#include <QMatrix4x4>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QSet>
#include <QVector3D>

#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include <sme/editing.h>
#include <sme/entity_catalog.h>
#include <sme/map.h>

#include "camera.h"
#include "material_cache.h"

namespace sme::app {

class ModelCache;

struct LineVertex {
	QVector3D position;
	QVector4D color;
};

/// What a click hit.
struct PickResult {
	double distance = 0;
	Vec3 point;
	Vec3 normal;
	SolidRef solid;          ///< solidId != 0 for brushes
	int entityId = 0;        ///< Point entity or brush entity owning the solid
	int sideId = 0;          ///< Face that was hit (brushes)
};

class SceneRenderer : protected QOpenGLFunctions_3_3_Core {
public:
	enum class Mode { Lit, Unlit, Wireframe, Flat2D };

	SceneRenderer();
	~SceneRenderer();

	/// Needs a current OpenGL 3.3 context.
	bool initialize(MaterialCache* materials, QString* error);
	/// Models are drawn once loaded (point entities show a box until then).
	void setModels(ModelCache* models) { m_models = models; }
	void release();

	/// Rebuilds all geometry from the document.
	void rebuild(const MapDocument& doc, const EntityCatalog* catalog);
	/// Updates selection flags (cheap).
	void setSelection(const MapDocument& doc, const Selection& selection, const Selection& hidden = {}, int hoverSideId = 0);
	/// Hides the selected objects (they are drawn by drawPreview while dragging).
	void setHideSelected(bool hide) { m_hideSelected = hide; }
	void materialReady(const QString& name);

	struct Params {
		QMatrix4x4 view, projection;
		QVector3D cameraPosition;
		Mode mode = Mode::Lit;
		bool showToolTextures = true;
		bool shadows = true;          ///< Real-time shadows in the lighting preview
	};
	void render(const Params& params);
	/// Selected objects drawn with an extra transform (move/rotate preview); `brushes` false
	/// leaves out selected brushes (they come from setPreviewSolids instead).
	void drawPreview(const Params& params, const QMatrix4x4& model, bool brushes = true);
	/// Brushes drawn exactly as they would become (scaling, new shapes, clipping); empty clears.
	void setPreviewSolids(const std::vector<Solid>& solids);
	void drawPreviewSolids(const Params& params);
	/// Lines; `onTop` ignores depth (gizmos).
	void drawLines(const std::vector<LineVertex>& lines, const QMatrix4x4& viewProjection, bool onTop, float width = 1.f);

	/// Ray picking against brushes (incl. displacements) and entity boxes.
	[[nodiscard]] std::optional<PickResult> pick(const Ray& ray, bool facesOnly = false) const;

	[[nodiscard]] Bounds sceneBounds() const { return m_sceneBounds; }
	[[nodiscard]] std::size_t triangleCount() const { return m_triangleCount; }
	[[nodiscard]] int lastDrawCalls() const { return m_drawCalls; }
	[[nodiscard]] int lightCount() const { return static_cast<int>(m_lights.size()); }
	/// Lights that cast shadows in the last frame (sun counts as one).
	[[nodiscard]] int shadowCasterCount() const { return m_shadowCasterCount; }

private:
	struct Batch {
		QString material;
		GLint first = 0;
		GLsizei count = 0;
	};
	struct GpuTexture {
		GLuint id = 0;
		GLuint id2 = 0;
		QSize size{64, 64};
		MaterialInfo::Kind kind = MaterialInfo::Kind::Opaque;
		bool isTool = false;
		bool isSky = false;
		bool ready = false;
	};
	struct PickItem {
		SolidRef solid;
		int entityId = 0;
		Bounds bounds;
		std::vector<Vec3> triangles;   ///< Displacement triangles (picked instead of the planes)
		std::vector<int> triangleSides;
	};
	struct ModelInstance {
		QString path;
		QMatrix4x4 transform;
		int face = 0;          ///< Flag slot (selection, hiding)
	};
	struct GpuModel {
		struct Mesh {
			QString material;
			GLuint first = 0;
			GLsizei count = 0;
		};
		GLuint vao = 0, vbo = 0, ibo = 0;
		std::vector<Mesh> meshes;
	};
	struct Light {
		QVector4D position;    ///< w = 50 % distance
		QVector4D color;       ///< rgb linear intensity
		QVector4D spot;        ///< xyz direction, w = cos(outer cone) (-2 = point light)
		QVector4D spot2;       ///< x = cos(inner cone), y = exponent
	};

	GpuTexture& texture(const QString& material);
	void uploadTexture(GpuTexture& tex, const MaterialInfo& info);
	void drawBatches(QOpenGLShaderProgram& program, const Params& params, bool translucentPass);
	void setCommonUniforms(QOpenGLShaderProgram& program, const Params& params, const QMatrix4x4& model);
	void ensureFlagTexture();
	GpuModel* gpuModel(const QString& path);
	// Shadows (renderer_shadows.cpp): a sun map over the whole map and an atlas of 16 tiles for
	// the nearest spot lights (one tile) and point lights (six tiles, one per cube side).
	bool initShadows(QString* error);
	void releaseShadows();
	/// Chooses the shadow casters among `lights` and redraws the maps if anything changed.
	/// Fills the per-light shadow info (x: first tile or -1, y: 1 for point lights).
	void updateShadows(const std::vector<const Light*>& lights, QVector4D* shadowInfo);
	void drawDepth(const QMatrix4x4& viewProjection);
	/// Draws model instances; `preview` set: only the selected ones, moved by the matrix.
	void drawModels(QOpenGLShaderProgram& program, bool flat, const QMatrix4x4* preview);

	MaterialCache* m_materials = nullptr;
	ModelCache* m_models = nullptr;
	std::vector<ModelInstance> m_modelInstances;
	QHash<QString, GpuModel> m_gpuModels;
	std::unique_ptr<QOpenGLShaderProgram> m_program;
	std::unique_ptr<QOpenGLShaderProgram> m_lineProgram;
	std::unique_ptr<QOpenGLShaderProgram> m_edgeProgram;
	std::unique_ptr<QOpenGLShaderProgram> m_depthProgram;
	GLuint m_sunFbo = 0, m_sunDepth = 0, m_atlasFbo = 0, m_atlasDepth = 0;
	QMatrix4x4 m_sunMatrix;
	QMatrix4x4 m_tileMatrices[16];
	bool m_shadowsDirty = true;
	bool m_hasShadowMaps = false;
	std::vector<const Light*> m_shadowLights;   ///< Casters the atlas was drawn for
	QVector3D m_shadowSunDirection;
	int m_shadowCasterCount = 0;

	GLuint m_vao = 0, m_vbo = 0;            // brush faces + entity boxes
	GLuint m_edgeVao = 0, m_edgeVbo = 0;    // wireframe edges
	GLuint m_lineVao = 0, m_lineVbo = 0;    // dynamic helper lines
	GLuint m_previewVao = 0, m_previewVbo = 0;           // live preview brushes
	GLuint m_previewEdgeVao = 0, m_previewEdgeVbo = 0;
	std::vector<Batch> m_previewBatches;
	GLsizei m_previewEdgeCount = 0;
	GLuint m_flagTexture = 0;
	GLuint m_whiteTexture = 0;

	std::vector<Batch> m_batches;
	GLint m_entityFirst = 0;
	GLsizei m_entityCount = 0;
	GLsizei m_edgeCount = 0;
	int m_faceCount = 0;
	std::vector<std::uint8_t> m_flags;
	QHash<QString, GpuTexture> m_textures;
	QSet<QString> m_dirtyTextures;

	// Face index ranges per object, for selection flags.
	std::unordered_map<int, std::vector<int>> m_sideFaces;      ///< side id -> face indices
	std::unordered_map<long long, std::vector<int>> m_solidFaces;
	std::unordered_map<int, std::vector<int>> m_entityFaces;    ///< entity id -> face indices (box or all brushes)
	// Selected geometry for drawPreview: ranges into the main buffer.
	std::vector<std::pair<GLint, GLsizei>> m_selectedRanges;

	std::vector<PickItem> m_pickItems;
	Selection m_hidden;
	std::vector<Light> m_lights;
	QVector3D m_sunDirection{0.3f, 0.2f, -0.9f};
	QVector3D m_sunColor{0, 0, 0};
	QVector3D m_ambient{0.25f, 0.25f, 0.28f};
	bool m_hasSun = false;
	Bounds m_sceneBounds;
	std::size_t m_triangleCount = 0;
	int m_drawCalls = 0;
	bool m_hideSelected = false;
	bool m_initialized = false;
	const MapDocument* m_doc = nullptr;   ///< For picking (valid between rebuilds)
	std::vector<std::pair<SolidRef, int>> m_faceToSide; ///< face index -> (solid, side id)
	std::vector<std::pair<int, int>> m_rangesByFace;    ///< face index -> vertex range (first, count)
};

} // namespace sme::app
