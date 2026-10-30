// round_edge_dialog.cpp - "Round or bevel edge" with a live preview (see round_edge_dialog.h).

#include "round_edge_dialog.h"

#include "editor.h"
#include "ui_helpers.h"

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>

namespace sme::app {

RoundEdgeDialog::RoundEdgeDialog(Editor* editor, QWidget* parent) : QDialog(parent), m_editor(editor) {
	setObjectName("roundEdgeDialog");
	setWindowTitle(tr("Round or bevel edge"));
	setModal(false);
	setProperty("smeNoFit", true);   // small enough for any screen; no scroll area (dialog_fit.h)
	auto* form = new QFormLayout(this);
	m_radius = new QDoubleSpinBox(this);
	m_radius->setObjectName("roundRadius");
	m_radius->setRange(1, 4096);
	m_radius->setDecimals(1);
	m_radius->setValue(QSettings().value("brushes/roundRadius", 16.0).toDouble());
	m_radius->setSuffix(tr(" units"));
	m_segments = new QSpinBox(this);
	m_segments->setObjectName("roundSegments");
	m_segments->setRange(1, 32);
	m_segments->setValue(QSettings().value("brushes/roundSegments", 4).toInt());
	form->addRow(tr("Radius"), m_radius);
	form->addRow(tr("Segments (1 = bevel)"), m_segments);
	m_note = noteLabel(QString());
	m_note->setObjectName("roundNote");
	// Room for up to four lines from the start: the dialog does not jump while values change.
	m_note->setMinimumWidth(360);
	m_note->setMinimumHeight(m_note->fontMetrics().lineSpacing() * 4 + 4);
	m_note->setAlignment(Qt::AlignLeft | Qt::AlignTop);
	form->addRow(m_note);
	m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	form->addRow(m_buttons);
	connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

	m_soon.setSingleShot(true);
	m_soon.setInterval(60);
	connect(&m_soon, &QTimer::timeout, this, &RoundEdgeDialog::refresh);
	const auto later = [this] { m_soon.start(); };
	connect(m_radius, &QDoubleSpinBox::valueChanged, this, later);
	connect(m_segments, &QSpinBox::valueChanged, this, later);
	// Another face picked or the map changed meanwhile: the preview follows.
	connect(editor, &Editor::selectionChanged, this, later);
	connect(editor, &Editor::documentChanged, this, later);
	refresh();
}

RoundEdgeDialog::~RoundEdgeDialog() {
	if (!m_finished) m_editor->clearBrushPreview();
}

QString RoundEdgeDialog::note() const {
	return m_note->text();
}

void RoundEdgeDialog::refresh() {
	if (m_finished) return;
	const auto plan = m_editor->planSelectedEdgeRound(m_radius->value(), m_segments->value());
	auto* ok = m_buttons->button(QDialogButtonBox::Ok);
	if (!plan.ok()) {
		m_editor->clearBrushPreview();
		m_note->setText(QString::fromStdString(plan.problem));
		ok->setEnabled(false);
		return;
	}
	m_editor->setBrushPreview(plan.resultSolids(), plan.replacedRefs());
	QStringList lines;
	if (plan.clamped) lines << tr("Limited to %1 units - the largest radius that fits here.").arg(plan.radius);
	if (plan.segments < m_segments->value()) lines << tr("%n segment(s) - more would get shorter than 2 units.", nullptr, plan.segments);
	if (plan.inner) lines << tr("Inner corner: filled up to the curve with %n brush(es).", nullptr, int(plan.added.size()));
	if (plan.keptSquare > 0) lines << tr("Stays square where the brushes around cover it (no hole next to them).");
	if (plan.replacements.size() > 1) lines << tr("Rounded across both brushes.");
	m_note->setText(lines.isEmpty() ? tr("The views show the result.") : lines.join('\n'));
	ok->setEnabled(true);
}

void RoundEdgeDialog::accept() {
	m_soon.stop();
	QSettings().setValue("brushes/roundRadius", m_radius->value());
	QSettings().setValue("brushes/roundSegments", m_segments->value());
	m_finished = true;
	m_editor->roundSelectedEdge(m_radius->value(), m_segments->value());   // clears the preview
	QDialog::accept();
}

void RoundEdgeDialog::done(int result) {
	m_soon.stop();
	if (!m_finished) {
		m_finished = true;
		m_editor->clearBrushPreview();
	}
	QDialog::done(result);
}

} // namespace sme::app
