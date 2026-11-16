// facade_texture.cpp - What a texture is for in a building (see facade_texture.h).

#include <sme/facade_texture.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <sstream>
#include <tuple>

#include <sme/text.h>

namespace sme {

namespace {

/// The picture on a small grid: luminance, saturation, opacity per cell.
struct Grid {
	int w = 0, h = 0;
	std::vector<double> lum, sat, alpha;
	[[nodiscard]] std::size_t at(int x, int y) const { return static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x); }
};

Grid sample(const ImageRgba& image, int maxWidth) {
	Grid g;
	const int iw = image.width, ih = image.height;
	g.w = std::min(iw, maxWidth);
	g.h = std::max(1, static_cast<int>(std::lround(static_cast<double>(g.w) * ih / std::max(1, iw))));
	g.h = std::min(g.h, std::max(1, ih));
	g.lum.assign(static_cast<std::size_t>(g.w) * static_cast<std::size_t>(g.h), 0);
	g.sat = g.lum;
	g.alpha = g.lum;
	std::vector<int> count(g.lum.size(), 0);
	const auto* px = reinterpret_cast<const unsigned char*>(image.pixels.data());
	for (int y = 0; y < ih; ++y) {
		const int gy = std::min(g.h - 1, y * g.h / ih);
		for (int x = 0; x < iw; ++x) {
			const int gx = std::min(g.w - 1, x * g.w / iw);
			const std::size_t p = (static_cast<std::size_t>(y) * static_cast<std::size_t>(iw) + static_cast<std::size_t>(x)) * 4;
			const double r = px[p] / 255.0, gr = px[p + 1] / 255.0, b = px[p + 2] / 255.0, a = px[p + 3] / 255.0;
			const double mx = std::max({r, gr, b}), mn = std::min({r, gr, b});
			const std::size_t c = g.at(gx, gy);
			g.lum[c] += 0.299 * r + 0.587 * gr + 0.114 * b;
			g.sat[c] += mx > 1e-6 ? (mx - mn) / mx : 0;
			g.alpha[c] += a;
			++count[c];
		}
	}
	for (std::size_t c = 0; c < count.size(); ++c) {
		const double n = std::max(1, count[c]);
		g.lum[c] /= n;
		g.sat[c] /= n;
		g.alpha[c] /= n;
	}
	return g;
}

double median(std::vector<double> v) {
	if (v.empty()) return 0;
	const auto mid = v.begin() + static_cast<std::ptrdiff_t>(v.size() / 2);
	std::nth_element(v.begin(), mid, v.end());
	return *mid;
}

/// Robust spread: 1.4826 x the median absolute deviation.
double spread(const std::vector<double>& v, double m) {
	std::vector<double> d(v.size());
	for (std::size_t i = 0; i < v.size(); ++i) d[i] = std::abs(v[i] - m);
	return 1.4826 * median(std::move(d));
}

double variance(const std::vector<double>& v) {
	if (v.size() < 2) return 0;
	const double mean = std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(v.size());
	double s = 0;
	for (const double x : v) s += (x - mean) * (x - mean);
	return s / static_cast<double>(v.size());
}

struct Blob {
	int x0 = 0, y0 = 0, x1 = 0, y1 = 0;   // inclusive, in the rolled grid
	int area = 0;
	double contrast = 0;
	[[nodiscard]] int w() const { return x1 - x0 + 1; }
	[[nodiscard]] int h() const { return y1 - y0 + 1; }
	[[nodiscard]] double fill() const { return static_cast<double>(area) / (w() * h()); }
	[[nodiscard]] double cx() const { return (x0 + x1) / 2.0; }
	[[nodiscard]] double cy() const { return (y0 + y1) / 2.0; }
};

/// Distinct centres (merged within `tolerance`).
std::vector<double> centres(std::vector<double> values, double tolerance) {
	std::sort(values.begin(), values.end());
	std::vector<double> out;
	for (const double v : values) {
		if (out.empty() || v - out.back() > tolerance) out.push_back(v);
	}
	return out;
}

struct WindowFind {
	bool found = false;
	int columns = 0, rows = 0;
	std::vector<FacadeWindowRect> windows;
	double share = 0;
	double confidence = 0;
	bool tiny = false;   ///< A regular grid of very small windows (a far / skybox facade)
};

/// Windows against the wall level `lm` (the luminance most of the wall has).
WindowFind findWindowsAt(const Grid& g, double lm) {
	WindowFind out;
	const int W = g.w, H = g.h;
	if (W < 16 || H < 16) return out;
	const double sm = median(g.sat);
	// The wall's own spread (cells near its level; the glass would widen it).
	std::vector<double> nearWall;
	for (const double l : g.lum) {
		if (std::abs(l - lm) < 0.15) nearWall.push_back(l);
	}
	const double tl = std::max(0.08, 2.5 * spread(nearWall.size() * 4 >= g.lum.size() ? nearWall : g.lum, lm));
	const double ts = std::max(0.12, 2.5 * spread(g.sat, sm));
	std::vector<char> marked(g.lum.size(), 0);
	int markedCount = 0;
	for (std::size_t c = 0; c < marked.size(); ++c) {
		marked[c] = std::abs(g.lum[c] - lm) > tl || std::abs(g.sat[c] - sm) > ts;
		markedCount += marked[c];
	}
	const double markedShare = static_cast<double>(markedCount) / static_cast<double>(marked.size());
	if (markedShare < 0.02 || markedShare > 0.75) return out;
	// Closing over thin mullions (on the torus: the texture repeats).
	const auto wrapAt = [&](int x, int y) { return g.at((x % W + W) % W, (y % H + H) % H); };
	std::vector<char> grown(marked.size(), 0);
	for (int y = 0; y < H; ++y) {
		for (int x = 0; x < W; ++x) {
			char v = 0;
			for (int dy = -1; dy <= 1 && !v; ++dy) {
				for (int dx = -1; dx <= 1 && !v; ++dx) v = marked[wrapAt(x + dx, y + dy)];
			}
			grown[g.at(x, y)] = v;
		}
	}
	std::vector<char> closed(marked.size(), 0);
	for (int y = 0; y < H; ++y) {
		for (int x = 0; x < W; ++x) {
			char v = 1;
			for (int dy = -1; dy <= 1 && v; ++dy) {
				for (int dx = -1; dx <= 1 && v; ++dx) v = grown[wrapAt(x + dx, y + dy)];
			}
			closed[g.at(x, y)] = v;
		}
	}
	// Roll so the plainest column and row lie at the edges.
	int bestX = 0, bestY = 0, fewestX = H + 1, fewestY = W + 1;
	for (int x = 0; x < W; ++x) {
		int n = 0;
		for (int y = 0; y < H; ++y) n += closed[g.at(x, y)];
		if (n < fewestX) {
			fewestX = n;
			bestX = x;
		}
	}
	for (int y = 0; y < H; ++y) {
		int n = 0;
		for (int x = 0; x < W; ++x) n += closed[g.at(x, y)];
		if (n < fewestY) {
			fewestY = n;
			bestY = y;
		}
	}
	const auto rolled = [&](int x, int y) { return g.at((x + bestX) % W, (y + bestY) % H); };
	// Blobs (4-connected).
	std::vector<int> label(marked.size(), -1);
	std::vector<Blob> blobs;
	std::vector<std::pair<int, int>> stack;
	for (int y = 0; y < H; ++y) {
		for (int x = 0; x < W; ++x) {
			const std::size_t c = g.at(x, y);
			if (!closed[rolled(x, y)] || label[c] >= 0) continue;
			Blob blob{x, y, x, y, 0, 0};
			double diff = 0;
			stack.clear();
			stack.emplace_back(x, y);
			label[c] = static_cast<int>(blobs.size());
			while (!stack.empty()) {
				const auto [px, py] = stack.back();
				stack.pop_back();
				++blob.area;
				diff += std::abs(g.lum[rolled(px, py)] - lm);
				blob.x0 = std::min(blob.x0, px);
				blob.x1 = std::max(blob.x1, px);
				blob.y0 = std::min(blob.y0, py);
				blob.y1 = std::max(blob.y1, py);
				const int nx[4] = {px - 1, px + 1, px, px}, ny[4] = {py, py, py - 1, py + 1};
				for (int k = 0; k < 4; ++k) {
					if (nx[k] < 0 || ny[k] < 0 || nx[k] >= W || ny[k] >= H) continue;
					const std::size_t n = g.at(nx[k], ny[k]);
					if (label[n] >= 0 || !closed[rolled(nx[k], ny[k])]) continue;
					label[n] = static_cast<int>(blobs.size());
					stack.emplace_back(nx[k], ny[k]);
				}
			}
			blob.contrast = diff / blob.area;
			blobs.push_back(blob);
		}
	}
	// Rectangles of a window's size.
	std::vector<Blob> candidates;
	for (const auto& b : blobs) {
		if (b.w() < std::max(3, W * 3 / 100) || b.h() < std::max(3, H * 3 / 100)) continue;
		if (b.w() > W * 0.92 || b.h() > H * 0.92) continue;
		const double aspect = static_cast<double>(b.w()) / b.h();
		if (aspect < 0.2 || aspect > 5 || b.fill() < 0.6) continue;
		candidates.push_back(b);
	}
	if (candidates.empty()) return out;
	// The largest group of the same size.
	std::vector<Blob> group;
	double groupArea = 0;
	for (const auto& c : candidates) {
		std::vector<Blob> same;
		for (const auto& d : candidates) {
			if (std::abs(d.w() - c.w()) <= 0.35 * c.w() + 1 && std::abs(d.h() - c.h()) <= 0.35 * c.h() + 1) same.push_back(d);
		}
		double area = 0;
		for (const auto& d : same) area += d.area;
		if (same.size() > group.size() || (same.size() == group.size() && area > groupArea)) {
			group = same;
			groupArea = area;
		}
	}
	double meanW = 0, meanH = 0, fill = 0, contrast = 0;
	std::vector<double> xs, ys;
	for (const auto& b : group) {
		meanW += b.w();
		meanH += b.h();
		fill += b.fill();
		contrast += b.contrast;
		xs.push_back(b.cx());
		ys.push_back(b.cy());
	}
	const double n = static_cast<double>(group.size());
	meanW /= n;
	meanH /= n;
	fill /= n;
	contrast /= n;
	const auto cols = centres(xs, std::max(2.0, meanW * 0.4));
	const auto rows = centres(ys, std::max(2.0, meanH * 0.4));
	const double gridFill = n / static_cast<double>(cols.size() * rows.size());
	const double share = groupArea / static_cast<double>(W * H);
	bool found = false;
	if (group.size() >= 2) {
		found = gridFill >= 0.6 && share <= 0.5;
	} else {
		const auto& b = group.front();
		found = b.fill() >= 0.82 && b.w() >= W * 0.12 && b.h() >= H * 0.12 && b.contrast >= 1.6 * tl;
	}
	if (!found) return out;
	out.found = true;
	out.columns = static_cast<int>(cols.size());
	out.rows = static_cast<int>(rows.size());
	out.share = share;
	out.tiny = (meanH < H * 0.1 && out.rows >= 3) || out.columns * out.rows > 12;
	out.confidence = std::clamp(0.4 * gridFill + 0.3 * fill + 0.3 * std::min(1.0, contrast / (3 * tl)), 0.0, 1.0);
	for (const auto& b : group) {
		FacadeWindowRect r;
		r.u0 = static_cast<double>(b.x0 + bestX) / W;
		r.u1 = static_cast<double>(b.x1 + 1 + bestX) / W;
		r.v0 = static_cast<double>(b.y0 + bestY) / H;
		r.v1 = static_cast<double>(b.y1 + 1 + bestY) / H;
		// Back into one tile (u0 / v0 in 0..1; the end may run past 1).
		if (r.u0 >= 1) {
			r.u0 -= 1;
			r.u1 -= 1;
		}
		if (r.v0 >= 1) {
			r.v0 -= 1;
			r.v1 -= 1;
		}
		out.windows.push_back(r);
	}
	std::sort(out.windows.begin(), out.windows.end(), [](const FacadeWindowRect& a, const FacadeWindowRect& b) {
		return std::tie(a.v0, a.u0) < std::tie(b.v0, b.u0);
	});
	return out;
}

/// Windows against the median luminance; when the glass covers most of the tile (a shop front) the
/// median is the glass - then against the other level (the wall around it).
WindowFind findWindows(const Grid& g) {
	const double lm = median(g.lum);
	WindowFind found = findWindowsAt(g, lm);
	if (found.found) return found;
	const double tl = std::max(0.08, 2.5 * spread(g.lum, lm));
	std::vector<double> other;
	for (const double l : g.lum) {
		if (std::abs(l - lm) > tl) other.push_back(l);
	}
	if (other.size() * 10 < g.lum.size()) return found;
	return findWindowsAt(g, median(std::move(other)));
}

bool nameHas(const std::string& lower, std::initializer_list<const char*> words) {
	return std::any_of(words.begin(), words.end(), [&](const char* w) { return lower.find(w) != std::string::npos; });
}

std::string number(double v) {
	char buffer[32];
	std::snprintf(buffer, sizeof buffer, "%.4g", v);
	return buffer;
}

} // namespace

double TextureAnalysis::scaleFor(double floorHeight) const {
	if (scaleOverride > 0) return scaleOverride;
	if ((role == TextureRole::FacadeWindows || role == TextureRole::ShopFront) && height > 0 && floorHeight > 1) {
		double s = floorsPerTile() * floorHeight / height;
		for (const double nice : {0.125, 0.25, 0.5, 1.0, 2.0}) {
			if (std::abs(s - nice) <= nice * 0.03) s = nice;
		}
		return s;
	}
	return 0.25;
}

std::string TextureAnalysis::describe(double floorHeight) const {
	std::string text = textureRoleName(role);
	if (role == TextureRole::FacadeWindows || role == TextureRole::ShopFront) {
		const int perTile = std::max(1, static_cast<int>(windows.size()));
		text += " · " + std::to_string(perTile) + (perTile == 1 ? " window" : " windows") + " per tile";
		text += " · " + std::to_string(floorsPerTile()) + (floorsPerTile() == 1 ? " floor" : " floors") + " per tile";
		text += " · scale " + number(scaleFor(floorHeight));
	} else if (role == TextureRole::Trim) {
		text += " · once over a band's height";
	} else if (role != TextureRole::Unknown) {
		text += " · scale 0.25";
	}
	if (!reason.empty() && (role == TextureRole::Unsuitable || role == TextureRole::Unknown)) text += " (" + reason + ")";
	if (decided) text += " · set by the decision file";
	return text;
}

TextureAnalysis analyzeTexture(const ImageRgba& image, std::string_view name, int fullWidth, int fullHeight) {
	TextureAnalysis a;
	a.width = fullWidth > 0 ? fullWidth : image.width;
	a.height = fullHeight > 0 ? fullHeight : image.height;
	if (image.width == 0 || image.height == 0 || image.pixels.size() < static_cast<std::size_t>(image.width) * image.height * 4) {
		a.reason = "no picture";
		return a;
	}
	const std::string lower = toLower(name);
	const Grid g = sample(image, 128);
	a.aspect = static_cast<double>(image.width) / image.height;
	a.brightness = std::accumulate(g.lum.begin(), g.lum.end(), 0.0) / static_cast<double>(g.lum.size());
	a.saturation = std::accumulate(g.sat.begin(), g.sat.end(), 0.0) / static_cast<double>(g.sat.size());
	a.alpha = std::accumulate(g.alpha.begin(), g.alpha.end(), 0.0) / static_cast<double>(g.alpha.size());
	{
		std::vector<double> rowMeans(static_cast<std::size_t>(g.h), 0), colMeans(static_cast<std::size_t>(g.w), 0);
		for (int y = 0; y < g.h; ++y) {
			for (int x = 0; x < g.w; ++x) {
				rowMeans[static_cast<std::size_t>(y)] += g.lum[g.at(x, y)] / g.w;
				colMeans[static_cast<std::size_t>(x)] += g.lum[g.at(x, y)] / g.h;
			}
		}
		a.anisotropy = (variance(rowMeans) + 1e-5) / (variance(colMeans) + 1e-5);
		// Rows that are even across, compared with how much the rows differ: most of a strip's rows
		// are (dentils or corbels in one band do not change that), a noisy wall's are not.
		const double between = std::sqrt(variance(rowMeans));
		if (between > 0.02) {
			int even = 0;
			std::vector<double> row;
			for (int y = 0; y < g.h; ++y) {
				row.assign(g.lum.begin() + g.at(0, y), g.lum.begin() + g.at(0, y) + g.w);
				if (std::sqrt(variance(row)) < between * 0.5) ++even;
			}
			a.banded = static_cast<double>(even) / g.h;
		}
	}
	const WindowFind found = findWindows(g);
	const bool nameRoof = nameHas(lower, {"roof", "tar", "gravel", "shingle", "asphalt"});
	const bool nameTrim = nameHas(lower, {"trim", "cornice", "molding", "moulding", "ledge"});
	const bool nameShop = nameHas(lower, {"shop", "store"});
	const auto decide = [&](TextureRole role, std::string why, double confidence) {
		a.role = role;
		a.reason = std::move(why);
		a.confidence = confidence;
	};
	// Unsuitable first: see-through, garish, tiny, far facades.
	if (a.alpha < 0.85) {
		decide(TextureRole::Unsuitable, "see-through (a decal or overlay)", 0.9);
		return a;
	}
	if (a.width < 64 || a.height < 64) {
		decide(TextureRole::Unsuitable, "too small for a wall", 0.8);
		return a;
	}
	if (a.saturation > 0.72) {
		decide(TextureRole::Unsuitable, "garish colour", 0.7);
		return a;
	}
	if (a.brightness < 0.06 || a.brightness > 0.96) {
		decide(TextureRole::Unsuitable, a.brightness < 0.06 ? "nearly black" : "nearly white", 0.7);
		return a;
	}
	if (found.found) {
		a.hasWindows = !found.tiny;
		a.columns = found.columns;
		a.rows = found.rows;
		a.windows = found.windows;
		a.windowShare = found.share;
		if (found.tiny) {
			decide(TextureRole::Unsuitable, "tiny windows in a dense grid (a far or skybox facade)", found.confidence);
			return a;
		}
		double maxW = 0, maxH = 0;
		for (const auto& w : found.windows) {
			maxW = std::max(maxW, w.u1 - w.u0);
			maxH = std::max(maxH, w.v1 - w.v0);
		}
		const bool shop = found.rows == 1 && maxH >= 0.4 && maxW >= 0.35 && maxW > maxH * 0.9;
		if (shop || (nameShop && found.rows == 1)) {
			decide(TextureRole::ShopFront, "one row of big windows", found.confidence);
		} else {
			decide(TextureRole::FacadeWindows, "a regular grid of windows", found.confidence);
		}
		return a;
	}
	// Strips: much wider than high, or banded across.
	if (a.aspect >= 2.0 || (a.anisotropy > 8 && nameTrim)) {
		decide(TextureRole::Trim, a.aspect >= 2.0 ? "a wide strip" : "bands across", 0.7);
		return a;
	}
	// Roofs: dark, grey, even (tar, gravel) - or named so.
	const bool roofLook = a.saturation < 0.15 && a.brightness < 0.42 && a.anisotropy < 4;
	if ((roofLook && !nameTrim && !nameHas(lower, {"wall", "brick", "plaster", "concrete"})) || (nameRoof && a.saturation < 0.72)) {
		decide(TextureRole::Roof, nameRoof ? "a roof by its name and grey even look" : "dark, grey and even", nameRoof ? 0.8 : 0.5);
		return a;
	}
	// Named a strip and banded (most rows even across; dentils make the columns vary too).
	if (nameTrim && (a.anisotropy > 3 || a.banded >= 0.45)) {
		decide(TextureRole::Trim, "bands across", 0.6);
		return a;
	}
	decide(TextureRole::Wall, "no windows in the picture", 0.6);
	return a;
}

const char* textureRoleName(TextureRole role) {
	switch (role) {
		case TextureRole::Unknown: return "not analysed";
		case TextureRole::FacadeWindows: return "facade with windows";
		case TextureRole::Wall: return "plain wall";
		case TextureRole::Trim: return "trim strip";
		case TextureRole::Roof: return "roof";
		case TextureRole::ShopFront: return "shop front";
		case TextureRole::Unsuitable: return "unsuitable";
	}
	return "";
}

const char* textureRoleKey(TextureRole role) {
	switch (role) {
		case TextureRole::Unknown: return "Unknown";
		case TextureRole::FacadeWindows: return "FacadeWindows";
		case TextureRole::Wall: return "Wall";
		case TextureRole::Trim: return "Trim";
		case TextureRole::Roof: return "Roof";
		case TextureRole::ShopFront: return "ShopFront";
		case TextureRole::Unsuitable: return "Unsuitable";
	}
	return "Unknown";
}

TextureRole textureRoleFromKey(std::string_view key) {
	for (const auto role : {TextureRole::FacadeWindows, TextureRole::Wall, TextureRole::Trim, TextureRole::Roof, TextureRole::ShopFront, TextureRole::Unsuitable}) {
		if (iequals(key, textureRoleKey(role))) return role;
	}
	return TextureRole::Unknown;
}

std::string encodeFacadeWindows(const std::vector<FacadeWindowRect>& windows) {
	std::string out;
	for (const auto& w : windows) {
		if (!out.empty()) out += ';';
		out += number(w.u0) + ' ' + number(w.v0) + ' ' + number(w.u1) + ' ' + number(w.v1);
	}
	return out;
}

std::vector<FacadeWindowRect> decodeFacadeWindows(std::string_view text) {
	std::vector<FacadeWindowRect> out;
	std::string part;
	const auto flush = [&] {
		std::istringstream in(part);
		FacadeWindowRect r;
		if (in >> r.u0 >> r.v0 >> r.u1 >> r.v1 && r.u1 > r.u0 && r.v1 > r.v0) out.push_back(r);
		part.clear();
	};
	for (const char ch : text) {
		if (ch == ';') {
			flush();
		} else {
			part += ch;
		}
	}
	flush();
	return out;
}

} // namespace sme
