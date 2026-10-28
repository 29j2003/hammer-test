// compile_window.cpp - Compile progress dock (see compile_window.h).

#include "compile_window.h"

#include "compile.h"
#include "mod_code_panel.h"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace sme::app {

namespace {

constexpr int STATE_ROLE = Qt::UserRole;

QString formatTime(qint64 ms) {
	const qint64 seconds = ms / 1000;
	return seconds >= 60 ? QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QChar('0')) : QStringLiteral("%1.%2 s").arg(seconds).arg((ms % 1000) / 100);
}

} // namespace

CompileWindow::CompileWindow(CompileRunner* runner, QWidget* parent) : QWidget(parent), m_runner(runner) {
	auto* v = new QVBoxLayout(this);
	v->setContentsMargins(6, 6, 6, 6);

	// Header: map, total time, overall progress, buttons.
	auto* header = new QHBoxLayout;
	m_title = new QLabel(tr("Nothing compiled yet - F9 compiles and runs the map"));
	QFont bold = m_title->font();
	bold.setBold(true);
	m_title->setFont(bold);
	m_time = new QLabel;
	m_total = new QProgressBar;
	m_total->setRange(0, 100);
	m_total->setTextVisible(true);
	m_total->setMaximumWidth(260);
	m_cancel = new QPushButton(tr("Cancel"));
	m_cancel->setEnabled(false);
	m_again = new QPushButton(tr("Compile again (F9)"));
	m_leakButton = new QPushButton(tr("Show leak"));
	m_leakButton->setVisible(false);
	m_reportButton = new QPushButton(tr("Make report"));
	m_reportButton->setObjectName("compileMakeReport");
	m_reportButton->setToolTip(reportHowTo());
	m_reportButton->setVisible(false);
	header->addWidget(m_title, 1);
	header->addWidget(m_time);
	header->addWidget(m_total);
	header->addWidget(m_leakButton);
	header->addWidget(m_reportButton);
	header->addWidget(m_cancel);
	header->addWidget(m_again);
	v->addLayout(header);

	auto* split = new QSplitter(Qt::Horizontal);
	// Steps with state, time and current phase.
	m_steps = new QTreeWidget;
	m_steps->setRootIsDecorated(false);
	m_steps->setColumnCount(4);
	m_steps->setHeaderLabels({tr("Step"), tr("State"), tr("Time"), tr("Progress")});
	m_steps->header()->setStretchLastSection(true);
	m_steps->header()->resizeSection(0, 70);
	m_steps->header()->resizeSection(1, 120);
	m_steps->header()->resizeSection(2, 60);
	m_steps->setMinimumWidth(320);
	split->addWidget(m_steps);
	// Warnings and errors with explanations (explanation as a child line).
	auto* right = new QWidget;
	auto* rv = new QVBoxLayout(right);
	rv->setContentsMargins(0, 0, 0, 0);
	m_problems = new QTreeWidget;
	m_problems->setHeaderLabels({tr("Warnings and errors")});
	m_problems->setWordWrap(true);
	rv->addWidget(m_problems, 1);
	m_summary = new QLabel;
	m_summary->setWordWrap(true);
	m_summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
	rv->addWidget(m_summary);
	split->addWidget(right);
	split->setStretchFactor(1, 1);

	auto* vertical = new QSplitter(Qt::Vertical);
	vertical->addWidget(split);
	m_log = new QPlainTextEdit;
	m_log->setReadOnly(true);
	m_log->setMaximumBlockCount(20000);
	m_log->setLineWrapMode(QPlainTextEdit::NoWrap);
	m_log->setPlaceholderText(tr("Full output of the compile tools"));
	vertical->addWidget(m_log);
	vertical->setStretchFactor(0, 2);
	vertical->setStretchFactor(1, 1);
	v->addWidget(vertical, 1);

	m_tick.setInterval(200);
	connect(&m_tick, &QTimer::timeout, this, &CompileWindow::updateTimes);
	connect(m_cancel, &QPushButton::clicked, runner, &CompileRunner::cancel);
	connect(m_again, &QPushButton::clicked, this, &CompileWindow::compileAgain);
	connect(m_leakButton, &QPushButton::clicked, this, &CompileWindow::showLeak);
	connect(m_reportButton, &QPushButton::clicked, this, &CompileWindow::reportRequested);

	connect(runner, &CompileRunner::started, this, [this](const QString& vmfPath, const QStringList& steps) {
		m_mapName = QFileInfo(vmfPath).fileName();
		m_title->setText(tr("Compiling %1").arg(m_mapName));
		m_steps->clear();
		m_problems->clear();
		m_log->clear();
		m_warnings = m_errors = 0;
		m_leaked = false;
		m_running = -1;
		m_leakButton->setVisible(false);
		for (const auto& step : steps) {
			auto* item = new QTreeWidgetItem(m_steps, {step, tr("waiting"), QString(), QString()});
			item->setData(0, STATE_ROLE, int(StepState::Waiting));
		}
		m_total->setValue(0);
		m_summary->setText(tr("Running ..."));
		m_cancel->setEnabled(true);
		m_again->setEnabled(false);
		m_clock.start();
		m_tick.start();
	});
	connect(runner, &CompileRunner::stepStarted, this, [this](int index, const QString& command) {
		m_running = index;
		m_stepClock.start();
		setState(index, StepState::Running);
		if (auto* item = m_steps->topLevelItem(index)) {
			item->setToolTip(0, command);
		}
	});
	connect(runner, &CompileRunner::stepProgress, this, [this](int index, const QString& phase, int percent) {
		if (auto* item = m_steps->topLevelItem(index)) {
			item->setText(3, phase.isEmpty() ? QStringLiteral("%1 %").arg(percent) : QStringLiteral("%1 %2 %").arg(phase).arg(percent));
		}
		const int count = std::max(1, m_steps->topLevelItemCount());
		m_total->setValue(std::min(100, (index * 100 + percent) / count));
	});
	connect(runner, &CompileRunner::stepFinished, this, [this](int index, bool ok, qint64 ms) {
		setState(index, ok ? StepState::Done : StepState::Failed);
		if (auto* item = m_steps->topLevelItem(index)) {
			item->setText(2, formatTime(ms));
			if (ok) item->setText(3, QStringLiteral("100 %"));
		}
		m_running = -1;
		m_total->setValue(std::min(100, (index + 1) * 100 / std::max(1, m_steps->topLevelItemCount())));
	});
	connect(runner, &CompileRunner::stepSkipped, this, [this](int index, const QString& reason) { setState(index, StepState::Skipped, reason); });
	connect(runner, &CompileRunner::problem, this, [this](int index, const QString& line, int severity, const QString& explanation) {
		if (severity >= 2) ++m_errors;
		else ++m_warnings;
		if (line.contains("leak", Qt::CaseInsensitive)) {
			m_leaked = true;
			m_leakButton->setVisible(true);
		}
		const QString step = index >= 0 && m_steps->topLevelItem(index) ? m_steps->topLevelItem(index)->text(0) : QString();
		auto* item = new QTreeWidgetItem(m_problems, {QStringLiteral("%1 %2: %3").arg(severity >= 2 ? QStringLiteral("✖") : QStringLiteral("⚠"), step, line)});
		item->setForeground(0, severity >= 2 ? QColor(255, 120, 120) : QColor(255, 200, 100));
		item->setToolTip(0, line);
		if (!explanation.isEmpty()) {
			auto* child = new QTreeWidgetItem(item, {explanation});
			child->setToolTip(0, explanation);
			item->setExpanded(m_problems->topLevelItemCount() <= 20);
		}
	});
	connect(runner, &CompileRunner::output, this, [this](const QString& text, const QColor& color) {
		if (color.isValid()) {
			m_log->appendHtml(QStringLiteral("<span style='color:%1'>%2</span>").arg(color.name(), text.toHtmlEscaped()));
		} else {
			m_log->appendPlainText(text);
		}
	});
	connect(runner, &CompileRunner::finished, this, &CompileWindow::finish);
}

int CompileWindow::stepCount() const {
	return m_steps->topLevelItemCount();
}

CompileWindow::StepState CompileWindow::stepState(int index) const {
	const auto* item = m_steps->topLevelItem(index);
	return item ? static_cast<StepState>(item->data(0, STATE_ROLE).toInt()) : StepState::Waiting;
}

QString CompileWindow::summary() const {
	return m_summary->text();
}

void CompileWindow::setState(int index, StepState state, const QString& detail) {
	auto* item = m_steps->topLevelItem(index);
	if (!item) {
		return;
	}
	item->setData(0, STATE_ROLE, int(state));
	switch (state) {
		case StepState::Waiting: item->setText(1, tr("waiting")); break;
		case StepState::Running:
			item->setText(1, tr("▶ running"));
			item->setForeground(1, QColor(140, 180, 255));
			break;
		case StepState::Done:
			item->setText(1, tr("✔ done"));
			item->setForeground(1, QColor(120, 220, 120));
			break;
		case StepState::Failed:
			item->setText(1, tr("✖ failed"));
			item->setForeground(1, QColor(255, 120, 120));
			break;
		case StepState::Skipped:
			item->setText(1, detail.isEmpty() ? tr("– skipped") : tr("– %1").arg(detail));
			item->setForeground(1, QColor(150, 150, 160));
			break;
	}
}

void CompileWindow::updateTimes() {
	m_time->setText(formatTime(m_clock.elapsed()));
	if (auto* item = m_running >= 0 ? m_steps->topLevelItem(m_running) : nullptr) {
		item->setText(2, formatTime(m_stepClock.elapsed()));
	}
}

void CompileWindow::finish(bool ok) {
	m_tick.stop();
	updateTimes();
	m_cancel->setEnabled(false);
	m_again->setEnabled(true);
	m_reportButton->setVisible(true);
	if (ok) {
		m_total->setValue(100);
	}
	m_title->setText(ok ? tr("%1 compiled").arg(m_mapName) : tr("%1: compile stopped").arg(m_mapName));
	QString text = ok ? tr("<b style='color:#78dc78'>Finished</b> in %1").arg(formatTime(m_clock.elapsed()))
	                  : tr("<b style='color:#ff7878'>Stopped</b> after %1").arg(formatTime(m_clock.elapsed()));
	text += tr(" - %1 error(s), %2 warning(s).").arg(m_errors).arg(m_warnings);
	if (m_leaked) {
		text += tr("<br>The map leaks: use <i>Show leak</i> to see the hole; VVIS and VRAD need a sealed map.");
	} else if (ok && m_warnings > 0) {
		text += tr("<br>The map works; the warnings above say what could be better.");
	}
	m_summary->setText(text);
}

} // namespace sme::app
