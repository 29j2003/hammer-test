// visgroups_panel.cpp - The VisGroups panel (see visgroups_panel.h).

#include "visgroups_panel.h"

#include "editor.h"

#include <QColorDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

#include <functional>

#include <sme/visgroups.h>

namespace sme::app {

namespace {

constexpr int ID_ROLE = Qt::UserRole + 1;

/// "r g b" -> colour (white if broken).
QColor parseRgb(const std::string& text) {
	const auto parts = QString::fromStdString(text).split(' ', Qt::SkipEmptyParts);
	if (parts.size() < 3) return Qt::white;
	return QColor(parts[0].toInt(), parts[1].toInt(), parts[2].toInt());
}

QIcon swatch(const QColor& color) {
	QPixmap pixmap(14, 14);
	pixmap.fill(Qt::transparent);
	QPainter p(&pixmap);
	p.setRenderHint(QPainter::Antialiasing);
	p.setPen(QColor(20, 20, 20));
	p.setBrush(color);
	p.drawRoundedRect(QRectF(1, 1, 12, 12), 3, 3);
	return QIcon(pixmap);
}

} // namespace

VisGroupsPanel::VisGroupsPanel(Editor* editor, QWidget* parent) : QWidget(parent), m_editor(editor) {
	setObjectName("visGroupsPanel");
	auto* v = new QVBoxLayout(this);
	v->setContentsMargins(4, 4, 4, 4);
	v->setSpacing(4);

	auto* newRow = new QHBoxLayout;
	m_name = new QLineEdit;
	m_name->setObjectName("visGroupName");
	m_name->setPlaceholderText(tr("Name of a new group"));
	auto* create = new QPushButton(tr("New from selection"));
	create->setObjectName("visNew");
	create->setToolTip(tr("A new group with the selected brushes and entities (inside the marked group, if one is marked)"));
	connect(create, &QPushButton::clicked, this, [this] { createFromSelection(); });
	connect(m_name, &QLineEdit::returnPressed, create, &QPushButton::click);
	newRow->addWidget(m_name, 1);
	newRow->addWidget(create);
	v->addLayout(newRow);

	m_tree = new QTreeWidget;
	m_tree->setObjectName("visGroupTree");
	m_tree->setHeaderLabels({tr("Group"), tr("Members")});
	m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
	m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
	m_tree->setEditTriggers(QAbstractItemView::EditKeyPressed);
	m_tree->setToolTip(tr("Tick: shown. Double-click: select the members. F2 or Rename: rename."));
	v->addWidget(m_tree, 1);

	auto* buttons = new QHBoxLayout;
	const auto button = [&](const QString& text, const char* name, const QString& tip) {
		auto* b = new QPushButton(text);
		b->setObjectName(name);
		b->setToolTip(tip);
		buttons->addWidget(b);
		return b;
	};
	m_add = button(tr("Add selection"), "visAdd", tr("Put the selected objects into the marked group"));
	m_remove = button(tr("Remove selection"), "visRemove", tr("Take the selected objects out of the marked group"));
	m_select = button(tr("Select members"), "visSelect", tr("Select everything in the marked group and its sub-groups"));
	v->addLayout(buttons);
	auto* buttons2 = new QHBoxLayout;
	m_color = new QPushButton(tr("Color ..."));
	m_color->setObjectName("visColor");
	m_delete = new QPushButton(tr("Delete"));
	m_delete->setObjectName("visDelete");
	m_delete->setToolTip(tr("Remove the group; its members stay in the map, sub-groups move up"));
	auto* rename = new QPushButton(tr("Rename"));
	rename->setObjectName("visRename");
	buttons2->addWidget(rename);
	buttons2->addWidget(m_color);
	buttons2->addWidget(m_delete);
	buttons2->addStretch(1);
	v->addLayout(buttons2);

	connect(m_add, &QPushButton::clicked, this, [this] { addSelection(true); });
	connect(m_remove, &QPushButton::clicked, this, [this] { addSelection(false); });
	connect(m_select, &QPushButton::clicked, this, [this] { selectMembers(currentGroup()); });
	connect(m_color, &QPushButton::clicked, this, &VisGroupsPanel::chooseColor);
	connect(rename, &QPushButton::clicked, this, [this] {
		if (auto* item = m_tree->currentItem()) m_tree->editItem(item, 0);
	});
	connect(m_delete, &QPushButton::clicked, this, [this] {
		if (const int id = currentGroup()) m_editor->commit(deleteVisGroup(m_editor->doc(), id));
	});
	connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) { selectMembers(item->data(0, ID_ROLE).toInt()); });
	connect(m_tree, &QTreeWidget::currentItemChanged, this, &VisGroupsPanel::updateButtons);
	connect(m_tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int column) {
		if (m_rebuilding || column != 0) return;
		const int id = item->data(0, ID_ROLE).toInt();
		const bool visible = item->checkState(0) == Qt::Checked;
		if (visible != m_editor->visGroupVisible(id)) {
			// After Qt is done with the tick: hiding rebuilds this tree, which would delete the item now.
			QTimer::singleShot(0, this, [this, id, visible] { m_editor->setVisGroupVisible(id, visible); });
			return;
		}
		const auto* group = findVisGroup(visGroups(m_editor->doc()), id);
		const std::string name = item->text(0).trimmed().toStdString();
		if (group && !name.empty() && name != group->name) {
			// Rename after the edit has finished (the tree is rebuilt by the commit).
			QTimer::singleShot(0, this, [this, id, name] { m_editor->commit(renameVisGroup(m_editor->doc(), id, name)); });
		}
	});

	// The list changes with the map; counts and ticks with hiding.
	connect(editor, &Editor::documentChanged, this, &VisGroupsPanel::rebuild);
	connect(editor, &Editor::documentReplaced, this, &VisGroupsPanel::rebuild);
	connect(editor, &Editor::viewFilterChanged, this, &VisGroupsPanel::rebuild);
	connect(editor, &Editor::selectionChanged, this, &VisGroupsPanel::updateButtons);
	rebuild();
}

int VisGroupsPanel::createFromSelection() {
	const auto members = visGroupCandidates(m_editor->selection());
	auto [change, id] = createVisGroup(m_editor->doc(), m_name->text().trimmed().toStdString(), members, currentGroup());
	m_editor->commit(std::move(change));
	m_name->clear();
	setCurrentGroup(id);
	return id;
}

int VisGroupsPanel::currentGroup() const {
	const auto* item = m_tree->currentItem();
	return item ? item->data(0, ID_ROLE).toInt() : 0;
}

void VisGroupsPanel::setCurrentGroup(int id) {
	for (QTreeWidgetItemIterator it(m_tree); *it; ++it) {
		if ((*it)->data(0, ID_ROLE).toInt() == id) {
			m_tree->setCurrentItem(*it);
			return;
		}
	}
}

void VisGroupsPanel::rebuild() {
	const int current = currentGroup();
	m_rebuilding = true;
	m_tree->clear();
	const auto& doc = m_editor->doc();
	std::function<void(const VisGroup&, QTreeWidgetItem*)> add = [&](const VisGroup& group, QTreeWidgetItem* parent) {
		auto* item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tree);
		item->setText(0, QString::fromStdString(group.name));
		item->setIcon(0, swatch(parseRgb(group.color)));
		item->setData(0, ID_ROLE, group.id);
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsEditable);
		item->setCheckState(0, m_editor->visGroupVisible(group.id) ? Qt::Checked : Qt::Unchecked);
		const auto members = visGroupMembers(doc, group.id, false);
		item->setText(1, QString::number(members.solids.size() + members.entities.size()));
		item->setToolTip(1, tr("%1 brushes, %2 entities").arg(members.solids.size()).arg(members.entities.size()));
		for (const auto& child : group.children) add(child, item);
	};
	for (const auto& group : visGroups(doc)) add(group, nullptr);
	m_tree->expandAll();
	if (m_tree->topLevelItemCount() == 0) {
		auto* hint = new QTreeWidgetItem(m_tree, {tr("No groups yet - select objects and press New from selection")});
		hint->setFlags(Qt::NoItemFlags);
	}
	m_rebuilding = false;
	setCurrentGroup(current);
	updateButtons();
}

void VisGroupsPanel::updateButtons() {
	const bool group = currentGroup() != 0;
	const bool selected = m_editor->selection().hasObjects() || !m_editor->selection().faces.empty();
	m_add->setEnabled(group && selected);
	m_remove->setEnabled(group && selected);
	m_select->setEnabled(group);
	m_color->setEnabled(group);
	m_delete->setEnabled(group);
}

void VisGroupsPanel::addSelection(bool add) {
	const int id = currentGroup();
	if (id == 0) return;
	const auto& selection = m_editor->selection();
	m_editor->commit(add ? addToVisGroup(m_editor->doc(), id, selection) : removeFromVisGroup(m_editor->doc(), id, selection));
}

void VisGroupsPanel::selectMembers(int id) {
	if (id == 0) return;
	m_editor->setVisGroupVisible(id, true);   // selecting what cannot be seen would confuse
	m_editor->setSelection(visGroupMembers(m_editor->doc(), id));
}

void VisGroupsPanel::chooseColor() {
	const int id = currentGroup();
	const auto* group = findVisGroup(visGroups(m_editor->doc()), id);
	if (!group) return;
	const QColor color = QColorDialog::getColor(parseRgb(group->color), this, tr("Group color"));
	if (!color.isValid()) return;
	m_editor->commit(setVisGroupColor(m_editor->doc(), id, QStringLiteral("%1 %2 %3").arg(color.red()).arg(color.green()).arg(color.blue()).toStdString()));
}

} // namespace sme::app
