// panels.cpp - Outliner, Problems and Log docks (see panels.h).

#include "panels.h"

#include "asset_browser.h"
#include "editor.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollBar>
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
	top->addWidget(m_summary, 1);
	top->addWidget(check);
	layout->addLayout(top);
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

	int errors = 0, warnings = 0;
	for (const Problem* problem : all) {
		errors += problem->severity == Problem::Severity::Error ? 1 : 0;
		warnings += problem->severity == Problem::Severity::Warning ? 1 : 0;
		auto* widget = new QWidget;
		auto* v = new QVBoxLayout(widget);
		v->setContentsMargins(6, 4, 6, 4);
		const char* icon = problem->severity == Problem::Severity::Error ? "⛔" : (problem->severity == Problem::Severity::Warning ? "⚠" : "ℹ");
		auto* message = new QLabel(QStringLiteral("%1 %2").arg(QString::fromUtf8(icon), QString::fromStdString(problem->message)));
		message->setTextFormat(Qt::PlainText);
		message->setWordWrap(true);
		auto* fix = new QLabel(QString::fromStdString(problem->howToFix));
		fix->setTextFormat(Qt::PlainText);
		fix->setWordWrap(true);
		fix->setProperty("role", "description");
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
	if (all.empty()) {
		m_summary->setText(tr("✔ No problems found"));
	} else {
		m_summary->setText(tr("%1 errors, %2 warnings, %3 hints").arg(errors).arg(warnings).arg(static_cast<int>(all.size()) - errors - warnings));
	}
}

// --- Log ----------------------------------------------------------------------------------

LogPanel::LogPanel(QWidget* parent) : QPlainTextEdit(parent) {
	setReadOnly(true);
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
