// class_picker.cpp - Entity class search list (see class_picker.h).

#include "class_picker.h"

#include <sme/entity_catalog.h>

#include <QApplication>
#include <QFrame>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QScreen>
#include <QVBoxLayout>

#include <set>

namespace sme::app {

namespace {

QString qs(const std::string& s) {
	return QString::fromStdString(s);
}

/// A section title in the list: not selectable, the arrow keys skip it.
void addHeader(QListWidget* list, const QString& title) {
	auto* item = new QListWidgetItem(title, list);
	item->setFlags(Qt::NoItemFlags);
	QFont font = item->font();
	font.setBold(true);
	font.setPointSizeF(font.pointSizeF() * 0.9);
	item->setFont(font);
	item->setForeground(QColor(240, 160, 40));
}

} // namespace

ClassPicker::ClassPicker(const EntityCatalog* catalog, bool solid, QWidget* parent)
    : QPushButton(parent), m_catalog(catalog), m_solid(solid) {
	setObjectName("entityClass");
	setToolTip(tr("Class of the entity - click to pick another (its keys stay). Related classes are on top."));
	setStyleSheet(QStringLiteral("text-align: left; padding-left: 8px;"));
	if (m_catalog) {
		for (const auto* cls : m_catalog->placeable()) {
			if (cls->isSolid() == m_solid) m_all << qs(cls->name);
		}
	}
	connect(this, &QPushButton::clicked, this, &ClassPicker::openList);
}

void ClassPicker::setClassName(const QString& name, const QString& placeholder) {
	m_class = name;
	setText((name.isEmpty() ? placeholder : name) + QStringLiteral("   ▾"));
}

void ClassPicker::openList() {
	if (!m_popup) {
		// A popup window: it closes by itself when the user clicks elsewhere.
		m_popup = new QFrame(this, Qt::Popup);
		m_popup->setObjectName("classPickerPopup");
		static_cast<QFrame*>(m_popup)->setFrameShape(QFrame::StyledPanel);
		auto* v = new QVBoxLayout(m_popup);
		v->setContentsMargins(6, 6, 6, 6);
		v->setSpacing(4);
		m_search = new QLineEdit;
		m_search->setObjectName("classSearch");
		m_search->setPlaceholderText(tr("Search classes (words: prop phys) - arrows move, Enter takes it"));
		m_search->setClearButtonEnabled(true);
		m_search->installEventFilter(this);
		v->addWidget(m_search);
		m_list = new QListWidget;
		m_list->setObjectName("classList");
		m_list->setUniformItemSizes(true);
		v->addWidget(m_list, 1);
		m_about = new QLabel;
		m_about->setWordWrap(true);
		m_about->setStyleSheet(QStringLiteral("color: #9aa3ad;"));
		m_about->setMinimumHeight(36);
		m_about->setAlignment(Qt::AlignTop | Qt::AlignLeft);
		v->addWidget(m_about);
		connect(m_search, &QLineEdit::textChanged, this, &ClassPicker::fill);
		connect(m_list, &QListWidget::itemClicked, this, [this] { choose(); });
		connect(m_list, &QListWidget::itemActivated, this, [this] { choose(); });
		connect(m_list, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* item) {
			const EntityClass* cls = (item && m_catalog) ? m_catalog->find(item->text().toStdString()) : nullptr;
			QString about = cls ? qs(cls->description) : QString();
			if (about.size() > 220) about = about.left(220) + QStringLiteral(" …");
			m_about->setText(about);
		});
	}
	m_search->clear();
	fill({});
	const int width = std::max(this->width(), 360);
	QPoint at = mapToGlobal(QPoint(0, height()));
	if (const QScreen* screen = this->screen()) {
		const QRect area = screen->availableGeometry();
		at.setX(std::clamp(at.x(), area.left(), std::max(area.left(), area.right() - width)));
		if (at.y() + 420 > area.bottom()) at.setY(std::max(area.top(), mapToGlobal(QPoint(0, 0)).y() - 420));
	}
	m_popup->setGeometry(QRect(at, QSize(width, 420)));
	m_popup->show();
	m_search->setFocus();
}

void ClassPicker::fill(const QString& text) {
	m_list->clear();
	std::vector<const EntityClass*> related;
	if (m_catalog && !m_class.isEmpty()) related = m_catalog->related(m_class.toStdString(), 8);
	std::set<QString> relatedNames;
	for (const auto* cls : related) relatedNames.insert(qs(cls->name));

	const QStringList words = text.split(' ', Qt::SkipEmptyParts);
	const auto matches = [&words](const QString& name) {
		return std::all_of(words.begin(), words.end(), [&name](const QString& w) { return name.contains(w, Qt::CaseInsensitive); });
	};
	QListWidgetItem* current = nullptr;
	const auto add = [&](const QString& name) {
		auto* item = new QListWidgetItem(name, m_list);
		if (name == m_class) current = item;
		return item;
	};
	if (words.isEmpty()) {
		if (!related.empty()) {
			addHeader(m_list, tr("Related to %1").arg(m_class));
			for (const auto* cls : related) add(qs(cls->name));
		}
		addHeader(m_list, tr("All classes"));
		for (const auto& name : std::as_const(m_all)) add(name);
	} else {
		// Best matches first: names that start with the search, then related ones, then the rest.
		QStringList starts, close, rest;
		for (const auto& name : std::as_const(m_all)) {
			if (!matches(name)) continue;
			if (name.startsWith(words.front(), Qt::CaseInsensitive)) starts << name;
			else if (relatedNames.count(name)) close << name;
			else rest << name;
		}
		for (const auto& name : starts + close + rest) add(name);
		if (m_list->count() == 0) addHeader(m_list, tr("No class matches \"%1\"").arg(text));
		current = nullptr;   // while searching the best match is highlighted
	}
	if (!current) {
		for (int i = 0; i < m_list->count(); ++i) {
			if (m_list->item(i)->flags() & Qt::ItemIsEnabled) {
				current = m_list->item(i);
				break;
			}
		}
	}
	if (current) {
		m_list->setCurrentItem(current);
		m_list->scrollToItem(current, QAbstractItemView::PositionAtCenter);
	}
}

void ClassPicker::choose() {
	QListWidgetItem* item = m_list ? m_list->currentItem() : nullptr;
	if (!item || !(item->flags() & Qt::ItemIsEnabled)) return;
	const QString name = item->text();
	m_popup->hide();
	if (name != m_class) {
		setClassName(name);
		emit classChosen(name);
	}
}

bool ClassPicker::eventFilter(QObject* watched, QEvent* event) {
	if (watched == m_search && event->type() == QEvent::KeyPress) {
		auto* key = static_cast<QKeyEvent*>(event);
		switch (key->key()) {
		case Qt::Key_Up:
		case Qt::Key_Down:
		case Qt::Key_PageUp:
		case Qt::Key_PageDown:
			QApplication::sendEvent(m_list, event);   // the list moves, the typing stays in the field
			return true;
		case Qt::Key_Return:
		case Qt::Key_Enter:
			choose();
			return true;
		case Qt::Key_Escape:
			m_popup->hide();
			return true;
		default:
			break;
		}
	}
	return QPushButton::eventFilter(watched, event);
}

} // namespace sme::app
