// particle_browser.h - Particles page (and dock): every particle system of the game's .pcf files,
// grouped by file and searchable. Picking one shows it moving (a CPU simulation of its common
// parts, sprites from its material) and lists its parts - emitters, initializers, operators,
// renderers, children - with their values as cards. Drag a system into the
// map for an info_particle_system, or double-click to set it on the selected one.

#pragma once

#include <QHash>
#include <QImage>
#include <QPointF>
#include <QTimer>
#include <QWidget>

#include <memory>
#include <optional>
#include <vector>

#include <sme/particles.h>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QTreeWidget;
class QTreeWidgetItem;
class QVBoxLayout;

namespace sme {
class GameFileSystem;
}

namespace sme::app {

class AssetBrowser;
class Editor;

/// Draws a running particle simulation with an orbit camera (left drag turns, wheel zooms).
/// Runs only while visible and playing.
class ParticlePreview : public QWidget {
	Q_OBJECT

public:
	explicit ParticlePreview(QWidget* parent = nullptr);

	/// Shows `root` from `library`; sprites come from the systems' materials in `fs`.
	void setSystem(std::vector<ParticleDefinition> library, const QString& root, const GameFileSystem* fs);
	void clear();

	void setPlaying(bool playing);
	[[nodiscard]] bool playing() const { return m_playing; }
	void restart();
	void setSpeed(double speed) { m_speed = speed; }
	void setGridShown(bool shown);
	/// Advances the simulation by hand (also when hidden or paused).
	void advance(double seconds);

	[[nodiscard]] std::size_t particleCount() const { return m_simulation ? m_simulation->particleCount() : 0; }
	[[nodiscard]] double time() const { return m_simulation ? m_simulation->time() : 0; }

signals:
	void advanced();

protected:
	void paintEvent(QPaintEvent* event) override;
	void showEvent(QShowEvent* event) override;
	void hideEvent(QHideEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void wheelEvent(QWheelEvent* event) override;

private:
	void tick();
	void updateTimer();
	[[nodiscard]] const QImage& tinted(int system, double r, double g, double b);
	[[nodiscard]] std::optional<QPointF> project(const Vec3& p, double* depth = nullptr) const;

	std::unique_ptr<ParticleSimulation> m_simulation;
	std::vector<QImage> m_sprites;            ///< per system, premultiplied
	QHash<quint64, QImage> m_tinted;
	QTimer m_timer;
	qint64 m_lastTick = 0;
	bool m_playing = true;
	bool m_grid = true;
	double m_speed = 1;
	double m_yaw = 35, m_pitch = 18, m_distance = 260;
	Vec3 m_target{0, 0, 48};
	QPointF m_dragFrom;
};

class ParticleBrowser : public QWidget {
	Q_OBJECT

public:
	ParticleBrowser(Editor* editor, AssetBrowser* assets, QWidget* parent = nullptr);
	[[nodiscard]] QTreeWidget* tree() const { return m_tree; }
	[[nodiscard]] ParticlePreview* preview() const { return m_preview; }
	/// Selects a system by name in the list (shows it); false if there is none.
	bool selectSystem(const QString& name);

private:
	void fill();
	void filter(const QString& text);
	void showSystem(const QString& name, const QString& file);
	void showParts(const ParticleDefinition* definition, const QString& file);
	[[nodiscard]] std::vector<ParticleDefinition> definitionsOf(const QString& file);

	Editor* m_editor;
	AssetBrowser* m_assets;
	QLineEdit* m_search;
	QTreeWidget* m_tree;
	QLabel* m_info;
	QLabel* m_title;
	QLabel* m_subtitle;
	ParticlePreview* m_preview;
	QPushButton* m_play;
	QComboBox* m_speed;
	QCheckBox* m_grid;
	QScrollArea* m_partsScroll;
	QWidget* m_parts = nullptr;
	QHash<QString, std::vector<ParticleDefinition>> m_files;   ///< read .pcf files (cleared with the game)
};

} // namespace sme::app
