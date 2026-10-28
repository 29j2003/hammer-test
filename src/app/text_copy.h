// text_copy.h - Copying what Mapsmith shows as text: compile errors, the log, problems. Every
// list and output gets Ctrl+C, a right-click "Copy" / "Copy all", so a message can go into a
// search or a bug report as it is.

#pragma once

#include <QString>

class QPlainTextEdit;
class QTreeWidget;
class QWidget;

namespace sme::app {

/// The tree's lines as text (an item, then its explanation lines indented); only the selected
/// items (with their children) when `selectedOnly`.
[[nodiscard]] QString treeText(const QTreeWidget* tree, bool selectedOnly);

/// Ctrl+C copies the selected lines, the right-click menu has Copy and Copy all; several lines
/// can be selected.
void makeTreeCopyable(QTreeWidget* tree);

/// Adds "Copy all" to the text's right-click menu (Copy and Select all are there already).
void addCopyAll(QPlainTextEdit* text);

} // namespace sme::app
