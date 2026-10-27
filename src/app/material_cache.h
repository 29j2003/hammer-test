// material_cache.h - Loads materials and their textures in background threads.
//
// The viewport and the asset browser ask for a material by name and get a placeholder
// until the VMT/VTF are decoded; materialReady() then fires on the GUI thread.

#pragma once

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QSet>
#include <QThreadPool>

#include <memory>

namespace sme {
class GameFileSystem;
}

namespace sme::app {

struct MaterialInfo {
	enum class Kind { Opaque, AlphaTest, Translucent, Additive };

	QString name;              ///< Normalized lowercase name, e.g. "concrete/wall01"
	QString shader;
	bool found = false;        ///< false: VMT missing (drawn as checkerboard)
	bool isTool = false;
	bool isSky = false;
	bool isNodraw = false;
	Kind kind = Kind::Opaque;
	QImage image;              ///< Base texture, RGBA8888 (null if missing)
	QImage image2;             ///< $basetexture2 for blended displacements
	QString error;
};

class MaterialCache : public QObject {
	Q_OBJECT

public:
	enum class Size { Viewport, Thumbnail };

	explicit MaterialCache(Size size, QObject* parent = nullptr);
	~MaterialCache() override;

	/// Game files to read from (shared with the editor's LoadedGame; may be null).
	void setFileSystem(const GameFileSystem* fs);

	/// Returns the material if loaded, otherwise starts loading it and returns nullptr.
	[[nodiscard]] std::shared_ptr<const MaterialInfo> get(const QString& name);

	/// Placeholder image for missing/not yet loaded materials.
	[[nodiscard]] static const QImage& checkerboard();

	void clear();

signals:
	void materialReady(const QString& name);

private:
	void finished(const QString& name, std::shared_ptr<MaterialInfo> info, quint64 generation);

	Size m_size;
	const GameFileSystem* m_fs = nullptr;
	QHash<QString, std::shared_ptr<const MaterialInfo>> m_loaded;
	QSet<QString> m_pending;
	QThreadPool m_pool;
	quint64 m_generation = 0;
};

/// "Concrete\\Wall01.vmt" -> "concrete/wall01".
[[nodiscard]] QString normalizedMaterialName(const QString& name);

} // namespace sme::app
