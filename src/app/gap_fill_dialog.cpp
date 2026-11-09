// gap_fill_dialog.cpp - "Fill gap" with a live preview (see gap_fill_dialog.h).

#include "gap_fill_dialog.h"

#include "editor.h"
#include "ui_helpers.h"

#include <QButtonGroup>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QVBoxLayout>

namespace sme::app {

GapFillDialog::GapFillDialog(Editor* editor, QWidget* parent) : QDialog(parent), m_editor(editor) {
	setObjectName("gapFillDialog");
	setWindowTitle(tr("Fill gap"));
	setModal(false);
	auto* outer = new QVBoxLayout(this);
	outer->addWidget(noteLabel(tr("Select the brushes around the gap (two or more). The fill takes the empty space between them - "
	                              "never anything that is already there - and continues their textures.")));

	auto* kindRow = new QHBoxLayout;
	m_hull = new QRadioButton(tr("Follow the brushes"), this);
	m_hull->setObjectName("gapFillHull");
	m_hull->setToolTip(tr("Everything between the brushes: a wedge where two pieces meet at an angle, a slope between two heights"));
	m_box = new QRadioButton(tr("Square box"), this);
	m_box->setObjectName("gapFillBox");
	m_box->setToolTip(tr("Only the box between them: square, as high as the lower of the two"));
	auto* group = new QButtonGroup(this);
	group->addButton(m_hull);
	group->addButton(m_box);
	(QSettings().value("gapFill/box", false).toBool() ? m_box : m_hull)->setChecked(true);
	kindRow->addWidget(m_hull);
	kindRow->addWidget(m_box);
	kindRow->addStretch(1);
	outer->addLayout(kindRow);

	auto* form = new QFormLayout;
	m_material = new QLineEdit(this);
	m_material->setObjectName("gapFillMaterial");
	m_material->setPlaceholderText(tr("From the neighbours"));
	m_material->setToolTip(tr("For faces with no neighbour face in the same plane. Empty: what the selected brushes use most"));
	auto* materialRow = new QWidget(this);
	auto* h = new QHBoxLayout(materialRow);
	h->setContentsMargins(0, 0, 0, 0);
	h->addWidget(m_material, 1);
	auto* current = new QPushButton(tr("Use current"), materialRow);
	connect(current, &QPushButton::clicked, this, [this] { m_material->setText(m_editor->currentMaterial()); });
	h->addWidget(current);
	form->addRow(tr("Material"), materialRow);
	outer->addLayout(form);

	m_note = new QLabel(this);
	m_note->setObjectName("gapFillNote");
	m_note->setWordWrap(true);
	outer->addWidget(m_note);

	m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	m_buttons->button(QDialogButtonBox::Ok)->setText(tr("Fill"));
	m_buttons->button(QDialogButtonBox::Ok)->setObjectName("gapFillOk");
	connect(m_buttons, &QDialogButtonBox::accepted, this, &GapFillDialog::accept);
	connect(m_buttons, &QDialogButtonBox::rejected, this, &GapFillDialog::reject);
	outer->addWidget(m_buttons);

	m_soon.setSingleShot(true);
	m_soon.setInterval(120);
	connect(&m_soon, &QTimer::timeout, this, &GapFillDialog::refresh);
	const auto later = [this] { m_soon.start(); };
	connect(m_hull, &QRadioButton::toggled, this, later);
	connect(m_material, &QLineEdit::textChanged, this, later);
	connect(editor, &Editor::selectionChanged, this, later);
	connect(editor, &Editor::documentChanged, this, later);
	setMinimumWidth(420);
	refresh();
}

GapFillDialog::~GapFillDialog() {
	if (!m_finished) m_editor->clearBrushPreview();
}

GapFillOptions GapFillDialog::options() const {
	GapFillOptions options;
	options.kind = m_box->isChecked() ? GapFillOptions::Kind::Box : GapFillOptions::Kind::Hull;
	options.material = m_material->text().trimmed().toStdString();
	return options;
}

QString GapFillDialog::note() const {
	return m_note->text();
}

void GapFillDialog::refresh() {
	if (m_finished) return;
	const auto plan = m_editor->planSelectedGapFill(options());
	auto* ok = m_buttons->button(QDialogButtonBox::Ok);
	QStringList lines;
	if (plan.ok()) {
		lines << tr("Fills %n brush(es), %1 cubic units.", nullptr, int(plan.fill.size())).arg(qRound64(plan.volume()));
		m_editor->setBrushPreview(plan.fill, {});
	} else {
		m_editor->clearBrushPreview();
	}
	for (const auto& note : plan.notes) lines << QString::fromStdString(note);
	if (lines.isEmpty()) lines << tr("Nothing to fill.");
	m_note->setText(lines.join('\n'));
	ok->setEnabled(plan.ok());
}

void GapFillDialog::accept() {
	m_soon.stop();
	QSettings().setValue("gapFill/box", m_box->isChecked());
	m_finished = true;
	if (!m_editor->applySelectedGapFill(options())) {
		m_finished = false;
		refresh();
		return;
	}
	QDialog::accept();
}

void GapFillDialog::done(int result) {
	m_soon.stop();
	if (!m_finished) {
		m_finished = true;
		m_editor->clearBrushPreview();
	}
	QDialog::done(result);
}

} // namespace sme::app
