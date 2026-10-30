// plural_text.cpp - English singular/plural for counted texts (see plural_text.h).

#include "plural_text.h"

#include <cstring>

#include <sme/text.h>

namespace sme::app {

QString PluralTranslator::translate(const char* /*context*/, const char* sourceText, const char* /*disambiguation*/, int n) const {
	if (n < 0 || !sourceText || !std::strstr(sourceText, "%n")) return {};
	const std::string resolved = englishPlural(sourceText, n);
	if (resolved == sourceText) return {};   // nothing to resolve: Qt keeps the text as written
	return QString::fromUtf8(resolved.data(), qsizetype(resolved.size()));
}

} // namespace sme::app
