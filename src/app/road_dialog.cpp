// road_dialog.cpp - "Road" with a live preview (see road_dialog.h).

#include "road_dialog.h"

#include "editor.h"
#include "ui_helpers.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>

namespace sme::app {

namespace {

/// The material the editor starts with (nothing picked yet).
const QString UNTOUCHED_MATERIAL = "DEV/DEV_MEASUREGENERIC01";

} // namespace

RoadDialog::RoadDialog(Editor* editor, const QString& material, QWidget* parent) : QDialog(parent), m_editor(editor) {
	setObjectName("roadDialog");
	setWindowTitle(tr("Road"));
	setModal(false);
	setProperty("smeNoFit", true);   // small enough for any screen; no scroll area (dialog_fit.h)
	auto* form = new QFormLayout(this);

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
	m_material->setObjectName("roadMaterial");
	m_material->setPlaceholderText(tr("the material the road has now"));
	QString start = material;
	if (start.isEmpty() && m_editor->currentMaterial().compare(UNTOUCHED_MATERIAL, Qt::CaseInsensitive) != 0) start = m_editor->currentMaterial();
	m_material->setText(start);
	form->addRow(tr("Road material"), materialRow(m_material, tr("The material picked in the browser")));

	m_along = new QComboBox(this);
	m_along->setObjectName("roadAlong");
	m_along->addItem(tr("Auto (the texture's longer side)"), int(RoadAlong::Auto));
	m_along->addItem(tr("U (across the picture)"), int(RoadAlong::U));
	m_along->addItem(tr("V (down the picture)"), int(RoadAlong::V));
	m_along->setToolTip(tr("Which way of the texture runs along the road. Auto: the longer side, V for square textures."));
	m_along->setCurrentIndex(std::clamp(QSettings().value("road/along", 0).toInt(), 0, 2));
	form->addRow(tr("Texture runs along"), m_along);

	m_flip = new QCheckBox(tr("Flip direction"), this);
	m_flip->setObjectName("roadFlip");
	m_flip->setToolTip(tr("Turn the texture around: it runs from the other end of the road"));
	form->addRow(QString(), m_flip);

	m_repeats = new QSpinBox(this);
	m_repeats->setObjectName("roadRepeats");
	m_repeats->setRange(1, 16);
	m_repeats->setValue(QSettings().value("road/repeats", 1).toInt());
	m_repeats->setToolTip(tr("How often the texture fits across the road's width (1: once, kerb to kerb)"));
	form->addRow(tr("Fit width (repeats across)"), m_repeats);

	m_width = new QDoubleSpinBox(this);
	m_width->setObjectName("roadWidth");
	m_width->setRange(0, 4096);
	m_width->setDecimals(0);
	m_width->setSingleStep(8);
	m_width->setSuffix(tr(" units"));
	m_width->setSpecialValueText(tr("Auto"));
	m_width->setValue(0);
	m_width->setToolTip(tr("The road's width the texture spans, kerb to kerb. Auto: the typical width of the road's pieces - a wider piece shows more of the texture's edge"));
	form->addRow(tr("Road width"), m_width);

	m_side = new QLineEdit(this);
	m_side->setObjectName("roadSideMaterial");
	m_side->setPlaceholderText(tr("leave kerbs and sides as they are"));
	m_side->setText(QSettings().value("road/sideMaterial").toString());
	form->addRow(tr("Kerb material"), materialRow(m_side, tr("The material picked in the browser, for the kerb tops beside the road and the standing sides")));

	m_note = noteLabel(QString());
	m_note->setObjectName("roadNote");
	m_note->setMinimumWidth(380);
	m_note->setMinimumHeight(m_note->fontMetrics().lineSpacing() * 4 + 4);
	m_note->setAlignment(Qt::AlignLeft | Qt::AlignTop);
	form->addRow(m_note);

	m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	form->addRow(m_buttons);
	connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

	m_soon.setSingleShot(true);
	m_soon.setInterval(60);
	connect(&m_soon, &QTimer::timeout, this, &RoadDialog::refresh);
	const auto later = [this] { m_soon.start(); };
	connect(m_material, &QLineEdit::textChanged, this, later);
	connect(m_side, &QLineEdit::textChanged, this, later);
	connect(m_along, &QComboBox::currentIndexChanged, this, later);
	connect(m_flip, &QCheckBox::toggled, this, later);
	connect(m_repeats, &QSpinBox::valueChanged, this, later);
	connect(m_width, &QDoubleSpinBox::valueChanged, this, later);
	// Another brush picked or the map changed meanwhile: the preview follows.
	connect(editor, &Editor::selectionChanged, this, later);
	connect(editor, &Editor::documentChanged, this, later);
	refresh();
}

RoadDialog::~RoadDialog() {
	if (!m_finished) m_editor->clearBrushPreview();
}

RoadOptions RoadDialog::options() const {
	RoadOptions options;
	options.material = m_material->text().trimmed().toStdString();
	options.along = static_cast<RoadAlong>(m_along->currentData().toInt());
	options.flip = m_flip->isChecked();
	options.repeatsAcross = m_repeats->value();
	options.sideMaterial = m_side->text().trimmed().toStdString();
	options.roadWidth = m_width->value();
	return options;
}

QString RoadDialog::note() const {
	return m_note->text();
}

void RoadDialog::refresh() {
	if (m_finished) return;
	const auto plan = m_editor->planSelectedRoad(options());
	auto* ok = m_buttons->button(QDialogButtonBox::Ok);
	if (plan.empty()) {
		m_editor->clearBrushPreview();
		m_note->setText(tr("Select the road's brushes (or their top faces): the faces facing up get the road."));
		ok->setEnabled(false);
		return;
	}
	std::vector<SolidRef> replaced;
	m_editor->setBrushPreview(roadPreview(m_editor->doc(), plan, &replaced), replaced);
	QStringList lines;
	lines << tr("%n face(s) with %1,", nullptr, int(plan.faces.size())).arg(qs(plan.material));
	lines.last() += ' ' + tr("in %n road(s) - pieces that touch run on into each other.", nullptr, plan.chains);
	QStringList widths;
	for (const double w : plan.widths) {
		if (!widths.contains(QString::number(w, 'g', 4))) widths << QString::number(w, 'g', 4);
	}
	lines << (plan.alongV ? tr("The texture's V (down the picture) runs along the road, %1 units wide.") : tr("The texture's U (across the picture) runs along the road, %1 units wide."))
	             .arg(widths.join(" / "));
	QStringList left;
	if (plan.kerbs > 0) left << tr("%n kerb or side face(s)", nullptr, plan.kerbs);
	if (plan.ignored > 0) left << tr("%n wall or bottom face(s)", nullptr, plan.ignored);
	if (!left.isEmpty()) lines << tr("Left out: %1.").arg(left.join(", "));
	if (plan.edgePieces > 0) lines << tr("%n small piece(s) at the road's edge continue the road next to them.", nullptr, plan.edgePieces);
	if (!plan.sideFaces.empty()) lines << tr("%n kerb face(s) get %1.", nullptr, int(plan.sideFaces.size())).arg(qs(plan.sideMaterial));
	m_note->setText(lines.join('\n'));
	ok->setEnabled(true);
}

void RoadDialog::accept() {
	m_soon.stop();
	QSettings().setValue("road/along", m_along->currentIndex());
	QSettings().setValue("road/repeats", m_repeats->value());
	QSettings().setValue("road/sideMaterial", m_side->text().trimmed());
	m_finished = true;
	m_editor->applySelectedRoad(options());   // clears the preview
	QDialog::accept();
}

void RoadDialog::done(int result) {
	m_soon.stop();
	if (!m_finished) {
		m_finished = true;
		m_editor->clearBrushPreview();
	}
	QDialog::done(result);
}

} // namespace sme::app
