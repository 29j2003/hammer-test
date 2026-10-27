// camera.h - Viewport cameras: free-flying perspective camera and the three 2D views.
//
// Source coordinates: X east, Y north, Z up. Yaw turns around Z, pitch looks up/down.

#pragma once

#include <QMatrix4x4>
#include <QVector3D>

#include <cmath>

#include <sme/math.h>

namespace sme::app {

/// Camera of one view: position, angles and zoom; makes the OpenGL matrices and picking rays.
struct Camera {
	enum class Kind { Perspective, Top, Front, Side };

	Kind kind = Kind::Perspective;
	QVector3D position{-512, -512, 384};
	float yaw = 45;              ///< degrees, 0 = looking along +X
	float pitch = -25;           ///< degrees, negative = looking down
	float fov = 80;              ///< vertical field of view in degrees
	float zoom = 0.5f;           ///< 2D views: pixels per world unit

	/// Top, front or side view (orthographic).
	[[nodiscard]] bool is2D() const { return kind != Kind::Perspective; }

	/// Direction the camera looks, and its up and right directions (fixed in the 2D views).
	[[nodiscard]] QVector3D forward() const {
		switch (kind) {
			case Kind::Top: return {0, 0, -1};
			case Kind::Front: return {-1, 0, 0};
			case Kind::Side: return {0, 1, 0};
			default: break;
		}
		const float y = qDegreesToRadians(yaw), p = qDegreesToRadians(pitch);
		return {std::cos(p) * std::cos(y), std::cos(p) * std::sin(y), std::sin(p)};
	}
	[[nodiscard]] QVector3D up() const {
		if (kind == Kind::Top) {
			return {0, 1, 0};
		}
		if (kind != Kind::Perspective) {
			return {0, 0, 1};
		}
		return QVector3D::crossProduct(right(), forward()).normalized();
	}
	[[nodiscard]] QVector3D right() const {
		switch (kind) {
			case Kind::Top: return {1, 0, 0};
			case Kind::Front: return {0, 1, 0};
			case Kind::Side: return {1, 0, 0};
			default: break;
		}
		return QVector3D::crossProduct(forward(), {0, 0, 1}).normalized();
	}

	/// View and projection matrices for OpenGL.
	[[nodiscard]] QMatrix4x4 view() const {
		QMatrix4x4 m;
		m.lookAt(position, position + forward(), up());
		return m;
	}
	[[nodiscard]] QMatrix4x4 projection(int width, int height) const {
		QMatrix4x4 m;
		const float aspect = height > 0 ? float(width) / float(height) : 1.f;
		if (is2D()) {
			const float hw = width / (2 * zoom), hh = height / (2 * zoom);
			m.ortho(-hw, hw, -hh, hh, -131072.f, 131072.f);
		} else {
			m.perspective(fov, aspect, 2.f, 131072.f);
		}
		return m;
	}

	/// World-space ray through a pixel (pixel coordinates, origin top-left).
	[[nodiscard]] Ray rayAt(double px, double py, int width, int height) const {
		const double nx = (2.0 * px / std::max(1, width)) - 1.0;
		const double ny = 1.0 - (2.0 * py / std::max(1, height));
		const QVector3D f = forward(), r = right(), u = up();
		if (is2D()) {
			const QVector3D p = position + r * float(nx * width / (2 * zoom)) + u * float(ny * height / (2 * zoom)) - f * 65536.f;
			return {{p.x(), p.y(), p.z()}, {f.x(), f.y(), f.z()}};
		}
		const double t = std::tan(qDegreesToRadians(fov) / 2);
		const double aspect = height > 0 ? double(width) / height : 1.0;
		QVector3D d = f + r * float(nx * t * aspect) + u * float(ny * t);
		d.normalize();
		return {{position.x(), position.y(), position.z()}, {d.x(), d.y(), d.z()}};
	}

	/// Pixel position of a world point; returns false if behind the camera.
	[[nodiscard]] bool project(const Vec3& p, int width, int height, QPointF& out) const {
		const QVector4D clip = projection(width, height) * view() * QVector4D(float(p.x), float(p.y), float(p.z), 1.f);
		if (clip.w() <= 1e-4f) {
			return false;
		}
		out = QPointF((clip.x() / clip.w() + 1) * 0.5 * width, (1 - clip.y() / clip.w()) * 0.5 * height);
		return true;
	}
};

/// Conversion between the core's Vec3 (double) and Qt's QVector3D (float).
[[nodiscard]] inline QVector3D toQt(const Vec3& v) {
	return {float(v.x), float(v.y), float(v.z)};
}
[[nodiscard]] inline Vec3 fromQt(const QVector3D& v) {
	return {v.x(), v.y(), v.z()};
}

} // namespace sme::app
