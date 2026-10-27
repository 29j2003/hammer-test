// style.h - Dark editor theme (Fusion + palette + a small style sheet).

#pragma once

class QApplication;

namespace sme::app {

/// Applies the dark theme to the whole application (call once at start).
void applyDarkStyle(QApplication& app);

} // namespace sme::app
