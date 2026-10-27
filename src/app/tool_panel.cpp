// tool_panel.cpp - Tool options dock and entity picker (see tool_panel.h).

#include "tool_panel.h"

#include "editor.h"
#include "material_cache.h"
#include "model_thumbnails.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPainter>
#include <QRegularExpression>
#include <QToolButton>
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

#include <algorithm>
#include <functional>
#include <memory>

#include <sme/editing.h>
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

namespace {

constexpr int ICON_SIZE = 20;

QString spriteMaterial(const EntityClass& cls) {
	return QString::fromStdString(edit::classSprite(cls));
}

/// Editor model of a class without sprite: studio("models/editor/playerstart.mdl").
QString editorModel(const EntityClass& cls) {
	for (const char* helper : {"studio", "studioprop"}) {
		QString model = QString::fromStdString(cls.helper(helper)).remove('"').trimmed();
		if (model.endsWith(".mdl", Qt::CaseInsensitive)) {
			return QString::fromStdString(normalizePath(model.toStdString()));
		}
	}
	return {};
}

QColor classColor(const EntityClass& cls) {
	const QStringList parts = QString::fromStdString(cls.helper("color")).split(' ', Qt::SkipEmptyParts);
	if (parts.size() >= 3) {
		return QColor(parts[0].toInt(), parts[1].toInt(), parts[2].toInt());
	}
	return cls.isSolid() ? QColor(90, 160, 255) : QColor(220, 30, 220);   // Hammer's defaults
}

/// Square in the class color; brush entities get a frame like a brush.
QIcon colorIcon(const EntityClass& cls) {
	QPixmap pixmap(ICON_SIZE, ICON_SIZE);
	pixmap.fill(Qt::transparent);
	QPainter p(&pixmap);
	p.setRenderHint(QPainter::Antialiasing);
	const QColor color = classColor(cls);
	if (cls.isSolid()) {
		p.setPen(QPen(color.lighter(130), 1.5));
		p.setBrush(QColor(color.red(), color.green(), color.blue(), 90));
		p.drawRect(QRectF(3.5, 3.5, ICON_SIZE - 7, ICON_SIZE - 7));
	} else {
		p.setPen(Qt::NoPen);
		p.setBrush(color);
		p.drawRoundedRect(QRectF(4, 4, ICON_SIZE - 8, ICON_SIZE - 8), 2, 2);
	}
	return QIcon(pixmap);
}

/// Editor sprite as an icon. Additive sprites have black backgrounds: their brightness becomes alpha.
QIcon spriteIcon(const MaterialInfo& info) {
	if (info.image.isNull()) {
		return {};
	}
	QImage image = info.image.convertToFormat(QImage::Format_RGBA8888);
	if (info.kind == MaterialInfo::Kind::Additive || !image.hasAlphaChannel()) {
		for (int y = 0; y < image.height(); ++y) {
			auto* row = reinterpret_cast<uchar*>(image.scanLine(y));
			for (int x = 0; x < image.width(); ++x) {
				uchar* px = row + x * 4;
				px[3] = std::max({px[0], px[1], px[2]});
			}
		}
	}
	return QIcon(QPixmap::fromImage(image.scaled(ICON_SIZE, ICON_SIZE, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
}

} // namespace

EntityPicker::EntityPicker(Editor* editor, QWidget* parent) : QWidget(parent), m_editor(editor) {
	auto* v = new QVBoxLayout(this);
	v->setContentsMargins(4, 4, 4, 4);
	v->setSpacing(4);
	auto* top = new QHBoxLayout;
	m_search = new QLineEdit;
	m_search->setPlaceholderText(tr("Find an entity class (e.g. light, door, global light) ..."));
	m_search->setClearButtonEnabled(true);
	top->addWidget(m_search, 1);
	m_grouped = new QToolButton;
	m_grouped->setText(tr("Groups"));
	m_grouped->setToolTip(tr("Sort the classes into groups (NPCs, triggers, lights ...) instead of A-Z"));
	m_grouped->setCheckable(true);
	m_grouped->setChecked(QSettings().value("entities/grouped", false).toBool());
	top->addWidget(m_grouped);
	v->addLayout(top);
	m_tree = new EntityTree;
	m_tree->setHeaderHidden(true);
	m_tree->setDragEnabled(true);
	m_tree->setDragDropMode(QAbstractItemView::DragOnly);
	m_tree->setUniformRowHeights(true);
	m_tree->setIconSize(QSize(ICON_SIZE, ICON_SIZE));
	v->addWidget(m_tree, 1);
	m_info = new QLabel;
	m_info->setWordWrap(true);
	m_info->setTextFormat(Qt::RichText);
	m_info->setTextInteractionFlags(Qt::TextSelectableByMouse);
	m_info->setAlignment(Qt::AlignTop | Qt::AlignLeft);
	m_info->setMaximumHeight(120);
	v->addWidget(m_info);

	// The armed class: what the next click (point entity) or box (brush entity) becomes.
	auto* armedRow = new QHBoxLayout;
	m_armedIcon = new QLabel;
	m_armedIcon->setFixedSize(ICON_SIZE, ICON_SIZE);
	armedRow->addWidget(m_armedIcon);
	m_armed = new QLabel;
	m_armed->setWordWrap(true);
	m_armed->setTextFormat(Qt::RichText);
	armedRow->addWidget(m_armed, 1);
	m_disarm = new QToolButton;
	m_disarm->setText(QStringLiteral("✕"));
	m_disarm->setToolTip(tr("Disarm: clicks in the view select again"));
	armedRow->addWidget(m_disarm);
	v->addLayout(armedRow);
	m_randomYaw = new QCheckBox(tr("Random yaw (3D placement)"));
	m_randomYaw->setToolTip(tr("Every point entity placed in the 3D view gets a random turn - props and debris look less repeated"));
	m_randomYaw->setChecked(editor->randomYaw());
	v->addWidget(m_randomYaw);
	m_summary = new QLabel;
	m_summary->setStyleSheet("color: #8a90a0;");
	m_summary->setWordWrap(true);
	v->addWidget(m_summary);

	connect(m_search, &QLineEdit::textChanged, this, &EntityPicker::filter);
	connect(m_search, &QLineEdit::returnPressed, this, [this] {
		// Enter arms the first match.
		for (QTreeWidgetItemIterator it(m_tree, QTreeWidgetItemIterator::NotHidden | QTreeWidgetItemIterator::NoChildren); *it; ++it) {
			if (!(*it)->data(0, Qt::UserRole).toString().isEmpty()) {
				m_tree->setCurrentItem(*it);
				break;
			}
		}
	});
	connect(m_grouped, &QToolButton::toggled, this, [this](bool on) {
		QSettings().setValue("entities/grouped", on);
		reload();
	});
	connect(m_tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) {
		const QString name = item ? item->data(0, Qt::UserRole).toString() : QString();
		if (!name.isEmpty()) {
			m_editor->setCurrentEntityClass(name);   // arms it
			showClass(name);
		}
	});
	connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) {
		const QString name = item ? item->data(0, Qt::UserRole).toString() : QString();
		if (!name.isEmpty()) {
			m_editor->setCurrentEntityClass(name);
			m_editor->setTool(Editor::Tool::Entity);
		}
	});
	connect(m_disarm, &QToolButton::clicked, this, [this] {
		m_tree->setCurrentItem(nullptr);
		m_editor->setCurrentEntityClass({});
	});
	connect(m_randomYaw, &QCheckBox::toggled, editor, &Editor::setRandomYaw);
	connect(editor, &Editor::settingsChanged, this, &EntityPicker::showArmed);
	// Queued: the material cache gets the new game's files in the same signal.
	connect(editor, &Editor::gameChanged, this, &EntityPicker::reload, Qt::QueuedConnection);
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

void EntityPicker::setMaterialCache(MaterialCache* materials) {
	m_materials = materials;
	connect(materials, &MaterialCache::materialReady, this, &EntityPicker::spriteReady);
	reload();
}

void EntityPicker::focusSearch() {
	m_search->setFocus();
	m_search->selectAll();
}

QIcon EntityPicker::iconFor(const QString& classname) {
	const auto* catalog = m_editor->entities();
	const EntityClass* cls = catalog ? catalog->find(classname.toStdString()) : nullptr;
	if (!cls) {
		return {};
	}
	const QString sprite = spriteMaterial(*cls);
	if (!sprite.isEmpty() && m_spriteIcons.contains(sprite)) {
		return m_spriteIcons.value(sprite);
	}
	if (const QString model = QStringLiteral("model:") + editorModel(*cls); m_spriteIcons.contains(model)) {
		return m_spriteIcons.value(model);
	}
	return colorIcon(*cls);
}

QTreeWidgetItem* EntityPicker::addClassItem(QTreeWidgetItem* parent, const EntityClass& cls) {
	const QString name = QString::fromStdString(cls.name);
	auto* item = new QTreeWidgetItem(parent, QStringList(name));
	item->setData(0, Qt::UserRole, name);
	item->setToolTip(0, QString::fromStdString(cls.description).left(300));
	item->setIcon(0, colorIcon(cls));
	const QString sprite = spriteMaterial(cls);
	if (!sprite.isEmpty()) {
		if (m_spriteIcons.contains(sprite)) {
			item->setIcon(0, m_spriteIcons.value(sprite));
		} else if (m_materials) {
			m_spriteItems[sprite].push_back(item);
			if (const auto info = m_materials->get(sprite)) {
				spriteReady(sprite);   // already loaded
			}
		}
		return item;
	}
	// No sprite: the editor model Hammer shows for it (player start, nodes, ...).
	const QString model = editorModel(cls);
	if (!model.isEmpty() && m_thumbnails) {
		m_modelItems[model].push_back(item);
		(void)m_thumbnails->get(model);   // starts drawing it
		if (m_thumbnails->hasPreview(model)) {
			modelReady(model);
		}
	}
	return item;
}

void EntityPicker::setModelThumbnails(ModelThumbnails* thumbnails) {
	m_thumbnails = thumbnails;
	connect(thumbnails, &ModelThumbnails::thumbnailReady, this, &EntityPicker::modelReady);
	reload();
}

void EntityPicker::modelReady(const QString& path) {
	if (!m_modelItems.contains(path) || !m_thumbnails || !m_thumbnails->hasPreview(path)) {
		m_modelItems.remove(path);   // missing model: the color square stays
		return;
	}
	const QIcon icon(m_thumbnails->get(path).scaled(ICON_SIZE, ICON_SIZE, Qt::KeepAspectRatio, Qt::SmoothTransformation));
	m_spriteIcons.insert(QStringLiteral("model:") + path, icon);
	// Kept: the picture is drawn again once the model's textures arrive.
	for (auto* item : m_modelItems.value(path)) {
		item->setIcon(0, icon);
	}
	showArmed();
}

void EntityPicker::spriteReady(const QString& material) {
	if (!m_spriteItems.contains(material) || !m_materials) {
		return;
	}
	const auto info = m_materials->get(material);
	if (!info) {
		return;
	}
	const QIcon icon = info->found ? spriteIcon(*info) : QIcon();
	if (!icon.isNull()) {
		m_spriteIcons.insert(material, icon);
	}
	for (auto* item : m_spriteItems.take(material)) {
		if (!icon.isNull()) {
			item->setIcon(0, icon);
		}
	}
	showArmed();
}

void EntityPicker::reload() {
	m_tree->clear();
	m_spriteItems.clear();
	m_modelItems.clear();
	m_recent = nullptr;
	const auto* catalog = m_editor->entities();
	if (!catalog || catalog->size() == 0) {
		m_summary->setText(tr("No entity definitions (FGD) loaded - see Game settings (⚙)."));
		showArmed();
		return;
	}
	const bool grouped = m_grouped->isChecked();
	auto* points = new QTreeWidgetItem(QStringList(tr("Point entities")));
	auto* brushes = new QTreeWidgetItem(QStringList(tr("Brush entities")));
	QMap<QString, QTreeWidgetItem*> groups[2];
	for (const auto* cls : catalog->placeable()) {
		const int kind = cls->isSolid() ? 1 : 0;
		QTreeWidgetItem* parent = kind ? brushes : points;
		if (grouped) {
			const QString group = groupOf(QString::fromStdString(cls->name));
			if (!groups[kind].contains(group)) {
				groups[kind].insert(group, new QTreeWidgetItem(parent, QStringList(group)));
			}
			parent = groups[kind][group];
		}
		addClassItem(parent, *cls);
	}
	const auto finishGroup = [](QTreeWidgetItem* group) {
		group->setFlags(group->flags() & ~Qt::ItemIsDragEnabled & ~Qt::ItemIsSelectable);
		QFont font = group->font(0);
		font.setBold(true);
		group->setFont(0, font);
	};
	for (auto* top : {points, brushes}) {
		int count = 0;
		for (int i = 0; i < top->childCount(); ++i) {
			auto* child = top->child(i);
			if (child->childCount() > 0) {
				child->setText(0, QStringLiteral("%1 (%2)").arg(child->text(0)).arg(child->childCount()));
				finishGroup(child);
				count += child->childCount();
			} else {
				++count;
			}
		}
		top->setText(0, QStringLiteral("%1 (%2)").arg(top->text(0)).arg(count));
		finishGroup(top);
		m_tree->addTopLevelItem(top);
	}
	points->setExpanded(true);
	// Recently used classes on top.
	m_recent = new QTreeWidgetItem(QStringList(tr("Recently used")));
	finishGroup(m_recent);
	m_tree->insertTopLevelItem(0, m_recent);
	for (const auto& name : QSettings().value("entities/recent").toStringList()) {
		if (const auto* cls = catalog->find(name.toStdString())) {
			addClassItem(m_recent, *cls);
		}
	}
	m_recent->setHidden(m_recent->childCount() == 0);
	m_recent->setExpanded(true);

	QStringList files;
	for (const auto& file : catalog->files()) {
		files << QString::fromStdString(displayPath(file.filename()));
	}
	m_summary->setText(tr("%1 classes from %2").arg(catalog->placeable().size()).arg(files.join(", ")));
	showClass(m_editor->currentEntityClass());
	showArmed();
	filter(m_search->text());
}

void EntityPicker::filter(const QString& text) {
	// "global light" finds env_global_light: words match the class name (also across "_"),
	// longer words the description too.
	const QStringList words = text.toLower().split(QRegularExpression("[\\s_]+"), Qt::SkipEmptyParts);
	int matches = 0;
	const std::function<bool(QTreeWidgetItem*)> apply = [&](QTreeWidgetItem* item) -> bool {
		const QString name = item->data(0, Qt::UserRole).toString();
		if (name.isEmpty()) {
			// A group: visible when a child is.
			bool any = false;
			for (int i = 0; i < item->childCount(); ++i) {
				any = apply(item->child(i)) || any;
			}
			item->setHidden(!any);
			item->setExpanded(any && (!words.isEmpty() || item == m_recent || item->isExpanded()));
			return any;
		}
		const QString lower = name.toLower();
		const QString haystack = lower + ' ' + item->toolTip(0).toLower();
		bool show = true;
		for (const auto& word : words) {
			show = show && (lower.contains(word) || (word.size() >= 4 && haystack.contains(word)));
		}
		item->setHidden(!show);
		if (show && item->parent() != m_recent) {
			++matches;
		}
		return show;
	};
	for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
		apply(m_tree->topLevelItem(i));
	}
	if (words.isEmpty()) {
		if (m_tree->topLevelItemCount() > 1) {
			m_tree->topLevelItem(1)->setExpanded(true);   // point entities
		}
		return;
	}
	if (matches == 0) {
		const auto* catalog = m_editor->entities();
		QStringList files;
		if (catalog) {
			for (const auto& file : catalog->files()) {
				files << QString::fromStdString(displayPath(file.filename()));
			}
		}
		const auto* entry = m_editor->gameEntry();
		QString hint = tr("No class matches \"%1\" in %2.").arg(text.toHtmlEscaped(), files.isEmpty() ? tr("the loaded FGDs") : files.join(", "));
		if (entry && !entry->usesMapbase) {
			hint += tr("<br>Mapbase classes (env_global_light, logic_script ...) need a Mapbase game or mod and its FGD.");
		}
		if (entry && entry->baseContent) {
			hint += tr("<br>%1 is only base content - choose %2's own folder in the game list.")
			            .arg(QString::fromStdString(entry->folder()), QString::fromStdString(entry->appName));
		}
		m_info->setText(hint);
	}
}

void EntityPicker::showClass(const QString& name) {
	const auto* catalog = m_editor->entities();
	const EntityClass* cls = catalog && !name.isEmpty() ? catalog->find(name.toStdString()) : nullptr;
	if (!cls) {
		m_info->setText(name.isEmpty() ? QString() : tr("<b>%1</b>").arg(name.toHtmlEscaped()));
		return;
	}
	m_info->setText(QStringLiteral("<b>%1</b> <span style='color:#8a90a0'>(%2)</span><br>%3")
	                    .arg(name.toHtmlEscaped(), QString::fromStdString(displayPath(cls->sourceFile.filename())).toHtmlEscaped(),
	                         QString::fromStdString(cls->description).left(400).toHtmlEscaped()));
}

void EntityPicker::showArmed() {
	const QString name = m_editor->currentEntityClass();
	const auto* catalog = m_editor->entities();
	const EntityClass* cls = catalog && !name.isEmpty() ? catalog->find(name.toStdString()) : nullptr;
	if (name.isEmpty()) {
		m_armedIcon->setText(QStringLiteral("⚡"));
		m_armedIcon->setPixmap({});
		m_armed->setText(tr("<span style='color:#8a90a0'>Nothing armed. Pick a class to arm it - the next click (or box for a brush "
		                    "entity) becomes it.</span>"));
		m_disarm->setVisible(false);
		return;
	}
	m_armedIcon->setPixmap(iconFor(name).pixmap(ICON_SIZE, ICON_SIZE));
	const QString how = cls && cls->isSolid() ? tr("drag a box in the view (or select brushes and Ctrl+T)")
	                                          : tr("click in the view or drag it in");
	m_armed->setText(tr("Armed: <b>%1</b> - %2").arg(name.toHtmlEscaped(), how));
	m_disarm->setVisible(true);
	if (m_randomYaw->isChecked() != m_editor->randomYaw()) {
		m_randomYaw->setChecked(m_editor->randomYaw());
	}
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
	const auto* catalog = m_editor->entities();
	if (m_recent && catalog) {
		for (auto* child : m_recent->takeChildren()) {
			for (auto& list : m_spriteItems) {
				list.removeAll(child);
			}
			for (auto& list : m_modelItems) {
				list.removeAll(child);
			}
			delete child;
		}
		for (const auto& entry : recent) {
			if (const auto* cls = catalog->find(entry.toStdString())) {
				addClassItem(m_recent, *cls);
			}
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
		case Editor::Tool::Clip:
			m_hint->setText(tr("<b>Clip</b><br>Select brushes. In a 2D view drag a line through them; in the 3D view click three points "
			                   "on surfaces.<br>Tab: keep both parts / the back / the front. Enter cuts, Esc starts over."));
			break;
		case Editor::Tool::Vertex:
			m_hint->setText(tr("<b>Vertex</b><br>Select a brush, then drag its corners (white) or edge middles (grey). Ctrl+click selects "
			                   "several, then drag one of them.<br>In 3D the corner moves across the view; use a 2D view for exact moves. "
			                   "Brushes always stay convex - corners that fold in are merged."));
			break;
		case Editor::Tool::Polygon:
			m_hint->setText(tr("<b>Polygon</b><br>Click the corners of the base (on the floor in 3D, or in a 2D view). Enter or double-click "
			                   "makes a brush as high as the last block. Backspace removes the last corner."));
			break;
	}
	m_pages->setCurrentWidget(m_hint);
}

} // namespace sme::app
