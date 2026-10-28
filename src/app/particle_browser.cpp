// particle_browser.cpp - Particles page with preview (see particle_browser.h).

#include "particle_browser.h"

#include "copy_paste.h"

#include "asset_browser.h"
#include "editor.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>

#include <sme/filesystem.h>
#include <sme/game.h>
#include <sme/material.h>
#include <sme/text.h>
#include <sme/texture.h>

namespace sme::app {

namespace {

/// Tree whose particle systems can be dragged into the 3D view.
class ParticleTree : public QTreeWidget {
public:
	using QTreeWidget::QTreeWidget;

protected:
	QStringList mimeTypes() const override { return {"application/x-sme-particle"}; }
	QMimeData* mimeData(const QList<QTreeWidgetItem*>& items) const override {
		if (items.isEmpty() || items.first()->childCount() > 0) {
			return nullptr;
		}
		auto* mime = new QMimeData;
		mime->setData("application/x-sme-particle", items.first()->text(0).toUtf8());
		mime->setText(items.first()->text(0));
		return mime;
	}
};

constexpr int SPRITE_SIZE = 64;

/// A soft round dot: the sprite when the material or its texture cannot be read.
QImage softDot() {
	QImage image(SPRITE_SIZE, SPRITE_SIZE, QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	QPainter p(&image);
	QRadialGradient gradient(QPointF(SPRITE_SIZE / 2.0, SPRITE_SIZE / 2.0), SPRITE_SIZE / 2.0);
	gradient.setColorAt(0, QColor(255, 255, 255, 255));
	gradient.setColorAt(1, QColor(255, 255, 255, 0));
	p.fillRect(image.rect(), gradient);
	return image;
}

/// The sprite of a particle material: its base texture, small; additive materials glow by
/// their brightness, so their alpha becomes the brightness.
QImage spriteOf(const GameFileSystem* fs, const std::string& materialName, bool* additive) {
	*additive = true;
	if (!fs || materialName.empty()) return softDot();
	const auto material = loadMaterial(*fs, materialName);
	if (!material) return softDot();
	*additive = material->flag("$additive") || iequals(material->param("$additive"), "1");
	const auto texture = material->previewTexture();
	const auto image = texture.empty() ? std::nullopt : loadTexture(*fs, texture, SPRITE_SIZE);
	if (!image || image->width == 0 || image->height == 0) return softDot();
	QImage out(image->width, image->height, QImage::Format_RGBA8888);
	for (int y = 0; y < image->height; ++y) {
		std::memcpy(out.scanLine(y), image->pixels.data() + static_cast<std::size_t>(y) * image->width * 4, static_cast<std::size_t>(image->width) * 4);
	}
	out = out.convertToFormat(QImage::Format_ARGB32_Premultiplied).scaled(SPRITE_SIZE, SPRITE_SIZE, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
	return out;
}

QLabel* label(const QString& text, const char* role) {
	auto* l = new QLabel(text);
	l->setProperty("role", role);
	l->setTextInteractionFlags(Qt::TextSelectableByMouse);
	return l;
}

/// One part as a card: its function name and its values.
QFrame* card(const QString& title, const std::vector<ParticleValue>& values, const QString& note = {}) {
	auto* frame = new QFrame;
	frame->setObjectName("particleCard");
	auto* v = new QVBoxLayout(frame);
	v->setContentsMargins(10, 8, 10, 8);
	v->setSpacing(4);
	v->addWidget(label(title, "cardTitle"));
	if (!note.isEmpty()) v->addWidget(label(note, "cardNote"));
	if (!values.empty()) {
		auto* grid = new QGridLayout;
		grid->setHorizontalSpacing(10);
		grid->setVerticalSpacing(2);
		int row = 0;
		for (const auto& value : values) {
			grid->addWidget(label(QString::fromStdString(value.key), "valueKey"), row, 0, Qt::AlignTop);
			auto* text = label(QString::fromStdString(value.text), "valueText");
			text->setWordWrap(true);
			grid->addWidget(text, row, 1);
			++row;
		}
		grid->setColumnStretch(1, 1);
		v->addLayout(grid);
	}
	return frame;
}

} // namespace

// --- Preview -------------------------------------------------------------------------------

ParticlePreview::ParticlePreview(QWidget* parent) : QWidget(parent) {
	setObjectName("particlePreview");
	setMinimumSize(160, 90);   // small enough for the dock under the map
	setAttribute(Qt::WA_OpaquePaintEvent);
	setCursor(Qt::OpenHandCursor);
	m_timer.setInterval(16);
	connect(&m_timer, &QTimer::timeout, this, &ParticlePreview::tick);
}

void ParticlePreview::setSystem(std::vector<ParticleDefinition> library, const QString& root, const GameFileSystem* fs) {
	{
		// Frame the effect: run a copy for a moment and look at where its particles go.
		ParticleSimulation probe(library, root.toStdString());
		probe.loop = false;
		Bounds box;
		for (int i = 0; i < 6; ++i) {
			probe.step(0.3);
			for (const auto& s : probe.sprites()) {
				box.add(s.position - Vec3{s.radius, s.radius, s.radius});
				box.add(s.position + Vec3{s.radius, s.radius, s.radius});
			}
		}
		if (!box.empty()) {
			const Vec3 size = box.size();
			m_target = box.center();
			m_distance = std::clamp(std::max({size.x, size.y, size.z}) * 1.9, 60.0, 3000.0);
		} else {
			m_target = {0, 0, 32};
			m_distance = 200;
		}
	}
	m_simulation = std::make_unique<ParticleSimulation>(std::move(library), root.toStdString());
	m_sprites.clear();
	m_tinted.clear();
	for (auto& system : m_simulation->systems()) {
		bool additive = true;
		m_sprites.push_back(spriteOf(fs, system.material, &additive));
		system.additive = additive;
	}
	m_lastTick = QDateTime::currentMSecsSinceEpoch();
	updateTimer();
	update();
}

void ParticlePreview::clear() {
	m_simulation.reset();
	m_sprites.clear();
	m_tinted.clear();
	updateTimer();
	update();
}

void ParticlePreview::setPlaying(bool playing) {
	m_playing = playing;
	m_lastTick = QDateTime::currentMSecsSinceEpoch();
	updateTimer();
}

void ParticlePreview::restart() {
	if (m_simulation) m_simulation->restart();
	m_lastTick = QDateTime::currentMSecsSinceEpoch();
	update();
}

void ParticlePreview::setGridShown(bool shown) {
	m_grid = shown;
	update();
}

void ParticlePreview::advance(double seconds) {
	if (!m_simulation) return;
	m_simulation->step(seconds);
	emit advanced();
	update();
}

void ParticlePreview::updateTimer() {
	// Simulate only while it can be seen.
	const bool run = m_simulation && m_simulation->valid() && m_playing && isVisible();
	if (run && !m_timer.isActive()) {
		m_lastTick = QDateTime::currentMSecsSinceEpoch();
		m_timer.start();
	} else if (!run) {
		m_timer.stop();
	}
}

void ParticlePreview::tick() {
	if (!isVisible()) {
		updateTimer();   // moved into a hidden dock or page: stop
		return;
	}
	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	const double seconds = std::clamp((now - m_lastTick) / 1000.0, 0.0, 0.1) * m_speed;
	m_lastTick = now;
	advance(seconds);
}

void ParticlePreview::showEvent(QShowEvent* event) {
	QWidget::showEvent(event);
	updateTimer();
}

void ParticlePreview::hideEvent(QHideEvent* event) {
	QWidget::hideEvent(event);
	updateTimer();
}

void ParticlePreview::mousePressEvent(QMouseEvent* event) {
	m_dragFrom = event->position();
}

void ParticlePreview::mouseMoveEvent(QMouseEvent* event) {
	if (!(event->buttons() & (Qt::LeftButton | Qt::RightButton))) return;
	const QPointF d = event->position() - m_dragFrom;
	m_dragFrom = event->position();
	if (event->buttons() & Qt::RightButton) {
		m_target.z += d.y() * m_distance / 400;   // right drag: up and down
	} else {
		m_yaw -= d.x() * 0.4;
		m_pitch = std::clamp(m_pitch + d.y() * 0.3, -85.0, 85.0);
	}
	update();
}

void ParticlePreview::wheelEvent(QWheelEvent* event) {
	m_distance = std::clamp(m_distance * std::pow(0.999, event->angleDelta().y()), 30.0, 4000.0);
	update();
}

std::optional<QPointF> ParticlePreview::project(const Vec3& p, double* depth) const {
	// Orbit camera around m_target; Source's Z is up.
	const double yaw = m_yaw * std::numbers::pi / 180, pitch = m_pitch * std::numbers::pi / 180;
	const Vec3 forward{-std::cos(pitch) * std::cos(yaw), -std::cos(pitch) * std::sin(yaw), -std::sin(pitch)};
	const Vec3 eye = m_target - forward * m_distance;
	const Vec3 right = normalize(cross(forward, {0, 0, 1}));
	const Vec3 up = cross(right, forward);
	const Vec3 d = p - eye;
	const double z = dot(d, forward);
	if (z < 1) return std::nullopt;
	if (depth) *depth = z;
	const double focal = height() / (2 * std::tan(30 * std::numbers::pi / 180));
	return QPointF(width() / 2.0 + dot(d, right) / z * focal, height() / 2.0 - dot(d, up) / z * focal);
}

const QImage& ParticlePreview::tinted(int system, double r, double g, double b) {
	// Colors in 32 steps per channel: enough for the eye, few images.
	const auto q = [](double v) { return static_cast<quint64>(std::clamp(v, 0.0, 1.0) * 31 + 0.5); };
	const quint64 key = (static_cast<quint64>(system) << 15) | (q(r) << 10) | (q(g) << 5) | q(b);
	if (const auto it = m_tinted.constFind(key); it != m_tinted.constEnd()) return *it;
	if (m_tinted.size() > 2048) m_tinted.clear();
	QImage image = m_sprites[static_cast<std::size_t>(system)];
	const int rr = int(q(r) * 255 / 31), gg = int(q(g) * 255 / 31), bb = int(q(b) * 255 / 31);
	for (int y = 0; y < image.height(); ++y) {
		auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
		for (int x = 0; x < image.width(); ++x) {
			const QRgb c = line[x];
			line[x] = qRgba(qRed(c) * rr / 255, qGreen(c) * gg / 255, qBlue(c) * bb / 255, qAlpha(c));
		}
	}
	return *m_tinted.insert(key, image);
}

void ParticlePreview::paintEvent(QPaintEvent*) {
	QPainter p(this);
	QLinearGradient sky(0, 0, 0, height());
	sky.setColorAt(0, QColor(42, 45, 52));
	sky.setColorAt(1, QColor(22, 23, 27));
	p.fillRect(rect(), sky);
	p.setRenderHint(QPainter::Antialiasing);
	if (m_grid) {
		// Floor grid, 16 units, every eighth line brighter.
		for (int i = -16; i <= 16; ++i) {
			const double a = i * 16.0;
			const QColor color = i == 0 ? QColor(90, 96, 108) : (i % 8 == 0 ? QColor(62, 66, 74) : QColor(44, 47, 53));
			p.setPen(QPen(color, 1));
			const auto x0 = project({a, -256, 0}), x1 = project({a, 256, 0});
			if (x0 && x1) p.drawLine(*x0, *x1);
			const auto y0 = project({-256, a, 0}), y1 = project({256, a, 0});
			if (y0 && y1) p.drawLine(*y0, *y1);
		}
	}
	if (!m_simulation || !m_simulation->valid()) {
		p.setPen(QColor(150, 155, 165));
		p.drawText(rect(), Qt::AlignCenter, m_simulation ? tr("This system could not be read") : tr("Pick a particle system on the left"));
		return;
	}
	struct Drawn {
		double depth;
		QPointF at;
		double size;
		const ParticleSprite* sprite;
	};
	const auto sprites = m_simulation->sprites();
	std::vector<Drawn> drawn;
	drawn.reserve(sprites.size());
	const double focal = height() / (2 * std::tan(30 * std::numbers::pi / 180));
	for (const auto& s : sprites) {
		double depth = 0;
		if (const auto at = project(s.position, &depth)) drawn.push_back({depth, *at, s.radius / depth * focal, &s});
	}
	std::sort(drawn.begin(), drawn.end(), [](const Drawn& a, const Drawn& b) { return a.depth > b.depth; });   // far first
	const auto& systems = m_simulation->systems();
	for (const auto& d : drawn) {
		const auto& s = *d.sprite;
		const auto& info = systems[static_cast<std::size_t>(s.system)];
		if (s.alpha <= 0.003 || d.size < 0.05) continue;
		p.setCompositionMode(info.additive ? QPainter::CompositionMode_Plus : QPainter::CompositionMode_SourceOver);
		p.setOpacity(std::clamp(s.alpha, 0.0, 1.0));
		if (info.trail) {
			// A streak along the movement.
			const auto tail = project(s.position - s.velocity * 0.04);
			QColor color = QColor::fromRgbF(float(s.r), float(s.g), float(s.b));
			p.setPen(QPen(color, std::max(1.0, d.size * 1.5), Qt::SolidLine, Qt::RoundCap));
			p.drawLine(d.at, tail.value_or(d.at));
			continue;
		}
		const QImage& image = tinted(s.system, s.r, s.g, s.b);
		const QRectF target(d.at.x() - d.size, d.at.y() - d.size, d.size * 2, d.size * 2);
		if (std::abs(s.rotation) > 0.5 && d.size > 3) {
			p.save();
			p.translate(d.at);
			p.rotate(s.rotation);
			p.drawImage(QRectF(-d.size, -d.size, d.size * 2, d.size * 2), image);
			p.restore();
		} else {
			p.drawImage(target, image);
		}
	}
	p.setOpacity(1);
	p.setCompositionMode(QPainter::CompositionMode_SourceOver);
	p.setPen(QColor(150, 155, 165));
	p.drawText(rect().adjusted(10, 8, -10, -8), Qt::AlignLeft | Qt::AlignBottom,
	           tr("%n particle(s)  ·  %1 s", nullptr, int(sprites.size())).arg(m_simulation->time(), 0, 'f', 1));
}

// --- Browser -------------------------------------------------------------------------------

ParticleBrowser::ParticleBrowser(Editor* editor, AssetBrowser* assets, QWidget* parent) : QWidget(parent), m_editor(editor), m_assets(assets) {
	setObjectName("particleBrowser");
	setStyleSheet(QStringLiteral(
		"QFrame#particleCard { background: #2a2c31; border: 1px solid #363940; border-radius: 6px; }"
		"QLabel[role=\"sectionHeader\"] { color: #8d949f; font-size: 8pt; font-weight: 600; letter-spacing: 1px; margin-top: 8px; }"
		"QLabel[role=\"cardTitle\"] { color: #f0f1f3; font-weight: 600; }"
		"QLabel[role=\"cardNote\"] { color: #e8a050; font-size: 8.5pt; }"
		"QLabel[role=\"valueKey\"] { color: #9399a3; }"
		"QLabel[role=\"valueText\"] { color: #d6d9de; }"
		"QLabel#particleTitle { font-size: 13pt; font-weight: 600; color: #f0f1f3; }"
		"QLabel#particleSubtitle { color: #9399a3; }"));
	auto* outer = new QVBoxLayout(this);
	outer->setContentsMargins(0, 0, 0, 0);
	auto* splitter = new QSplitter(Qt::Horizontal);
	splitter->setObjectName("particleSplitter");
	splitter->setChildrenCollapsible(true);   // as a narrow dock the parts or the list may fold away
	outer->addWidget(splitter);

	// Left: the systems.
	auto* left = new QWidget;
	auto* v = new QVBoxLayout(left);
	v->setContentsMargins(4, 4, 4, 4);
	m_search = new QLineEdit;
	m_search->setPlaceholderText(tr("Search particle systems (fire, smoke, spark ...)"));
	m_search->setClearButtonEnabled(true);
	auto* searchRow = new QHBoxLayout;
	searchRow->addWidget(m_search, 1);
	auto* importParticles = new QPushButton(tr("Import ..."));
	importParticles->setObjectName("importParticles");
	importParticles->setToolTip(tr("Bring a particle file (.pcf) into the mod and its particle list"));
	searchRow->addWidget(importParticles);
	connect(importParticles, &QPushButton::clicked, this, &ParticleBrowser::importRequested);
	v->addLayout(searchRow);
	m_tree = new ParticleTree;
	addCopyShortcut(m_tree, Qt::DisplayRole);   // Ctrl+C: the effect name
	m_tree->setHeaderHidden(true);
	m_tree->setDragEnabled(true);
	m_tree->setDragDropMode(QAbstractItemView::DragOnly);
	m_tree->setUniformRowHeights(true);
	v->addWidget(m_tree, 1);
	m_info = new QLabel(tr("No game loaded"));
	m_info->setWordWrap(true);
	m_info->setProperty("role", "description");
	v->addWidget(m_info);
	splitter->addWidget(left);

	// Middle: the preview.
	auto* middle = new QWidget;
	auto* m = new QVBoxLayout(middle);
	m->setContentsMargins(8, 6, 8, 6);
	m->setSpacing(6);
	m_title = new QLabel(tr("Particles"));
	m_title->setObjectName("particleTitle");
	m_subtitle = new QLabel(tr("Pick a system to see it move. The preview is an approximation of the game."));
	m_subtitle->setObjectName("particleSubtitle");
	m_subtitle->setWordWrap(true);
	m->addWidget(m_title);
	m->addWidget(m_subtitle);
	m_preview = new ParticlePreview;
	m->addWidget(m_preview, 1);
	auto* controls = new QHBoxLayout;
	m_play = new QPushButton(tr("Pause"));
	m_play->setObjectName("particlePlay");
	auto* restart = new QPushButton(tr("Restart"));
	restart->setObjectName("particleRestart");
	m_speed = new QComboBox;
	m_speed->setObjectName("particleSpeed");
	for (const double s : {0.25, 0.5, 1.0, 2.0}) m_speed->addItem(QStringLiteral("%1×").arg(s), s);
	m_speed->setCurrentIndex(2);
	m_grid = new QCheckBox(tr("Grid"));
	m_grid->setChecked(true);
	controls->addWidget(m_play);
	controls->addWidget(restart);
	controls->addWidget(new QLabel(tr("Speed")));
	controls->addWidget(m_speed);
	controls->addWidget(m_grid);
	controls->addStretch(1);
	m->addLayout(controls);
	// On its own line, so the dock under the map can stay narrow.
	auto* hint = new QLabel(tr("Drag: turn · right drag: up/down · wheel: zoom"));
	hint->setProperty("role", "description");
	hint->setWordWrap(true);
	m->addWidget(hint);
	splitter->addWidget(middle);

	// Right: the parts as cards.
	m_partsScroll = new QScrollArea;
	m_partsScroll->setObjectName("particleParts");
	m_partsScroll->setWidgetResizable(true);
	m_partsScroll->setFrameShape(QFrame::NoFrame);
	splitter->addWidget(m_partsScroll);
	splitter->setStretchFactor(0, 2);
	splitter->setStretchFactor(1, 5);
	splitter->setStretchFactor(2, 3);
	splitter->setSizes({260, 640, 380});
	showParts(nullptr, {});

	connect(m_play, &QPushButton::clicked, this, [this] {
		m_preview->setPlaying(!m_preview->playing());
		m_play->setText(m_preview->playing() ? tr("Pause") : tr("Play"));
	});
	connect(restart, &QPushButton::clicked, m_preview, &ParticlePreview::restart);
	connect(m_speed, &QComboBox::currentIndexChanged, this, [this] { m_preview->setSpeed(m_speed->currentData().toDouble()); });
	connect(m_grid, &QCheckBox::toggled, m_preview, &ParticlePreview::setGridShown);
	connect(m_search, &QLineEdit::textChanged, this, &ParticleBrowser::filter);
	connect(assets, &AssetBrowser::materialsIndexed, this, &ParticleBrowser::fill);
	connect(editor, &Editor::gameChanged, this, [this] {
		m_files.clear();
		m_preview->clear();
		showParts(nullptr, {});
	});
	connect(m_tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) {
		if (item && item->childCount() == 0 && item->parent()) showSystem(item->text(0), item->toolTip(0));
	});
	connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) {
		if (!item || item->childCount() > 0) {
			return;
		}
		std::vector<int> targets;
		for (const int id : m_editor->selection().entities) {
			if (const auto* entity = m_editor->doc().findEntity(id); entity && entity->classname() == "info_particle_system") {
				targets.push_back(id);
			}
		}
		if (targets.empty()) {
			emit m_editor->message(tr("Drag the particle system into the 3D view, or select an info_particle_system to set it"));
			return;
		}
		m_editor->commit(edit::setKeyValue(m_editor->doc(), targets, "effect_name", item->text(0).toStdString()));
	});
}

void ParticleBrowser::fill() {
	m_tree->clear();
	m_files.clear();
	QMap<QString, QTreeWidgetItem*> files;
	for (const auto& system : m_assets->particles()) {
		const QString file = QString::fromStdString(system.file);
		auto it = files.find(file);
		if (it == files.end()) {
			auto* group = new QTreeWidgetItem(m_tree, {file.section('/', -1)});
			group->setToolTip(0, file);
			group->setFlags(group->flags() & ~Qt::ItemIsDragEnabled);
			it = files.insert(file, group);
		}
		auto* item = new QTreeWidgetItem(*it, {QString::fromStdString(system.name)});
		item->setToolTip(0, file);
	}
	m_info->setText(m_assets->particles().empty()
	                    ? tr("No particle systems found (particles/*.pcf)")
	                    : tr("%1 particle systems in %2 files - drag one into the map (info_particle_system).")
	                          .arg(m_assets->particles().size())
	                          .arg(files.size()));
	filter(m_search->text());
}

void ParticleBrowser::filter(const QString& text) {
	const QStringList words = text.toLower().split(' ', Qt::SkipEmptyParts);
	for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
		auto* group = m_tree->topLevelItem(i);
		int visible = 0;
		for (int j = 0; j < group->childCount(); ++j) {
			auto* item = group->child(j);
			bool show = true;
			for (const auto& word : words) show = show && item->text(0).toLower().contains(word);
			item->setHidden(!show);
			visible += show ? 1 : 0;
		}
		group->setHidden(visible == 0);
		group->setExpanded(!words.isEmpty() || m_tree->topLevelItemCount() <= 3);
	}
}

bool ParticleBrowser::selectSystem(const QString& name) {
	const auto found = m_tree->findItems(name, Qt::MatchFixedString | Qt::MatchRecursive);
	for (auto* item : found) {
		if (item->parent()) {
			item->parent()->setExpanded(true);
			m_tree->setCurrentItem(item);
			return true;
		}
	}
	return false;
}

std::vector<ParticleDefinition> ParticleBrowser::definitionsOf(const QString& file) {
	if (const auto it = m_files.constFind(file); it != m_files.constEnd()) return *it;
	std::vector<ParticleDefinition> definitions;
	if (const auto* game = m_editor->game()) {
		if (const auto data = game->files.read(file.toStdString())) definitions = readParticleDefinitions(*data);
	}
	m_files.insert(file, definitions);
	return definitions;
}

void ParticleBrowser::showSystem(const QString& name, const QString& file) {
	auto library = definitionsOf(file);
	// Children may live in other files: add the files of the names that are missing.
	for (int pass = 0; pass < 3; ++pass) {
		std::vector<std::string> missing;
		for (const auto& definition : library) {
			for (const auto& child : definition.children) {
				const bool have = std::any_of(library.begin(), library.end(), [&](const ParticleDefinition& d) { return iequals(d.name, child.name); });
				if (!have) missing.push_back(child.name);
			}
		}
		if (missing.empty()) break;
		for (const auto& childName : missing) {
			for (const auto& system : m_assets->particles()) {
				if (iequals(system.name, childName)) {
					auto more = definitionsOf(QString::fromStdString(system.file));
					library.insert(library.end(), std::make_move_iterator(more.begin()), std::make_move_iterator(more.end()));
					break;
				}
			}
		}
	}
	const ParticleDefinition* definition = nullptr;
	for (const auto& d : library) {
		if (iequals(d.name, name.toStdString())) definition = &d;
	}
	m_title->setText(name);
	if (!definition) {
		m_subtitle->setText(tr("%1 - could not be read").arg(file));
		m_preview->clear();
		showParts(nullptr, file);
		return;
	}
	showParts(definition, file);
	m_subtitle->setText(tr("%1  ·  material %2").arg(file, QString::fromStdString(definition->material())));
	m_preview->setSystem(library, name, m_editor->game() ? &m_editor->game()->files : nullptr);
}

void ParticleBrowser::showParts(const ParticleDefinition* definition, const QString& file) {
	auto* parts = new QWidget;
	auto* v = new QVBoxLayout(parts);
	v->setContentsMargins(8, 4, 12, 8);
	v->setSpacing(6);
	if (!definition) {
		auto* empty = label(file.isEmpty() ? tr("The parts of the picked system show here: how it emits, what each particle starts with, "
		                                         "how it moves and changes, how it is drawn, and its child systems.")
		                                   : tr("This system could not be read from %1.").arg(file),
		                    "description");
		empty->setWordWrap(true);
		v->addWidget(empty);
		v->addStretch(1);
		m_partsScroll->setWidget(parts);
		m_parts = parts;
		return;
	}
	const auto section = [&](const QString& title, const std::vector<ParticleFunction>& functions, const QString& tip) {
		if (functions.empty()) return;
		auto* header = label(title.toUpper() + QStringLiteral("  ·  %1").arg(functions.size()), "sectionHeader");
		header->setToolTip(tip);
		v->addWidget(header);
		for (const auto& f : functions) v->addWidget(card(QString::fromStdString(f.name), f.values));
	};
	v->addWidget(label(tr("SYSTEM"), "sectionHeader"));
	v->addWidget(card(QString::fromStdString(definition->name), definition->properties.values));
	section(tr("Emitters"), definition->emitters, tr("When and how many particles are made"));
	section(tr("Initializers"), definition->initializers, tr("What every new particle starts with"));
	section(tr("Operators"), definition->operators, tr("How the particles move and change while they live"));
	section(tr("Forces"), definition->forces, tr("Pushes on the particles"));
	section(tr("Constraints"), definition->constraints, tr("Limits on the particles"));
	section(tr("Renderers"), definition->renderers, tr("How the particles are drawn"));
	if (!definition->children.empty()) {
		v->addWidget(label(tr("CHILDREN  ·  %1").arg(definition->children.size()), "sectionHeader"));
		for (const auto& child : definition->children) {
			auto* frame = card(QString::fromStdString(child.name), {}, child.delay > 0 ? tr("starts after %1 s").arg(child.delay) : QString());
			auto* open = new QPushButton(tr("Show"));
			open->setObjectName("particleChild");
			const QString childName = QString::fromStdString(child.name);
			connect(open, &QPushButton::clicked, this, [this, childName] { selectSystem(childName); });
			frame->layout()->addWidget(open);
			v->addWidget(frame);
		}
	}
	v->addStretch(1);
	m_partsScroll->setWidget(parts);   // deletes the old cards
	m_parts = parts;
}

} // namespace sme::app
