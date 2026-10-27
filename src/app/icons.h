// icons.h - The editor's icons, drawn with QPainter (no image files, no icon font): coloured
// ones for the side bar, calmer line icons for tools and tool bars.

#pragma once

#include <QIcon>
#include <QString>

namespace sme::app {

/// Icon by name, e.g. "page:map", "tool:select", "undo", "run". Unknown names give an empty icon.
/// `shortcut` (one or two characters) is drawn small in the lower right corner.
[[nodiscard]] QIcon icon(const QString& name, const QString& shortcut = {});

/// Mapsmith's logo in the usual sizes (window icon; also written as the .ico of the Windows program).
[[nodiscard]] QIcon appIcon();

} // namespace sme::app
