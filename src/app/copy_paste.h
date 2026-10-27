// copy_paste.h - Ctrl+C / Ctrl+V helpers: copying names out of the browsers (materials, models,
// sounds, particles, entity classes) and reading key/value text pasted from wikis or other maps.

#pragma once

#include <QKeySequence>
#include <QString>

#include <functional>

#include <utility>
#include <vector>

class QAbstractItemView;
class QWidget;

namespace sme::app {

/// Ctrl+C in the view copies the current item's name (`role` of column 0; Qt::DisplayRole for
/// the shown text). Items without a name (group rows) copy nothing.
void addCopyShortcut(QAbstractItemView* view, int role);

/// Runs `action` when `key` (Copy, Paste, ...) is pressed while `widget` has the focus. Item views
/// handle Ctrl+C/Ctrl+V themselves before any QShortcut sees them; this filter comes first.
void onStandardKey(QWidget* widget, QKeySequence::StandardKey key, std::function<void()> action);

/// Key/value lines as wikis and VMF files write them: `"key" "value"`, `key<Tab>value`,
/// `key = value`, `key: value` or `key value`. Lines that are no pair are skipped.
[[nodiscard]] std::vector<std::pair<QString, QString>> parseKeyValueLines(const QString& text);

} // namespace sme::app
