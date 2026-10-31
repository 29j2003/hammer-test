// style.cpp - Dark editor theme (see style.h).

#include "style.h"

#include <QApplication>
#include <QPalette>
#include <QStyleFactory>

namespace sme::app {

void applyDarkStyle(QApplication& app) {
	app.setStyle(QStyleFactory::create("Fusion"));
	QPalette p;
	// Neutral greys and one orange.
	const QColor window(38, 38, 38), base(30, 30, 30), text(228, 228, 228), accent(245, 165, 36);
	p.setColor(QPalette::Window, window);
	p.setColor(QPalette::WindowText, text);
	p.setColor(QPalette::Base, base);
	p.setColor(QPalette::AlternateBase, QColor(36, 36, 36));
	p.setColor(QPalette::ToolTipBase, QColor(48, 48, 48));
	p.setColor(QPalette::ToolTipText, text);
	p.setColor(QPalette::Text, text);
	p.setColor(QPalette::Button, QColor(51, 51, 51));
	p.setColor(QPalette::ButtonText, text);
	p.setColor(QPalette::Highlight, accent);
	p.setColor(QPalette::HighlightedText, QColor(26, 26, 26));
	p.setColor(QPalette::Link, accent);
	p.setColor(QPalette::PlaceholderText, QColor(128, 128, 128));
	p.setColor(QPalette::Disabled, QPalette::Text, QColor(112, 112, 112));
	p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(112, 112, 112));
	p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(112, 112, 112));
	app.setPalette(p);
	app.setStyleSheet(R"(
QMainWindow { background: #1f1f1f; }
QMainWindow::separator { background: #1b1b1b; width: 5px; height: 5px; }
QMainWindow::separator:hover { background: #f5a524; }
QSplitter::handle { background: #1b1b1b; }
QToolTip { background: #303030; color: #e8e8e8; border: 1px solid #484848; padding: 5px; border-radius: 4px; }

QMenuBar { background: #1f1f1f; padding: 2px; }
QMenuBar::item { padding: 4px 10px; border-radius: 4px; }
QMenuBar::item:selected { background: #333333; }
QMenu { background: #2a2a2a; border: 1px solid #3a3a3a; padding: 4px; }
QMenu::item { padding: 5px 26px 5px 22px; border-radius: 4px; }
QMenu::item:selected { background: #3b3b3b; color: #ffffff; }
QMenu::item:disabled { color: #6e6e6e; }
QMenu::separator { height: 1px; background: #3a3a3a; margin: 4px 8px; }
QMenu::section { color: #9a9a9a; }

QDockWidget { color: #d8d8d8; }
QLabel#dockTitle { background: #262626; padding: 7px 10px; font-weight: 600; color: #e0e0e0; border-bottom: 1px solid #323232; }
QWidget#dockHeader { background: #262626; border-bottom: 1px solid #323232; }
QWidget#dockHeader QLabel#dockTitle { border: none; }
QToolButton#dockMenuButton { padding: 2px 8px; color: #a6a6a6; font-weight: 600; }
QDockWidget::title { background: #262626; padding: 6px 10px; text-align: left; border-bottom: 1px solid #323232; }

QToolBar { background: #262626; border: none; spacing: 3px; padding: 3px 6px; }
QToolBar::separator { background: #3a3a3a; width: 1px; height: 1px; margin: 4px 6px; }
QToolBar#Main { border-bottom: 1px solid #303030; }
QToolBar QToolButton { padding: 4px 8px; border: 1px solid transparent; border-radius: 5px; color: #dcdcdc; }
QToolBar QToolButton:hover { background: #333333; }
QToolBar QToolButton:checked { background: #383838; border-color: #4a4a4a; color: #ffffff; }
QToolBar QToolButton[iconOnly="true"] { padding: 4px; }
QToolBar QToolButton[segment="first"] { border-top-right-radius: 0; border-bottom-right-radius: 0; background: #2e2e2e; border-color: #3a3a3a; }
QToolBar QToolButton[segment="last"] { border-top-left-radius: 0; border-bottom-left-radius: 0; background: #2e2e2e; border-color: #3a3a3a; margin-left: -4px; }
QToolBar QToolButton[segment="first"]:checked, QToolBar QToolButton[segment="last"]:checked { background: #444444; color: #f5b541; }
QToolBar QToolButton[chooser="true"] { background: #2d2d2d; border: 1px solid #5c5c5c; border-radius: 6px; font-weight: 600; padding: 4px 12px; }
QToolBar QToolButton[chooser="true"]:hover { border-color: #f5a524; }
QToolBar QToolButton[accent="true"] { background: #f5a524; border-color: #f5a524; color: #1b1b1b; font-weight: 600; padding: 4px 14px; }
QToolBar QToolButton[accent="true"]:hover { background: #ffb53f; }
QToolBar#Tools { background: #232323; padding: 6px 5px; spacing: 4px; border-right: 1px solid #2e2e2e; }
QToolBar#Tools QToolButton { padding: 5px; border-radius: 7px; }
QToolBar#Tools QToolButton:checked { background: #f5a524; border-color: #f5a524; }
QToolBar#Pages { background: #1b1b1b; padding: 8px 0; spacing: 2px; border-right: 1px solid #2a2a2a; }
QToolBar#Pages QToolButton { min-width: 66px; padding: 8px 2px 7px 2px; color: #b4b4b4; border: none; border-left: 3px solid transparent; border-radius: 0; }
QToolBar#Pages QToolButton:hover { background: #262626; color: #e8e8e8; }
QToolBar#Pages QToolButton:checked { background: #2a2a2a; color: #ffffff; border-left: 3px solid #f5a524; }

QTabBar::tab { padding: 6px 11px; background: transparent; border: none; border-bottom: 2px solid transparent; color: #a6a6a6; }
QTabBar::tab:hover { color: #e4e4e4; }
QTabBar::tab:selected { color: #f2f2f2; border-bottom: 2px solid #f5a524; }
QTabWidget::pane { border: none; }
QTabBar { qproperty-drawBase: 0; }

QGroupBox { background: #2a2a2a; border: 1px solid #363636; border-radius: 7px; margin-top: 20px; padding: 10px 8px 8px 8px; }
QGroupBox::title { subcontrol-origin: margin; left: 6px; padding: 0 2px; color: #e3a64f; font-weight: 600; }
QLabel[role="heading"] { font-weight: 600; font-size: 14px; color: #f0f0f0; margin-top: 6px; }
QFrame[role="card"] { background: #2a2a2a; border: 1px solid #363636; border-radius: 7px; }
QFrame[role="card"][highlight="true"] { border: 1px solid #5ccf7a; }

QPushButton { padding: 5px 12px; border: 1px solid #3e3e3e; border-radius: 5px; background: #343434; color: #e6e6e6; }
QPushButton:hover { background: #3e3e3e; border-color: #4c4c4c; }
QPushButton:pressed { background: #2a2a2a; }
QPushButton:disabled { color: #727272; background: #2c2c2c; border-color: #343434; }
QPushButton[role="action"] { text-align: left; padding: 6px 10px; }
QPushButton[accent="true"] { background: #f5a524; color: #1b1b1b; border-color: #f5a524; font-weight: 600; }
QPushButton[accent="true"]:hover { background: #ffb53f; }
QPushButton[accent="true"]:disabled { background: #4b3c2c; color: #8f8272; border-color: #4b3c2c; }
QPushButton[role="groupHeader"] { text-align: left; font-weight: 600; color: #e6e6e6; background: transparent; border: none; border-bottom: 1px solid #343434; border-radius: 0; padding: 6px 2px 5px 2px; margin-top: 4px; }
QPushButton[role="groupHeader"]:hover { color: #ffffff; background: #2c2c2c; }

QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox { padding: 4px 6px; border: 1px solid #3b3b3b; border-radius: 5px; background: #1e1e1e; selection-background-color: #f5a524; selection-color: #1b1b1b; }
QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus { border-color: #b8801f; }
QSpinBox, QDoubleSpinBox { padding-right: 20px; }
QAbstractSpinBox QLineEdit { border: none; border-radius: 0; background: transparent; padding: 0; }
QSpinBox::up-button, QDoubleSpinBox::up-button { subcontrol-origin: padding; subcontrol-position: top right; width: 18px; border: none; border-left: 1px solid #333333; border-top-right-radius: 5px; background: #262626; }
QSpinBox::down-button, QDoubleSpinBox::down-button { subcontrol-origin: padding; subcontrol-position: bottom right; width: 18px; border: none; border-left: 1px solid #333333; border-top: 1px solid #2e2e2e; border-bottom-right-radius: 5px; background: #262626; }
QSpinBox::up-button:hover, QDoubleSpinBox::up-button:hover, QSpinBox::down-button:hover, QDoubleSpinBox::down-button:hover { background: #343434; }
QSpinBox::up-button:pressed, QDoubleSpinBox::up-button:pressed, QSpinBox::down-button:pressed, QDoubleSpinBox::down-button:pressed { background: #3e3e3e; }
QSpinBox::up-arrow, QDoubleSpinBox::up-arrow { image: url(:/ui/chevron-up.png); width: 9px; height: 5px; }
QSpinBox::down-arrow, QDoubleSpinBox::down-arrow { image: url(:/ui/chevron-down.png); width: 9px; height: 5px; }
QSpinBox::up-arrow:hover, QDoubleSpinBox::up-arrow:hover { image: url(:/ui/chevron-up-hover.png); }
QSpinBox::down-arrow:hover, QDoubleSpinBox::down-arrow:hover { image: url(:/ui/chevron-down-hover.png); }
QSpinBox::up-arrow:disabled, QDoubleSpinBox::up-arrow:disabled, QSpinBox::up-arrow:off, QDoubleSpinBox::up-arrow:off { image: url(:/ui/chevron-up-disabled.png); }
QSpinBox::down-arrow:disabled, QDoubleSpinBox::down-arrow:disabled, QSpinBox::down-arrow:off, QDoubleSpinBox::down-arrow:off { image: url(:/ui/chevron-down-disabled.png); }
QComboBox::drop-down { border: none; border-left: 1px solid #333333; width: 20px; }
QComboBox::down-arrow { image: url(:/ui/chevron-down.png); width: 10px; height: 6px; }
QComboBox::down-arrow:hover, QComboBox::down-arrow:on { image: url(:/ui/chevron-down-hover.png); }
QComboBox QAbstractItemView { background: #2a2a2a; border: 1px solid #3a3a3a; selection-background-color: #3e3e3e; selection-color: #ffffff; }
QComboBox[problem="true"], QLineEdit[problem="true"] { border: 1px solid #d04040; }

QTreeView, QListView, QTableView { background: #222222; border: 1px solid #2f2f2f; border-radius: 6px; alternate-background-color: #262626; }
QTreeView::item, QListView::item { padding: 2px 0; }
QTreeView::item:hover, QListView::item:hover { background: #2c2c2c; }
QTreeView::item:selected, QListView::item:selected, QTableView::item:selected { background: #46392a; color: #ffd38f; }
QHeaderView::section { background: #262626; color: #a2a2a2; border: none; border-bottom: 1px solid #333333; padding: 5px 6px; font-weight: 600; }
QTableWidget#toolKeys { background: transparent; border: none; }

QScrollBar:vertical { background: transparent; width: 10px; margin: 0; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 0; }
QScrollBar::handle { background: #3d3d3d; border-radius: 4px; min-height: 24px; min-width: 24px; }
QScrollBar::handle:hover { background: #4d4d4d; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

QStatusBar { background: #1c1c1c; color: #a6a6a6; border-top: 1px solid #2c2c2c; }
QStatusBar QLabel { color: #a6a6a6; }
QStatusBar::item { border: none; }

QLabel[role="description"] { color: #929292; font-size: 8.5pt; }
QLabel[role="key"] { color: #e6e6e6; font-weight: 600; margin-top: 2px; }
QLabel[role="rowLabel"] { color: #c6c6c6; }
QLabel[role="help"] { background: #232323; border-top: 1px solid #333333; color: #b8b8b8; }
QLabel[role="warning"] { color: #ffb050; }
QLabel#pageTitle { font-size: 16pt; font-weight: 600; color: #f4f4f4; }
QLabel#entityClass { font-size: 11pt; font-weight: 600; color: #f0f0f0; }
QFrame#featureHeader { background: #2e2e2e; border-radius: 5px; }
QFrame#featureHeader QCheckBox::indicator { width: 13px; height: 13px; border: 1px solid #6a6a6a; border-radius: 3px; background: #1e1e1e; }
QFrame#featureHeader QCheckBox::indicator:checked { background: #f5a524; border-color: #f5a524; }
QGroupBox#displaceTools QPushButton:checked { background: #f5a524; border-color: #f5a524; color: #1a1a1a; font-weight: 600; }
QSlider::groove:horizontal { height: 4px; background: #3a3a3a; border-radius: 2px; }
QSlider::sub-page:horizontal { background: #f5a524; border-radius: 2px; }
QSlider::handle:horizontal { background: #e8e8e8; width: 12px; height: 12px; margin: -5px 0; border-radius: 6px; }
)");
}

} // namespace sme::app
