// panels.cpp - Outliner, Problems, Map Budget and Log docks (see panels.h).

#include "panels.h"

#include "asset_browser.h"
#include "editor.h"
#include "mod_code_panel.h"
#include "text_copy.h"

#include <QApplication>
#include <QClipboard>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollBar>
#include <QStyle>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <map>

#include <sme/game.h>

namespace sme::app {

// --- Outliner -------------------------------------------------------------------------

Outliner::Outliner(Editor* editor, QWidget* parent) : QWidget(parent), m_editor(editor) {
	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	m_filter = new QLineEdit;
	m_filter->setPlaceholderText(tr("Find by name or class ..."));
	m_filter->setClearButtonEnabled(true);
	layout->addWidget(m_filter);
	m_tree = new QTreeWidget;
	m_tree->setHeaderLabels({tr("Name"), tr("Class")});
	m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
	layout->addWidget(m_tree, 1);

	const auto schedule = [this] {
		if (!m_pending) {
			m_pending = true;
			QTimer::singleShot(100, this, [this] {
				m_pending = false;
				rebuild();
			});
		}
	};
	connect(editor, &Editor::documentChanged, this, schedule);
	connect(m_filter, &QLineEdit::textChanged, this, schedule);
	connect(m_tree, &QTreeWidget::itemSelectionChanged, this, [this] {
		if (m_updatingSelection) {
			return;
		}
		Selection selection;
		for (auto* item : m_tree->selectedItems()) {
			const int id = item->data(0, Qt::UserRole).toInt();
			const int solid = item->data(0, Qt::UserRole + 1).toInt();
			if (solid != 0) {
				selection.solids.push_back({0, solid});
			} else if (id > 0) {
				selection.entities.push_back(id);
			}
		}
		m_editor->setSelection(std::move(selection));
	});
	connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this] { emit frameRequested(); });
	rebuild();
}

void Outliner::setIconProvider(std::function<QIcon(const QString&)> provider) {
	m_icons = std::move(provider);
	rebuild();
}

void Outliner::refreshIcons() {
	if (!m_icons) {
		return;
	}
	for (int i = 1; i < m_tree->topLevelItemCount(); ++i) {   // 0 is the world
		auto* group = m_tree->topLevelItem(i);
		group->setIcon(0, m_icons(group->text(0)));
	}
}

void Outliner::rebuild() {
	const QString filter = m_filter->text();
	m_tree->clear();
	const auto& doc = m_editor->doc();
	auto* world = new QTreeWidgetItem(m_tree, {tr("World brushes (%1)").arg(doc.world.solids.size()), "worldspawn"});
	if (filter.isEmpty() && doc.world.solids.size() <= 2000) {
		for (const auto& solid : doc.world.solids) {
			auto* item = new QTreeWidgetItem(world, {tr("Brush %1").arg(solid.id), QString::fromStdString(solid.sides.empty() ? "" : solid.sides[0].material)});
			item->setData(0, Qt::UserRole + 1, solid.id);
		}
	}
	std::map<std::string, QTreeWidgetItem*> groups;
	for (const auto& entity : doc.entities) {
		const QString cls = QString::fromStdString(entity.classname());
		const QString name = QString::fromStdString(entity.get("targetname"));
		if (!filter.isEmpty() && !cls.contains(filter, Qt::CaseInsensitive) && !name.contains(filter, Qt::CaseInsensitive)) {
			continue;
		}
		auto& group = groups[entity.classname()];
		if (!group) {
			group = new QTreeWidgetItem(m_tree, {cls, QString()});
			if (m_icons) {
				group->setIcon(0, m_icons(cls));
			}
		}
		auto* item = new QTreeWidgetItem(group, {name.isEmpty() ? tr("(unnamed %1)").arg(entity.id) : name, cls});
		item->setData(0, Qt::UserRole, entity.id);
	}
	for (auto& [cls, group] : groups) {
		group->setText(1, tr("%1 entities").arg(group->childCount()));
		group->setExpanded(!filter.isEmpty());
	}
	m_tree->resizeColumnToContents(0);
}

// --- Problems ---------------------------------------------------------------------------

ProblemsPanel::ProblemsPanel(Editor* editor, AssetBrowser* assets, QWidget* parent) : QWidget(parent), m_editor(editor), m_assets(assets) {
	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	auto* top = new QHBoxLayout;
	m_summary = new QLabel(tr("Not checked yet"));
	auto* check = new QPushButton(tr("Check map"));
	check->setToolTip(tr("Look for problems now (also runs before every compile)"));
	// Problems that only show in the game (a wall looks wrong, an effect is missing) go into a
	// report: the button is here because this is where one looks after testing.
	auto* report = new QPushButton(tr("Make report"));
	report->setObjectName("problemsMakeReport");
	report->setToolTip(reportHowTo());
	auto* copyAll = new QPushButton(tr("Copy all"));
	copyAll->setObjectName("problemsCopyAll");
	copyAll->setToolTip(tr("Copy every problem with its fix as text"));
	connect(copyAll, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(problemsText()); });
	top->addWidget(m_summary, 1);
	top->addWidget(copyAll);
	top->addWidget(report);
	top->addWidget(check);
	layout->addLayout(top);
	connect(report, &QPushButton::clicked, this, &ProblemsPanel::reportRequested);
	m_list = new QListWidget;
	m_list->setWordWrap(true);
	m_list->setSelectionMode(QAbstractItemView::NoSelection);
	layout->addWidget(m_list, 1);
	connect(check, &QPushButton::clicked, this, [this] {
		m_cache = {};   // read models and sounds again (they may have changed on disk)
		runChecks();
	});
	connect(editor, &Editor::gameChanged, this, [this] { m_cache = {}; });
	connect(editor, &Editor::documentReplaced, this, [this] {
		m_leaks.clear();
		QTimer::singleShot(300, this, &ProblemsPanel::runChecks);
	});
}

int ProblemsPanel::errorCount() const {
	int count = 0;
	for (const auto& p : m_problems) {
		count += p.severity == Problem::Severity::Error ? 1 : 0;
	}
	return count + static_cast<int>(m_leaks.size());
}

void ProblemsPanel::runChecks() {
	const auto names = m_assets->materialNames();
	CheckContext context;
	context.entities = m_editor->entities();
	context.files = m_editor->game() ? &m_editor->game()->files : nullptr;
	context.materialNames = names.empty() ? nullptr : &names;
	context.cache = &m_cache;
	m_problems = checkMap(m_editor->doc(), context);
	showProblems();
}

void ProblemsPanel::addLeak(const std::vector<Vec3>& line) {
	m_leaks = {leakProblem(m_editor->doc(), line, m_editor->entities())};
	showProblems();
}

void ProblemsPanel::showProblems() {
	m_list->clear();
	std::vector<const Problem*> all;
	for (const auto& p : m_leaks) all.push_back(&p);
	for (const auto& p : m_problems) all.push_back(&p);
	std::stable_sort(all.begin(), all.end(), [](const Problem* a, const Problem* b) { return a->severity < b->severity; });

	// A row is a widget with labels and buttons: thousands of them (a big map with many problems)
	// froze the editor for half a minute. The worst come first; the rest are counted, not listed.
	constexpr int MAX_ROWS = 150;
	int errors = 0, warnings = 0, rows = 0;
	for (const Problem* problem : all) {
		errors += problem->severity == Problem::Severity::Error ? 1 : 0;
		warnings += problem->severity == Problem::Severity::Warning ? 1 : 0;
		if (++rows > MAX_ROWS) continue;
		auto* widget = new QWidget;
		auto* v = new QVBoxLayout(widget);
		v->setContentsMargins(6, 4, 6, 4);
		const char* icon = problem->severity == Problem::Severity::Error ? "⛔" : (problem->severity == Problem::Severity::Warning ? "⚠" : "ℹ");
		auto* message = new QLabel(QStringLiteral("%1 %2").arg(QString::fromUtf8(icon), QString::fromStdString(problem->message)));
		message->setTextFormat(Qt::PlainText);
		message->setWordWrap(true);
		message->setTextInteractionFlags(Qt::TextSelectableByMouse);
		auto* fix = new QLabel(QString::fromStdString(problem->howToFix));
		fix->setTextFormat(Qt::PlainText);
		fix->setWordWrap(true);
		fix->setProperty("role", "description");
		fix->setTextInteractionFlags(Qt::TextSelectableByMouse);
		v->addWidget(message);
		v->addWidget(fix);
		auto* buttons = new QHBoxLayout;
		if (!problem->objects.empty()) {
			auto* select = new QPushButton(tr("Select"));
			const Selection objects = problem->objects;
			connect(select, &QPushButton::clicked, this, [this, objects] {
				if (!objects.faces.empty()) {
					m_editor->setSelectMode(Editor::SelectMode::Face);
				} else {
					m_editor->setSelectMode(Editor::SelectMode::Object);
				}
				m_editor->setSelection(objects);
				emit frameRequested();
			});
			buttons->addWidget(select);
		}
		if (!problem->line.empty()) {
			auto* show = new QPushButton(tr("Show leak line"));
			const auto line = problem->line;
			connect(show, &QPushButton::clicked, this, [this, line] { emit showLeak(line); });
			buttons->addWidget(show);
		}
		if (!problem->fixLabel.empty()) {
			auto* fixButton = new QPushButton(QString::fromStdString(problem->fixLabel));
			fixButton->setProperty("accent", true);
			const Problem copy = *problem;
			connect(fixButton, &QPushButton::clicked, this, [this, copy] {
				m_editor->commit(applyFix(m_editor->doc(), copy, m_editor->entities()));
				QTimer::singleShot(0, this, &ProblemsPanel::runChecks);
			});
			buttons->addWidget(fixButton);
		}
		buttons->addStretch(1);
		v->addLayout(buttons);
		auto* item = new QListWidgetItem(m_list);
		item->setSizeHint(widget->sizeHint());
		m_list->setItemWidget(item, widget);
	}
	if (rows > MAX_ROWS) {
		auto* more = new QListWidgetItem(tr("... and %n more not listed here - fix the ones above, then check again.", nullptr,
		                                    rows - MAX_ROWS),
		                                 m_list);
		more->setForeground(QColor(170, 170, 170));
	}
	if (all.empty()) {
		m_summary->setText(tr("✔ No problems found"));
	} else {
		m_summary->setText(tr("%1 errors, %2 warnings, %3 hints").arg(errors).arg(warnings).arg(static_cast<int>(all.size()) - errors - warnings));
	}
}

QString ProblemsPanel::problemsText() const {
	QStringList lines;
	for (const auto* list : {&m_leaks, &m_problems}) {
		for (const auto& problem : *list) {
			const char* kind = problem.severity == Problem::Severity::Error ? "Error" : (problem.severity == Problem::Severity::Warning ? "Warning" : "Hint");
			lines << QStringLiteral("%1: %2").arg(QString::fromLatin1(kind), QString::fromStdString(problem.message));
			if (!problem.howToFix.empty()) lines << QStringLiteral("    %1").arg(QString::fromStdString(problem.howToFix));
		}
	}
	return lines.isEmpty() ? m_summary->text() : lines.join('\n');
}

// --- Map Budget -------------------------------------------------------------------------

BudgetPanel::BudgetPanel(Editor* editor, QWidget* parent) : QWidget(parent), m_editor(editor) {
	auto* layout = new QVBoxLayout(this);
	m_summary = new QLabel;
	m_summary->setObjectName("budgetSummary");
	m_summary->setWordWrap(true);
	layout->addWidget(m_summary);
	m_grid = new QGridLayout;
	m_grid->setColumnStretch(2, 1);
	layout->addLayout(m_grid);
	layout->addStretch(1);
	connect(editor, &Editor::documentChanged, this, &BudgetPanel::scheduleRefresh);
	connect(editor, &Editor::gameChanged, this, &BudgetPanel::scheduleRefresh);
}

void BudgetPanel::scheduleRefresh() {
	m_dirty = true;
	// Hidden docks count when they are shown; visible ones a moment after the last change
	// (a drag sends many changes).
	if (!isVisible() || m_scheduled) return;
	m_scheduled = true;
	QTimer::singleShot(250, this, [this] {
		m_scheduled = false;
		if (m_dirty && isVisible()) refresh();
	});
}

void BudgetPanel::showEvent(QShowEvent* event) {
	QWidget::showEvent(event);
	if (m_dirty) refresh();
}

void BudgetPanel::refresh() {
	m_dirty = false;
	const auto* game = m_editor->game();
	const std::uint32_t app = game ? (game->entry.ownerAppId != 0 ? game->entry.ownerAppId : game->entry.baseAppId) : 0;
	m_items = mapBudget(m_editor->doc(), budgetLimitsForApp(app));
	// Rows are made once and then only updated.
	if (m_grid->count() == 0) {
		for (int row = 0; row < static_cast<int>(m_items.size()); ++row) {
			const QString name = QString::fromStdString(m_items[std::size_t(row)].name);
			auto* label = new QLabel(name);
			auto* count = new QLabel;
			count->setObjectName("budgetCount:" + name);
			count->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
			auto* bar = new QProgressBar;
			bar->setObjectName("budgetBar:" + name);
			bar->setRange(0, 1000);
			bar->setTextVisible(false);
			bar->setMaximumHeight(8);
			m_grid->addWidget(label, row, 0);
			m_grid->addWidget(count, row, 1);
			m_grid->addWidget(bar, row, 2);
		}
	}
	int over = 0, close = 0;
	for (int row = 0; row < static_cast<int>(m_items.size()); ++row) {
		const auto& item = m_items[std::size_t(row)];
		auto* label = qobject_cast<QLabel*>(m_grid->itemAtPosition(row, 0)->widget());
		auto* count = qobject_cast<QLabel*>(m_grid->itemAtPosition(row, 1)->widget());
		auto* bar = qobject_cast<QProgressBar*>(m_grid->itemAtPosition(row, 2)->widget());
		const QString prefix = item.estimate ? QStringLiteral("~") : QString();
		count->setText(item.limit ? QStringLiteral("%1%2 / %3").arg(prefix).arg(item.count).arg(item.limit) : prefix + QString::number(item.count));
		const QString tip = QString::fromStdString(item.note) + (item.estimate ? tr("\n~ = estimate before compiling.") : QString());
		label->setToolTip(tip);
		count->setToolTip(tip);
		bar->setToolTip(tip);
		bar->setVisible(item.limit != 0);
		bar->setValue(static_cast<int>(std::min(1.0, item.fraction()) * 1000));
		// Warn from 80 % on: the compile adds some (bevels, split faces, instances).
		const char* role = item.fraction() >= 0.8 ? "warning" : "";
		count->setProperty("role", role);
		count->style()->unpolish(count);
		count->style()->polish(count);
		over += item.over() ? 1 : 0;
		close += !item.over() && item.fraction() >= 0.8 ? 1 : 0;
	}
	m_summary->setText(over > 0     ? tr("⛔ %1 limits exceeded - the map will not compile or run.").arg(over)
	                   : close > 0  ? tr("⚠ %1 limits above 80%.").arg(close)
	                                : tr("✔ Well within the engine limits."));
}

// --- Log ----------------------------------------------------------------------------------

LogPanel::LogPanel(QWidget* parent) : QPlainTextEdit(parent) {
	setReadOnly(true);
	addCopyAll(this);   // select and Ctrl+C, or right-click Copy all
	setMaximumBlockCount(20000);
	QFont font("monospace");
	font.setStyleHint(QFont::Monospace);
	font.setPointSizeF(9);
	setFont(font);
}

void LogPanel::append(const QString& text, const QColor& color) {
	QTextCharFormat format;
	if (color.isValid()) {
		format.setForeground(color);
	}
	auto cursor = textCursor();
	cursor.movePosition(QTextCursor::End);
	cursor.insertText(text + '\n', format);
	verticalScrollBar()->setValue(verticalScrollBar()->maximum());
}

} // namespace sme::app
