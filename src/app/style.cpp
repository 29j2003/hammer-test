// style.cpp - Dark editor theme (see style.h).

#include "style.h"

#include <QApplication>
#include <QPalette>
#include <QStyleFactory>

namespace sme::app {

void applyDarkStyle(QApplication& app) {
	app.setStyle(QStyleFactory::create("Fusion"));
	QPalette p;
	const QColor window(37, 39, 43), base(28, 30, 33), text(222, 224, 228), accent(232, 140, 40);
	p.setColor(QPalette::Window, window);
	p.setColor(QPalette::WindowText, text);
	p.setColor(QPalette::Base, base);
	p.setColor(QPalette::AlternateBase, QColor(33, 35, 39));
	p.setColor(QPalette::ToolTipBase, QColor(50, 52, 58));
	p.setColor(QPalette::ToolTipText, text);
	p.setColor(QPalette::Text, text);
	p.setColor(QPalette::Button, QColor(48, 51, 56));
	p.setColor(QPalette::ButtonText, text);
	p.setColor(QPalette::Highlight, accent);
	p.setColor(QPalette::HighlightedText, QColor(20, 20, 20));
	p.setColor(QPalette::Link, accent);
	p.setColor(QPalette::PlaceholderText, QColor(130, 134, 142));
	p.setColor(QPalette::Disabled, QPalette::Text, QColor(110, 112, 118));
	p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(110, 112, 118));
	app.setPalette(p);
	app.setStyleSheet(R"(
QToolTip { border: 1px solid #555a63; padding: 4px; }
QDockWidget::title { background: #2c2f34; padding: 5px 8px; font-weight: bold; }
QToolBar { background: #2a2c31; border: none; spacing: 2px; padding: 2px; }
QToolBar QToolButton { padding: 4px 6px; border-radius: 3px; }
QToolBar QToolButton:checked { background: #e88c28; color: #1a1a1a; }
QToolBar QToolButton:hover { background: #3c4047; }
QGroupBox { border: 1px solid #3a3d44; border-radius: 4px; margin-top: 14px; padding-top: 6px; }
QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; color: #e8a050; font-weight: bold; }
QLabel[role="description"] { color: #9399a3; font-size: 8.5pt; }
QLabel[role="key"] { color: #e3e5e8; font-weight: 600; margin-top: 2px; }
QLabel[role="rowLabel"] { color: #c4c8ce; }
QPushButton[role="groupHeader"] { text-align: left; font-weight: 600; color: #e3e5e8; background: transparent; border: none; border-bottom: 1px solid #34373d; border-radius: 0; padding: 5px 2px 4px 2px; margin-top: 4px; }
QPushButton[role="groupHeader"]:hover { color: #ffffff; background: #2c2f34; }
QLabel[role="help"] { background: #24262a; border-top: 1px solid #34373d; color: #b8bdc5; }
QLabel[role="warning"] { color: #ffb050; }
QComboBox[problem="true"], QLineEdit[problem="true"] { border: 1px solid #d04040; }
QPushButton { padding: 4px 10px; border: 1px solid #4a4e56; border-radius: 3px; background: #383b41; }
QPushButton:hover { background: #43474e; }
QPushButton[accent="true"] { background: #e88c28; color: #1a1a1a; border-color: #e88c28; font-weight: bold; }
QTabBar::tab { padding: 5px 12px; background: #2c2f34; border: none; }
QTabBar::tab:selected { background: #3a3d44; color: #f0a050; }
QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox { padding: 2px 4px; border: 1px solid #44484f; border-radius: 3px; background: #1d1f22; }
QStatusBar { background: #2a2c31; }
QFrame#featureHeader { background: #2c2f34; border-radius: 3px; }
QFrame#featureHeader QCheckBox::indicator { width: 13px; height: 13px; border: 1px solid #6a707a; border-radius: 3px; background: #1d1f22; }
QFrame#featureHeader QCheckBox::indicator:checked { background: #e88c28; border-color: #e88c28; }
QToolBar#Pages { background: #1b1c1f; border-right: 1px solid #34373d; padding: 4px 2px; spacing: 4px; }
QToolBar#Pages QToolButton { min-width: 58px; padding: 7px 4px; color: #aab0b9; border-left: 3px solid transparent; border-radius: 0; }
QToolBar#Pages QToolButton:hover { background: #2a2c31; color: #e3e5e8; }
QToolBar#Pages QToolButton:checked { background: #2c2f34; color: #f0a050; border-left: 3px solid #e88c28; }
QLabel#pageTitle { font-size: 15pt; font-weight: 600; color: #f0f1f3; }
QPushButton[accent="true"]:disabled { background: #4b3c2c; color: #8f8272; border-color: #4b3c2c; }
QFrame[role="card"] { background: #2b2e33; border: 1px solid #3a3d44; border-radius: 4px; }
)");
}

} // namespace sme::app
