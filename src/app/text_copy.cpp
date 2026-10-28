// text_copy.cpp - Copying lists and output as text (see text_copy.h).

#include "text_copy.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QKeySequence>
#include <QMenu>
#include <QPlainTextEdit>
#include <QShortcut>
#include <QTreeWidget>

#include <functional>

namespace sme::app {

namespace {

void appendItem(const QTreeWidgetItem* item, int depth, QStringList& lines) {
	QStringList columns;
	for (int c = 0; c < item->columnCount(); ++c) {
		if (!item->text(c).isEmpty()) columns << item->text(c);
	}
	lines << QString(depth * 4, ' ') + columns.join(QStringLiteral("  "));
	for (int i = 0; i < item->childCount(); ++i) appendItem(item->child(i), depth + 1, lines);
}

} // namespace

QString treeText(const QTreeWidget* tree, bool selectedOnly) {
	QStringList lines;
	for (int i = 0; i < tree->topLevelItemCount(); ++i) {
		const QTreeWidgetItem* item = tree->topLevelItem(i);
		if (selectedOnly && !item->isSelected()) {
			for (int c = 0; c < item->childCount(); ++c) {
				if (item->child(c)->isSelected()) appendItem(item->child(c), 0, lines);
			}
			continue;
		}
		appendItem(item, 0, lines);
	}
	return lines.join('\n');
}

void makeTreeCopyable(QTreeWidget* tree) {
	tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
	auto* copy = new QShortcut(QKeySequence::Copy, tree, nullptr, nullptr, Qt::WidgetWithChildrenShortcut);
	QObject::connect(copy, &QShortcut::activated, tree, [tree] {
		const QString text = treeText(tree, true);
		QApplication::clipboard()->setText(text.isEmpty() ? treeText(tree, false) : text);
	});
	tree->setContextMenuPolicy(Qt::CustomContextMenu);
	QObject::connect(tree, &QWidget::customContextMenuRequested, tree, [tree](const QPoint& pos) {
		QMenu menu;
		QAction* copySelected = menu.addAction(QObject::tr("Copy"), tree, [tree] { QApplication::clipboard()->setText(treeText(tree, true)); });
		copySelected->setShortcut(QKeySequence::Copy);
		copySelected->setEnabled(!tree->selectedItems().isEmpty());
		menu.addAction(QObject::tr("Copy all"), tree, [tree] { QApplication::clipboard()->setText(treeText(tree, false)); });
		menu.exec(tree->viewport()->mapToGlobal(pos));
	});
}

void addCopyAll(QPlainTextEdit* text) {
	text->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
	text->setContextMenuPolicy(Qt::CustomContextMenu);
	QObject::connect(text, &QWidget::customContextMenuRequested, text, [text](const QPoint& pos) {
		QMenu* menu = text->createStandardContextMenu(pos);
		menu->addSeparator();
		menu->addAction(QObject::tr("Copy all"), text, [text] { QApplication::clipboard()->setText(text->toPlainText()); });
		menu->exec(text->viewport()->mapToGlobal(pos));
		delete menu;
	});
}

} // namespace sme::app
