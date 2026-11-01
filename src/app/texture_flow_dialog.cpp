// texture_flow_dialog.cpp - "Texture flow" with a live preview (see texture_flow_dialog.h).

#include "texture_flow_dialog.h"

#include "editor.h"
#include "ui_helpers.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>

namespace sme::app {

namespace {

QString modeName(FlowMode mode) {
	switch (mode) {
		case FlowMode::Wrap: return QObject::tr("Wrap");
		case FlowMode::Path: return QObject::tr("Along a path");
		case FlowMode::Curve: return QObject::tr("Around a curve");
		case FlowMode::Match: return QObject::tr("Match neighbour");
		case FlowMode::Auto: break;
	}
	return QObject::tr("Auto");
}

QString presetName(FlowPreset preset) {
	switch (preset) {
		case FlowPreset::Road: return QObject::tr("Road");
		case FlowPreset::Kerb: return QObject::tr("Kerb");
		case FlowPreset::River: return QObject::tr("River");
		case FlowPreset::Trim: return QObject::tr("Trim");
		case FlowPreset::Fence: return QObject::tr("Fence / rail");
	}
	return {};
}

} // namespace

TextureFlowDialog::TextureFlowDialog(Editor* editor, const QString& material, QWidget* parent) : QDialog(parent), m_editor(editor) {
	setObjectName("textureFlowDialog");
	setWindowTitle(tr("Texture flow"));
	setModal(false);
	setProperty("smeNoFit", true);   // small enough for any screen (dialog_fit.h)
	auto* form = new QFormLayout(this);

	m_mode = new QComboBox(this);
	m_mode->setObjectName("flowMode");
	m_mode->addItem(tr("Auto (from the faces)"), int(FlowMode::Auto));
	m_mode->addItem(modeName(FlowMode::Wrap), int(FlowMode::Wrap));
	m_mode->addItem(modeName(FlowMode::Path), int(FlowMode::Path));
	m_mode->addItem(modeName(FlowMode::Curve), int(FlowMode::Curve));
	m_mode->addItem(modeName(FlowMode::Match), int(FlowMode::Match));
	m_mode->setItemData(1, tr("From the first face picked, folded over every common edge like a sheet of paper: pillars, boxes, corners, stairs"), Qt::ToolTipRole);
	m_mode->setItemData(2, tr("Along strips: roads, kerbs, rivers on the floor; trims and fences standing - one scale, on through corners"), Qt::ToolTipRole);
	m_mode->setItemData(3, tr("Round a rounded edge, arch or cylinder: along its axis and round it, without a jump"), Qt::ToolTipRole);
	m_mode->setItemData(4, tr("The selected faces continue the texture of the faces next to them"), Qt::ToolTipRole);
	form->addRow(tr("Mode"), m_mode);

	m_preset = new QComboBox(this);
	m_preset->setObjectName("flowPreset");
	for (const auto preset : {FlowPreset::Road, FlowPreset::Kerb, FlowPreset::River, FlowPreset::Trim, FlowPreset::Fence}) m_preset->addItem(presetName(preset), int(preset));
	m_preset->setToolTip(tr("Along a path: floors (Road, Kerb, River) get the texture across their width, standing strips (Trim, Fence) across their height"));
	form->addRow(tr("Strip kind"), m_preset);

	m_material = new QLineEdit(this);
	m_material->setObjectName("flowMaterial");
	m_material->setPlaceholderText(tr("the first face's material"));
	m_material->setText(material);
	auto* materialRow = new QWidget(this);
	auto* h = new QHBoxLayout(materialRow);
	h->setContentsMargins(0, 0, 0, 0);
	h->addWidget(m_material, 1);
	auto* current = new QPushButton(tr("Use current"), materialRow);
	current->setToolTip(tr("The material picked in the browser"));
	connect(current, &QPushButton::clicked, this, [this] { m_material->setText(m_editor->currentMaterial()); });
	h->addWidget(current);
	form->addRow(tr("Material"), materialRow);

	m_repeats = new QSpinBox(this);
	m_repeats->setObjectName("flowRepeats");
	m_repeats->setRange(1, 16);
	m_repeats->setValue(QSettings().value("flow/repeats", 1).toInt());
	m_repeats->setToolTip(tr("How often the texture fits across a strip (its width on the floor, its height standing)"));
	form->addRow(tr("Fit across (repeats)"), m_repeats);

	m_flip = new QCheckBox(tr("Flip direction"), this);
	m_flip->setObjectName("flowFlip");
	form->addRow(QString(), m_flip);
	m_curve = new QCheckBox(tr("Curve corners"), this);
	m_curve->setObjectName("flowCurveCorners");
	m_curve->setToolTip(tr("Floors: cut a square corner between two strips into a few pieces, so the texture runs on round it"));
	m_curve->setChecked(QSettings().value("road/curveCorners", true).toBool());
	form->addRow(QString(), m_curve);

	m_note = noteLabel(QString());
	m_note->setObjectName("flowNote");
	m_note->setMinimumWidth(380);
	m_note->setMinimumHeight(m_note->fontMetrics().lineSpacing() * 4 + 4);
	m_note->setAlignment(Qt::AlignLeft | Qt::AlignTop);
	form->addRow(m_note);

	m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	auto* match = m_buttons->addButton(tr("Match neighbour"), QDialogButtonBox::ActionRole);
	match->setObjectName("flowMatch");
	match->setToolTip(tr("Continue the texture of the faces next to the selection"));
	connect(match, &QPushButton::clicked, this, [this] { m_mode->setCurrentIndex(m_mode->findData(int(FlowMode::Match))); });
	form->addRow(m_buttons);
	connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

	m_soon.setSingleShot(true);
	m_soon.setInterval(60);
	connect(&m_soon, &QTimer::timeout, this, &TextureFlowDialog::refresh);
	const auto later = [this] { m_soon.start(); };
	connect(m_mode, &QComboBox::currentIndexChanged, this, later);
	connect(m_preset, &QComboBox::currentIndexChanged, this, later);
	connect(m_material, &QLineEdit::textChanged, this, later);
	connect(m_flip, &QCheckBox::toggled, this, later);
	connect(m_curve, &QCheckBox::toggled, this, later);
	connect(m_repeats, &QSpinBox::valueChanged, this, later);
	connect(editor, &Editor::selectionChanged, this, later);
	connect(editor, &Editor::documentChanged, this, later);
	refresh();
}

TextureFlowDialog::~TextureFlowDialog() {
	if (!m_finished) m_editor->clearBrushPreview();
}

FlowOptions TextureFlowDialog::options() const {
	FlowOptions options;
	options.mode = static_cast<FlowMode>(m_mode->currentData().toInt());
	options.preset = static_cast<FlowPreset>(m_preset->currentData().toInt());
	options.material = m_material->text().trimmed().toStdString();
	options.flip = m_flip->isChecked();
	options.curveCorners = m_curve->isChecked();
	options.repeatsAcross = m_repeats->value();
	return options;
}

QString TextureFlowDialog::note() const {
	return m_note->text();
}

void TextureFlowDialog::updateFields(const FlowPlan* plan) {
	const auto chosen = static_cast<FlowMode>(m_mode->currentData().toInt());
	const FlowMode mode = plan && !plan->empty() ? plan->mode : chosen;
	const bool path = mode == FlowMode::Path;
	const bool floors = path && (plan ? plan->preset : static_cast<FlowPreset>(m_preset->currentData().toInt())) <= FlowPreset::River;
	m_preset->setEnabled(chosen == FlowMode::Path);
	m_repeats->setEnabled(path);
	m_flip->setEnabled(path);
	m_curve->setEnabled(floors);
	if (plan && chosen == FlowMode::Auto && path) {
		const QSignalBlocker block(m_preset);
		m_preset->setCurrentIndex(m_preset->findData(int(plan->preset)));
	}
}

void TextureFlowDialog::refresh() {
	if (m_finished) return;
	const auto plan = m_editor->planSelectedTextureFlow(options());
	auto* ok = m_buttons->button(QDialogButtonBox::Ok);
	updateFields(&plan);
	if (plan.empty()) {
		m_editor->clearBrushPreview();
		m_note->setText(plan.mode == FlowMode::Match ? tr("Nothing to match: no face next to the selection that is not selected itself.")
		                                             : tr("Select faces that touch each other (Faces mode: the first one picked is where the texture starts), or brushes."));
		ok->setEnabled(false);
		return;
	}
	std::vector<SolidRef> replaced;
	m_editor->setBrushPreview(roadPreview(m_editor->doc(), plan.plan, &replaced), replaced);
	QStringList lines;
	QString what = modeName(plan.mode);
	if (plan.mode == FlowMode::Path) what += " - " + presetName(plan.preset);
	if (static_cast<FlowMode>(m_mode->currentData().toInt()) == FlowMode::Auto) what = tr("Auto: %1").arg(what);
	lines << tr("%1: %n face(s) with %2.", nullptr, plan.faceCount()).arg(what, qs(plan.plan.material));
	if (plan.mode == FlowMode::Path && plan.stripSize > 0) lines << tr("The texture's height spans the strip's %1 units.").arg(plan.stripSize, 0, 'g', 4);
	if (plan.unevenStrips) lines << tr("Strips of different heights share one scale (the first strip's).");
	if (plan.seams > 0) lines << tr("%n edge(s) where the texture meets itself again after going round (a seam).", nullptr, plan.seams);
	if (plan.unreached > 0) lines << tr("%n face(s) not connected to the others - left as they are.", nullptr, plan.unreached);
	if (plan.stretched) lines << tr("The first face's texture lies at a slant to some faces - it is stretched there. Align that face first (Face align).");
	if (!plan.plan.corners.empty()) lines << tr("%n corner(s) cut into curve pieces.", nullptr, int(plan.plan.corners.size()));
	m_note->setText(lines.join('\n'));
	ok->setEnabled(true);
}

void TextureFlowDialog::accept() {
	m_soon.stop();
	QSettings().setValue("flow/repeats", m_repeats->value());
	m_finished = true;
	m_editor->applySelectedTextureFlow(options());
	QDialog::accept();
}

void TextureFlowDialog::done(int result) {
	m_soon.stop();
	if (!m_finished) {
		m_finished = true;
		m_editor->clearBrushPreview();
	}
	QDialog::done(result);
}

} // namespace sme::app
