// renderer.h - Draws the map with OpenGL 3.3: textured brushes batched per material,
// displacements, entity boxes, wireframe edges, selection highlight, lighting preview,
// grid and helper lines (gizmos, previews).
//
// Geometry is rebuilt only when the document changes. Selection and "hide while dragging"
// are per-face flags in a small texture, so clicking never rebuilds the scene.

#pragma once

#include <QHash>
#include <QImage>
#include <QMatrix4x4>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QSet>
#include <QVector3D>

#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include <sme/brush.h>
#include <sme/editing.h>
#include <sme/entity_catalog.h>
#include <sme/instances.h>
#include <sme/map.h>

#include "camera.h"
#include "material_cache.h"

namespace sme::app {

class ModelCache;

/// One end of a line, with its colour (gizmos, grid, helper lines).
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

/// Draws the map in one view (see the file comment); every Viewport owns one.
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
	/// Gives the read-only content of a func_instance (nullptr if there is none); asked for on
	/// every rebuild, so the provider should cache (the Editor does).
	using InstanceProvider = std::function<const InstancePreview*(const Entity&)>;
	void setInstanceProvider(InstanceProvider provider) { m_instanceProvider = std::move(provider); }
	/// Box around the drawn content of a func_instance (empty if it has none).
	[[nodiscard]] Bounds instanceBounds(int entityId) const;
	/// Displacement tool: new vertex positions and blend of one displacement face while a stroke
	/// runs, without rebuilding the scene (the vertex count stays the same). False if the face
	/// is not drawn as a displacement of that size - then rebuild.
	bool updateDisplacement(int sideId, const Side& side, const FaceGeometry& face, const DisplacementMesh& mesh);
	/// Updates selection flags (cheap).
	void setSelection(const MapDocument& doc, const Selection& selection, const Selection& hidden = {}, int hoverSideId = 0);
	/// Hides the selected objects (they are drawn by drawPreview while dragging).
	void setHideSelected(bool hide) { m_hideSelected = hide; }
	void materialReady(const QString& name);

	/// Camera and drawing mode of one frame.
	struct Params {
		QMatrix4x4 view, projection;
		QVector3D cameraPosition;
		Mode mode = Mode::Lit;
		bool showToolTextures = true;
		bool shadows = true;          ///< Real-time shadows in the lighting preview
	};
	/// Draws the whole scene for one frame.
	void render(const Params& params);
	/// mapbase-fidelity's clouds over the sky faces (0 amount = none).
	struct SkyLook {
		float clouds = 0;
		float coverage = 0.38f;
		float darkness = 0.5f;
		float scale = 1;
	};
	void setSkyLook(const SkyLook& look) { m_skyLook = look; }
	/// mapbase-fidelity's colour grading in the lighting preview.
	struct Grade {
		bool on = false;
		float exposure = 1, contrast = 0.18f, saturation = 1, vibrance = 0.2f, temperature = 0, tint = 0;
		float lift = 0, gamma = 1, gain = 1, vignette = 0.18f;
	};
	void setGrade(const Grade& grade) { m_grade = grade; }
	/// Selected objects drawn with an extra transform (move/rotate preview); `brushes` false
	/// leaves out selected brushes (they come from setPreviewSolids instead).
	void drawPreview(const Params& params, const QMatrix4x4& model, bool brushes = true);
	/// A model drawn tinted where it would be placed (entity tool preview, a model dragged in);
	/// `opacity` below 1 lets the map show through.
	void drawGhostModel(const Params& params, const QString& path, const QMatrix4x4& transform, float opacity = 1.f);
	/// Brushes drawn exactly as they would become (scaling, new shapes, clipping); empty clears.
	void setPreviewSolids(const std::vector<Solid>& solids);
	void drawPreviewSolids(const Params& params);
	/// Lines; `onTop` ignores depth (gizmos).
	void drawLines(const std::vector<LineVertex>& lines, const QMatrix4x4& viewProjection, bool onTop, float width = 1.f);
	/// Draws a CPU-painted image (premultiplied alpha) over the whole view; uploads only when changed.
	void drawOverlay(const QImage& image, bool changed);
	/// Puts the OpenGL state back to what QPainter expects (call before painting text on top).
	void endFrame();

	/// Ray picking against brushes (incl. displacements) and entity boxes.
	[[nodiscard]] std::optional<PickResult> pick(const Ray& ray, bool facesOnly = false) const;

	/// Numbers about the scene and the last frame (status bar and its tooltip).
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
		GLuint height = 0;     ///< Relief heights (mapbase-fidelity preview)
		float parallaxDepth = 0;
		QSize size{64, 64};
		MaterialInfo::Kind kind = MaterialInfo::Kind::Opaque;
		bool isTool = false;
		bool isSky = false;
		bool found = false;    ///< The VMT exists (otherwise the checkerboard is shown)
		bool ready = false;
	};
	struct SpriteBatch {
		QString material;
		GLint first = 0;       ///< First instance
		GLsizei count = 0;
	};
	struct PickItem {
		SolidRef solid;
		int entityId = 0;
		Bounds bounds;
		std::vector<Vec3> triangles;   ///< Displacement triangles (picked instead of the planes)
		std::vector<int> triangleSides;
		const Solid* detached = nullptr;   ///< Instance content: not in the document (selects its func_instance)
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
	/// The map's skybox faces on units 5..10 and the cloud settings.
	void bindSky(QOpenGLShaderProgram& program, const Params& params);
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
	/// Editor sprites (iconsprite) of point entities as camera-facing quads (3D views).
	void drawSprites(const Params& params);

	MaterialCache* m_materials = nullptr;
	ModelCache* m_models = nullptr;
	std::vector<ModelInstance> m_modelInstances;
	QString m_skyName;          ///< worldspawn "skyname"
	SkyLook m_skyLook;
	Grade m_grade;
	QHash<QString, GpuModel> m_gpuModels;
	std::unique_ptr<QOpenGLShaderProgram> m_program;
	std::unique_ptr<QOpenGLShaderProgram> m_lineProgram;
	std::unique_ptr<QOpenGLShaderProgram> m_overlayProgram;
	std::unique_ptr<QOpenGLShaderProgram> m_spriteProgram;
	GLuint m_emptyVao = 0;                   // for quads made from gl_VertexID
	GLuint m_spriteVao = 0, m_spriteVbo = 0; // one instance per sprite: center, size, color, face
	std::vector<SpriteBatch> m_spriteBatches;
	GLsizei m_spriteBoxVertices = 0;
	std::vector<int> m_volumeFaces;          // faces of helper volumes (parallax_obb)         // entity box vertices at the end that sprites replace in 3D
	GLuint m_overlayTexture = 0;
	QSize m_overlaySize;
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
	InstanceProvider m_instanceProvider;
	std::deque<Solid> m_instanceSolids;              ///< Instance brushes drawn this build (stable addresses for picking)
	std::unordered_map<int, Bounds> m_instanceBounds;
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
