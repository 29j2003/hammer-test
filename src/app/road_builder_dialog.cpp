// road_builder_dialog.cpp - "Build road" with a live preview (see road_builder_dialog.h).

#include "road_builder_dialog.h"

#include "editor.h"
#include "ui_helpers.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QShortcut>
#include <QSpinBox>
#include <QVBoxLayout>

namespace sme::app {

namespace {

/// The material the editor starts with (nothing picked yet).
const QString UNTOUCHED_MATERIAL = "DEV/DEV_MEASUREGENERIC01";

QDoubleSpinBox* unitsBox(QWidget* parent, const char* name, double lo, double hi, double value, double step) {
	auto* box = new QDoubleSpinBox(parent);
	box->setObjectName(name);
	box->setRange(lo, hi);
	box->setDecimals(0);
	box->setSingleStep(step);
	box->setSuffix(QObject::tr(" units"));
	box->setValue(value);
	return box;
}

} // namespace

RoadBuilderDialog::RoadBuilderDialog(Editor* editor, const QString& material, QWidget* parent) : QDialog(parent), m_editor(editor) {
	setObjectName("roadBuilderDialog");
	setWindowTitle(tr("Build road"));
	setModal(false);
	QSettings settings;
	auto* outer = new QVBoxLayout(this);

	// Where the road goes: clicked points, or the selected floor brushes.
	auto* modeRow = new QHBoxLayout;
	m_drawMode = new QRadioButton(tr("Draw a path"), this);
	m_drawMode->setObjectName("roadBuilderDraw");
	m_drawMode->setToolTip(tr("Click points along the road's middle in the 3D or top view; every corner becomes a curve"));
	m_selectionMode = new QRadioButton(tr("Along the selected floor brushes"), this);
	m_selectionMode->setObjectName("roadBuilderFromSelection");
	m_selectionMode->setToolTip(tr("The road follows the selected blocks (blocked in by hand) and replaces them; what the curves leave of them stays"));
	auto* group = new QButtonGroup(this);
	group->addButton(m_drawMode);
	group->addButton(m_selectionMode);
	modeRow->addWidget(m_drawMode);
	modeRow->addWidget(m_selectionMode);
	modeRow->addStretch(1);
	outer->addLayout(modeRow);

	auto* pointRow = new QHBoxLayout;
	m_clickPoints = new QPushButton(tr("Click points"), this);
	m_clickPoints->setObjectName("roadBuilderClick");
	m_clickPoints->setCheckable(true);
	m_clickPoints->setToolTip(tr("Each click in a view adds a point. Enter builds, Backspace takes the last point back, Esc stops"));
	m_undoPoint = new QPushButton(tr("Remove last"), this);
	m_undoPoint->setObjectName("roadBuilderUndoPoint");
	m_clearPoints = new QPushButton(tr("Clear"), this);
	m_clearPoints->setObjectName("roadBuilderClear");
	pointRow->addWidget(m_clickPoints);
	pointRow->addWidget(m_undoPoint);
	pointRow->addWidget(m_clearPoints);
	pointRow->addStretch(1);
	outer->addLayout(pointRow);

	auto* form = new QFormLayout;
	outer->addLayout(form);

	m_preset = new QComboBox(this);
	m_preset->setObjectName("roadBuilderPreset");
	m_preset->addItem(tr("2 lanes (256)"), 256);
	m_preset->addItem(tr("Wide 2 lanes (512)"), 512);
	m_preset->addItem(tr("4 lanes (1024)"), 1024);
	m_preset->addItem(tr("Custom"), 0);
	m_width = unitsBox(this, "roadBuilderWidth", 16, 8192, settings.value("roadBuilder/width", 256).toDouble(), 16);
	m_width->setToolTip(tr("Kerb to kerb. Typical: 256 for two lanes, 1024 for four"));
	auto* widthRow = new QWidget(this);
	auto* widthLayout = new QHBoxLayout(widthRow);
	widthLayout->setContentsMargins(0, 0, 0, 0);
	widthLayout->addWidget(m_preset);
	widthLayout->addWidget(m_width, 1);
	form->addRow(tr("Width"), widthRow);
	const auto syncPreset = [this] {
		const int index = m_preset->findData(int(m_width->value()));
		QSignalBlocker block(m_preset);
		m_preset->setCurrentIndex(index >= 0 ? index : m_preset->count() - 1);
	};
	syncPreset();
	connect(m_preset, &QComboBox::currentIndexChanged, this, [this] {
		if (const int w = m_preset->currentData().toInt(); w > 0) m_width->setValue(w);
	});
	connect(m_width, &QDoubleSpinBox::valueChanged, this, syncPreset);

	m_radius = unitsBox(this, "roadBuilderRadius", 0, 8192, settings.value("roadBuilder/radius", 128).toDouble(), 16);
	m_radius->setSpecialValueText(tr("Tight (0)"));
	m_radius->setToolTip(tr("Radius of a curve's inner edge. 0: as tight as it gets; corners too close together get the largest curve that fits"));
	form->addRow(tr("Curve radius"), m_radius);

	m_segments = new QSpinBox(this);
	m_segments->setObjectName("roadBuilderSegments");
	m_segments->setRange(1, 32);
	m_segments->setValue(settings.value("roadBuilder/segments", 8).toInt());
	m_segments->setToolTip(tr("Pieces per 90 degrees of a curve (like the Arch tool's sides)"));
	form->addRow(tr("Curve pieces per 90°"), m_segments);

	m_thickness = unitsBox(this, "roadBuilderThickness", 1, 1024, settings.value("roadBuilder/thickness", 16).toDouble(), 8);
	m_thickness->setToolTip(tr("How far the road's brushes reach below its surface"));
	form->addRow(tr("Thickness"), m_thickness);

	// A material field with a button that takes the current material from the browser.
	const auto materialRow = [this](QLineEdit* field, const QString& tip) {
		auto* row = new QWidget(this);
		auto* h = new QHBoxLayout(row);
		h->setContentsMargins(0, 0, 0, 0);
		h->addWidget(field, 1);
		auto* current = new QPushButton(tr("Use current"), row);
		current->setToolTip(tip);
		connect(current, &QPushButton::clicked, this, [this, field] { field->setText(m_editor->currentMaterial()); });
		h->addWidget(current);
		return row;
	};
	m_material = new QLineEdit(this);
	m_material->setObjectName("roadBuilderMaterial");
	QString start = material;
	// Along selected blocks their material is used (filled in below); else the picked one or the last.
	if (start.isEmpty() && m_editor->selectedRoadPaths().empty()) {
		if (m_editor->currentMaterial().compare(UNTOUCHED_MATERIAL, Qt::CaseInsensitive) != 0) start = m_editor->currentMaterial();
		if (start.isEmpty()) start = settings.value("roadBuilder/material").toString();
	}
	m_material->setText(start);
	form->addRow(tr("Road material"), materialRow(m_material, tr("The material picked in the browser")));

	m_displacement = new QCheckBox(tr("Displacement with a raised middle"), this);
	m_displacement->setObjectName("roadBuilderDisplacement");
	m_displacement->setToolTip(tr("Every road piece becomes a displacement you can sculpt; the middle is raised like a real road (water runs off). Nodraw brushes underneath keep the map sealed"));
	m_displacement->setChecked(settings.value("roadBuilder/displacement", false).toBool());
	form->addRow(QString(), m_displacement);
	m_power = new QComboBox(this);
	m_power->setObjectName("roadBuilderPower");
	m_power->addItem(tr("2 (5 x 5 points)"), 2);
	m_power->addItem(tr("3 (9 x 9 points)"), 3);
	m_power->setCurrentIndex(settings.value("roadBuilder/power", 3).toInt() == 2 ? 0 : 1);
	m_crown = unitsBox(this, "roadBuilderCrown", 0, 64, settings.value("roadBuilder/crown", 4).toDouble(), 1);
	m_roughness = unitsBox(this, "roadBuilderRoughness", 0, 32, settings.value("roadBuilder/roughness", 0).toDouble(), 1);
	auto* dispRow = new QWidget(this);
	auto* dispLayout = new QHBoxLayout(dispRow);
	dispLayout->setContentsMargins(0, 0, 0, 0);
	dispLayout->addWidget(new QLabel(tr("Power"), dispRow));
	dispLayout->addWidget(m_power);
	dispLayout->addWidget(new QLabel(tr("Middle up"), dispRow));
	dispLayout->addWidget(m_crown);
	dispLayout->addWidget(new QLabel(tr("Uneven"), dispRow));
	dispLayout->addWidget(m_roughness);
	form->addRow(QString(), dispRow);

	m_kerbs = new QCheckBox(tr("Kerbs along both sides"), this);
	m_kerbs->setObjectName("roadBuilderKerbs");
	m_kerbs->setToolTip(tr("Strips beside the road that follow it round the curves (wider ones are sidewalks)"));
	m_kerbs->setChecked(settings.value("roadBuilder/kerbs", false).toBool());
	form->addRow(QString(), m_kerbs);
	m_kerbWidth = unitsBox(this, "roadBuilderKerbWidth", 1, 1024, settings.value("roadBuilder/kerbWidth", 16).toDouble(), 8);
	m_kerbHeight = unitsBox(this, "roadBuilderKerbHeight", 1, 256, settings.value("roadBuilder/kerbHeight", 8).toDouble(), 2);
	auto* kerbRow = new QWidget(this);
	auto* kerbLayout = new QHBoxLayout(kerbRow);
	kerbLayout->setContentsMargins(0, 0, 0, 0);
	kerbLayout->addWidget(new QLabel(tr("Width"), kerbRow));
	kerbLayout->addWidget(m_kerbWidth);
	kerbLayout->addWidget(new QLabel(tr("Height"), kerbRow));
	kerbLayout->addWidget(m_kerbHeight);
	form->addRow(QString(), kerbRow);

	m_fit = new QCheckBox(tr("Fit into surroundings"), this);
	m_fit->setObjectName("roadBuilderFit");
	m_fit->setToolTip(tr("Brushes the road reaches into (the floor it is sunk into, a sidewalk in a curve's way) are carved around it, like Carve"));
	m_fit->setChecked(settings.value("roadBuilder/fit", true).toBool());
	form->addRow(QString(), m_fit);

	// Less common settings, folded away.
	auto* more = new QWidget(this);
	auto* moreForm = new QFormLayout(more);
	moreForm->setContentsMargins(0, 0, 0, 0);
	m_along = new QComboBox(more);
	m_along->setObjectName("roadBuilderAlong");
	m_along->addItem(tr("Auto (the texture's longer side)"), int(RoadAlong::Auto));
	m_along->addItem(tr("U (across the picture)"), int(RoadAlong::U));
	m_along->addItem(tr("V (down the picture)"), int(RoadAlong::V));
	m_along->setCurrentIndex(std::clamp(settings.value("roadBuilder/along", 0).toInt(), 0, 2));
	moreForm->addRow(tr("Texture runs along"), m_along);
	m_flip = new QCheckBox(tr("Flip direction"), more);
	m_flip->setObjectName("roadBuilderFlip");
	moreForm->addRow(QString(), m_flip);
	m_side = new QLineEdit(more);
	m_side->setObjectName("roadBuilderSideMaterial");
	m_side->setPlaceholderText(tr("the road material"));
	m_side->setText(settings.value("roadBuilder/sideMaterial").toString());
	moreForm->addRow(tr("Side material"), materialRow(m_side, tr("For the road's open sides and ends")));
	m_kerbMaterial = new QLineEdit(more);
	m_kerbMaterial->setObjectName("roadBuilderKerbMaterial");
	m_kerbMaterial->setPlaceholderText(tr("the side material"));
	m_kerbMaterial->setText(settings.value("roadBuilder/kerbMaterial").toString());
	moreForm->addRow(tr("Kerb material"), materialRow(m_kerbMaterial, tr("For the kerbs")));
	m_junction = new QLineEdit(more);
	m_junction->setObjectName("roadBuilderJunctionMaterial");
	m_junction->setPlaceholderText(tr("none: the other road runs through"));
	m_junction->setText(settings.value("roadBuilder/junctionMaterial").toString());
	moreForm->addRow(tr("Junction material"), materialRow(m_junction, tr("Where the road meets another one, that road's piece in the crossing gets this (plain asphalt, no lane lines)")));
	m_fill = new QLineEdit(more);
	m_fill->setObjectName("roadBuilderFillMaterial");
	m_fill->setPlaceholderText(tr("as it was"));
	m_fill->setText(settings.value("roadBuilder/fillMaterial").toString());
	moreForm->addRow(tr("Fill material"), materialRow(m_fill, tr("Along the selected floor brushes: the top of what the curves leave of them")));
	m_clearance = unitsBox(more, "roadBuilderClearance", 0, 4096, settings.value("roadBuilder/clearance", 128).toDouble(), 16);
	m_clearance->setToolTip(tr("Fit into surroundings: brushes are carved up to this height above the road"));
	moreForm->addRow(tr("Clear above the road"), m_clearance);
	outer->addWidget(detailsFoldOut(more, tr("More settings")));

	m_note = noteLabel(QString());
	m_note->setObjectName("roadBuilderNote");
	m_note->setMinimumWidth(420);
	m_note->setMinimumHeight(m_note->fontMetrics().lineSpacing() * 4 + 4);
	m_note->setAlignment(Qt::AlignLeft | Qt::AlignTop);
	outer->addWidget(m_note);

	m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	m_buttons->button(QDialogButtonBox::Ok)->setText(tr("Build"));
	outer->addWidget(m_buttons);
	connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

	m_soon.setSingleShot(true);
	m_soon.setInterval(60);
	connect(&m_soon, &QTimer::timeout, this, &RoadBuilderDialog::refresh);
	const auto later = [this] { m_soon.start(); };
	for (auto* field : {m_material, m_side, m_junction, m_kerbMaterial, m_fill}) connect(field, &QLineEdit::textChanged, this, later);
	for (auto* box : {m_width, m_thickness, m_radius, m_crown, m_roughness, m_kerbWidth, m_kerbHeight, m_clearance}) {
		connect(box, &QDoubleSpinBox::valueChanged, this, later);
	}
	connect(m_segments, &QSpinBox::valueChanged, this, later);
	for (auto* box : {m_flip, m_displacement, m_kerbs, m_fit}) connect(box, &QCheckBox::toggled, this, later);
	connect(m_along, &QComboBox::currentIndexChanged, this, later);
	connect(m_power, &QComboBox::currentIndexChanged, this, later);
	connect(m_displacement, &QCheckBox::toggled, this, [this](bool on) {
		for (QWidget* w : {static_cast<QWidget*>(m_power), static_cast<QWidget*>(m_crown), static_cast<QWidget*>(m_roughness)}) w->setEnabled(on);
	});
	connect(m_kerbs, &QCheckBox::toggled, this, [this](bool on) {
		m_kerbWidth->setEnabled(on);
		m_kerbHeight->setEnabled(on);
	});
	m_power->setEnabled(m_displacement->isChecked());
	m_crown->setEnabled(m_displacement->isChecked());
	m_roughness->setEnabled(m_displacement->isChecked());
	m_kerbWidth->setEnabled(m_kerbs->isChecked());
	m_kerbHeight->setEnabled(m_kerbs->isChecked());

	connect(m_clickPoints, &QPushButton::toggled, this, [this](bool on) {
		if (on) {
			m_drawing = true;
			requestPoint();
		} else {
			stopDrawing();
		}
	});
	connect(m_undoPoint, &QPushButton::clicked, this, &RoadBuilderDialog::undoPoint);
	connect(m_clearPoints, &QPushButton::clicked, this, [this] {
		m_points.clear();
		m_soon.start();
	});
	auto* back = new QShortcut(QKeySequence(Qt::Key_Backspace), this);
	connect(back, &QShortcut::activated, this, [this] {
		if (!qobject_cast<QLineEdit*>(focusWidget()) && !qobject_cast<QAbstractSpinBox*>(focusWidget())) undoPoint();
	});
	// Enter / Backspace in a view while clicking points.
	connect(editor, &Editor::scenePointKey, this, [this](int key) {
		if (!m_drawing) return;
		if (key == Qt::Key_Backspace) {
			undoPoint();
		} else if (m_buttons->button(QDialogButtonBox::Ok)->isEnabled()) {
			accept();
		}
	});
	// Esc in a view (or another tool) ends the request: the button follows.
	connect(editor, &Editor::scenePointRequestChanged, this, [this] {
		if (m_drawing && !m_editor->scenePointPending() && !m_finished) {
			QTimer::singleShot(0, this, [this] {
				if (m_drawing && !m_editor->scenePointPending()) stopDrawing();
			});
		}
	});
	connect(m_drawMode, &QRadioButton::toggled, this, &RoadBuilderDialog::modeChanged);
	connect(editor, &Editor::selectionChanged, this, later);
	connect(editor, &Editor::documentChanged, this, later);

	// Floor brushes selected: build along them; else start clicking.
	{
		QSignalBlocker block(m_drawMode);
		if (!m_editor->selectedRoadPaths().empty()) {
			m_selectionMode->setChecked(true);
		} else {
			m_drawMode->setChecked(true);
		}
	}
	modeChanged();
	refresh();
}

RoadBuilderDialog::~RoadBuilderDialog() {
	if (m_drawing) m_editor->cancelScenePoint();
	if (!m_finished) m_editor->clearBrushPreview();
}

RoadBuildOptions RoadBuilderDialog::options() const {
	RoadBuildOptions o;
	o.width = m_width->value();
	o.thickness = m_thickness->value();
	o.innerRadius = m_radius->value();
	o.segments = m_segments->value();
	o.material = m_material->text().trimmed().toStdString();
	o.sideMaterial = m_side->text().trimmed().toStdString();
	o.along = static_cast<RoadAlong>(m_along->currentData().toInt());
	o.flip = m_flip->isChecked();
	o.junctionMaterial = m_junction->text().trimmed().toStdString();
	o.displacement = m_displacement->isChecked();
	o.power = m_power->currentData().toInt();
	o.crown = m_crown->value();
	o.roughness = m_roughness->value();
	o.fit = m_fit->isChecked();
	o.clearance = m_clearance->value();
	o.fillMaterial = m_fill->text().trimmed().toStdString();
	o.kerbs = m_kerbs->isChecked();
	o.kerbWidth = m_kerbWidth->value();
	o.kerbHeight = m_kerbHeight->value();
	o.kerbMaterial = m_kerbMaterial->text().trimmed().toStdString();
	return o;
}

bool RoadBuilderDialog::fromSelection() const {
	return m_selectionMode->isChecked();
}

void RoadBuilderDialog::modeChanged() {
	const bool draw = m_drawMode->isChecked();
	m_clickPoints->setEnabled(draw);
	m_undoPoint->setEnabled(draw);
	m_clearPoints->setEnabled(draw);
	m_fill->setEnabled(!draw);
	if (!draw) {
		stopDrawing();
		// The selected blocks tell the road's width, thickness and material.
		RoadBuildOptions suggested;
		suggested.material = m_material->text().trimmed().toStdString();
		if (!m_editor->selectedRoadPaths(&suggested).empty()) {
			m_width->setValue(suggested.width);
			m_thickness->setValue(suggested.thickness);
			if (m_material->text().trimmed().isEmpty()) m_material->setText(qs(suggested.material));
		}
	} else if (m_points.size() < 2) {
		m_clickPoints->setChecked(true);
	}
	m_soon.start();
}

void RoadBuilderDialog::addPoint(const Vec3& point) {
	m_points.push_back(point);
	m_soon.start();
}

QString RoadBuilderDialog::note() const {
	return m_note->text();
}

void RoadBuilderDialog::requestPoint() {
	const QString hint = m_points.empty() ? tr("Road: click where it starts - Esc stops")
	                                      : tr("Road: click the next point - Enter builds, Backspace takes one back, Esc stops");
	m_editor->requestScenePoint(hint, [this](const Vec3& point) {
		m_points.push_back(point);
		m_soon.start();
		if (m_drawing) requestPoint();
	});
}

void RoadBuilderDialog::stopDrawing() {
	if (!m_drawing) return;
	m_drawing = false;
	m_editor->cancelScenePoint();
	QSignalBlocker block(m_clickPoints);
	m_clickPoints->setChecked(false);
}

void RoadBuilderDialog::undoPoint() {
	if (m_points.empty()) return;
	m_points.pop_back();
	m_soon.start();
	if (m_drawing) requestPoint();   // the hint follows
}

void RoadBuilderDialog::refresh() {
	if (m_finished) return;
	auto* ok = m_buttons->button(QDialogButtonBox::Ok);
	const bool selection = fromSelection();
	std::vector<RoadPath> paths;
	if (!selection) paths.push_back(RoadPath{m_points});
	const auto plan = m_editor->planRoadBuilder(options(), paths, selection);
	if (!plan.ok()) {
		m_editor->clearBrushPreview();
		if (selection) {
			m_note->setText(tr("Select the road's floor brushes (blocked in by hand): the road follows them and replaces them."));
		} else if (m_points.size() == 1) {
			m_note->setText(tr("1 point - click the next one where the road goes."));
		} else {
			m_note->setText(tr("Click points along the road's middle in the 3D or top view (Click points). Every corner becomes a curve."));
		}
		ok->setEnabled(false);
		return;
	}
	m_editor->setBrushPreview(plan.resultSolids(), plan.replacedRefs());
	QStringList lines;
	QString road = tr("%n road brush(es)", nullptr, int(plan.road.size()) - plan.seals);
	if (plan.seals > 0) road += ", " + tr("%n nodraw seal(s) below", nullptr, plan.seals);
	if (!plan.kerbs.empty()) road += ", " + tr("%n kerb(s)", nullptr, int(plan.kerbs.size()));
	if (!selection) road += " - " + tr("%n point(s)", nullptr, int(m_points.size()));
	lines << road + '.';
	if (plan.curves > 0) lines << tr("%n curve(s), inner radius from %1 units; the texture runs on through them.", nullptr, plan.curves).arg(plan.radius, 0, 'g', 4);
	if (plan.displacements > 0) lines << tr("%n displacement(s), sewn, with nodraw seals underneath.", nullptr, plan.displacements);
	if (plan.junctions > 0) lines << tr("Meets %n road piece(s) that are already there: it stops at their edge.", nullptr, plan.junctions);
	if (plan.carved > 0) lines << tr("Carves %n brush(es) around it.", nullptr, plan.carved);
	for (const auto& note : plan.notes) lines << qs(note);
	m_note->setText(lines.join('\n'));
	ok->setEnabled(true);
}

void RoadBuilderDialog::accept() {
	m_soon.stop();
	QSettings settings;
	settings.setValue("roadBuilder/width", m_width->value());
	settings.setValue("roadBuilder/thickness", m_thickness->value());
	settings.setValue("roadBuilder/radius", m_radius->value());
	settings.setValue("roadBuilder/segments", m_segments->value());
	settings.setValue("roadBuilder/material", m_material->text().trimmed());
	settings.setValue("roadBuilder/displacement", m_displacement->isChecked());
	settings.setValue("roadBuilder/power", m_power->currentData().toInt());
	settings.setValue("roadBuilder/crown", m_crown->value());
	settings.setValue("roadBuilder/roughness", m_roughness->value());
	settings.setValue("roadBuilder/kerbs", m_kerbs->isChecked());
	settings.setValue("roadBuilder/kerbWidth", m_kerbWidth->value());
	settings.setValue("roadBuilder/kerbHeight", m_kerbHeight->value());
	settings.setValue("roadBuilder/fit", m_fit->isChecked());
	settings.setValue("roadBuilder/along", m_along->currentIndex());
	settings.setValue("roadBuilder/sideMaterial", m_side->text().trimmed());
	settings.setValue("roadBuilder/kerbMaterial", m_kerbMaterial->text().trimmed());
	settings.setValue("roadBuilder/junctionMaterial", m_junction->text().trimmed());
	settings.setValue("roadBuilder/fillMaterial", m_fill->text().trimmed());
	settings.setValue("roadBuilder/clearance", m_clearance->value());
	const bool selection = fromSelection();
	std::vector<RoadPath> paths;
	if (!selection) paths.push_back(RoadPath{m_points});
	stopDrawing();
	m_finished = true;
	m_editor->applyRoadBuilder(options(), paths, selection);   // clears the preview
	QDialog::accept();
}

void RoadBuilderDialog::done(int result) {
	m_soon.stop();
	stopDrawing();
	if (!m_finished) {
		m_finished = true;
		m_editor->clearBrushPreview();
	}
	QDialog::done(result);
}

} // namespace sme::app
