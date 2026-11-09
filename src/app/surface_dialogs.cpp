// surface_dialogs.cpp - Blend along selection and Overlay strip dialogs (see surface_dialogs.h).

#include "surface_dialogs.h"

#include "editor.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

namespace sme::app {

namespace {

QDoubleSpinBox* unitsBox(const QString& name, double min, double max, double value, const QString& tip) {
	auto* box = new QDoubleSpinBox;
	box->setObjectName(name);
	box->setRange(min, max);
	box->setDecimals(0);
	box->setSuffix(QObject::tr(" units"));
	box->setValue(value);
	box->setToolTip(tip);
	return box;
}

/// Undo the dialog's own last step if it is still the newest one.
void takeBack(Editor* editor, const char* name) {
	if (editor->history().undoName() == name) editor->undo();
}

} // namespace

BlendAlongDialog::BlendAlongDialog(Editor* editor, QWidget* parent) : QDialog(parent), m_editor(editor) {
	setObjectName("blendAlongDialog");
	setWindowTitle(tr("Blend along selection"));
	setAttribute(Qt::WA_DeleteOnClose);
	QSettings settings;
	auto* layout = new QVBoxLayout(this);
	auto* intro = new QLabel(tr("Paints the second material of blend displacements (WorldVertexTransition) next to the selected road or path: "
	                            "full blend close to it, fading out further away. Hand-painted blend stays."));
	intro->setWordWrap(true);
	layout->addWidget(intro);
	auto* form = new QFormLayout;
	m_width = unitsBox("blendWidth", 0, 4096, settings.value("blend/width", 32.0).toDouble(), tr("Full blend up to this far from the selection"));
	m_softness = unitsBox("blendSoftness", 0, 4096, settings.value("blend/softness", 64.0).toDouble(), tr("Then it fades out over this distance"));
	m_noise = unitsBox("blendNoise", 0, 512, settings.value("blend/noise", 16.0).toDouble(), tr("Frays the border so it does not look drawn with a ruler"));
	m_near = new QComboBox;
	m_near->setObjectName("blendNear");
	m_near->addItem(tr("Second material (e.g. dirt next to the road)"), true);
	m_near->addItem(tr("First material"), false);
	m_near->setCurrentIndex(settings.value("blend/second", true).toBool() ? 0 : 1);
	form->addRow(tr("Width"), m_width);
	form->addRow(tr("Softness"), m_softness);
	form->addRow(tr("Noise"), m_noise);
	form->addRow(tr("Near the selection"), m_near);
	layout->addLayout(form);
	m_note = new QLabel;
	m_note->setObjectName("blendNote");
	m_note->setWordWrap(true);
	layout->addWidget(m_note);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);
	m_soon.setSingleShot(true);
	m_soon.setInterval(150);
	connect(&m_soon, &QTimer::timeout, this, &BlendAlongDialog::apply);
	for (auto* box : {m_width, m_softness, m_noise}) connect(box, &QDoubleSpinBox::valueChanged, this, [this] { m_soon.start(); });
	connect(m_near, &QComboBox::currentIndexChanged, this, [this] { m_soon.start(); });
	apply();
}

QString BlendAlongDialog::note() const {
	return m_note->text();
}

void BlendAlongDialog::apply() {
	BlendNearOptions options;
	options.width = m_width->value();
	options.softness = m_softness->value();
	options.noise = m_noise->value();
	options.seed = 7;
	options.secondMaterialNear = m_near->currentData().toBool();
	m_applied = m_editor->blendAlongSelection(options, m_applied);
	m_note->setText(m_applied ? tr("Painted - change the values to adjust, OK keeps it.")
	                          : tr("Nothing to paint: select the road or path brushes (not the displacements); displacements near them get the blend."));
}

void BlendAlongDialog::accept() {
	m_soon.stop();
	QSettings settings;
	settings.setValue("blend/width", m_width->value());
	settings.setValue("blend/softness", m_softness->value());
	settings.setValue("blend/noise", m_noise->value());
	settings.setValue("blend/second", m_near->currentData().toBool());
	QDialog::accept();
}

void BlendAlongDialog::reject() {
	m_soon.stop();
	if (m_applied) takeBack(m_editor, "Blend along selection");
	QDialog::reject();
}

OverlayStripDialog::OverlayStripDialog(Editor* editor, QWidget* parent) : QDialog(parent), m_editor(editor) {
	setObjectName("overlayStripDialog");
	setWindowTitle(tr("Overlay strip"));
	setAttribute(Qt::WA_DeleteOnClose);
	QSettings settings;
	auto* layout = new QVBoxLayout(this);
	auto* intro = new QLabel(tr("Lays the current material as a band of overlays along the selected faces (road pieces, a kerb, a path): "
	                            "round curves without gaps, the texture running on. Use it for lines, cracks or dirt along an edge."));
	intro->setWordWrap(true);
	layout->addWidget(intro);
	auto* form = new QFormLayout;
	m_align = new QComboBox;
	m_align->setObjectName("stripAlign");
	m_align->addItem(tr("Middle"), int(OverlayStripOptions::Align::Middle));
	m_align->addItem(tr("Left edge"), int(OverlayStripOptions::Align::Left));
	m_align->addItem(tr("Right edge"), int(OverlayStripOptions::Align::Right));
	m_align->setCurrentIndex(std::clamp(settings.value("strip/align", 0).toInt(), 0, 2));
	m_width = unitsBox("stripWidth", 1, 2048, settings.value("strip/width", 32.0).toDouble(), tr("How wide the band is"));
	m_offset = unitsBox("stripOffset", -2048, 2048, settings.value("strip/offset", 0.0).toDouble(),
	                    tr("Middle: moves the band sideways (+ = left). Edges: moves it in from the edge."));
	m_reverse = new QCheckBox(tr("Run the other way (left and right swap, the texture turns round)"));
	m_reverse->setObjectName("stripReverse");
	form->addRow(tr("Position"), m_align);
	form->addRow(tr("Width"), m_width);
	form->addRow(tr("Offset"), m_offset);
	form->addRow(QString(), m_reverse);
	layout->addLayout(form);
	m_note = new QLabel;
	m_note->setObjectName("stripNote");
	m_note->setWordWrap(true);
	layout->addWidget(m_note);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);
	m_soon.setSingleShot(true);
	m_soon.setInterval(150);
	connect(&m_soon, &QTimer::timeout, this, &OverlayStripDialog::apply);
	for (auto* box : {m_width, m_offset}) connect(box, &QDoubleSpinBox::valueChanged, this, [this] { m_soon.start(); });
	connect(m_align, &QComboBox::currentIndexChanged, this, [this] { m_soon.start(); });
	connect(m_reverse, &QCheckBox::toggled, this, [this] { m_soon.start(); });
	apply();
}

QString OverlayStripDialog::note() const {
	return m_note->text();
}

void OverlayStripDialog::apply() {
	OverlayStripOptions options;
	options.align = OverlayStripOptions::Align(m_align->currentData().toInt());
	options.width = m_width->value();
	options.offset = m_offset->value();
	options.reverse = m_reverse->isChecked();
	m_applied = m_editor->overlayStripOnSelectedFaces(options, m_applied);
	m_note->setText(m_applied ? tr("%n overlay(s) with %1 - change the values to adjust, OK keeps them.", nullptr, m_editor->lastStripCount())
	                                .arg(m_editor->currentMaterial())
	                          : tr("Nothing made: select the faces the strip runs along (face mode, Ctrl+click), pick the material first."));
}

void OverlayStripDialog::accept() {
	m_soon.stop();
	QSettings settings;
	settings.setValue("strip/align", m_align->currentIndex());
	settings.setValue("strip/width", m_width->value());
	settings.setValue("strip/offset", m_offset->value());
	QDialog::accept();
}

void OverlayStripDialog::reject() {
	m_soon.stop();
	if (m_applied) takeBack(m_editor, "Overlay strip");
	QDialog::reject();
}

} // namespace sme::app
