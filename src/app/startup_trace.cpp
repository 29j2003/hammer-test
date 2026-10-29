// startup_trace.cpp - Start milestones (see startup_trace.h).

#include "startup_trace.h"

#include <QElapsedTimer>

#include <cstdio>

namespace sme::app {

namespace {

QElapsedTimer& clock() {
	static QElapsedTimer timer;
	return timer;
}

QList<QPair<QString, qint64>>& marks() {
	static QList<QPair<QString, qint64>> list;
	return list;
}

} // namespace

void startupBegin() {
	if (!clock().isValid()) clock().start();
}

void startupMark(const char* step) {
	startupBegin();
	const QString name = QString::fromLatin1(step);
	for (const auto& mark : std::as_const(marks())) {
		if (mark.first == name) return;
	}
	const qint64 ms = clock().elapsed();
	marks().append({name, ms});
	static const bool print = qEnvironmentVariableIsSet("SME_STARTUP_TRACE");
	if (print) std::fprintf(stderr, "startup %6lld ms  %s\n", static_cast<long long>(ms), step);
}

qint64 startupTime(const char* step) {
	const QString name = QString::fromLatin1(step);
	for (const auto& mark : std::as_const(marks())) {
		if (mark.first == name) return mark.second;
	}
	return -1;
}

QList<QPair<QString, qint64>> startupMarks() {
	return marks();
}

} // namespace sme::app
