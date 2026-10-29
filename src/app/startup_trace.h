// startup_trace.h - Milestones of the start (how long until the window can be used).
//
// With the environment variable SME_STARTUP_TRACE set, each milestone (games found, window built,
// game read, first frame ...) is written to stderr with the milliseconds since the start; with
// SME_STARTUP_TRACE=quit the program also quits once the first frame is drawn (for measuring).
// Without it a mark costs one clock read. The marks also stay readable (tests guard the time).

#pragma once

#include <QList>
#include <QPair>
#include <QString>

namespace sme::app {

/// Starts the clock (the first call wins; main() calls it first).
void startupBegin();
/// Records a milestone (only its first occurrence counts).
void startupMark(const char* step);
/// Milliseconds since startupBegin() at `step`, or -1 if it has not happened.
[[nodiscard]] qint64 startupTime(const char* step);
/// Every milestone so far with its time.
[[nodiscard]] QList<QPair<QString, qint64>> startupMarks();

} // namespace sme::app
