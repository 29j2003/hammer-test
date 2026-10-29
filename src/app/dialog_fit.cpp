// dialog_fit.cpp - Windows that fit the screen (see dialog_fit.h).

#include "dialog_fit.h"

#include <QAbstractButton>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QLabel>
#include <QLayout>
#include <QMainWindow>
#include <QScreen>
#include <QScrollArea>
#include <QStyle>
#include <QVBoxLayout>

#include <algorithm>
#include <vector>

namespace sme::app {

namespace {

const char* FITTED = "smeFittedToScreen";
QRect g_testArea;

/// Qt's own dialogs size themselves and are never taller than a screen.
bool isQtStandardDialog(const QWidget* window) {
	for (const char* name : {"QMessageBox", "QFileDialog", "QInputDialog", "QProgressDialog", "QColorDialog", "QFontDialog", "QErrorMessage", "QWizard"}) {
		if (window->inherits(name)) return true;
	}
	return false;
}

/// Whether a direct item of the dialog's layout belongs to the button row at its bottom: a button
/// box, or a row holding only buttons (and labels or stretches beside them).
bool isButtonRow(QLayoutItem* item) {
	if (QWidget* widget = item->widget()) return qobject_cast<QDialogButtonBox*>(widget) != nullptr;
	QLayout* row = item->layout();
	if (!row) return false;
	bool button = false;
	for (int i = 0; i < row->count(); ++i) {
		QLayoutItem* child = row->itemAt(i);
		if (child->spacerItem()) continue;
		QWidget* widget = child->widget();
		if (!widget) return false;
		if (qobject_cast<QAbstractButton*>(widget) || qobject_cast<QDialogButtonBox*>(widget)) {
			button = true;
		} else if (!qobject_cast<QLabel*>(widget)) {
			return false;
		}
	}
	return button;
}

/// Moves the dialog's layout into a scroll area; the button rows at its bottom stay below it.
void wrapInScrollArea(QDialog* dialog) {
	QLayout* top = dialog->layout();
	if (!top || dialog->findChild<QScrollArea*>("dialogFitScroll", Qt::FindDirectChildrenOnly)) return;
	// The button rows: for a box layout the trailing ones, for other layouts any button box in it.
	std::vector<QLayoutItem*> footer;
	if (auto* box = qobject_cast<QBoxLayout*>(top); box && (box->direction() == QBoxLayout::TopToBottom || box->direction() == QBoxLayout::BottomToTop)) {
		while (box->count() > 0) {
			QLayoutItem* last = box->itemAt(box->count() - 1);
			if (last->spacerItem() && footer.empty()) {
				delete box->takeAt(box->count() - 1);   // a stretch under the buttons
				continue;
			}
			if (!isButtonRow(last)) break;
			footer.insert(footer.begin(), box->takeAt(box->count() - 1));
		}
	} else {
		for (int i = top->count() - 1; i >= 0; --i) {
			if (QWidget* widget = top->itemAt(i)->widget(); widget && qobject_cast<QDialogButtonBox*>(widget)) {
				footer.insert(footer.begin(), top->takeAt(i));
			}
		}
	}
	const QMargins margins = top->contentsMargins();
	auto* content = new QWidget;
	content->setObjectName("dialogFitContent");
	content->setLayout(top);   // takes the layout (and its widgets) from the dialog
	auto* scroll = new QScrollArea;
	scroll->setObjectName("dialogFitScroll");
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setWidget(content);
	auto* outer = new QVBoxLayout;
	outer->setContentsMargins(0, 0, 0, footer.empty() ? 0 : margins.bottom());
	outer->setSpacing(footer.empty() ? 0 : 6);
	outer->addWidget(scroll, 1);
	for (QLayoutItem* item : footer) {
		auto* row = new QHBoxLayout;
		row->setContentsMargins(margins.left(), 0, margins.right(), 0);
		if (QWidget* widget = item->widget()) {
			row->addWidget(widget);
			delete item;
		} else if (QLayout* layout = item->layout()) {
			row->addLayout(layout);
		}
		outer->addLayout(row);
	}
	dialog->setLayout(outer);
	outer->activate();
}

/// Centres `size` on the parent window (when there is one) inside `area`.
QPoint centredIn(const QWidget* window, const QSize& size, const QRect& area) {
	QPoint centre = area.center();
	if (const QWidget* parent = window->parentWidget() ? window->parentWidget()->window() : nullptr; parent && parent->isVisible()) {
		const QPoint parentCentre = parent->frameGeometry().center();
		if (area.contains(parentCentre)) centre = parentCentre;
	}
	QRect rect(QPoint(0, 0), size);
	rect.moveCenter(centre);
	rect.moveLeft(std::clamp(rect.left(), area.left(), std::max(area.left(), area.right() - rect.width() + 1)));
	rect.moveTop(std::clamp(rect.top(), area.top(), std::max(area.top(), area.bottom() - rect.height() + 1)));
	return rect.topLeft();
}

} // namespace

QRect availableWindowArea(const QWidget* window) {
	if (g_testArea.isValid()) return g_testArea;
	const QWidget* anchor = window && window->parentWidget() ? window->parentWidget()->window() : window;
	const QScreen* screen = anchor ? anchor->screen() : nullptr;
	if (!screen) screen = QGuiApplication::primaryScreen();
	if (!screen) return QRect(0, 0, 1024, 700);
	// Room for the window frame and its title bar, which are outside a widget's geometry.
	return screen->availableGeometry().adjusted(8, 36, -8, -8);
}

void setAvailableWindowAreaForTests(const QRect& area) {
	g_testArea = area;
}

void fitDialogToScreen(QDialog* dialog) {
	if (!dialog || dialog->windowFlags().testFlag(Qt::Popup) || isQtStandardDialog(dialog)) return;
	const bool resizedByCode = dialog->testAttribute(Qt::WA_Resized);
	const QSize asked = dialog->size();
	wrapInScrollArea(dialog);
	const QRect area = availableWindowArea(dialog);
	QSize size = asked;
	if (auto* scroll = dialog->findChild<QScrollArea*>("dialogFitScroll"); scroll && scroll->widget()) {
		// What the content would like (its hint, never below its minimum), plus room for a scroll bar
		// and the button rows.
		QWidget* content = scroll->widget();
		content->layout()->activate();
		const QSize contentSize = content->sizeHint().expandedTo(content->minimumSizeHint());
		const int bar = dialog->style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, scroll);
		const QSize whole = dialog->layout()->sizeHint();
		const int footer = std::max(0, whole.height() - scroll->sizeHint().height());
		QSize wanted(std::max(contentSize.width() + bar, whole.width()), contentSize.height() + footer + 2);
		size = resizedByCode ? asked : wanted;   // a size the dialog asked for itself counts
	}
	// Never larger than the screen, and the minimum may not force it to be.
	dialog->setMinimumSize(std::min(dialog->minimumWidth(), area.width()), std::min(dialog->minimumHeight(), area.height()));
	size = size.boundedTo(area.size()).expandedTo(QSize(std::min(200, area.width()), std::min(120, area.height())));
	dialog->resize(size);
	dialog->move(centredIn(dialog, size, area));
	dialog->setProperty(FITTED, true);
}

void fitWindowToScreen(QWidget* window) {
	if (!window) return;
	const QRect area = availableWindowArea(window);
	window->setMinimumSize(std::min(window->minimumWidth(), area.width()), std::min(window->minimumHeight(), area.height()));
	const QSize size = window->size().boundedTo(area.size());
	if (size != window->size()) window->resize(size);
	if (!area.contains(QRect(window->pos(), size))) window->move(centredIn(window, size, area));
	window->setProperty(FITTED, true);
}

DialogFitter::DialogFitter(QObject* parent) : QObject(parent) {}

void DialogFitter::install() {
	if (qApp->property("smeDialogFitter").toBool()) return;
	qApp->installEventFilter(new DialogFitter(qApp));
	qApp->setProperty("smeDialogFitter", true);
}

bool DialogFitter::eventFilter(QObject* watched, QEvent* event) {
	if (event->type() == QEvent::Show && watched->isWidgetType()) {
		auto* widget = static_cast<QWidget*>(watched);
		if (widget->isWindow() && !widget->property(FITTED).toBool() && !widget->property("smeNoFit").toBool()) {
			if (auto* dialog = qobject_cast<QDialog*>(widget)) {
				fitDialogToScreen(dialog);
			} else if (qobject_cast<QMainWindow*>(widget)) {
				fitWindowToScreen(widget);
			}
		}
	}
	return QObject::eventFilter(watched, event);
}

} // namespace sme::app
