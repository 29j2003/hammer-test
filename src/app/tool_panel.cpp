// tool_panel.cpp - Tool options dock and entity picker (see tool_panel.h).

#include "tool_panel.h"

#include "editor.h"

#include <QHeaderView>
#include <QSpinBox>
#include <QFormLayout>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMimeData>
#include <QSettings>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <memory>

#include <sme/entity_catalog.h>
#include <sme/shapes.h>
#include <sme/game.h>
#include <sme/text.h>

namespace sme::app {

namespace {

constexpr int MAX_RECENT = 10;

/// Readable group for a class name prefix.
QString groupOf(const QString& name) {
	static const QMap<QString, QString> GROUPS = {
		{"npc", QObject::tr("NPCs")},           {"monster", QObject::tr("NPCs")},       {"prop", QObject::tr("Props")},
		{"func", QObject::tr("Brush entities (func_)")}, {"trigger", QObject::tr("Triggers")}, {"info", QObject::tr("Info / markers")},
		{"logic", QObject::tr("Logic")},        {"math", QObject::tr("Logic")},         {"env", QObject::tr("Effects (env_)")},
		{"light", QObject::tr("Lights")},       {"point", QObject::tr("Point helpers")}, {"ai", QObject::tr("AI")},
		{"game", QObject::tr("Game")},          {"filter", QObject::tr("Filters")},     {"item", QObject::tr("Items")},
		{"weapon", QObject::tr("Weapons")},     {"ambient", QObject::tr("Sound")},      {"path", QObject::tr("Paths")},
		{"scripted", QObject::tr("Scripted sequences")}, {"keyframe", QObject::tr("Paths")}, {"move", QObject::tr("Paths")},
		{"phys", QObject::tr("Physics")},       {"vgui", QObject::tr("Effects (env_)")}, {"sky", QObject::tr("Sky / fog")},
		{"shadow", QObject::tr("Lights")},      {"color", QObject::tr("Effects (env_)")}, {"water", QObject::tr("Effects (env_)")},
	};
	const auto underscore = name.indexOf('_');
	const QString prefix = underscore > 0 ? name.left(underscore).toLower() : QString();
	return GROUPS.value(prefix, QObject::tr("Other"));
}

/// Tree that lets classes be dragged into the 3D view.
class EntityTree : public QTreeWidget {
public:
	using QTreeWidget::QTreeWidget;

protected:
	QStringList mimeTypes() const override { return {"application/x-sme-entity"}; }
	QMimeData* mimeData(const QList<QTreeWidgetItem*>& items) const override {
		if (items.isEmpty() || items.first()->childCount() > 0 || items.first()->data(0, Qt::UserRole).toString().isEmpty()) {
			return nullptr;
		}
		auto* mime = new QMimeData;
		mime->setData("application/x-sme-entity", items.first()->data(0, Qt::UserRole).toString().toUtf8());
		return mime;
	}
};

} // namespace

// --- Entity picker -------------------------------------------------------------------

EntityPicker::EntityPicker(Editor* editor, QWidget* parent) : QWidget(parent), m_editor(editor) {
	auto* v = new QVBoxLayout(this);
	v->setContentsMargins(4, 4, 4, 4);
	m_search = new QLineEdit;
	m_search->setPlaceholderText(tr("Search entities (e.g. light, door, citizen) ..."));
	m_search->setClearButtonEnabled(true);
	v->addWidget(m_search);
	m_tree = new EntityTree;
	m_tree->setHeaderHidden(true);
	m_tree->setDragEnabled(true);
	m_tree->setDragDropMode(QAbstractItemView::DragOnly);
	m_tree->setUniformRowHeights(true);
	v->addWidget(m_tree, 1);
	m_info = new QLabel;
	m_info->setWordWrap(true);
	m_info->setTextFormat(Qt::RichText);
	m_info->setTextInteractionFlags(Qt::TextSelectableByMouse);
	m_info->setMinimumHeight(60);
	m_info->setAlignment(Qt::AlignTop | Qt::AlignLeft);
	v->addWidget(m_info);
	m_summary = new QLabel;
	m_summary->setStyleSheet("color: #8a90a0;");
	m_summary->setWordWrap(true);
	v->addWidget(m_summary);

	connect(m_search, &QLineEdit::textChanged, this, &EntityPicker::filter);
	connect(m_search, &QLineEdit::returnPressed, this, [this] {
		// Enter takes the first match.
		for (QTreeWidgetItemIterator it(m_tree, QTreeWidgetItemIterator::NotHidden | QTreeWidgetItemIterator::NoChildren); *it; ++it) {
			if (!(*it)->data(0, Qt::UserRole).toString().isEmpty()) {
				m_tree->setCurrentItem(*it);
				break;
			}
		}
	});
	connect(m_tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) {
		if (item && item->childCount() == 0) {
			const QString name = item->data(0, Qt::UserRole).toString();
			if (!name.isEmpty()) {
				m_editor->setCurrentEntityClass(name);
				showClass(name);
			}
		}
	});
	connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) {
		const QString name = item ? item->data(0, Qt::UserRole).toString() : QString();
		if (!name.isEmpty()) {
			m_editor->setCurrentEntityClass(name);
			m_editor->setTool(Editor::Tool::Entity);
		}
	});
	connect(editor, &Editor::gameChanged, this, &EntityPicker::reload);
	connect(editor, &Editor::documentChanged, this, [this] {
		// Classes that were just placed move to "Recently used".
		const auto& entities = m_editor->doc().entities;
		if (!entities.empty()) {
			const QString last = QString::fromStdString(entities.back().classname());
			if (!last.isEmpty() && last == m_editor->currentEntityClass()) {
				remember(last);
			}
		}
	});
	reload();
}

void EntityPicker::focusSearch() {
	m_search->setFocus();
	m_search->selectAll();
}

void EntityPicker::reload() {
	m_tree->clear();
	m_recent = nullptr;
	const auto* catalog = m_editor->entities();
	if (!catalog || catalog->size() == 0) {
		m_summary->setText(tr("No entity definitions (FGD) loaded - see Game settings (⚙)."));
		return;
	}
	QMap<QString, QTreeWidgetItem*> groups;
	int count = 0;
	for (const auto* cls : catalog->placeable()) {
		const QString name = QString::fromStdString(cls->name);
		const QString group = groupOf(name);
		if (!groups.contains(group)) {
			groups.insert(group, new QTreeWidgetItem(QStringList(group)));
		}
		auto* item = new QTreeWidgetItem(groups[group], QStringList(name));
		item->setData(0, Qt::UserRole, name);
		item->setToolTip(0, QString::fromStdString(cls->description).left(300));
		if (cls->isSolid()) {
			item->setForeground(0, QColor(120, 180, 255));   // brush entity
		}
		++count;
	}
	for (auto* group : groups) {
		group->setText(0, QStringLiteral("%1 (%2)").arg(group->text(0)).arg(group->childCount()));
		group->setFlags(group->flags() & ~Qt::ItemIsDragEnabled);
		m_tree->addTopLevelItem(group);
	}
	// Recently used classes on top.
	m_recent = new QTreeWidgetItem(QStringList(tr("Recently used")));
	m_recent->setFlags(m_recent->flags() & ~Qt::ItemIsDragEnabled);
	m_tree->insertTopLevelItem(0, m_recent);
	for (const auto& name : QSettings().value("entities/recent").toStringList()) {
		if (catalog->find(name.toStdString())) {
			auto* item = new QTreeWidgetItem(m_recent, QStringList(name));
			item->setData(0, Qt::UserRole, name);
		}
	}
	m_recent->setHidden(m_recent->childCount() == 0);
	m_recent->setExpanded(true);

	QStringList files;
	for (const auto& file : catalog->files()) {
		files << QString::fromStdString(displayPath(file.filename()));
	}
	m_summary->setText(tr("%1 classes from %2 - blue = brush entity").arg(count).arg(files.join(", ")));
	showClass(m_editor->currentEntityClass());
	filter(m_search->text());
}

void EntityPicker::filter(const QString& text) {
	const QStringList words = text.toLower().split(' ', Qt::SkipEmptyParts);
	for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
		auto* group = m_tree->topLevelItem(i);
		int visible = 0;
		for (int j = 0; j < group->childCount(); ++j) {
			auto* item = group->child(j);
			const QString haystack = item->text(0).toLower() + ' ' + item->toolTip(0).toLower();
			bool show = true;
			for (const auto& word : words) {
				// Class names must match; descriptions count only for longer words.
				show = show && (item->text(0).contains(word) || (word.size() >= 4 && haystack.contains(word)));
			}
			item->setHidden(!show);
			visible += show ? 1 : 0;
		}
		group->setHidden(visible == 0);
		group->setExpanded(!words.isEmpty() || group == m_recent);
	}
}

void EntityPicker::showClass(const QString& name) {
	const auto* catalog = m_editor->entities();
	const EntityClass* cls = catalog ? catalog->find(name.toStdString()) : nullptr;
	if (!cls) {
		m_info->setText(tr("<b>%1</b>").arg(name.toHtmlEscaped()));
		return;
	}
	const QString how = cls->isSolid() ? tr("Brush entity: select brushes, then Ctrl+T (Tie to entity).")
	                                   : tr("Click in the 3D view to place it (or drag it in).");
	m_info->setText(QStringLiteral("<b>%1</b> <span style='color:#8a90a0'>(%2)</span><br>%3<br><i>%4</i>")
	                    .arg(name.toHtmlEscaped(), QString::fromStdString(displayPath(cls->sourceFile.filename())).toHtmlEscaped(),
	                         QString::fromStdString(cls->description).left(400).toHtmlEscaped(), how));
}

void EntityPicker::remember(const QString& name) {
	QStringList recent = QSettings().value("entities/recent").toStringList();
	if (!recent.isEmpty() && recent.front() == name) {
		return;
	}
	recent.removeAll(name);
	recent.prepend(name);
	while (recent.size() > MAX_RECENT) {
		recent.removeLast();
	}
	QSettings().setValue("entities/recent", recent);
	if (m_recent) {
		qDeleteAll(m_recent->takeChildren());
		for (const auto& entry : recent) {
			auto* item = new QTreeWidgetItem(m_recent, QStringList(entry));
			item->setData(0, Qt::UserRole, entry);
		}
		m_recent->setHidden(false);
		m_recent->setExpanded(true);
	}
}

// --- Block tool: shapes --------------------------------------------------------------------

namespace {

/// Shape settings; every change also rebuilds the shape made last (live editing).
QWidget* makeShapePage(Editor* editor) {
	auto* page = new QWidget;
	auto* v = new QVBoxLayout(page);
	v->setContentsMargins(8, 8, 8, 8);
	auto* title = new QLabel(QObject::tr("<b>Block tool</b> - drag the base, move up for the height, click."));
	title->setWordWrap(true);
	v->addWidget(title);
	auto* form = new QFormLayout;
	auto* kind = new QComboBox;
	const std::pair<ShapeOptions::Kind, QString> kinds[] = {
		{ShapeOptions::Kind::Block, QObject::tr("Block")},       {ShapeOptions::Kind::Wedge, QObject::tr("Wedge (ramp)")},
		{ShapeOptions::Kind::Cylinder, QObject::tr("Cylinder")}, {ShapeOptions::Kind::Spike, QObject::tr("Spike (pyramid / cone)")},
		{ShapeOptions::Kind::Sphere, QObject::tr("Sphere")},     {ShapeOptions::Kind::Arch, QObject::tr("Arch / ring / spiral")},
		{ShapeOptions::Kind::Stairs, QObject::tr("Stairs")},
	};
	for (const auto& [k, name] : kinds) {
		kind->addItem(name, int(k));
	}
	const auto spin = [](int min, int max, int step, const QString& suffix = {}) {
		auto* s = new QSpinBox;
		s->setRange(min, max);
		s->setSingleStep(step);
		s->setSuffix(suffix);
		s->setKeyboardTracking(false);
		return s;
	};
	auto* sides = spin(3, 64, 1);
	auto* arc = spin(1, 360, 15, QStringLiteral("°"));
	auto* start = spin(-360, 360, 15, QStringLiteral("°"));
	auto* width = spin(1, 4096, 8, QObject::tr(" units"));
	auto* addHeight = spin(-1024, 1024, 8, QObject::tr(" units"));
	auto* steps = spin(1, 256, 1);
	form->addRow(QObject::tr("Shape"), kind);
	form->addRow(QObject::tr("Sides / pieces"), sides);
	form->addRow(QObject::tr("Arc"), arc);
	form->addRow(QObject::tr("Start angle"), start);
	form->addRow(QObject::tr("Wall width"), width);
	form->addRow(QObject::tr("Add height"), addHeight);
	form->addRow(QObject::tr("Steps"), steps);
	v->addLayout(form);
	auto* live = new QLabel;
	live->setWordWrap(true);
	live->setStyleSheet("color: #8a90a0;");
	v->addWidget(live);
	v->addStretch(1);

	auto updating = std::make_shared<bool>(false);
	const auto load = [=] {
		*updating = true;
		const auto& o = editor->shapeOptions();
		kind->setCurrentIndex(kind->findData(int(o.kind)));
		sides->setValue(o.sides);
		arc->setValue(int(o.arc));
		start->setValue(int(o.startAngle));
		width->setValue(int(o.wallWidth));
		addHeight->setValue(int(o.addHeight));
		steps->setValue(o.steps);
		const bool round = o.kind == ShapeOptions::Kind::Cylinder || o.kind == ShapeOptions::Kind::Spike || o.kind == ShapeOptions::Kind::Sphere;
		const bool isArch = o.kind == ShapeOptions::Kind::Arch;
		for (auto* w : std::initializer_list<QWidget*>{sides}) {
			w->setVisible(round || isArch);
			form->labelForField(w)->setVisible(round || isArch);
		}
		for (auto* w : {arc, start, width, addHeight}) {
			w->setVisible(isArch);
			form->labelForField(w)->setVisible(isArch);
		}
		steps->setVisible(o.kind == ShapeOptions::Kind::Stairs);
		form->labelForField(steps)->setVisible(o.kind == ShapeOptions::Kind::Stairs);
		live->setText(editor->shapeEditable() ? QObject::tr("Changes here rebuild the shape you just made (still one undo step).")
		                                      : QObject::tr("Arch: 360° makes a ring, Add height a spiral staircase."));
		*updating = false;
	};
	const auto store = [=] {
		if (*updating) return;
		ShapeOptions o = editor->shapeOptions();
		o.kind = static_cast<ShapeOptions::Kind>(kind->currentData().toInt());
		o.sides = sides->value();
		o.arc = arc->value();
		o.startAngle = start->value();
		o.wallWidth = width->value();
		o.addHeight = addHeight->value();
		o.steps = steps->value();
		editor->setShapeOptions(o);
	};
	QObject::connect(kind, &QComboBox::currentIndexChanged, page, store);
	for (auto* s : {sides, arc, start, width, addHeight, steps}) {
		QObject::connect(s, &QSpinBox::valueChanged, page, store);
	}
	QObject::connect(editor, &Editor::settingsChanged, page, load);
	QObject::connect(editor, &Editor::documentChanged, page, load);
	load();
	return page;
}

} // namespace

// --- Tool panel ----------------------------------------------------------------------------

ToolPanel::ToolPanel(Editor* editor, QWidget* parent) : QWidget(parent), m_editor(editor) {
	auto* v = new QVBoxLayout(this);
	v->setContentsMargins(0, 0, 0, 0);
	m_pages = new QStackedWidget;
	v->addWidget(m_pages);
	m_hint = new QLabel;
	m_hint->setWordWrap(true);
	m_hint->setAlignment(Qt::AlignTop | Qt::AlignLeft);
	m_hint->setMargin(8);
	m_hint->setTextFormat(Qt::RichText);
	m_pages->addWidget(m_hint);
	m_entities = new EntityPicker(editor);
	m_pages->addWidget(m_entities);
	m_block = makeShapePage(editor);
	m_pages->addWidget(m_block);
	connect(editor, &Editor::toolChanged, this, [this] {
		refresh();
		if (m_editor->tool() == Editor::Tool::Entity && m_editor->selectMode() == Editor::SelectMode::Object) {
			emit wantsAttention();
			m_entities->focusSearch();
		}
	});
	refresh();
}

void ToolPanel::setBlockPage(QWidget* page) {
	m_block = page;
	m_pages->addWidget(page);
	refresh();
}

void ToolPanel::refresh() {
	if (m_editor->selectMode() == Editor::SelectMode::Face) {
		m_hint->setText(tr("<b>Faces</b><br>Click a face to select it, Ctrl+click adds, double-click selects the whole wall.<br>"
		                   "Drag a material onto a face, Shift+T applies the current material, Ctrl+right mouse paints.<br>"
		                   "The Object Properties panel aligns and fits the selected faces together."));
		m_pages->setCurrentWidget(m_hint);
		return;
	}
	switch (m_editor->tool()) {
		case Editor::Tool::Entity: m_pages->setCurrentWidget(m_entities); return;
		case Editor::Tool::Block:
			if (m_block) {
				m_pages->setCurrentWidget(m_block);
				return;
			}
			m_hint->setText(tr("<b>Block</b><br>Drag the base on the grid, move the mouse up for the height and click."));
			break;
		case Editor::Tool::Select:
			m_hint->setText(tr("<b>Select</b><br>Click selects, Ctrl+click adds, drag a frame for many.<br>Double-click frames the selection (F).<br>"
			                   "Right mouse: look around, W A S D fly."));
			break;
		case Editor::Tool::Move:
			m_hint->setText(tr("<b>Move</b><br>Drag an arrow or a square of the gizmo. Shift+drag copies.<br>Arrow keys nudge by the grid."));
			break;
		case Editor::Tool::Rotate:
			m_hint->setText(tr("<b>Rotate</b><br>Drag a ring. Steps follow the rotation snap (Ctrl: 1°).<br>Alt+arrow keys rotate by one step."));
			break;
		case Editor::Tool::Scale:
			m_hint->setText(tr("<b>Scale</b><br>Drag a handle on a side of the box. Textures stay in place (texture lock)."));
			break;
	}
	m_pages->setCurrentWidget(m_hint);
}

} // namespace sme::app
