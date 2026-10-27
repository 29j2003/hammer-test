// find_dialog.cpp - Find entities and the entity report (see find_dialog.h).

#include "find_dialog.h"

#include "editor.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QMap>
#include <QSet>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <sme/entity_search.h>

namespace sme::app {

namespace {

enum Column { ClassColumn, NameColumn, OriginColumn, OutputsColumn, IdColumn };

/// One row for an entity: class, name, origin, number of outputs, id (sorted as a number).
QTreeWidgetItem* entityRow(const Entity& entity) {
	auto* item = new QTreeWidgetItem;
	item->setText(ClassColumn, QString::fromStdString(entity.classname()));
	item->setText(NameColumn, QString::fromStdString(entity.get("targetname")));
	if (entity.hasOrigin()) {
		const Vec3 o = entity.origin();
		item->setText(OriginColumn, QStringLiteral("%1 %2 %3").arg(o.x).arg(o.y).arg(o.z));
	} else {
		item->setText(OriginColumn, QObject::tr("(brushes)"));
	}
	item->setData(OutputsColumn, Qt::DisplayRole, static_cast<int>(entity.connections.size()));
	item->setData(IdColumn, Qt::DisplayRole, entity.id);
	item->setData(0, Qt::UserRole, entity.id);
	return item;
}

QTreeWidget* entityTable(const QString& objectName) {
	auto* tree = new QTreeWidget;
	tree->setObjectName(objectName);
	tree->setHeaderLabels({QObject::tr("Class"), QObject::tr("Name"), QObject::tr("Origin"), QObject::tr("Outputs"), QObject::tr("Id")});
	tree->setRootIsDecorated(false);
	tree->setUniformRowHeights(true);
	tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
	tree->setSortingEnabled(true);
	tree->sortByColumn(ClassColumn, Qt::AscendingOrder);
	tree->header()->setSectionResizeMode(ClassColumn, QHeaderView::ResizeToContents);
	tree->header()->setSectionResizeMode(NameColumn, QHeaderView::Stretch);
	return tree;
}

} // namespace

FindDialog::FindDialog(Editor* editor, std::function<void()> frameSelection, QWidget* parent)
	: QDialog(parent), m_editor(editor), m_frame(std::move(frameSelection)) {
	setObjectName("findDialog");
	setWindowTitle(tr("Find entities"));
	setModal(false);
	resize(720, 520);
	auto* layout = new QVBoxLayout(this);
	m_tabs = new QTabWidget;
	layout->addWidget(m_tabs);

	// --- Find: results while typing --------------------------------------------------------
	auto* find = new QWidget;
	auto* fv = new QVBoxLayout(find);
	auto* row = new QHBoxLayout;
	m_field = new QComboBox;
	m_field->setObjectName("findField");
	m_field->addItems({tr("Anything"), tr("Class"), tr("Name"), tr("Key = value"), tr("Output target"), tr("Model"), tr("Sound")});
	m_query = new QLineEdit;
	m_query->setObjectName("findQuery");
	m_query->setClearButtonEnabled(true);
	m_query->setPlaceholderText(tr("e.g.  door   ·   class:prop_*   ·   rendercolor=255*   ·   target:relay_*"));
	row->addWidget(m_field);
	row->addWidget(m_query, 1);
	fv->addLayout(row);
	auto* help = new QLabel(tr("Words match class, name, model or any value; * and ? are wildcards; key=value, class:, name:, target:, "
	                           "model: and sound: narrow it down. Several terms must all match."));
	help->setWordWrap(true);
	help->setProperty("role", "description");
	fv->addWidget(help);
	m_results = entityTable("findResults");
	fv->addWidget(m_results, 1);
	auto* actions = new QHBoxLayout;
	m_count = new QLabel;
	m_count->setProperty("role", "description");
	actions->addWidget(m_count, 1);
	auto* selectAll = new QPushButton(tr("Select all"));
	selectAll->setObjectName("findSelectAll");
	auto* goTo = new QPushButton(tr("Go to"));
	goTo->setObjectName("findGoTo");
	goTo->setProperty("accent", true);
	actions->addWidget(selectAll);
	actions->addWidget(goTo);
	fv->addLayout(actions);
	m_tabs->addTab(find, tr("Find"));

	// --- Entity report: every entity -------------------------------------------------------
	auto* report = new QWidget;
	auto* rv = new QVBoxLayout(report);
	auto* filterRow = new QHBoxLayout;
	m_reportClass = new QComboBox;
	m_reportClass->setObjectName("reportClass");
	m_reportFilter = new QLineEdit;
	m_reportFilter->setObjectName("reportFilter");
	m_reportFilter->setClearButtonEnabled(true);
	m_reportFilter->setPlaceholderText(tr("Filter by class, name or any value ..."));
	filterRow->addWidget(m_reportClass);
	filterRow->addWidget(m_reportFilter, 1);
	rv->addLayout(filterRow);
	m_report = entityTable("entityReport");
	rv->addWidget(m_report, 1);
	m_reportCount = new QLabel;
	m_reportCount->setProperty("role", "description");
	rv->addWidget(m_reportCount);
	m_tabs->addTab(report, tr("Entity report"));

	connect(m_query, &QLineEdit::textChanged, this, &FindDialog::search);
	connect(m_field, &QComboBox::currentIndexChanged, this, &FindDialog::search);
	connect(selectAll, &QPushButton::clicked, this, [this] { selectRows(m_results, true, false); });
	connect(goTo, &QPushButton::clicked, this, [this] { selectRows(m_results, false, true); });
	connect(m_reportFilter, &QLineEdit::textChanged, this, &FindDialog::fillReport);
	connect(m_reportClass, &QComboBox::currentIndexChanged, this, &FindDialog::fillReport);
	for (auto* tree : {m_results, m_report}) {
		connect(tree, &QTreeWidget::itemSelectionChanged, this, [this, tree] { selectRows(tree, false, false); });
		connect(tree, &QTreeWidget::itemDoubleClicked, this, [this, tree] { selectRows(tree, false, true); });
	}
	// The map changes under the dialog: keep both lists true (only while it is shown).
	connect(editor, &Editor::documentChanged, this, [this] {
		if (!isVisible()) return;
		search();
		fillReport();
	});
	connect(m_tabs, &QTabWidget::currentChanged, this, [this](int tab) {
		if (tab == 1) fillReport();
	});
	search();
	fillReport();
}

void FindDialog::setSearch(int field, const QString& query) {
	m_field->setCurrentIndex(field);
	m_query->setText(query);
	search();
}

void FindDialog::showTab(int index) {
	m_tabs->setCurrentIndex(index);
}

void FindDialog::search() {
	const auto ids = findEntities(m_editor->doc(), static_cast<EntityField>(m_field->currentIndex()), m_query->text().toStdString());
	const QSet<int> wanted(ids.begin(), ids.end());
	m_results->setSortingEnabled(false);
	m_results->clear();
	for (const auto& entity : m_editor->doc().entities) {
		if (wanted.contains(entity.id)) m_results->addTopLevelItem(entityRow(entity));
	}
	m_results->setSortingEnabled(true);
	m_count->setText(tr("%n entity/entities found", nullptr, m_results->topLevelItemCount()));
}

void FindDialog::fillReport() {
	const auto& doc = m_editor->doc();
	// Class list: every class in the map, with how many there are.
	const QString chosen = m_reportClass->currentData().toString();
	QMap<QString, int> classes;
	for (const auto& entity : doc.entities) ++classes[QString::fromStdString(entity.classname())];
	{
		const QSignalBlocker block(m_reportClass);
		m_reportClass->clear();
		m_reportClass->addItem(tr("All classes (%1)").arg(doc.entities.size()), QString());
		for (auto it = classes.constBegin(); it != classes.constEnd(); ++it) {
			m_reportClass->addItem(QStringLiteral("%1 (%2)").arg(it.key()).arg(it.value()), it.key());
		}
		m_reportClass->setCurrentIndex(std::max(0, m_reportClass->findData(chosen)));
	}
	const QString onlyClass = m_reportClass->currentData().toString();
	const std::string filter = m_reportFilter->text().toStdString();
	const auto ids = findEntities(doc, EntityField::Anything, filter);
	const QSet<int> matching(ids.begin(), ids.end());
	m_report->setSortingEnabled(false);
	m_report->clear();
	for (const auto& entity : doc.entities) {
		if (!matching.contains(entity.id)) continue;
		if (!onlyClass.isEmpty() && QString::fromStdString(entity.classname()) != onlyClass) continue;
		m_report->addTopLevelItem(entityRow(entity));
	}
	m_report->setSortingEnabled(true);
	m_reportCount->setText(tr("%1 of %2 entities").arg(m_report->topLevelItemCount()).arg(doc.entities.size()));
}

void FindDialog::selectRows(QTreeWidget* tree, bool all, bool frame) {
	Selection selection;
	const auto rows = all ? [tree] {
		QList<QTreeWidgetItem*> items;
		for (int i = 0; i < tree->topLevelItemCount(); ++i) items << tree->topLevelItem(i);
		return items;
	}() : tree->selectedItems();
	for (auto* item : rows) {
		selection.entities.push_back(item->data(0, Qt::UserRole).toInt());
	}
	if (selection.entities.empty()) {
		return;
	}
	m_editor->setSelection(std::move(selection));
	if (frame && m_frame) {
		m_frame();
	}
}

FindDialog* showFindDialog(Editor* editor, QWidget* parent, std::function<void()> frameSelection, int tab) {
	auto* dialog = parent ? parent->findChild<FindDialog*>("findDialog", Qt::FindDirectChildrenOnly) : nullptr;
	if (!dialog) {
		dialog = new FindDialog(editor, std::move(frameSelection), parent);
	}
	dialog->showTab(tab);
	dialog->show();
	dialog->raise();
	dialog->activateWindow();
	return dialog;
}

} // namespace sme::app
