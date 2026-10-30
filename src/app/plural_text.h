// plural_text.h - English singular/plural for counted texts ("%n file(s)" -> "1 file", "3 files").
//
// Mapsmith has no translations, so Qt shows tr("%n file(s)", nullptr, n) as written. This
// translator, installed once on the application, resolves such texts with sme::englishPlural.

#pragma once

#include <QTranslator>

namespace sme::app {

/// Answers only counted texts (n >= 0) whose noun is written "word(s)", "word(es)" or "one/many".
class PluralTranslator : public QTranslator {
public:
	using QTranslator::QTranslator;

	[[nodiscard]] bool isEmpty() const override { return false; }
	[[nodiscard]] QString translate(const char* context, const char* sourceText, const char* disambiguation = nullptr, int n = -1) const override;
};

} // namespace sme::app
