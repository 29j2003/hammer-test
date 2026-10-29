// ui_helpers.h - Small helpers many panels share: text from the core, labels in the roles of the
// app's style sheet (style.cpp), a readable width for text-heavy pages and a "Details" fold-out.

#pragma once

#include <QEvent>
#include <QImage>
#include <QLabel>
#include <QLayout>
#include <QString>
#include <QTextDocument>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
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

/// Keeps the content of a page at most `maxWidth` wide, centred: long text lines and wide cards are
/// hard to read on big screens. Works on the widget's own layout (its side margins grow).
class ReadableWidth : public QObject {
public:
	ReadableWidth(QWidget* page, int maxWidth) : QObject(page), m_page(page), m_maxWidth(maxWidth) {
		if (page->layout()) m_base = page->layout()->contentsMargins();
		page->installEventFilter(this);
	}

protected:
	bool eventFilter(QObject* watched, QEvent* event) override {
		if (watched == m_page && event->type() == QEvent::Resize && m_page->layout()) {
			const int extra = std::max(0, m_page->width() - m_maxWidth - m_base.left() - m_base.right());
			const QMargins wanted(m_base.left() + extra / 2, m_base.top(), m_base.right() + extra - extra / 2, m_base.bottom());
			if (m_page->layout()->contentsMargins() != wanted) m_page->layout()->setContentsMargins(wanted);
		}
		return false;
	}

private:
	QWidget* m_page;
	int m_maxWidth;
	QMargins m_base;
};

/// Limits the content of `page` (its layout) to a readable width (see ReadableWidth).
inline void limitReadableWidth(QWidget* page, int maxWidth = 1100) {
	new ReadableWidth(page, maxWidth);
}

/// A fold-out: a small "▸ Details" button that shows and hides `content` (closed at first unless
/// `open`). The button is named "<content's object name>Toggle" for tests.
inline QWidget* detailsFoldOut(QWidget* content, const QString& title, bool open = false) {
	auto* box = new QWidget;
	auto* v = new QVBoxLayout(box);
	v->setContentsMargins(0, 0, 0, 0);
	v->setSpacing(4);
	auto* toggle = new QToolButton(box);
	toggle->setObjectName(content->objectName() + "Toggle");
	toggle->setText(title);
	toggle->setCheckable(true);
	toggle->setChecked(open);
	toggle->setAutoRaise(true);
	toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	toggle->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
	v->addWidget(toggle, 0, Qt::AlignLeft);
	content->setParent(box);
	v->addWidget(content, 1);
	content->setVisible(open);
	QObject::connect(toggle, &QToolButton::toggled, box, [toggle, content](bool on) {
		toggle->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
		content->setVisible(on);
	});
	return box;
}

/// Opens the fold-out around a log (detailsFoldOut) as soon as text arrives in it.
template <typename TextEdit>
void openFoldOutOnText(TextEdit* log) {
	QObject::connect(log, &TextEdit::textChanged, log, [log] {
		if (log->document()->isEmpty()) return;
		auto* toggle = log->parentWidget() ? log->parentWidget()->template findChild<QToolButton*>(log->objectName() + "Toggle") : nullptr;
		if (toggle && !toggle->isChecked()) toggle->setChecked(true);
	});
}

} // namespace sme::app
