// hud_editor.cpp - Project > HUD (see hud_editor.h).

#include "hud_editor.h"

#include "editor.h"
#include "lazy_refresh.h"
#include "ui_helpers.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLinearGradient>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QSpinBox>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include <sme/game.h>
#include <sme/text.h>

namespace sme::app {

namespace {

constexpr int HANDLE = 8;   // Resize grip at an element's bottom-right corner (widget pixels)

/// Resolutions offered (most common first).
const QList<QSize> RESOLUTIONS = {{1920, 1080}, {2560, 1440}, {1600, 900}, {1366, 768}, {1280, 720}, {1280, 1024}, {1024, 768}, {800, 600}};

QLabel* header(const QString& text) {
	auto* label = new QLabel(text.toUpper());
	label->setProperty("role", "sectionHeader");
	label->setStyleSheet("color: #8d949f; font-size: 8pt; font-weight: 600; letter-spacing: 1px; margin-top: 6px;");
	return label;
}

QColor toQColor(const HudColor& c) {
	return QColor(c[0], c[1], c[2], c[3]);
}

HudColor fromQColor(const QColor& c) {
	return {c.red(), c.green(), c.blue(), c.alpha()};
}

/// A small swatch icon for lists and buttons (checker under transparent colours).
QIcon swatch(const QColor& color) {
	QPixmap pm(18, 14);
	pm.fill(Qt::transparent);
	QPainter p(&pm);
	for (int y = 0; y < 14; y += 4) {
		for (int x = 0; x < 18; x += 4) p.fillRect(x, y, 4, 4, ((x + y) / 4) % 2 ? QColor(90, 90, 90) : QColor(150, 150, 150));
	}
	p.fillRect(pm.rect(), color);
	p.setPen(QColor(20, 20, 20));
	p.drawRect(pm.rect().adjusted(0, 0, -1, -1));
	return QIcon(pm);
}

/// The fonts the HUD uses, listed first in the Fonts tab.
bool hudFont(std::string_view name) {
	return startsWithI(name, "Hud") || startsWithI(name, "Weapon") || iequals(name, "Default") || iequals(name, "Crosshairs") ||
	       startsWithI(name, "CloseCaption") || startsWithI(name, "Squad");
}

/// Numbers in a .res value ("50", "-1").
int resInt(const ResNode* block, std::string_view key, int fallback) {
	if (!block) return fallback;
	const std::string v(trim(block->get(key)));
	if (v.empty()) return fallback;
	return std::atoi(v.c_str());
}

} // namespace

// --- HudPreview -------------------------------------------------------------------------------

HudPreview::HudPreview(QWidget* parent) : QWidget(parent) {
	setObjectName("hudPreview");
	setMouseTracking(true);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void HudPreview::setScreen(QSize size) {
	m_screen = size.isValid() ? size : QSize(1920, 1080);
	update();
}

void HudPreview::setItems(std::vector<HudPreviewItem> items) {
	m_items = std::move(items);
	update();
}

void HudPreview::setSelected(const QString& name) {
	m_selected = name;
	update();
}

void HudPreview::setBackground(const QImage& image) {
	m_background = image;
	update();
}

void HudPreview::setShowOutlines(bool on) {
	m_outlines = on;
	update();
}

QRectF HudPreview::screenArea() const {
	const QRectF area = QRectF(rect()).adjusted(4, 4, -4, -4);
	if (m_screen.isEmpty() || area.isEmpty()) return area;
	const double s = std::min(area.width() / m_screen.width(), area.height() / m_screen.height());
	const QSizeF size(m_screen.width() * s, m_screen.height() * s);
	return QRectF(area.center() - QPointF(size.width() / 2, size.height() / 2), size);
}

double HudPreview::scale() const {
	return m_screen.width() > 0 ? screenArea().width() / m_screen.width() : 1.0;
}

QRect HudPreview::widgetRect(const QString& name) const {
	for (const auto& item : m_items) {
		if (item.name.compare(name, Qt::CaseInsensitive) == 0) {
			const QRectF area = screenArea();
			const double s = scale();
			return QRectF(area.left() + item.rect.x() * s, area.top() + item.rect.y() * s, item.rect.width() * s, item.rect.height() * s).toRect();
		}
	}
	return {};
}

HudPreviewItem* HudPreview::item(const QString& name) {
	for (auto& i : m_items) {
		if (i.name.compare(name, Qt::CaseInsensitive) == 0) return &i;
	}
	return nullptr;
}

void HudPreview::paintEvent(QPaintEvent*) {
	QPainter p(this);
	p.fillRect(rect(), QColor(24, 26, 30));
	const QRectF area = screenArea();
	p.save();
	p.setClipRect(area);
	if (!m_background.isNull()) {
		// Cover the screen, cut what sticks out (like a screenshot at another aspect).
		const QSizeF img = QSizeF(m_background.size()).scaled(area.size(), Qt::KeepAspectRatioByExpanding);
		p.drawImage(QRectF(area.center() - QPointF(img.width() / 2, img.height() / 2), img), m_background);
	} else {
		// A game-like scene: sky, a horizon, a dim floor.
		QLinearGradient sky(area.topLeft(), QPointF(area.left(), area.center().y()));
		sky.setColorAt(0, QColor(70, 86, 104));
		sky.setColorAt(1, QColor(150, 158, 160));
		p.fillRect(QRectF(area.left(), area.top(), area.width(), area.height() * 0.55), sky);
		QLinearGradient ground(QPointF(area.left(), area.top() + area.height() * 0.55), area.bottomLeft());
		ground.setColorAt(0, QColor(84, 80, 72));
		ground.setColorAt(1, QColor(42, 40, 36));
		p.fillRect(QRectF(area.left(), area.top() + area.height() * 0.55, area.width(), area.height() * 0.45), ground);
		p.fillRect(QRectF(area.left() + area.width() * 0.62, area.top() + area.height() * 0.22, area.width() * 0.22, area.height() * 0.33), QColor(58, 60, 62));
	}
	p.translate(area.topLeft());
	p.scale(scale(), scale());
	p.setRenderHint(QPainter::Antialiasing);
	p.setRenderHint(QPainter::TextAntialiasing);
	for (const auto& item : m_items) {
		const bool selected = item.name.compare(m_selected, Qt::CaseInsensitive) == 0;
		if (!item.visible && !selected) continue;
		paintItem(p, item, selected);
	}
	p.restore();
	p.setPen(QColor(90, 94, 100));
	p.drawRect(area.adjusted(-0.5, -0.5, 0.5, 0.5));
	p.setPen(QColor(150, 155, 165));
	p.drawText(rect().adjusted(8, 4, -8, -4), Qt::AlignRight | Qt::AlignBottom, QStringLiteral("%1 × %2").arg(m_screen.width()).arg(m_screen.height()));
}

void HudPreview::paintItem(QPainter& p, const HudPreviewItem& item, bool selected) const {
	const QRect r = item.rect;
	const double s = 1.0 / std::max(0.01, scale());   // one widget pixel in screen pixels
	const QString lower = item.name.toLower();
	if (lower == "hudcrosshair") {
		const QPoint c(m_screen.width() / 2, m_screen.height() / 2);
		const int arm = std::max(4, m_screen.height() / 90);
		p.setPen(QPen(QColor(255, 220, 0, 200), std::max(1.0, m_screen.height() / 540.0)));
		p.drawLine(c - QPoint(arm, 0), c - QPoint(arm / 3, 0));
		p.drawLine(c + QPoint(arm / 3, 0), c + QPoint(arm, 0));
		p.drawLine(c - QPoint(0, arm), c - QPoint(0, arm / 3));
		p.drawLine(c + QPoint(0, arm / 3), c + QPoint(0, arm));
	} else if (!item.fullScreen && r.isValid()) {
		const bool numeric = !item.sample.isEmpty() || !item.label.isEmpty() || !item.barRect.isEmpty();
		if (numeric || lower == "hudclosecaption" || lower == "hudflashlight") {
			// PaintBackgroundType 2: rounded corners.
			QPainterPath path;
			const double radius = std::max(2.0, m_screen.height() / 480.0 * 6);
			path.addRoundedRect(QRectF(r), radius, radius);
			p.fillPath(path, item.background);
		}
		const auto composition = item.additive ? QPainter::CompositionMode_Plus : QPainter::CompositionMode_SourceOver;
		p.save();
		p.setCompositionMode(composition);
		if (!item.label.isEmpty()) {
			p.setFont(item.labelFont);
			p.setPen(item.text);
			p.drawText(r.topLeft() + item.labelPos + QPoint(0, QFontMetrics(item.labelFont).ascent()), item.label);
		}
		if (!item.sample.isEmpty()) {
			p.setFont(item.numberFont);
			p.setPen(item.text);
			p.drawText(r.topLeft() + item.numberPos + QPoint(0, QFontMetrics(item.numberFont).ascent()), item.sample);
		}
		if (!item.sample2.isEmpty()) {
			p.setFont(item.smallFont);
			p.setPen(item.text);
			p.drawText(r.topLeft() + item.number2Pos + QPoint(0, QFontMetrics(item.smallFont).ascent()), item.sample2);
		}
		if (!item.barRect.isEmpty()) {
			const QRect bar = item.barRect.translated(r.topLeft());
			const int chunk = std::max(1, item.chunk), step = chunk + std::max(0, item.gap);
			int i = 0;
			for (int x = bar.left(); x + chunk <= bar.right() + 1; x += step, ++i) {
				QColor c = item.bar;
				if (i > 7) c.setAlpha(std::min(c.alpha(), 70));   // part of the power used up
				p.fillRect(QRect(x, bar.top(), chunk, bar.height()), c);
			}
		}
		if (lower == "hudclosecaption") {
			p.setFont(item.labelFont);
			p.setPen(QColor(255, 255, 255, 230));
			p.drawText(QRectF(r).adjusted(8, 4, -8, -4), Qt::AlignCenter | Qt::TextWordWrap, QObject::tr("[Sample caption] This is what a caption looks like."));
		}
		p.restore();
	}
	if ((selected || m_outlines) && r.isValid() && !item.fullScreen) {
		p.setBrush(Qt::NoBrush);
		QPen pen(selected ? QColor(255, 200, 60) : QColor(120, 200, 255, 160), 1.5 * s, item.visible ? Qt::SolidLine : Qt::DashLine);
		p.setPen(pen);
		p.drawRect(QRectF(r).adjusted(-1 * s, -1 * s, 1 * s, 1 * s));
		if (selected && item.movable) {
			const double h = HANDLE * s;
			p.fillRect(QRectF(r.right() - h / 2, r.bottom() - h / 2, h, h), QColor(255, 200, 60));
		}
		if (m_outlines || !item.visible) {
			QFont f = font();
			f.setPixelSize(std::max(8, int(11 * s)));
			p.setFont(f);
			p.setPen(selected ? QColor(255, 200, 60) : QColor(160, 215, 255));
			p.drawText(QPointF(r.left(), r.top() - 3 * s), item.name);
		}
	}
}

void HudPreview::mousePressEvent(QMouseEvent* event) {
	if (event->button() != Qt::LeftButton) return;
	const QPointF at = event->position();
	// The selected element first (its grip lies on its edge), then the topmost under the mouse.
	QString hit;
	bool grip = false;
	if (const QRect sel = widgetRect(m_selected); sel.isValid()) {
		const HudPreviewItem* it = item(m_selected);
		if (it && it->movable && QRectF(sel.right() - HANDLE, sel.bottom() - HANDLE, HANDLE * 2, HANDLE * 2).contains(at)) {
			hit = m_selected;
			grip = true;
		}
	}
	if (hit.isEmpty()) {
		for (auto it = m_items.rbegin(); it != m_items.rend(); ++it) {
			if (it->fullScreen || (!it->visible && it->name.compare(m_selected, Qt::CaseInsensitive) != 0)) continue;
			if (widgetRect(it->name).contains(at.toPoint())) {
				hit = it->name;
				break;
			}
		}
	}
	if (hit.isEmpty()) return;
	if (hit.compare(m_selected, Qt::CaseInsensitive) != 0) {
		m_selected = hit;
		emit selected(hit);
	}
	const HudPreviewItem* it = item(hit);
	if (it && it->movable) {
		m_drag = hit;
		m_resize = grip;
		m_pressAt = at;
		m_startRect = it->rect;
	}
	update();
}

void HudPreview::mouseMoveEvent(QMouseEvent* event) {
	if (m_drag.isEmpty()) {
		// Cursor hint on the grip.
		const QRect sel = widgetRect(m_selected);
		const HudPreviewItem* it = item(m_selected);
		const bool onGrip = it && it->movable && sel.isValid() &&
		                    QRectF(sel.right() - HANDLE, sel.bottom() - HANDLE, HANDLE * 2, HANDLE * 2).contains(event->position());
		setCursor(onGrip ? Qt::SizeFDiagCursor : Qt::ArrowCursor);
		return;
	}
	HudPreviewItem* it = item(m_drag);
	if (!it) return;
	const double s = scale();
	const QPoint delta = ((event->position() - m_pressAt) / std::max(0.01, s)).toPoint();
	if (m_resize) {
		it->rect = QRect(m_startRect.topLeft(), QSize(std::max(4, m_startRect.width() + delta.x()), std::max(4, m_startRect.height() + delta.y())));
	} else {
		QRect moved = m_startRect.translated(delta);
		// Keep it on the screen.
		moved.moveLeft(std::clamp(moved.left(), -moved.width() / 2, m_screen.width() - moved.width() / 2));
		moved.moveTop(std::clamp(moved.top(), -moved.height() / 2, m_screen.height() - moved.height() / 2));
		it->rect = moved;
	}
	update();
}

void HudPreview::mouseReleaseEvent(QMouseEvent*) {
	if (m_drag.isEmpty()) return;
	const QString name = m_drag;
	const bool resized = m_resize;
	m_drag.clear();
	const HudPreviewItem* it = item(name);
	if (it && it->rect != m_startRect) emit changed(name, it->rect, resized);
}

// --- HudEditor --------------------------------------------------------------------------------

HudEditor::HudEditor(Editor* editor, QWidget* parent) : QWidget(parent), m_editor(editor) {
	setObjectName("hudEditor");
	buildUi();
	auto* lazy = new RefreshWhenShown(this, [this] { reload(); });
	connect(editor, &Editor::gameChanged, lazy, &RefreshWhenShown::request);
	lazy->request();
}

HudEditor::~HudEditor() {
	for (const int id : std::as_const(m_fontIds)) QFontDatabase::removeApplicationFont(id);
}

void HudEditor::buildUi() {
	auto* root = new QVBoxLayout(this);
	root->setContentsMargins(10, 8, 10, 8);
	root->setSpacing(6);

	// Top: resolution, outlines, undo, save, restore.
	auto* top = new QHBoxLayout;
	auto* title = new QLabel(tr("<b>HUD</b>"));
	title->setToolTip(tr("Health, ammo and the rest of what the game draws over the picture"));
	top->addWidget(title);
	top->addStretch(1);
	top->addWidget(new QLabel(tr("Screen")));
	m_resolution = new QComboBox;
	m_resolution->setObjectName("hudResolution");
	for (const auto& size : RESOLUTIONS) {
		const double aspect = double(size.width()) / size.height();
		const QString ratio = std::abs(aspect - 16.0 / 9) < 0.02 ? "16:9" : std::abs(aspect - 4.0 / 3) < 0.02 ? "4:3" : std::abs(aspect - 1.25) < 0.02 ? "5:4" : "";
		m_resolution->addItem(QStringLiteral("%1 × %2  %3").arg(size.width()).arg(size.height()).arg(ratio), size);
	}
	connect(m_resolution, &QComboBox::currentIndexChanged, this, [this](int i) { setResolution(m_resolution->itemData(i).toSize()); });
	top->addWidget(m_resolution);
	auto* picture = new QPushButton(tr("Background ..."));
	picture->setObjectName("hudBackground");
	picture->setToolTip(tr("Show a screenshot of your game behind the HUD (any PNG or JPG)"));
	connect(picture, &QPushButton::clicked, this, [this] {
		const QString file = QFileDialog::getOpenFileName(this, tr("Background picture"), QSettings().value("hud/background").toString(), tr("Pictures (*.png *.jpg *.jpeg *.bmp)"));
		if (file.isEmpty()) return;
		QSettings().setValue("hud/background", file);
		m_preview->setBackground(QImage(file));
	});
	top->addWidget(picture);
	m_outlines = new QCheckBox(tr("Outlines"));
	m_outlines->setObjectName("hudOutlines");
	m_outlines->setToolTip(tr("Show the box of every element"));
	top->addWidget(m_outlines);
	m_undoButton = new QPushButton(tr("Undo"));
	m_undoButton->setObjectName("hudUndo");
	connect(m_undoButton, &QPushButton::clicked, this, &HudEditor::undo);
	top->addWidget(m_undoButton);
	m_saveButton = new QPushButton(tr("Save to mod"));
	m_saveButton->setObjectName("hudSave");
	m_saveButton->setProperty("role", "primary");
	connect(m_saveButton, &QPushButton::clicked, this, [this] {
		QString error;
		if (!save(&error)) QMessageBox::warning(this, tr("HUD"), tr("Could not save:\n%1").arg(error));
	});
	top->addWidget(m_saveButton);
	m_restoreButton = new QPushButton(tr("Restore original"));
	m_restoreButton->setObjectName("hudRestore");
	m_restoreButton->setToolTip(tr("Put the mod's HUD files back as they were before Mapsmith changed them"));
	connect(m_restoreButton, &QPushButton::clicked, this, [this] {
		if (QMessageBox::question(this, tr("HUD"), tr("Put the HUD back as it was before Mapsmith changed it?")) != QMessageBox::Yes) return;
		QString error;
		if (!restoreOriginal(&error)) QMessageBox::warning(this, tr("HUD"), tr("Could not restore:\n%1").arg(error));
	});
	top->addWidget(m_restoreButton);
	root->addLayout(top);
	m_source = new QLabel;
	m_source->setObjectName("hudSource");
	m_source->setProperty("role", "description");
	m_source->setWordWrap(true);
	root->addWidget(m_source);

	auto* body = new QHBoxLayout;
	body->setSpacing(10);

	// Left: elements and the selected one's settings.
	auto* left = new QWidget;
	left->setFixedWidth(250);
	auto* lv = new QVBoxLayout(left);
	lv->setContentsMargins(0, 0, 0, 0);
	lv->addWidget(header(tr("Elements")));
	m_elements = new QListWidget;
	m_elements->setObjectName("hudElements");
	connect(m_elements, &QListWidget::currentRowChanged, this, [this](int row) {
		if (m_updating || row < 0) return;
		selectElement(m_elements->item(row)->data(Qt::UserRole).toString());
	});
	lv->addWidget(m_elements, 1);
	lv->addWidget(header(tr("Selected")));
	m_elementNote = new QLabel;
	m_elementNote->setWordWrap(true);
	m_elementNote->setProperty("role", "description");
	lv->addWidget(m_elementNote);
	auto* form = new QFormLayout;
	form->setContentsMargins(0, 0, 0, 0);
	const auto anchorBox = [](const QStringList& names, const char* object) {
		auto* box = new QComboBox;
		box->setObjectName(object);
		box->addItems(names);
		return box;
	};
	const auto spin = [](const char* object, int min, int max) {
		auto* s = new QSpinBox;
		s->setObjectName(object);
		s->setRange(min, max);
		s->setKeyboardTracking(false);
		return s;
	};
	m_anchorX = anchorBox({tr("From left"), tr("From centre"), tr("From right")}, "hudAnchorX");
	m_x = spin("hudX", -2000, 2000);
	auto* xRow = new QHBoxLayout;
	xRow->addWidget(m_anchorX, 1);
	xRow->addWidget(m_x);
	form->addRow(tr("X"), xRow);
	m_anchorY = anchorBox({tr("From top"), tr("From centre"), tr("From bottom")}, "hudAnchorY");
	m_y = spin("hudY", -2000, 2000);
	auto* yRow = new QHBoxLayout;
	yRow->addWidget(m_anchorY, 1);
	yRow->addWidget(m_y);
	form->addRow(tr("Y"), yRow);
	m_wide = spin("hudWide", 0, 2000);
	m_tall = spin("hudTall", 0, 2000);
	auto* sizeRow = new QHBoxLayout;
	sizeRow->addWidget(m_wide);
	sizeRow->addWidget(new QLabel("×"));
	sizeRow->addWidget(m_tall);
	form->addRow(tr("Size"), sizeRow);
	m_visible = new QCheckBox(tr("Visible"));
	m_visible->setObjectName("hudVisible");
	form->addRow(QString(), m_visible);
	m_textColor = new QPushButton(tr("Text colour ..."));
	m_textColor->setObjectName("hudTextColor");
	m_bgColor = new QPushButton(tr("Background ..."));
	m_bgColor->setObjectName("hudBgColor");
	auto* colorRow = new QHBoxLayout;
	colorRow->addWidget(m_textColor);
	colorRow->addWidget(m_bgColor);
	form->addRow(tr("Colours"), colorRow);
	m_numberFont = new QComboBox;
	m_numberFont->setObjectName("hudNumberFont");
	form->addRow(tr("Number font"), m_numberFont);
	lv->addLayout(form);
	auto* units = new QLabel(tr("Numbers are in the game's 640 × 480 units; the game scales them to the screen."));
	units->setWordWrap(true);
	units->setProperty("role", "description");
	lv->addWidget(units);
	body->addWidget(left);

	// Position fields: anchor + number together.
	const auto writePos = [this](bool x) {
		if (m_updating || m_selected.isEmpty()) return;
		const auto anchor = static_cast<HudAnchor>((x ? m_anchorX : m_anchorY)->currentIndex());
		setElementValue(m_selected, x ? "xpos" : "ypos", qs(hudPositionFrom(anchor, (x ? m_x : m_y)->value())));
	};
	connect(m_anchorX, &QComboBox::currentIndexChanged, this, [writePos] { writePos(true); });
	connect(m_x, &QSpinBox::valueChanged, this, [writePos] { writePos(true); });
	connect(m_anchorY, &QComboBox::currentIndexChanged, this, [writePos] { writePos(false); });
	connect(m_y, &QSpinBox::valueChanged, this, [writePos] { writePos(false); });
	connect(m_wide, &QSpinBox::valueChanged, this, [this](int v) {
		if (!m_updating && !m_selected.isEmpty()) setElementValue(m_selected, "wide", QString::number(v));
	});
	connect(m_tall, &QSpinBox::valueChanged, this, [this](int v) {
		if (!m_updating && !m_selected.isEmpty()) setElementValue(m_selected, "tall", QString::number(v));
	});
	connect(m_visible, &QCheckBox::toggled, this, [this](bool on) {
		if (!m_updating && !m_selected.isEmpty()) setElementValue(m_selected, "visible", on ? "1" : "0");
	});
	const auto pickElementColor = [this](const char* key, const QColor& current) {
		if (m_selected.isEmpty()) return;
		const QColor c = QColorDialog::getColor(current, this, tr("Colour"), QColorDialog::ShowAlphaChannel);
		if (c.isValid()) setElementValue(m_selected, key, qs(hudColorText(fromQColor(c))));
	};
	connect(m_textColor, &QPushButton::clicked, this, [this, pickElementColor] {
		const ResNode* block = m_layout ? hudElementBlock(*m_layout, m_selected.toStdString()) : nullptr;
		pickElementColor("TextColor", color(block ? block->get("TextColor", "FgColor") : "FgColor", QColor(255, 220, 0, 100)));
	});
	connect(m_bgColor, &QPushButton::clicked, this, [this, pickElementColor] {
		const ResNode* block = m_layout ? hudElementBlock(*m_layout, m_selected.toStdString()) : nullptr;
		pickElementColor("bgcolor_override", color(block ? block->get("bgcolor_override", "BgColor") : "BgColor", QColor(0, 0, 0, 76)));
	});
	connect(m_numberFont, &QComboBox::currentIndexChanged, this, [this] {
		if (!m_updating && !m_selected.isEmpty() && !m_numberFont->currentText().isEmpty()) setElementValue(m_selected, "NumberFont", m_numberFont->currentText());
	});

	// Centre: the preview.
	m_preview = new HudPreview;
	connect(m_preview, &HudPreview::selected, this, &HudEditor::selectElement);
	connect(m_preview, &HudPreview::changed, this, &HudEditor::previewChanged);
	connect(m_outlines, &QCheckBox::toggled, m_preview, &HudPreview::setShowOutlines);
	auto* centre = new QVBoxLayout;
	centre->addWidget(m_preview, 1);
	auto* hint = new QLabel(tr("Drag an element to move it, its corner to resize it. Colours and fonts on the right apply to the whole HUD."));
	hint->setWordWrap(true);
	hint->setProperty("role", "description");
	centre->addWidget(hint);
	body->addLayout(centre, 1);

	// Right: colours and fonts of the scheme.
	auto* tabs = new QTabWidget;
	tabs->setObjectName("hudSchemeTabs");
	tabs->setFixedWidth(270);
	m_colors = new QTreeWidget;
	m_colors->setObjectName("hudColors");
	m_colors->setHeaderLabels({tr("Colour"), tr("Value")});
	m_colors->setRootIsDecorated(false);
	m_colors->header()->setSectionResizeMode(0, QHeaderView::Stretch);
	connect(m_colors, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item) {
		const QString name = item->data(0, Qt::UserRole).toString();
		const QColor current = item->data(1, Qt::UserRole).value<QColor>();
		const QColor c = QColorDialog::getColor(current, this, name, QColorDialog::ShowAlphaChannel);
		if (c.isValid()) setSchemeColor(name, c);
	});
	auto* colorsPage = new QWidget;
	auto* cv = new QVBoxLayout(colorsPage);
	cv->setContentsMargins(4, 4, 4, 4);
	cv->addWidget(m_colors, 1);
	auto* colorsNote = new QLabel(tr("Double-click a colour to change it. FgColor is the HUD's yellow, BgColor the box behind numbers."));
	colorsNote->setWordWrap(true);
	colorsNote->setProperty("role", "description");
	cv->addWidget(colorsNote);
	tabs->addTab(colorsPage, tr("Colours"));

	auto* fontsPage = new QWidget;
	auto* fv = new QVBoxLayout(fontsPage);
	fv->setContentsMargins(4, 4, 4, 4);
	m_fonts = new QTreeWidget;
	m_fonts->setObjectName("hudFonts");
	m_fonts->setHeaderLabels({tr("Font"), tr("Typeface"), tr("Size")});
	m_fonts->setRootIsDecorated(false);
	m_fonts->header()->setSectionResizeMode(0, QHeaderView::Stretch);
	fv->addWidget(m_fonts, 1);
	auto* fontForm = new QFormLayout;
	m_fontFamily = new QComboBox;
	m_fontFamily->setObjectName("hudFontFamily");
	m_fontFamily->setEditable(true);
	m_fontFamily->setInsertPolicy(QComboBox::NoInsert);
	fontForm->addRow(tr("Typeface"), m_fontFamily);
	m_fontSize = spin("hudFontSize", 4, 400);
	m_fontSize->setSuffix(tr(" px"));
	fontForm->addRow(tr("Size here"), m_fontSize);
	fv->addLayout(fontForm);
	auto* fontsNote = new QLabel(tr("The size is for the chosen screen: fonts with a resolution range change only that range."));
	fontsNote->setWordWrap(true);
	fontsNote->setProperty("role", "description");
	fv->addWidget(fontsNote);
	tabs->addTab(fontsPage, tr("Fonts"));
	connect(m_fonts, &QTreeWidget::currentItemChanged, this, [this] {
		auto* item = m_fonts->currentItem();
		if (!item) return;
		m_updating = true;
		m_fontFamily->setCurrentText(item->text(1));
		m_fontSize->setValue(item->data(2, Qt::UserRole).toInt());
		m_updating = false;
	});
	connect(m_fontSize, &QSpinBox::valueChanged, this, [this](int v) {
		if (m_updating || !m_fonts->currentItem()) return;
		setFontSize(m_fonts->currentItem()->text(0), v);
	});
	connect(m_fontFamily, &QComboBox::currentTextChanged, this, [this](const QString& family) {
		if (m_updating || !m_fonts->currentItem() || family.trimmed().isEmpty()) return;
		setFontFamily(m_fonts->currentItem()->text(0), family.trimmed());
	});
	body->addWidget(tabs);
	root->addLayout(body, 1);

	auto* undoKey = new QShortcut(QKeySequence::Undo, this);
	undoKey->setContext(Qt::WidgetWithChildrenShortcut);
	connect(undoKey, &QShortcut::activated, this, &HudEditor::undo);

	const QString background = QSettings().value("hud/background").toString();
	if (!background.isEmpty()) m_preview->setBackground(QImage(background));
	updateButtons();
}

void HudEditor::reload() {
	m_layout.reset();
	m_scheme.reset();
	m_undo.clear();
	m_layoutDirty = m_schemeDirty = false;
	const auto* game = m_editor->game();
	const auto* entry = m_editor->gameEntry();
	QStringList problems;
	if (game) {
		const auto read = [&](std::string_view path, std::optional<ResFile>& into, bool& fromMod) {
			fromMod = entry && std::filesystem::exists(entry->gameDir / std::filesystem::path(std::string(path)));
			const auto text = game->files.readText(path);
			if (!text) {
				problems << tr("%1 not found").arg(qs(path));
				return;
			}
			std::string error;
			into = parseResFile(*text, &error);
			if (!into) problems << tr("%1 could not be read: %2").arg(qs(path), qs(error));
		};
		read(HUD_LAYOUT_FILE, m_layout, m_layoutFromMod);
		read(HUD_SCHEME_FILE, m_scheme, m_schemeFromMod);
	}
	loadFonts();
	if (!game) {
		m_source->setText(tr("Choose a game or mod first."));
	} else if (!problems.isEmpty()) {
		m_source->setText(problems.join(" · "));
	} else {
		const auto where = [this](bool fromMod) { return fromMod ? tr("your mod") : tr("the game (saved into your mod on the first change)"); };
		m_source->setText(tr("Layout from %1, colours and fonts from %2.").arg(where(m_layoutFromMod), where(m_schemeFromMod)));
	}
	if (m_layout && m_selected.isEmpty()) m_selected = "HudHealth";
	refresh();
}

void HudEditor::loadFonts() {
	for (const int id : std::as_const(m_fontIds)) QFontDatabase::removeApplicationFont(id);
	m_fontIds.clear();
	m_customFamilies.clear();
	const auto* game = m_editor->game();
	if (!game || !m_scheme) return;
	for (const auto& path : schemeFontFiles(*m_scheme)) {
		const auto bytes = game->files.read(path);
		if (!bytes || bytes->size() > 32u << 20) continue;
		const int id = QFontDatabase::addApplicationFontFromData(QByteArray(reinterpret_cast<const char*>(bytes->data()), qsizetype(bytes->size())));
		if (id < 0) continue;
		m_fontIds << id;
		for (const auto& family : QFontDatabase::applicationFontFamilies(id)) m_customFamilies.insert(family.toLower(), family);
	}
}

QFont HudEditor::qtFont(const QString& schemeFont, double) const {
	QFont font(QStringLiteral("Verdana"));
	font.setPixelSize(12);
	if (!m_scheme) return font;
	const int tall = m_preview->screen().height();
	for (const auto& f : schemeFonts(*m_scheme)) {
		if (!iequals(f.name, schemeFont.toStdString())) continue;
		const auto* entry = pickFontEntry(f, tall);
		if (!entry) break;
		const QString family = qs(entry->family);
		font.setFamily(m_customFamilies.value(family.toLower(), family));
		font.setPixelSize(std::max(1, fontPixelTall(*entry, tall)));
		font.setWeight(static_cast<QFont::Weight>(std::clamp(entry->weight > 0 ? entry->weight : 400, 100, 900)));
		font.setStyleStrategy(entry->antialias ? QFont::PreferAntialias : QFont::NoAntialias);
		break;
	}
	return font;
}

QColor HudEditor::color(const std::string& nameOrColor, const QColor& fallback) const {
	if (!m_scheme) {
		if (auto c = parseHudColor(nameOrColor)) return toQColor(*c);
		return fallback;
	}
	if (auto c = resolveSchemeColor(*m_scheme, nameOrColor)) return toQColor(*c);
	return fallback;
}

void HudEditor::refresh() {
	m_updating = true;
	const QSize screen = m_preview->screen();
	std::vector<HudPreviewItem> items;
	const QString current = m_selected;
	m_elements->clear();
	if (m_layout) {
		for (const auto& e : hudElements(*m_layout)) {
			const auto info = hudElementInfo(e.name);
			const ResNode* block = hudElementBlock(*m_layout, e.name);
			HudPreviewItem item;
			item.name = qs(e.name);
			item.visible = e.visible;
			item.fullScreen = e.fullScreen;
			item.movable = e.hasPosition && !e.fullScreen;
			const HudRect r = resolveHudRect(e.xpos, e.ypos, e.wide, e.tall, screen.width(), screen.height());
			item.rect = QRect(r.x, r.y, r.wide, r.tall);
			item.label = qs(info.label);
			item.sample = qs(info.sample);
			item.sample2 = qs(info.sample2);
			item.text = color(block ? block->get("TextColor", "FgColor") : "FgColor", QColor(255, 220, 0, 100));
			item.background = color(block ? block->get("bgcolor_override", "BgColor") : "BgColor", QColor(0, 0, 0, 76));
			item.labelFont = qtFont(qs(block ? block->get("TextFont", "Default") : "Default"), 1);
			item.numberFont = qtFont(qs(block ? block->get("NumberFont", "HudNumbers") : "HudNumbers"), 1);
			item.smallFont = qtFont(qs(block ? block->get("SmallNumberFont", "HudNumbersSmall") : "HudNumbersSmall"), 1);
			const auto px = [&](std::string_view key, int fallback) { return hudScaled(resInt(block, key, fallback), screen.height()); };
			item.labelPos = QPoint(px("text_xpos", 8), px("text_ypos", 20));
			item.numberPos = QPoint(px("digit_xpos", 50), px("digit_ypos", 2));
			item.number2Pos = QPoint(px("digit2_xpos", 98), px("digit2_ypos", 16));
			if (iequals(e.name, "HudSuitPower")) {
				item.labelPos = QPoint(px("text_xpos", 8), px("text_ypos", 4));
				item.barRect = QRect(px("BarInsetX", 8), px("BarInsetY", 15), px("BarWidth", 92), std::max(1, px("BarHeight", 4)));
				item.chunk = px("BarChunkWidth", 6);
				item.gap = px("BarChunkGap", 3);
				item.bar = color(block ? block->get("AuxPowerHighColor", "255 220 0 220") : "255 220 0 220", QColor(255, 220, 0, 220));
			} else if (iequals(e.name, "HudFlashlight")) {
				item.barRect = QRect(px("BarInsetX", 4), px("BarInsetY", 18), px("BarWidth", 28), std::max(1, px("BarHeight", 2)));
				item.chunk = px("BarChunkWidth", 2);
				item.gap = px("BarChunkGap", 1);
				item.bar = item.text;
			}
			items.push_back(item);

			auto* row = new QListWidgetItem(info.title.empty() ? qs(e.name) : QStringLiteral("%1  ·  %2").arg(qs(info.title), qs(e.name)));
			row->setData(Qt::UserRole, qs(e.name));
			if (!e.visible) row->setForeground(QColor(130, 135, 145));
			row->setToolTip(qs(info.description) + (e.visible ? QString() : tr(" (hidden)")) +
			                (item.movable ? QString() : tr("\nPlaced by the game code or full screen: only its settings can change.")));
			m_elements->addItem(row);
			if (row->data(Qt::UserRole).toString().compare(current, Qt::CaseInsensitive) == 0) m_elements->setCurrentItem(row);
		}
	}
	m_preview->setItems(std::move(items));
	m_preview->setSelected(current);
	m_updating = false;
	refreshProperties();
	refreshColors();
	refreshFonts();
	updateButtons();
}

void HudEditor::refreshProperties() {
	m_updating = true;
	const ResNode* block = m_layout && !m_selected.isEmpty() ? hudElementBlock(*m_layout, m_selected.toStdString()) : nullptr;
	const bool on = block != nullptr;
	for (QWidget* w : std::initializer_list<QWidget*>{m_anchorX, m_anchorY, m_x, m_y, m_wide, m_tall, m_visible, m_textColor, m_bgColor, m_numberFont}) {
		w->setEnabled(on);
	}
	if (block) {
		const auto info = hudElementInfo(m_selected.toStdString());
		m_elementNote->setText(QStringLiteral("<b>%1</b> – %2").arg(info.title.empty() ? m_selected : qs(info.title), qs(info.description)));
		const std::string x = block->get("xpos"), y = block->get("ypos");
		m_anchorX->setCurrentIndex(static_cast<int>(hudAnchor(x)));
		m_anchorY->setCurrentIndex(static_cast<int>(hudAnchor(y)));
		m_x->setValue(static_cast<int>(std::lround(hudPositionNumber(x))));
		m_y->setValue(static_cast<int>(std::lround(hudPositionNumber(y))));
		m_wide->setValue(std::atoi(std::string(trim(block->get("wide"))).c_str()));
		m_tall->setValue(std::atoi(std::string(trim(block->get("tall"))).c_str()));
		m_visible->setChecked(trim(block->get("visible", "1")) != "0");
		const bool numbers = !hudElementInfo(m_selected.toStdString()).sample.empty();
		m_numberFont->setEnabled(numbers);
		m_textColor->setIcon(swatch(color(block->get("TextColor", "FgColor"), QColor(255, 220, 0, 100))));
		m_bgColor->setIcon(swatch(color(block->get("bgcolor_override", "BgColor"), QColor(0, 0, 0, 76))));
		m_numberFont->clear();
		if (m_scheme) {
			for (const auto& f : schemeFonts(*m_scheme)) {
				if (hudFont(f.name)) m_numberFont->addItem(qs(f.name));
			}
		}
		m_numberFont->setCurrentText(qs(block->get("NumberFont", "HudNumbers")));
	} else {
		m_elementNote->setText(tr("Click an element in the picture or in the list."));
	}
	m_updating = false;
}

void HudEditor::refreshColors() {
	const QString current = m_colors->currentItem() ? m_colors->currentItem()->data(0, Qt::UserRole).toString() : QString();
	m_colors->clear();
	if (!m_scheme) return;
	for (const auto& c : schemeColors(*m_scheme)) {
		auto* item = new QTreeWidgetItem({qs(c.name), qs(c.text)});
		const QColor qc = c.color ? toQColor(*c.color) : QColor();
		item->setIcon(0, swatch(qc));
		item->setData(0, Qt::UserRole, qs(c.name));
		item->setData(1, Qt::UserRole, qc);
		item->setToolTip(0, c.section == "Colors" ? tr("Named colour (Colors)") : tr("Base setting"));
		m_colors->addTopLevelItem(item);
		if (item->text(0) == current) m_colors->setCurrentItem(item);
	}
}

void HudEditor::refreshFonts() {
	m_updating = true;
	const QString current = m_fonts->currentItem() ? m_fonts->currentItem()->text(0) : QString();
	m_fonts->clear();
	m_fontFamily->clear();
	QStringList families = m_customFamilies.values();
	families.sort(Qt::CaseInsensitive);
	families << "Verdana" << "Tahoma" << "Arial" << "Trebuchet MS" << "Courier New";
	families.removeDuplicates();
	m_fontFamily->addItems(families);
	if (m_scheme) {
		const int tall = m_preview->screen().height();
		auto fonts = schemeFonts(*m_scheme);
		std::stable_partition(fonts.begin(), fonts.end(), [](const SchemeFont& f) { return hudFont(f.name); });
		for (const auto& f : fonts) {
			const auto* entry = pickFontEntry(f, tall);
			if (!entry) continue;
			auto* item = new QTreeWidgetItem({qs(f.name), qs(entry->family), QString::number(fontPixelTall(*entry, tall))});
			item->setData(2, Qt::UserRole, fontPixelTall(*entry, tall));
			item->setToolTip(0, entry->yresMin || entry->yresMax ? tr("Used from %1 to %2 pixels high").arg(entry->yresMin).arg(entry->yresMax)
			                                                     : tr("Scales with the screen height"));
			m_fonts->addTopLevelItem(item);
			if (item->text(0) == current) m_fonts->setCurrentItem(item);
		}
	}
	if (auto* item = m_fonts->currentItem()) {
		m_fontFamily->setCurrentText(item->text(1));
		m_fontSize->setValue(item->data(2, Qt::UserRole).toInt());
	}
	m_updating = false;
}

void HudEditor::selectElement(const QString& name) {
	m_selected = name;
	m_preview->setSelected(name);
	m_updating = true;
	for (int i = 0; i < m_elements->count(); ++i) {
		if (m_elements->item(i)->data(Qt::UserRole).toString().compare(name, Qt::CaseInsensitive) == 0) m_elements->setCurrentRow(i);
	}
	m_updating = false;
	refreshProperties();
}

void HudEditor::pushUndo() {
	m_undo.emplace_back(m_layout ? m_layout->text : std::string(), m_scheme ? m_scheme->text : std::string());
	if (m_undo.size() > 200) m_undo.erase(m_undo.begin());
}

void HudEditor::applyLayout(std::string text) {
	if (auto parsed = parseResFile(text)) {
		m_layout = std::move(parsed);
		m_layoutDirty = true;
	}
}

void HudEditor::applyScheme(std::string text) {
	if (auto parsed = parseResFile(text)) {
		m_scheme = std::move(parsed);
		m_schemeDirty = true;
	}
}

void HudEditor::setElementValue(const QString& element, const QString& key, const QString& value) {
	if (!m_layout) return;
	const ResNode* block = hudElementBlock(*m_layout, element.toStdString());
	if (!block || (block->find(key.toStdString()) && block->get(key.toStdString()) == value.toStdString())) return;
	pushUndo();
	applyLayout(withResValue(*m_layout, *block, key.toStdString(), value.toStdString()));
	refresh();
}

void HudEditor::setSchemeColor(const QString& name, const QColor& c) {
	if (!m_scheme) return;
	const std::string key = name.toStdString();
	// The entry as listed: BaseSettings first, then Colors.
	for (const std::string_view section : {"BaseSettings", "Colors"}) {
		const ResNode* top = nullptr;
		for (const auto& child : m_scheme->root.children) {
			if (child.block && child.active()) {
				top = &child;
				break;
			}
		}
		const ResNode* block = top ? top->find(section) : nullptr;
		if (!block || !block->find(key) || block->find(key)->block) continue;
		pushUndo();
		applyScheme(withResValue(*m_scheme, *block, key, hudColorText(fromQColor(c))));
		refresh();
		return;
	}
}

void HudEditor::setFontSize(const QString& font, int pixels) {
	if (!m_scheme) return;
	const int tall = m_preview->screen().height();
	for (const auto& f : schemeFonts(*m_scheme)) {
		if (!iequals(f.name, font.toStdString())) continue;
		const auto* entry = pickFontEntry(f, tall);
		if (!entry) return;
		// An entry without a resolution range is in 480-line units; one with a range is in pixels.
		const int value = entry->yresMin || entry->yresMax ? pixels : std::max(1, int(std::lround(pixels * 480.0 / std::max(1, tall))));
		const ResNode* block = findResBlock(*m_scheme, {firstActive(), "Fonts", f.name, entry->id});
		if (!block || entry->tall == value) return;
		pushUndo();
		applyScheme(withResValue(*m_scheme, *block, "tall", std::to_string(value)));
		refresh();
		return;
	}
}

void HudEditor::setFontFamily(const QString& font, const QString& family) {
	if (!m_scheme) return;
	const int tall = m_preview->screen().height();
	for (const auto& f : schemeFonts(*m_scheme)) {
		if (!iequals(f.name, font.toStdString())) continue;
		const auto* entry = pickFontEntry(f, tall);
		if (!entry || entry->family == family.toStdString()) return;
		const ResNode* block = findResBlock(*m_scheme, {firstActive(), "Fonts", f.name, entry->id});
		if (!block) return;
		pushUndo();
		applyScheme(withResValue(*m_scheme, *block, "name", family.toStdString()));
		refresh();
		return;
	}
}

std::string_view HudEditor::firstActive() const {
	if (!m_scheme) return {};
	for (const auto& child : m_scheme->root.children) {
		if (child.block && child.active()) return child.key;
	}
	return {};
}

void HudEditor::setResolution(QSize size) {
	m_preview->setScreen(size);
	refresh();
}

void HudEditor::undo() {
	if (m_undo.empty()) return;
	auto [layout, scheme] = std::move(m_undo.back());
	m_undo.pop_back();
	if (!layout.empty() || m_layout) applyLayout(std::move(layout));
	if (!scheme.empty() || m_scheme) applyScheme(std::move(scheme));
	refresh();
}

void HudEditor::previewChanged(const QString& name, QRect rect, bool resized) {
	if (!m_layout) return;
	const ResNode* block = hudElementBlock(*m_layout, name.toStdString());
	if (!block) return;
	const QSize screen = m_preview->screen();
	pushUndo();
	std::string text;
	if (resized) {
		text = withResValue(*m_layout, *block, "wide", hudSizeText(block->get("wide"), rect.width(), screen.width(), screen.height()));
		auto next = parseResFile(text);
		if (!next) return;
		block = hudElementBlock(*next, name.toStdString());
		if (!block) return;
		text = withResValue(*next, *block, "tall", hudSizeText(block->get("tall"), rect.height(), screen.height(), screen.height()));
	} else {
		text = withResValue(*m_layout, *block, "xpos", hudPositionText(block->get("xpos"), rect.x(), rect.width(), screen.width(), screen.height()));
		auto next = parseResFile(text);
		if (!next) return;
		block = hudElementBlock(*next, name.toStdString());
		if (!block) return;
		text = withResValue(*next, *block, "ypos", hudPositionText(block->get("ypos"), rect.y(), rect.height(), screen.height(), screen.height()));
	}
	applyLayout(std::move(text));
	refresh();
}

bool HudEditor::save(QString* error) {
	const auto* entry = m_editor->gameEntry();
	if (!entry) {
		if (error) *error = tr("No game or mod is open");
		return false;
	}
	std::string why;
	if (m_layoutDirty && m_layout && !saveHudFile(entry->gameDir, HUD_LAYOUT_FILE, m_layout->text, &why)) {
		if (error) *error = qs(why);
		return false;
	}
	if (m_schemeDirty && m_scheme && !saveHudFile(entry->gameDir, HUD_SCHEME_FILE, m_scheme->text, &why)) {
		if (error) *error = qs(why);
		return false;
	}
	if (m_layoutDirty) m_layoutFromMod = true;
	if (m_schemeDirty) m_schemeFromMod = true;
	m_layoutDirty = m_schemeDirty = false;
	m_source->setText(tr("Saved into your mod. Restart the game (or run hud_reloadscheme in its console) to see it."));
	updateButtons();
	emit saved();
	return true;
}

bool HudEditor::restoreOriginal(QString* error) {
	const auto* entry = m_editor->gameEntry();
	if (!entry) return false;
	std::string why;
	if (!restoreHudFiles(entry->gameDir, &why)) {
		if (error) *error = qs(why);
		return false;
	}
	reload();
	return true;
}

void HudEditor::updateButtons() {
	m_undoButton->setEnabled(!m_undo.empty());
	m_saveButton->setEnabled(dirty());
	const auto* entry = m_editor->gameEntry();
	m_restoreButton->setEnabled(entry && hudFilesChanged(entry->gameDir));
}

} // namespace sme::app
