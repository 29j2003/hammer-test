// ui_helpers.h - Small helpers many panels share: text from the core and labels in the roles of
// the app's style sheet (style.cpp).

#pragma once

#include <QImage>
#include <QLabel>
#include <QString>

#include <cstring>
#include <string_view>

#include <sme/texture.h>

namespace sme::app {

/// QString from the core's UTF-8 text (std::string, string_view, literals).
inline QString qs(std::string_view text) {
	return QString::fromUtf8(text.data(), qsizetype(text.size()));
}

/// A QImage (RGBA) of the core's pixels; a null image when the pixels are fewer than the size says.
inline QImage toQImage(const ImageRgba& image) {
	const std::size_t row = std::size_t(image.width) * 4;
	if (image.width == 0 || image.height == 0 || image.pixels.size() < row * image.height) return {};
	QImage out(int(image.width), int(image.height), QImage::Format_RGBA8888);
	for (int y = 0; y < int(image.height); ++y) std::memcpy(out.scanLine(y), image.pixels.data() + std::size_t(y) * row, row);
	return out;
}

/// A word-wrapped label in a role of the style sheet ("description": the grey help text).
inline QLabel* noteLabel(const QString& text, const char* role = "description") {
	auto* label = new QLabel(text);
	label->setWordWrap(true);
	label->setProperty("role", role);
	return label;
}

/// A small heading in capitals ("caps" role, or another role of the style sheet).
inline QLabel* capsLabel(const QString& text, const char* role = "caps") {
	auto* label = new QLabel(text.toUpper());
	label->setProperty("role", role);
	return label;
}

} // namespace sme::app
