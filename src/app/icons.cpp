// icons.cpp - Icons drawn with QPainter (see icons.h). Every icon is drawn on a 64x64 canvas;
// QIcon scales it down smoothly for the tool bars.

#include "icons.h"

#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QRadialGradient>

#include <cmath>
#include <functional>

namespace sme::app {

namespace {

constexpr int SIZE = 64;
const QColor LINE(222, 227, 234);       // light line icons
const QColor DIM(150, 158, 170);
const QColor ACCENT(240, 160, 40);       // the editor's orange

QPen pen(const QColor& color, double width) {
	QPen p(color, width);
	p.setCapStyle(Qt::RoundCap);
	p.setJoinStyle(Qt::RoundJoin);
	return p;
}

void arrowHead(QPainter& p, QPointF tip, double angleDegrees, double size, const QColor& color) {
	const double a = angleDegrees * M_PI / 180.0;
	const QPointF back(std::cos(a), std::sin(a));
	const QPointF side(-back.y(), back.x());
	QPainterPath path;
	path.moveTo(tip);
	path.lineTo(tip - back * size + side * size * 0.6);
	path.lineTo(tip - back * size - side * size * 0.6);
	path.closeSubpath();
	p.setPen(Qt::NoPen);
	p.setBrush(color);
	p.drawPath(path);
}

/// Isometric cube: top, left, right faces.
void cube(QPainter& p, QPointF center, double r, const QColor& top, const QColor& left, const QColor& right) {
	const QPointF up(0, -r), lt(-r * 0.87, -r * 0.5), rt(r * 0.87, -r * 0.5), dn(0, r), lb(-r * 0.87, r * 0.5), rb(r * 0.87, r * 0.5);
	const QPointF c = center;
	p.setPen(Qt::NoPen);
	p.setBrush(top);
	p.drawPolygon(QPolygonF{c + up, c + rt, c, c + lt});
	p.setBrush(left);
	p.drawPolygon(QPolygonF{c + lt, c, c + dn, c + lb});
	p.setBrush(right);
	p.drawPolygon(QPolygonF{c, c + rt, c + rb, c + dn});
}

void drawIcon(QPainter& p, const QString& name) {
	p.setRenderHint(QPainter::Antialiasing);
	// --- Side bar pages (coloured) -----------------------------------------------------------
	if (name == "page:map") {
		QPainterPath frame;
		frame.addRoundedRect(QRectF(6, 10, 52, 44), 8, 8);
		p.setClipPath(frame);
		QLinearGradient sky(0, 10, 0, 54);
		sky.setColorAt(0, QColor(94, 196, 240));
		sky.setColorAt(1, QColor(170, 225, 250));
		p.fillRect(QRectF(6, 10, 52, 44), sky);
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(255, 214, 80));
		p.drawEllipse(QPointF(44, 22), 5, 5);
		p.setBrush(QColor(48, 158, 86));
		p.drawPolygon(QPolygonF{{2, 56}, {24, 26}, {44, 56}});
		p.setBrush(QColor(78, 196, 108));
		p.drawPolygon(QPolygonF{{20, 56}, {42, 32}, {62, 56}});
		p.setClipping(false);
		return;
	}
	if (name == "page:materials") {
		QRadialGradient ball(QPointF(24, 22), 34);
		ball.setColorAt(0, QColor(250, 250, 252));
		ball.setColorAt(0.55, QColor(170, 176, 186));
		ball.setColorAt(1, QColor(84, 90, 102));
		p.setPen(Qt::NoPen);
		p.setBrush(ball);
		p.drawEllipse(QPointF(32, 32), 24, 24);
		p.setPen(pen(QColor(255, 255, 255, 120), 3));
		p.drawLine(QPointF(18, 46), QPointF(46, 18));
		return;
	}
	if (name == "page:models") {
		cube(p, {32, 34}, 24, QColor(222, 168, 96), QColor(186, 132, 68), QColor(150, 102, 50));
		p.setPen(pen(QColor(110, 72, 34), 2));
		p.drawLine(QPointF(32, 34), QPointF(32, 58));
		p.drawLine(QPointF(11, 22), QPointF(32, 34));
		p.drawLine(QPointF(53, 22), QPointF(32, 34));
		return;
	}
	if (name == "page:sounds") {
		const QColor pink(255, 84, 140);
		p.setPen(Qt::NoPen);
		p.setBrush(pink);
		p.drawEllipse(QPointF(22, 48), 10, 8);
		p.setPen(pen(pink, 6));
		p.drawLine(QPointF(30, 46), QPointF(30, 10));
		QPainterPath flag;
		flag.moveTo(30, 10);
		flag.cubicTo(40, 14, 50, 18, 48, 32);
		p.setPen(pen(pink, 6));
		p.setBrush(Qt::NoBrush);
		p.drawPath(flag);
		return;
	}
	if (name == "page:particles") {
		p.setPen(Qt::NoPen);
		for (int i = 0; i < 8; ++i) {
			const double a = i * M_PI / 4;
			const double r = (i % 2) ? 5.0 : 6.5;
			p.setBrush(i % 2 ? QColor(255, 140, 190) : QColor(240, 80, 160));
			p.drawEllipse(QPointF(32 + std::cos(a) * 21, 32 + std::sin(a) * 21), r, r);
		}
		p.setBrush(QColor(255, 200, 90));
		p.drawEllipse(QPointF(32, 32), 7, 7);
		return;
	}
	if (name == "page:mod") {
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(58, 142, 230));
		p.drawRoundedRect(QRectF(8, 10, 48, 44), 7, 7);
		p.setBrush(QColor(235, 242, 252));
		p.drawRoundedRect(QRectF(14, 22, 36, 26), 3, 3);
		p.setBrush(QColor(170, 206, 245));
		p.drawEllipse(QPointF(15, 16), 2.5, 2.5);
		p.drawEllipse(QPointF(23, 16), 2.5, 2.5);
		return;
	}
	if (name == "page:texts") {
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(166, 98, 255));
		p.drawRect(QRectF(12, 10, 40, 9));
		p.drawRect(QRectF(27, 10, 10, 44));
		return;
	}

	// --- Tools (line icons) ------------------------------------------------------------------
	if (name == "tool:select") {
		QPainterPath arrow;
		arrow.moveTo(18, 8);
		arrow.lineTo(18, 50);
		arrow.lineTo(28, 40);
		arrow.lineTo(36, 56);
		arrow.lineTo(43, 52);
		arrow.lineTo(35, 37);
		arrow.lineTo(49, 36);
		arrow.closeSubpath();
		p.setPen(pen(QColor(30, 34, 40), 3));
		p.setBrush(LINE);
		p.drawPath(arrow);
		return;
	}
	if (name == "tool:move") {
		p.setPen(pen(LINE, 5));
		p.drawLine(QPointF(32, 12), QPointF(32, 52));
		p.drawLine(QPointF(12, 32), QPointF(52, 32));
		arrowHead(p, {32, 6}, -90, 10, LINE);
		arrowHead(p, {32, 58}, 90, 10, LINE);
		arrowHead(p, {6, 32}, 180, 10, LINE);
		arrowHead(p, {58, 32}, 0, 10, LINE);
		return;
	}
	if (name == "tool:rotate") {
		p.setPen(pen(LINE, 5));
		p.setBrush(Qt::NoBrush);
		p.drawArc(QRectF(12, 12, 40, 40), 60 * 16, 290 * 16);
		arrowHead(p, {44, 10}, -10, 12, LINE);
		return;
	}
	if (name == "tool:scale") {
		p.setPen(pen(LINE, 4));
		p.setBrush(Qt::NoBrush);
		p.drawRect(QRectF(10, 30, 24, 24));
		p.setPen(pen(LINE, 5));
		p.drawLine(QPointF(26, 38), QPointF(50, 14));
		arrowHead(p, {55, 9}, -45, 12, LINE);
		return;
	}
	if (name == "tool:block") {
		cube(p, {32, 33}, 24, QColor(150, 174, 210), QColor(104, 128, 168), QColor(78, 98, 136));
		return;
	}
	if (name == "tool:entity") {
		p.setPen(Qt::NoPen);
		QRadialGradient glow(QPointF(28, 22), 26);
		glow.setColorAt(0, QColor(255, 244, 180));
		glow.setColorAt(1, QColor(255, 196, 40));
		p.setBrush(glow);
		p.drawEllipse(QPointF(32, 26), 17, 17);
		p.drawRect(QRectF(24, 34, 16, 10));
		p.setBrush(QColor(170, 176, 186));
		p.drawRoundedRect(QRectF(23, 44, 18, 6), 2, 2);
		p.drawRoundedRect(QRectF(25, 51, 14, 5), 2, 2);
		return;
	}
	if (name == "tool:clip") {
		p.setPen(pen(LINE, 4));
		p.setBrush(Qt::NoBrush);
		p.drawEllipse(QPointF(18, 46), 8, 8);
		p.drawEllipse(QPointF(46, 46), 8, 8);
		p.setPen(pen(LINE, 5));
		p.drawLine(QPointF(23, 40), QPointF(46, 8));
		p.drawLine(QPointF(41, 40), QPointF(18, 8));
		return;
	}
	if (name == "tool:vertex") {
		p.setPen(pen(DIM, 3));
		p.setBrush(Qt::NoBrush);
		const QPointF c(32, 33);
		const double r = 22;
		const QPointF pts[] = {c + QPointF(0, -r), c + QPointF(r * 0.87, -r * 0.5), c + QPointF(r * 0.87, r * 0.5), c + QPointF(0, r),
		                       c + QPointF(-r * 0.87, r * 0.5), c + QPointF(-r * 0.87, -r * 0.5)};
		p.drawPolygon(pts, 6);
		p.drawLine(c, pts[1]);
		p.drawLine(c, pts[3]);
		p.drawLine(c, pts[5]);
		p.setPen(Qt::NoPen);
		p.setBrush(ACCENT);
		for (const auto& q : pts) p.drawEllipse(q, 4.5, 4.5);
		p.drawEllipse(c, 4.5, 4.5);
		return;
	}
	if (name == "tool:polygon") {
		QPolygonF penta;
		for (int i = 0; i < 5; ++i) {
			const double a = -M_PI / 2 + i * 2 * M_PI / 5;
			penta << QPointF(32 + std::cos(a) * 23, 34 + std::sin(a) * 23);
		}
		p.setPen(pen(LINE, 4));
		p.setBrush(QColor(222, 227, 234, 40));
		p.drawPolygon(penta);
		p.setPen(Qt::NoPen);
		p.setBrush(ACCENT);
		for (const auto& q : penta) p.drawEllipse(q, 4.5, 4.5);
		return;
	}
	if (name == "mode:objects") {
		cube(p, {32, 33}, 22, QColor(236, 186, 110), QColor(200, 140, 60), QColor(160, 104, 40));
		return;
	}
	if (name == "mode:faces") {
		cube(p, {32, 33}, 22, QColor(90, 98, 112), QColor(70, 76, 88), QColor(60, 66, 76));
		const QPointF c(32, 33);
		const double r = 22;
		p.setBrush(ACCENT);
		p.drawPolygon(QPolygonF{c + QPointF(0, -r), c + QPointF(r * 0.87, -r * 0.5), c, c + QPointF(-r * 0.87, -r * 0.5)});
		return;
	}

	// --- Tool bar ---------------------------------------------------------------------------
	if (name == "undo" || name == "redo") {
		if (name == "redo") {
			p.translate(SIZE, 0);
			p.scale(-1, 1);
		}
		QPainterPath path;
		path.moveTo(16, 26);
		path.lineTo(40, 26);
		path.cubicTo(54, 26, 54, 50, 40, 50);
		path.lineTo(28, 50);
		p.setPen(pen(LINE, 5));
		p.setBrush(Qt::NoBrush);
		p.drawPath(path);
		arrowHead(p, {8, 26}, 180, 12, LINE);
		return;
	}
	if (name == "grid") {
		p.setPen(pen(LINE, 3));
		for (int i = 0; i < 4; ++i) {
			const double v = 12 + i * 13.3;
			p.drawLine(QPointF(v, 10), QPointF(v, 54));
			p.drawLine(QPointF(10, v), QPointF(54, v));
		}
		return;
	}
	if (name == "snap") {
		p.setBrush(Qt::NoBrush);
		p.setPen(pen(QColor(230, 80, 80), 10));
		p.setPen(QPen(QColor(230, 80, 80), 10, Qt::SolidLine, Qt::FlatCap));
		p.drawArc(QRectF(14, 16, 36, 36), 180 * 16, 180 * 16);
		p.drawLine(QPointF(14, 34), QPointF(14, 18));
		p.drawLine(QPointF(50, 34), QPointF(50, 18));
		p.setPen(QPen(LINE, 10, Qt::SolidLine, Qt::FlatCap));
		p.drawLine(QPointF(14, 18), QPointF(14, 9));
		p.drawLine(QPointF(50, 18), QPointF(50, 9));
		return;
	}
	if (name == "lock") {
		p.setPen(pen(LINE, 5));
		p.setBrush(Qt::NoBrush);
		p.drawArc(QRectF(20, 8, 24, 28), 0, 180 * 16);
		p.drawLine(QPointF(20, 22), QPointF(20, 28));
		p.drawLine(QPointF(44, 22), QPointF(44, 28));
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(240, 190, 70));
		p.drawRoundedRect(QRectF(13, 28, 38, 28), 5, 5);
		return;
	}
	if (name == "views") {
		p.setPen(pen(LINE, 4));
		p.setBrush(Qt::NoBrush);
		p.drawRoundedRect(QRectF(9, 11, 46, 42), 4, 4);
		p.drawLine(QPointF(32, 11), QPointF(32, 53));
		p.drawLine(QPointF(9, 32), QPointF(55, 32));
		return;
	}
	if (name == "check") {
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(60, 180, 100));
		p.drawEllipse(QPointF(32, 32), 24, 24);
		p.setPen(pen(Qt::white, 6));
		p.drawPolyline(QPolygonF{{20, 33}, {29, 42}, {45, 24}});
		return;
	}
	if (name == "compile") {
		// Wrench.
		p.setPen(pen(LINE, 9));
		p.drawLine(QPointF(18, 48), QPointF(38, 28));
		p.setPen(Qt::NoPen);
		p.setBrush(LINE);
		p.drawEllipse(QPointF(42, 22), 13, 13);
		p.setBrush(QColor(37, 39, 43));
		p.drawRect(QRectF(40, 6, 8, 16));
		return;
	}
	if (name == "run") {
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(28, 28, 30));
		p.drawPolygon(QPolygonF{{18, 10}, {54, 32}, {18, 54}});
		return;
	}
	if (name == "gear") {
		p.setPen(Qt::NoPen);
		p.setBrush(LINE);
		for (int i = 0; i < 8; ++i) {
			p.save();
			p.translate(32, 32);
			p.rotate(i * 45);
			p.drawRoundedRect(QRectF(-5, -26, 10, 12), 2, 2);
			p.restore();
		}
		p.drawEllipse(QPointF(32, 32), 18, 18);
		p.setBrush(QColor(37, 39, 43));
		p.drawEllipse(QPointF(32, 32), 7, 7);
		return;
	}
	if (name.startsWith("panel:")) {
		p.setPen(pen(DIM, 4));
		p.setBrush(Qt::NoBrush);
		const QRectF frame(8, 12, 48, 40);
		p.drawRoundedRect(frame, 5, 5);
		p.setPen(Qt::NoPen);
		p.setBrush(LINE);
		if (name == "panel:left") p.drawRect(QRectF(12, 16, 12, 32));
		if (name == "panel:right") p.drawRect(QRectF(40, 16, 12, 32));
		if (name == "panel:bottom") p.drawRect(QRectF(12, 38, 40, 10));
		return;
	}
	// --- View switches (what is shown) -------------------------------------------------------
	if (name == "view:helpers") {
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(90, 170, 240));
		p.drawRoundedRect(QRectF(12, 12, 40, 40), 8, 8);
		p.setBrush(Qt::white);
		p.drawEllipse(QPointF(32, 22), 4, 4);
		p.drawRoundedRect(QRectF(28, 29, 8, 16), 2, 2);
		return;
	}
	if (name == "view:triggers") {
		p.setPen(pen(QColor(255, 150, 60), 4));
		p.setBrush(QColor(255, 150, 60, 70));
		p.drawRect(QRectF(12, 14, 40, 36));
		return;
	}
	if (name == "view:tools") {
		p.setPen(pen(QColor(220, 200, 80), 4));
		p.setBrush(QColor(220, 200, 80, 60));
		p.drawRect(QRectF(12, 14, 40, 36));
		p.setPen(pen(QColor(220, 200, 80), 3));
		p.drawLine(QPointF(12, 14), QPointF(52, 50));
		p.drawLine(QPointF(52, 14), QPointF(12, 50));
		return;
	}
	if (name == "view:sky") {
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(120, 190, 245));
		p.drawEllipse(QPointF(24, 36), 13, 13);
		p.drawEllipse(QPointF(38, 30), 16, 16);
		p.drawEllipse(QPointF(48, 40), 10, 10);
		p.drawRect(QRectF(20, 38, 32, 12));
		return;
	}
	if (name == "view:cubemaps") {
		QRadialGradient ball(QPointF(26, 24), 30);
		ball.setColorAt(0, QColor(240, 250, 255));
		ball.setColorAt(0.6, QColor(110, 170, 220));
		ball.setColorAt(1, QColor(40, 80, 130));
		p.setPen(Qt::NoPen);
		p.setBrush(ball);
		p.drawEllipse(QPointF(32, 32), 20, 20);
		return;
	}
	if (name == "view:names") {
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(200, 200, 200));
		p.drawRoundedRect(QRectF(8, 18, 48, 28), 6, 6);
		QFont font = p.font();
		font.setPixelSize(22);
		font.setBold(true);
		p.setFont(font);
		p.setPen(QColor(30, 30, 30));
		p.drawText(QRectF(8, 18, 48, 28), Qt::AlignCenter, QStringLiteral("Aa"));
		return;
	}
	if (name == "view:connections") {
		p.setPen(pen(QColor(90, 220, 110), 4));
		p.drawLine(QPointF(18, 44), QPointF(44, 20));
		arrowHead(p, {48, 16}, -45, 12, QColor(90, 220, 110));
		p.setPen(Qt::NoPen);
		p.setBrush(LINE);
		p.drawEllipse(QPointF(16, 46), 7, 7);
		return;
	}
	if (name == "view:grid3d") {
		p.setPen(pen(LINE, 3));
		for (int i = 0; i < 5; ++i) {
			const double t = i / 4.0;
			p.drawLine(QPointF(20 + t * 24, 14), QPointF(4 + t * 56, 54));   // lines into the distance
		}
		for (const double y : {14.0, 26.0, 39.0, 54.0}) {
			const double inset = (54 - y) * 0.4;
			p.drawLine(QPointF(4 + inset, y), QPointF(60 - inset, y));
		}
		return;
	}
	if (name == "search") {
		p.setPen(pen(DIM, 5));
		p.setBrush(Qt::NoBrush);
		p.drawEllipse(QPointF(27, 27), 15, 15);
		p.drawLine(QPointF(38, 38), QPointF(54, 54));
		return;
	}
	if (name == "eye") {
		QPainterPath lid;
		lid.moveTo(6, 32);
		lid.quadTo(32, 6, 58, 32);
		lid.quadTo(32, 58, 6, 32);
		p.setPen(pen(LINE, 4));
		p.setBrush(Qt::NoBrush);
		p.drawPath(lid);
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(58, 160, 230));
		p.drawEllipse(QPointF(32, 32), 9, 9);
		return;
	}
}

/// Mapsmith's logo (64 units square): a map tile on a dark tile with a hammer across it.
void drawAppLogo(QPainter& p) {
	p.setRenderHint(QPainter::Antialiasing);
	QLinearGradient back(0, 0, 0, 64);
	back.setColorAt(0, QColor(46, 46, 48));
	back.setColorAt(1, QColor(24, 24, 26));
	p.setPen(Qt::NoPen);
	p.setBrush(back);
	p.drawRoundedRect(QRectF(2, 2, 60, 60), 13, 13);
	// The map: an orange floor tile seen from above at an angle, with its grid.
	const QPointF top(32, 22), right(54, 34), bottom(32, 46), left(10, 34);
	p.setBrush(QColor(245, 165, 36));
	p.drawPolygon(QPolygonF{top, right, bottom, left});
	p.setPen(QPen(QColor(150, 92, 12), 1.4));
	for (int i = 1; i < 4; ++i) {
		const double t = i / 4.0;
		p.drawLine(left + (top - left) * t, bottom + (right - bottom) * t);
		p.drawLine(left + (bottom - left) * t, top + (right - top) * t);
	}
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(196, 120, 16));
	p.drawPolygon(QPolygonF{left, bottom, bottom + QPointF(0, 5), left + QPointF(0, 5)});
	p.setBrush(QColor(160, 96, 10));
	p.drawPolygon(QPolygonF{bottom, right, right + QPointF(0, 5), bottom + QPointF(0, 5)});
	// The hammer: wooden handle, steel head.
	p.save();
	p.translate(36, 28);
	p.rotate(-38);
	p.setBrush(QColor(170, 112, 60));
	p.drawRoundedRect(QRectF(-3, -4, 6, 34), 3, 3);
	QLinearGradient steel(0, -16, 0, -4);
	steel.setColorAt(0, QColor(236, 238, 242));
	steel.setColorAt(1, QColor(150, 156, 166));
	p.setBrush(steel);
	p.drawRoundedRect(QRectF(-14, -16, 28, 12), 3, 3);
	p.setBrush(QColor(120, 126, 136));
	p.drawRect(QRectF(10, -16, 4, 12));
	p.restore();
}

} // namespace

QIcon appIcon() {
	static const QIcon result = [] {
		QIcon out;
		for (const int size : {16, 24, 32, 48, 64, 128, 256}) {
			QPixmap pixmap(size, size);
			pixmap.fill(Qt::transparent);
			QPainter p(&pixmap);
			p.scale(size / double(SIZE), size / double(SIZE));
			drawAppLogo(p);
			p.end();
			out.addPixmap(pixmap);
		}
		return out;
	}();
	return result;
}

QIcon icon(const QString& name, const QString& shortcut) {
	static QHash<QString, QIcon> cache;
	const QString key = name + '|' + shortcut;
	if (const auto it = cache.constFind(key); it != cache.constEnd()) {
		return *it;
	}
	QPixmap pixmap(SIZE, SIZE);
	pixmap.fill(Qt::transparent);
	{
		QPainter p(&pixmap);
		drawIcon(p, name);
		if (!shortcut.isEmpty()) {
			p.resetTransform();
			QFont font = p.font();
			font.setPixelSize(17);
			font.setBold(true);
			p.setFont(font);
			p.setPen(QColor(160, 168, 180));
			p.drawText(QRectF(0, 0, SIZE, SIZE + 2), Qt::AlignRight | Qt::AlignBottom, shortcut);
		}
	}
	QIcon result(pixmap);
	cache.insert(key, result);
	return result;
}

} // namespace sme::app
