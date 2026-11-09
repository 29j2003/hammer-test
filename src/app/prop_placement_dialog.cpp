// prop_placement_dialog.cpp - "Place props" with a live preview (see prop_placement_dialog.h).

#include "prop_placement_dialog.h"

#include "editor.h"
#include "model_cache.h"
#include "ui_helpers.h"

#include <QAbstractSpinBox>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QShortcut>
#include <QSpinBox>
#include <QVBoxLayout>

#include <sme/studio_model.h>

namespace sme::app {

namespace {

QDoubleSpinBox* numberBox(QWidget* parent, const char* name, double lo, double hi, double value, double step, const QString& suffix, int decimals = 0) {
	auto* box = new QDoubleSpinBox(parent);
	box->setObjectName(name);
	box->setRange(lo, hi);
	box->setDecimals(decimals);
	box->setSingleStep(step);
	box->setSuffix(suffix);
	box->setValue(value);
	return box;
}

QWidget* row(QWidget* parent, std::initializer_list<QWidget*> widgets) {
	auto* w = new QWidget(parent);
	auto* h = new QHBoxLayout(w);
	h->setContentsMargins(0, 0, 0, 0);
	for (auto* widget : widgets) h->addWidget(widget);
	h->addStretch(1);
	return w;
}

} // namespace

PropPlacementDialog::PropPlacementDialog(Editor* editor, std::function<QString()> browserModel, QWidget* parent)
    : QDialog(parent), m_editor(editor), m_browserModel(std::move(browserModel)) {
	setObjectName("propPlacementDialog");
	setWindowTitle(tr("Place props"));
	setModal(false);
	QSettings settings;
	auto* outer = new QVBoxLayout(this);

	// What to place.
	auto* top = new QFormLayout;
	m_model = new QLineEdit(this);
	m_model->setObjectName("propsModel");
	m_model->setPlaceholderText(tr("models/props_c17/fence01a.mdl"));
	auto* useModel = new QPushButton(tr("Use selected"), this);
	useModel->setObjectName("propsUseModel");
	useModel->setToolTip(tr("The model of the selected prop in the map, else the model picked in the asset browser"));
	auto* modelRow = new QWidget(this);
	auto* mh = new QHBoxLayout(modelRow);
	mh->setContentsMargins(0, 0, 0, 0);
	mh->addWidget(m_model, 1);
	mh->addWidget(useModel);
	top->addRow(tr("Model"), modelRow);
	m_class = new QComboBox(this);
	m_class->setObjectName("propsClass");
	m_class->setEditable(true);
	m_class->addItems({"prop_static", "prop_dynamic", "prop_physics"});
	m_class->setToolTip(tr("prop_static: cheapest, for anything that never moves. Scale works with prop_dynamic and prop_physics; "
	                       "prop_static ignores it in most Source 1 games"));
	top->addRow(tr("Entity"), m_class);
	outer->addLayout(top);

	auto* modeRow = new QHBoxLayout;
	m_pathMode = new QRadioButton(tr("Along a path"), this);
	m_pathMode->setObjectName("propsPathMode");
	m_pathMode->setToolTip(tr("Fences, kerb stones, lamp posts: one every so many units along clicked points or selected edges"));
	m_scatterMode = new QRadioButton(tr("Scatter over faces"), this);
	m_scatterMode->setObjectName("propsScatterMode");
	m_scatterMode->setToolTip(tr("Trees, bushes, rocks: at random over the selected faces or displacements"));
	auto* modes = new QButtonGroup(this);
	modes->addButton(m_pathMode);
	modes->addButton(m_scatterMode);
	modeRow->addWidget(m_pathMode);
	modeRow->addWidget(m_scatterMode);
	modeRow->addStretch(1);
	outer->addLayout(modeRow);

	// Along a path.
	m_pathBox = new QWidget(this);
	auto* pathLayout = new QVBoxLayout(m_pathBox);
	pathLayout->setContentsMargins(0, 0, 0, 0);
	m_clickPoints = new QPushButton(tr("Click points"), m_pathBox);
	m_clickPoints->setObjectName("propsClick");
	m_clickPoints->setCheckable(true);
	m_clickPoints->setToolTip(tr("Each click in a view adds a point of the path. Enter places, Backspace takes the last point back, Esc stops"));
	auto* undoPoint = new QPushButton(tr("Remove last"), m_pathBox);
	auto* clearPoints = new QPushButton(tr("Clear"), m_pathBox);
	auto* edges = new QPushButton(tr("Use selected edges"), m_pathBox);
	edges->setObjectName("propsUseEdges");
	edges->setToolTip(tr("Along edges picked with the vertex tool (or between two selected faces): a kerb's top edge, a wall's foot"));
	pathLayout->addWidget(row(m_pathBox, {m_clickPoints, undoPoint, clearPoints, edges}));
	auto* pathForm = new QFormLayout;
	m_spacing = numberBox(m_pathBox, "propsSpacing", 1, 16384, settings.value("props/spacing", 128).toDouble(), 8, tr(" units"));
	auto* modelLength = new QPushButton(tr("Model length"), m_pathBox);
	modelLength->setObjectName("propsModelLength");
	modelLength->setToolTip(tr("Spacing = the model's length, so pieces of a fence or kerb line up end to end"));
	pathForm->addRow(tr("Spacing"), row(m_pathBox, {m_spacing, modelLength}));
	m_sideOffset = numberBox(m_pathBox, "propsSideOffset", -8192, 8192, settings.value("props/sideOffset", 0).toDouble(), 8, tr(" units"));
	m_sideOffset->setToolTip(tr("To the right of the path (negative: left), e.g. lamps beside a road's centre line"));
	pathForm->addRow(tr("Beside the path"), m_sideOffset);
	m_yawOffset = numberBox(m_pathBox, "propsYawOffset", -360, 360, settings.value("props/yawOffset", 0).toDouble(), 90, tr("°"));
	m_yawOffset->setToolTip(tr("Extra turn: 90 for a model that faces sideways"));
	pathForm->addRow(tr("Extra turn"), m_yawOffset);
	m_align = new QCheckBox(tr("Turn along the path"), m_pathBox);
	m_align->setChecked(settings.value("props/align", true).toBool());
	m_includeEnd = new QCheckBox(tr("One at the very end"), m_pathBox);
	m_includeEnd->setChecked(settings.value("props/includeEnd", true).toBool());
	pathForm->addRow(QString(), row(m_pathBox, {m_align, m_includeEnd}));
	m_jitter = numberBox(m_pathBox, "propsJitter", 0, 180, settings.value("props/jitter", 0).toDouble(), 5, tr("°"));
	m_jitter->setToolTip(tr("Turned at random up to this much either way (debris, rocks)"));
	pathForm->addRow(tr("Random turn"), m_jitter);
	pathLayout->addLayout(pathForm);
	outer->addWidget(m_pathBox);

	// Scatter.
	m_scatterBox = new QWidget(this);
	auto* scatterLayout = new QVBoxLayout(m_scatterBox);
	scatterLayout->setContentsMargins(0, 0, 0, 0);
	auto* surfaceButton = new QPushButton(tr("Use selected faces"), m_scatterBox);
	surfaceButton->setObjectName("propsUseSurface");
	surfaceButton->setToolTip(tr("The ground: selected faces or displacements (Faces mode), or the tops of selected brushes"));
	m_surfaceLabel = new QLabel(m_scatterBox);
	scatterLayout->addWidget(row(m_scatterBox, {surfaceButton, m_surfaceLabel}));
	auto* avoidButton = new QPushButton(tr("Keep off selected"), m_scatterBox);
	avoidButton->setObjectName("propsUseAvoid");
	avoidButton->setToolTip(tr("Select the road (or paths, buildings) and click: no prop stands on or near them"));
	auto* clearAvoid = new QPushButton(tr("Clear"), m_scatterBox);
	m_avoidLabel = new QLabel(m_scatterBox);
	scatterLayout->addWidget(row(m_scatterBox, {avoidButton, clearAvoid, m_avoidLabel}));
	auto* scatterForm = new QFormLayout;
	m_density = numberBox(m_scatterBox, "propsDensity", 0.1, 400, settings.value("props/density", 4).toDouble(), 1, tr(" per 512×512"), 1);
	scatterForm->addRow(tr("How many"), m_density);
	m_minSpacing = numberBox(m_scatterBox, "propsMinSpacing", 0, 4096, settings.value("props/minSpacing", 64).toDouble(), 8, tr(" units"));
	m_minSpacing->setToolTip(tr("Never closer to each other than this"));
	scatterForm->addRow(tr("Apart at least"), m_minSpacing);
	m_keepAway = numberBox(m_scatterBox, "propsKeepAway", 0, 4096, settings.value("props/keepAway", 32).toDouble(), 8, tr(" units"));
	scatterForm->addRow(tr("Off the kept-off brushes"), m_keepAway);
	m_maxSlope = numberBox(m_scatterBox, "propsMaxSlope", 0, 90, settings.value("props/maxSlope", 35).toDouble(), 5, tr("°"));
	m_maxSlope->setToolTip(tr("Steeper ground gets nothing"));
	scatterForm->addRow(tr("Steepest ground"), m_maxSlope);
	m_randomYaw = new QCheckBox(tr("Random turn"), m_scatterBox);
	m_randomYaw->setChecked(settings.value("props/randomYaw", true).toBool());
	m_tilt = new QCheckBox(tr("Tilt with the ground"), m_scatterBox);
	m_tilt->setToolTip(tr("Rocks lie on the slope; trees stay upright"));
	m_tilt->setChecked(settings.value("props/tilt", false).toBool());
	scatterForm->addRow(QString(), row(m_scatterBox, {m_randomYaw, m_tilt}));
	scatterLayout->addLayout(scatterForm);
	outer->addWidget(m_scatterBox);

	// Both.
	auto* common = new QFormLayout;
	m_scaleMin = numberBox(this, "propsScaleMin", 0.1, 10, settings.value("props/scaleMin", 1).toDouble(), 0.05, QString(), 2);
	m_scaleMax = numberBox(this, "propsScaleMax", 0.1, 10, settings.value("props/scaleMax", 1).toDouble(), 0.05, QString(), 2);
	auto* to = new QLabel(tr("to"), this);
	common->addRow(tr("Scale"), row(this, {m_scaleMin, to, m_scaleMax}));
	m_seed = new QSpinBox(this);
	m_seed->setObjectName("propsSeed");
	m_seed->setRange(1, 99999);
	m_seed->setValue(settings.value("props/seed", 1).toInt());
	auto* shuffle = new QPushButton(tr("Shuffle"), this);
	shuffle->setToolTip(tr("Other random choices (the same number always gives the same result)"));
	common->addRow(tr("Variation"), row(this, {m_seed, shuffle}));
	outer->addLayout(common);

	m_note = new QLabel(this);
	m_note->setObjectName("propsNote");
	m_note->setWordWrap(true);
	outer->addWidget(m_note);
	m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	m_buttons->button(QDialogButtonBox::Ok)->setText(tr("Place"));
	m_buttons->button(QDialogButtonBox::Ok)->setObjectName("propsOk");
	connect(m_buttons, &QDialogButtonBox::accepted, this, &PropPlacementDialog::accept);
	connect(m_buttons, &QDialogButtonBox::rejected, this, &PropPlacementDialog::reject);
	outer->addWidget(m_buttons);

	// Changes show soon (not on every key press).
	m_soon.setSingleShot(true);
	m_soon.setInterval(100);
	connect(&m_soon, &QTimer::timeout, this, &PropPlacementDialog::refresh);
	const auto later = [this] { m_soon.start(); };
	connect(m_model, &QLineEdit::textChanged, this, later);
	for (auto* box : {m_spacing, m_sideOffset, m_yawOffset, m_jitter, m_density, m_maxSlope, m_minSpacing, m_keepAway, m_scaleMin, m_scaleMax}) {
		connect(box, &QDoubleSpinBox::valueChanged, this, later);
	}
	for (auto* box : {m_align, m_includeEnd, m_randomYaw, m_tilt}) connect(box, &QCheckBox::toggled, this, later);
	connect(m_seed, &QSpinBox::valueChanged, this, later);
	connect(m_pathMode, &QRadioButton::toggled, this, &PropPlacementDialog::modeChanged);
	connect(shuffle, &QPushButton::clicked, this, [this] { m_seed->setValue(m_seed->value() % 99999 + 1); });
	connect(useModel, &QPushButton::clicked, this, [this] {
		const auto& selection = m_editor->selection();
		for (const int id : selection.entities) {
			if (const Entity* entity = m_editor->doc().findEntity(id); entity && !entity->get("model").empty()) {
				m_model->setText(QString::fromStdString(entity->get("model")));
				const int index = m_class->findText(QString::fromStdString(entity->classname()));
				if (index >= 0) m_class->setCurrentIndex(index);
				return;
			}
		}
		if (m_browserModel) {
			if (const QString picked = m_browserModel(); !picked.isEmpty()) m_model->setText(picked);
		}
	});
	connect(modelLength, &QPushButton::clicked, this, [this] {
		auto* cache = m_editor->models();
		const auto geometry = cache ? cache->get(model()) : nullptr;
		if (!geometry) {
			m_note->setText(tr("The model is still loading (or was not found) - try again in a moment."));
			return;
		}
		const Vec3 size = geometry->bounds.size();
		m_spacing->setValue(std::max(1.0, std::round(std::max(size.x, size.y))));
	});
	connect(m_clickPoints, &QPushButton::toggled, this, [this](bool on) {
		if (on) {
			m_drawing = true;
			requestPoint();
		} else {
			stopDrawing();
		}
	});
	connect(undoPoint, &QPushButton::clicked, this, [this] {
		if (m_points.empty()) return;
		m_points.pop_back();
		m_soon.start();
		if (m_drawing) requestPoint();
	});
	connect(clearPoints, &QPushButton::clicked, this, [this] {
		m_points.clear();
		m_soon.start();
	});
	connect(edges, &QPushButton::clicked, this, [this] { useSelectedEdges(); });
	connect(surfaceButton, &QPushButton::clicked, this, &PropPlacementDialog::useSelectedSurface);
	connect(avoidButton, &QPushButton::clicked, this, &PropPlacementDialog::useSelectedAvoid);
	connect(clearAvoid, &QPushButton::clicked, this, [this] {
		m_avoid.clear();
		m_soon.start();
	});
	auto* back = new QShortcut(QKeySequence(Qt::Key_Backspace), this);
	connect(back, &QShortcut::activated, this, [this, undoPoint] {
		if (!qobject_cast<QLineEdit*>(focusWidget()) && !qobject_cast<QAbstractSpinBox*>(focusWidget())) undoPoint->click();
	});
	connect(editor, &Editor::scenePointKey, this, [this, undoPoint](int key) {
		if (!m_drawing) return;
		if (key == Qt::Key_Backspace) {
			undoPoint->click();
		} else if (m_buttons->button(QDialogButtonBox::Ok)->isEnabled()) {
			accept();
		}
	});
	connect(editor, &Editor::scenePointRequestChanged, this, [this] {
		if (m_drawing && !m_editor->scenePointPending() && !m_finished) {
			QTimer::singleShot(0, this, [this] {
				if (m_drawing && !m_editor->scenePointPending()) stopDrawing();
			});
		}
	});
	if (auto* cache = editor->models()) connect(cache, &ModelCache::modelReady, this, later);

	// Start from what is selected: a prop gives the model, faces the ground, edges the path.
	useModel->click();
	if (m_model->text().isEmpty()) m_model->setText(settings.value("props/model").toString());
	const auto& selection = m_editor->selection();
	const bool faces = !selection.faces.empty();
	{
		QSignalBlocker block(m_pathMode);
		(faces || settings.value("props/scatter", false).toBool() ? m_scatterMode : m_pathMode)->setChecked(true);
	}
	if (faces) useSelectedSurface();
	if (!faces && !useSelectedEdges() && m_pathMode->isChecked()) m_clickPoints->setChecked(true);
	modeChanged();
	setMinimumWidth(460);
	refresh();
}

PropPlacementDialog::~PropPlacementDialog() {
	if (m_drawing) m_editor->cancelScenePoint();
	if (!m_finished) m_editor->clearPropPreview();
}

QString PropPlacementDialog::model() const {
	QString path = m_model->text().trimmed().replace('\\', '/');
	if (!path.isEmpty() && !path.startsWith("models/", Qt::CaseInsensitive)) path = "models/" + path;
	if (!path.isEmpty() && !path.endsWith(".mdl", Qt::CaseInsensitive)) path += ".mdl";
	return path;
}

bool PropPlacementDialog::scatterMode() const {
	return m_scatterMode->isChecked();
}

std::vector<PropPlacement> PropPlacementDialog::placements(bool* limited) const {
	if (limited) *limited = false;
	if (scatterMode()) {
		ScatterOptions options;
		options.density = m_density->value();
		options.maxSlope = m_maxSlope->value();
		options.minSpacing = m_minSpacing->value();
		options.keepAway = m_keepAway->value();
		options.randomYaw = m_randomYaw->isChecked();
		options.tiltWithGround = m_tilt->isChecked();
		options.scaleMin = m_scaleMin->value();
		options.scaleMax = m_scaleMax->value();
		options.seed = static_cast<std::uint32_t>(m_seed->value());
		std::vector<const Solid*> avoid;
		for (const auto& ref : m_avoid) {
			if (const Solid* solid = m_editor->doc().findSolid(ref)) avoid.push_back(solid);
		}
		return scatterProps(scatterSurface(m_editor->doc(), m_surface), avoid, options, limited);
	}
	PathPropOptions options;
	options.spacing = m_spacing->value();
	options.sideOffset = m_sideOffset->value();
	options.yawOffset = m_yawOffset->value();
	options.alignToPath = m_align->isChecked();
	options.includeEnd = m_includeEnd->isChecked();
	options.randomYaw = m_jitter->value();
	options.scaleMin = m_scaleMin->value();
	options.scaleMax = m_scaleMax->value();
	options.seed = static_cast<std::uint32_t>(m_seed->value());
	auto result = propsAlongPath(m_points, options);
	if (limited) *limited = int(result.size()) >= options.maxCount;
	return result;
}

void PropPlacementDialog::addPoint(const Vec3& point) {
	m_points.push_back(point);
	m_soon.start();
}

void PropPlacementDialog::useSelectedSurface() {
	const auto& selection = m_editor->selection();
	m_surface = selection.faces;
	for (const auto& ref : selection.solids) {
		if (const Solid* solid = m_editor->doc().findSolid(ref)) {
			for (const auto& side : solid->sides) m_surface.push_back({ref, side.id});
		}
	}
	m_soon.start();
}

void PropPlacementDialog::useSelectedAvoid() {
	m_avoid = m_editor->selection().solids;
	for (const auto& face : m_editor->selection().faces) {
		if (std::find(m_avoid.begin(), m_avoid.end(), face.solid) == m_avoid.end()) m_avoid.push_back(face.solid);
	}
	m_soon.start();
}

bool PropPlacementDialog::useSelectedEdges() {
	auto path = m_editor->selectedEdgePath();
	if (path.size() < 2) return false;
	stopDrawing();
	m_points = std::move(path);
	{
		QSignalBlocker block(m_pathMode);
		m_pathMode->setChecked(true);
	}
	modeChanged();
	m_soon.start();
	return true;
}

QString PropPlacementDialog::note() const {
	return m_note->text();
}

void PropPlacementDialog::modeChanged() {
	const bool scatter = scatterMode();
	m_pathBox->setVisible(!scatter);
	m_scatterBox->setVisible(scatter);
	if (scatter) stopDrawing();
	adjustSize();
	m_soon.start();
}

void PropPlacementDialog::requestPoint() {
	const QString hint = m_points.empty() ? tr("Props: click where the path starts - Esc stops")
	                                      : tr("Props: click the next point - Enter places, Backspace takes one back, Esc stops");
	m_editor->requestScenePoint(hint, [this](const Vec3& point) {
		m_points.push_back(point);
		m_soon.start();
		if (m_drawing) requestPoint();
	});
}

void PropPlacementDialog::stopDrawing() {
	if (!m_drawing) return;
	m_drawing = false;
	m_editor->cancelScenePoint();
	QSignalBlocker block(m_clickPoints);
	m_clickPoints->setChecked(false);
}

void PropPlacementDialog::refresh() {
	if (m_finished) return;
	m_surfaceLabel->setText(m_surface.empty() ? tr("no ground yet") : tr("%n face(s)", nullptr, int(m_surface.size())));
	m_avoidLabel->setText(m_avoid.empty() ? tr("nothing kept off") : tr("%n brush(es)", nullptr, int(m_avoid.size())));
	bool limited = false;
	const auto result = placements(&limited);
	const QString path = model();
	QStringList lines;
	auto* ok = m_buttons->button(QDialogButtonBox::Ok);
	if (path.isEmpty()) {
		lines << tr("Pick a model first (select a prop in the map, or one in the asset browser, then Use selected).");
	} else if (result.empty()) {
		lines << (scatterMode() ? (m_surface.empty() ? tr("Select the ground (faces or displacements) and click Use selected faces.")
		                                             : tr("No place for a prop: the ground is too steep, too small or all kept off."))
		                        : tr("Click at least two points of the path (or use selected edges)."));
	} else {
		lines << tr("%n prop(s) of %1.", nullptr, int(result.size())).arg(path.section('/', -1));
		if (limited) lines << tr("Limited to %n - fewer per area or a smaller ground gives the rest.", nullptr, int(result.size()));
		if (!scatterMode()) lines << tr("Path: %n point(s).", nullptr, int(m_points.size()));
	}
	m_note->setText(lines.join('\n'));
	ok->setEnabled(!path.isEmpty() && !result.empty());
	m_editor->setPropPreview(path, path.isEmpty() ? std::vector<PropPlacement>{} : result);
}

void PropPlacementDialog::accept() {
	m_soon.stop();
	QSettings settings;
	settings.setValue("props/model", m_model->text().trimmed());
	settings.setValue("props/scatter", scatterMode());
	settings.setValue("props/spacing", m_spacing->value());
	settings.setValue("props/sideOffset", m_sideOffset->value());
	settings.setValue("props/yawOffset", m_yawOffset->value());
	settings.setValue("props/align", m_align->isChecked());
	settings.setValue("props/includeEnd", m_includeEnd->isChecked());
	settings.setValue("props/jitter", m_jitter->value());
	settings.setValue("props/density", m_density->value());
	settings.setValue("props/minSpacing", m_minSpacing->value());
	settings.setValue("props/keepAway", m_keepAway->value());
	settings.setValue("props/maxSlope", m_maxSlope->value());
	settings.setValue("props/randomYaw", m_randomYaw->isChecked());
	settings.setValue("props/tilt", m_tilt->isChecked());
	settings.setValue("props/scaleMin", m_scaleMin->value());
	settings.setValue("props/scaleMax", m_scaleMax->value());
	settings.setValue("props/seed", m_seed->value());
	const auto result = placements();
	if (result.empty() || model().isEmpty()) return;
	stopDrawing();
	m_finished = true;
	m_editor->placeProps(m_class->currentText().trimmed(), model(), result);   // clears the preview
	QDialog::accept();
}

void PropPlacementDialog::done(int result) {
	m_soon.stop();
	stopDrawing();
	if (!m_finished) {
		m_finished = true;
		m_editor->clearPropPreview();
	}
	QDialog::done(result);
}

} // namespace sme::app
