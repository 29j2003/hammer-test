// relief_pattern.cpp - Layouts of bricks, tiles and boards for relief (see relief_pattern.h).

#include <sme/relief_pattern.h>

#include <sme/filesystem.h>
#include <sme/material.h>
#include <sme/text.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <numeric>
#include <sstream>

namespace sme {

namespace {

constexpr unsigned MAX_SIZE = 512;   // as the height maps (heightmap.cpp)

unsigned floorPowerOfTwo(unsigned n) {
	unsigned p = 1;
	while (p * 2 <= n) p *= 2;
	return p;
}

/// Brightness and alpha (0..1) of an image, scaled down (box filter) to a power of two of at
/// most `maxSize` per side - the same grid heightmap.cpp works on.
struct Gray {
	unsigned w = 0, h = 0;
	std::vector<double> lum, alpha;
};

Gray gray(const ImageRgba& image, unsigned maxSize) {
	Gray out;
	const unsigned sw = image.width, sh = image.height;
	if (sw == 0 || sh == 0 || image.pixels.size() < std::size_t(sw) * sh * 4) return out;
	out.w = std::min(maxSize, floorPowerOfTwo(sw));
	out.h = std::min(maxSize, floorPowerOfTwo(sh));
	const std::size_t n = std::size_t(out.w) * out.h;
	out.lum.assign(n, 0);
	out.alpha.assign(n, 0);
	for (unsigned y = 0; y < out.h; ++y) {
		const unsigned y0 = y * sh / out.h, y1 = std::max(y0 + 1, (y + 1) * sh / out.h);
		for (unsigned x = 0; x < out.w; ++x) {
			const unsigned x0 = x * sw / out.w, x1 = std::max(x0 + 1, (x + 1) * sw / out.w);
			double r = 0, g = 0, b = 0, a = 0;
			for (unsigned sy = y0; sy < y1; ++sy) {
				for (unsigned sx = x0; sx < x1; ++sx) {
					const auto* p = &image.pixels[(std::size_t(sy) * sw + sx) * 4];
					const double pr = std::to_integer<int>(p[0]) / 255.0, pg = std::to_integer<int>(p[1]) / 255.0, pb = std::to_integer<int>(p[2]) / 255.0;
					r += pr;
					g += pg;
					b += pb;
					a += std::to_integer<int>(p[3]) / 255.0;
				}
			}
			const double count = double(x1 - x0) * (y1 - y0);
			const std::size_t i = std::size_t(y) * out.w + x;
			out.lum[i] = (0.299 * r + 0.587 * g + 0.114 * b) / count;
			out.alpha[i] = a / count;
		}
	}
	return out;
}

/// Rows become columns (an image w x h becomes h x w).
std::vector<double> transpose(const std::vector<double>& values, unsigned w, unsigned h) {
	std::vector<double> out(values.size());
	for (unsigned y = 0; y < h; ++y) {
		for (unsigned x = 0; x < w; ++x) out[std::size_t(x) * h + y] = values[std::size_t(y) * w + x];
	}
	return out;
}

/// Mean over a square of (2 radius + 1) texels, wrapping (textures tile).
std::vector<double> boxMean(const std::vector<double>& values, unsigned w, unsigned h, int radius) {
	std::vector<double> across(values.size()), out(values.size());
	const double size = 2.0 * radius + 1;
	const int iw = int(w), ih = int(h);
	for (int y = 0; y < ih; ++y) {
		double sum = 0;
		for (int d = -radius; d <= radius; ++d) sum += values[std::size_t(y) * w + std::size_t(((d % iw) + iw) % iw)];
		for (int x = 0; x < iw; ++x) {
			across[std::size_t(y) * w + x] = sum / size;
			sum += values[std::size_t(y) * w + std::size_t((x + radius + 1) % iw)] - values[std::size_t(y) * w + std::size_t(((x - radius) % iw + iw) % iw)];
		}
	}
	for (int x = 0; x < iw; ++x) {
		double sum = 0;
		for (int d = -radius; d <= radius; ++d) sum += across[std::size_t(((d % ih) + ih) % ih) * w + x];
		for (int y = 0; y < ih; ++y) {
			out[std::size_t(y) * w + x] = sum / size;
			sum += across[std::size_t((y + radius + 1) % ih) * w + x] - across[std::size_t(((y - radius) % ih + ih) % ih) * w + x];
		}
	}
	return out;
}

/// A profile folded with a period: the mean per bin of one period.
struct Fold {
	std::vector<double> mean;
	double explained = 0;   ///< Share of the profile's variance the period explains, beyond chance (0..1)
};

Fold fold(const std::vector<double>& profile, double period) {
	Fold out;
	const std::size_t n = profile.size();
	const int bins = std::max(2, int(std::lround(period)));
	out.mean.assign(std::size_t(bins), 0);
	std::vector<int> count(std::size_t(bins), 0);
	const double mean = std::accumulate(profile.begin(), profile.end(), 0.0) / double(n);
	double total = 0;
	for (std::size_t i = 0; i < n; ++i) {
		const int bin = std::min(bins - 1, int(std::fmod(double(i), period) / period * bins));
		out.mean[std::size_t(bin)] += profile[i];
		++count[std::size_t(bin)];
		total += (profile[i] - mean) * (profile[i] - mean);
	}
	double between = 0;
	for (int b = 0; b < bins; ++b) {
		if (count[std::size_t(b)] == 0) continue;
		out.mean[std::size_t(b)] /= count[std::size_t(b)];
		between += count[std::size_t(b)] * (out.mean[std::size_t(b)] - mean) * (out.mean[std::size_t(b)] - mean);
	}
	if (total < 1e-12) return out;
	// Noise alone explains about (bins - 1) / (n - 1) of the variance: only what is beyond counts.
	const double chance = double(bins - 1) / double(std::max<std::size_t>(n - 1, 1));
	out.explained = std::clamp((between / total - chance) / std::max(1.0 - chance, 1e-6), 0.0, 1.0);
	return out;
}

/// The joint in a folded profile: the narrow extreme (lighter or darker). Its centre and width in
/// bins, and how narrow it is (share of the period above half its height; small = a line).
struct Joint {
	double centre = 0;     ///< Bin (fractional)
	double width = 0;      ///< Bins
	double share = 1;      ///< Width / period
	double strength = 0;   ///< Peak above the median
};

Joint jointIn(const std::vector<double>& folded, double polarity) {
	Joint best;
	const int bins = int(folded.size());
	if (bins < 2) return best;
	std::vector<double> sorted = folded;
	std::sort(sorted.begin(), sorted.end());
	const double median = sorted[sorted.size() / 2];
	int top = 0;
	for (int b = 1; b < bins; ++b) {
		if (polarity * (folded[std::size_t(b)] - median) > polarity * (folded[std::size_t(top)] - median)) top = b;
	}
	const double peak = polarity * (folded[std::size_t(top)] - median);
	if (peak <= 0) return best;
	// The connected run of bins above half the peak around the top (wrapping).
	const auto above = [&](int b) { return polarity * (folded[std::size_t(((b % bins) + bins) % bins)] - median) > 0.5 * peak; };
	int left = 0, right = 0;
	while (left < bins && above(top - left - 1)) ++left;
	while (right < bins && above(top + right + 1)) ++right;
	double weight = 0, sum = 0;
	for (int d = -left; d <= right; ++d) {
		const double v = polarity * (folded[std::size_t(((top + d) % bins + bins) % bins)] - median);
		weight += v;
		sum += v * d;
	}
	best.centre = top + (weight > 0 ? sum / weight : 0.0);
	best.width = double(left + right + 1);
	best.share = best.width / bins;
	best.strength = peak;
	return best;
}

/// The best whole number of repeats of a profile (the texture tiles: a whole number of courses),
/// between `fewest` and the count that leaves periods of `shortest` texels: the count that explains
/// most - or a multiple of it whose joint is nearly as strong (a period of two courses explains as
/// much as one, but its fold shows two joints).
struct Repeat {
	int count = 0;
	double explained = 0;
};

template <typename Score, typename Line>
Repeat bestRepeat(std::size_t length, int fewest, double shortest, Score score, Line line) {
	Repeat best;
	const int most = int(double(length) / shortest);
	std::vector<double> scores(std::size_t(std::max(most + 1, 0)), 0.0);
	for (int k = fewest; k <= most; ++k) {
		scores[std::size_t(k)] = score(double(length) / k);
		if (scores[std::size_t(k)] > best.explained) best = {k, scores[std::size_t(k)]};
	}
	if (best.count == 0) return best;
	const double strength = line(double(length) / best.count);
	Repeat chosen = best;
	for (int k = best.count * 2; k <= most; k += best.count) {
		if (scores[std::size_t(k)] >= 0.4 * best.explained && line(double(length) / k) >= 0.7 * strength) chosen = {k, scores[std::size_t(k)]};
	}
	return chosen;
}

/// Without the slow changes (each course's own shade): what stays are the thin joints.
std::vector<double> highPass(const std::vector<double>& profile, int radius) {
	const int n = int(profile.size());
	std::vector<double> out(profile.size());
	double sum = 0;
	for (int d = -radius; d <= radius; ++d) sum += profile[std::size_t(((d % n) + n) % n)];
	for (int i = 0; i < n; ++i) {
		out[std::size_t(i)] = profile[std::size_t(i)] - sum / (2 * radius + 1);
		sum += profile[std::size_t((i + radius + 1) % n)] - profile[std::size_t(((i - radius) % n + n) % n)];
	}
	return out;
}

/// The strength of the thinnest joint in a fold (either polarity), 0 when there is no line.
double lineStrength(const std::vector<double>& folded) {
	double best = 0;
	for (const double polarity : {1.0, -1.0}) {
		const Joint joint = [&] {
			Joint j;
			const int bins = int(folded.size());
			if (bins < 2) return j;
			std::vector<double> sorted = folded;
			std::sort(sorted.begin(), sorted.end());
			const double median = sorted[sorted.size() / 2];
			int top = 0;
			for (int b = 1; b < bins; ++b) {
				if (polarity * folded[std::size_t(b)] > polarity * folded[std::size_t(top)]) top = b;
			}
			const double peak = polarity * (folded[std::size_t(top)] - median);
			int width = 0;
			for (int b = 0; b < bins; ++b) width += polarity * (folded[std::size_t(b)] - median) > 0.5 * peak ? 1 : 0;
			j.strength = peak;
			j.share = double(width) / bins;
			return j;
		}();
		if (joint.share <= 0.4) best = std::max(best, joint.strength);
	}
	return best;
}

double wrapDistance(double a, double period) {
	const double d = std::fmod(std::fmod(a, period) + period, period);
	return std::min(d, period - d);
}

/// The layout of one orientation (courses across the rows of `lum`, w x h).
ReliefLayout analyse(const std::vector<double>& lum, unsigned w, unsigned h) {
	ReliefLayout layout;
	layout.width = w;
	layout.height = h;
	// Bed joints: the rows' brightness repeats with the course height.
	std::vector<double> rows(h, 0.0);
	for (unsigned y = 0; y < h; ++y) {
		for (unsigned x = 0; x < w; ++x) rows[y] += lum[std::size_t(y) * w + x] / w;
	}
	rows = highPass(rows, std::max(3, int(h) / 64));
	const Repeat courses = bestRepeat(
		h, 3, 5.0, [&](double period) { return fold(rows, period).explained; }, [&](double period) { return lineStrength(fold(rows, period).mean); });
	if (courses.count == 0 || courses.explained < 0.3) return layout;
	const double courseHeight = double(h) / courses.count;
	const Fold bedFold = fold(rows, courseHeight);
	// The mortar: lighter or darker - whichever makes a thin line.
	const Joint light = jointIn(bedFold.mean, 1.0), dark = jointIn(bedFold.mean, -1.0);
	const bool lightJoints = light.share < dark.share || (light.share == dark.share && light.strength >= dark.strength);
	const Joint bed = lightJoints ? light : dark;
	if (bed.share > 0.4) return layout;   // no line: soft waves (plaster, stone)
	const double polarity = lightJoints ? 1.0 : -1.0;
	const double binSize = courseHeight / double(bedFold.mean.size());
	layout.courseHeight = courseHeight;
	layout.bedPhase = (bed.centre + 0.5) * binSize;
	layout.bedJoint = std::clamp(bed.width * binSize, 1.0, courseHeight * 0.4);
	layout.lightJoints = lightJoints;

	// Upright joints: inside each course (between its bed joints) the columns repeat with the unit width.
	const int courseCount = courses.count;
	std::vector<std::vector<double>> columns(std::size_t(courseCount), std::vector<double>(w, 0.0));
	for (int c = 0; c < courseCount; ++c) {
		const double top = layout.bedPhase + c * courseHeight + layout.bedJoint * 0.5 + 0.5;
		const double bottom = layout.bedPhase + (c + 1) * courseHeight - layout.bedJoint * 0.5 - 0.5;
		int rowsUsed = 0;
		for (int y = int(std::ceil(top)); y <= int(std::floor(bottom)); ++y) {
			const std::size_t row = std::size_t((y % int(h) + int(h)) % int(h)) * w;
			for (unsigned x = 0; x < w; ++x) columns[std::size_t(c)][x] += lum[row + x];
			++rowsUsed;
		}
		if (rowsUsed > 0) {
			for (auto& v : columns[std::size_t(c)]) v /= rowsUsed;
		}
		columns[std::size_t(c)] = highPass(columns[std::size_t(c)], std::max(3, int(w) / 64));
	}
	const Repeat units = bestRepeat(
		w, 2, 6.0,
		[&](double period) {
			double sum = 0;
			for (const auto& column : columns) sum += fold(column, period).explained;
			return sum / courseCount;
		},
		[&](double period) {
			double sum = 0;
			for (const auto& column : columns) sum += lineStrength(fold(column, period).mean);
			return sum / courseCount;
		});
	std::vector<double> phases(std::size_t(courseCount), 0.0);
	if (units.count > 0 && units.explained >= 0.15) {
		const double unitWidth = double(w) / units.count;
		// Each course's offset; the joint's width from all courses laid over each other.
		std::vector<double> strengths(std::size_t(courseCount), 0.0);
		double widthSum = 0, widthWeight = 0;
		for (int c = 0; c < courseCount; ++c) {
			const Fold f = fold(columns[std::size_t(c)], unitWidth);
			const Joint joint = jointIn(f.mean, polarity);
			const double size = unitWidth / double(f.mean.size());
			phases[std::size_t(c)] = (joint.centre + 0.5) * size;
			strengths[std::size_t(c)] = joint.share < 0.45 ? joint.strength : 0.0;
			if (joint.share < 0.45) {
				widthSum += joint.width * size * joint.strength;
				widthWeight += joint.strength;
			}
		}
		const double typical = *std::max_element(strengths.begin(), strengths.end());
		int clear = 0;
		for (const double s : strengths) clear += s > 0.25 * typical ? 1 : 0;
		if (widthWeight > 0 && clear * 2 >= courseCount) {
			layout.unitWidth = unitWidth;
			layout.headJoint = std::clamp(widthSum / widthWeight, 1.0, unitWidth * 0.4);
			// The bond: how far each course moves against the one above.
			int half = 0, none = 0;
			for (int c = 0; c < courseCount; ++c) {
				const double shift = wrapDistance(phases[std::size_t((c + 1) % courseCount)] - phases[std::size_t(c)], unitWidth) / unitWidth;
				if (std::abs(shift - 0.5) < 0.15) ++half;
				else if (shift < 0.12) ++none;
			}
			// Unclear courses (a window's rows) take the bond's offset from the course above.
			const auto weak = [&](int c) { return strengths[std::size_t(c)] <= 0.25 * typical; };
			if (half * 3 >= courseCount * 2) {
				layout.kind = ReliefLayout::Kind::RunningBond;
				for (int pass = 0; pass < 2; ++pass) {
					for (int c = 0; c < courseCount; ++c) {
						if (weak(c)) phases[std::size_t(c)] = phases[std::size_t((c + courseCount - 1) % courseCount)] + unitWidth * 0.5;
					}
				}
			} else if (none * 3 >= courseCount * 2) {
				layout.kind = ReliefLayout::Kind::Stacked;
				for (int c = 0; c < courseCount; ++c) {
					if (weak(c)) phases[std::size_t(c)] = phases[std::size_t((c + courseCount - 1) % courseCount)];
				}
			} else {
				layout.kind = ReliefLayout::Kind::Offset;
			}
		}
	}
	if (layout.kind == ReliefLayout::Kind::None) layout.kind = ReliefLayout::Kind::Courses;
	layout.headPhase = phases;
	layout.confidence = std::clamp((courses.explained - 0.3) / 0.5, 0.0, 1.0) * std::clamp((0.45 - bed.share) / 0.25, 0.0, 1.0);

	// Where the pattern is: busy at the scale of the units, neither nearly black (dark glass) nor
	// nearly white and smooth (bright glass, a plaster panel) - those parts stay flat.
	const std::size_t n = std::size_t(w) * h;
	const int radius = std::max(2, int(std::lround(0.4 * courseHeight)));
	const auto around = boxMean(lum, w, h, radius);
	std::vector<double> busyIn(n);
	for (std::size_t i = 0; i < n; ++i) busyIn[i] = std::abs(lum[i] - around[i]);
	const auto busy = boxMean(busyIn, w, h, 2 * radius), lit = boxMean(lum, w, h, 2 * radius);
	// Close by too, so the pattern stops at the window's edge and not a brick further in.
	const auto near = boxMean(lum, w, h, 2), nearBusy = boxMean(busyIn, w, h, 2);
	std::vector<bool> inPattern(n);
	for (std::size_t i = 0; i < n; ++i) {
		const double contrast = busy[i] / std::max(lit[i], 0.02);
		const bool pane = near[i] < 0.045 || (near[i] > 0.92 && nearBusy[i] < 0.01);
		inPattern[i] = !pane && contrast > 0.035 && lit[i] > 0.06 && !(lit[i] > 0.9 && contrast < 0.08);
	}
	// The units: every texel its brick (or the joint); a brick mostly outside the pattern is outside whole.
	const int perCourse = layout.unitWidth > 0 ? int(std::lround(w / layout.unitWidth)) : 1;
	layout.units.assign(n, ReliefLayout::JOINT);
	std::vector<int> inside(std::size_t(courseCount * perCourse), 0), total(std::size_t(courseCount * perCourse), 0);
	for (unsigned y = 0; y < h; ++y) {
		const double v = double(y) - layout.bedPhase;
		const int course = int(std::floor(v / courseHeight));
		const int c = ((course % courseCount) + courseCount) % courseCount;
		const bool bedJoint = wrapDistance(v, courseHeight) < layout.bedJoint * 0.5;
		for (unsigned x = 0; x < w; ++x) {
			const std::size_t i = std::size_t(y) * w + x;
			int unit = 0;
			bool headJoint = false;
			if (layout.unitWidth > 0) {
				const double u = double(x) - layout.headPhase[std::size_t(c)];
				unit = ((int(std::floor(u / layout.unitWidth)) % perCourse) + perCourse) % perCourse;
				headJoint = wrapDistance(u, layout.unitWidth) < layout.headJoint * 0.5;
			}
			const int id = c * perCourse + unit;
			++total[std::size_t(id)];
			inside[std::size_t(id)] += inPattern[i] ? 1 : 0;
			if (!bedJoint && !headJoint) layout.units[i] = id;
		}
	}
	int covered = 0;
	for (std::size_t i = 0; i < n; ++i) {
		std::int32_t& unit = layout.units[i];
		if (!inPattern[i] || (unit >= 0 && inside[std::size_t(unit)] * 2 < total[std::size_t(unit)])) unit = ReliefLayout::OUTSIDE;
		covered += unit != ReliefLayout::OUTSIDE ? 1 : 0;
	}
	layout.unitCount = courseCount * perCourse;
	layout.coverage = double(covered) / double(n);
	if (layout.coverage < 0.3) layout.kind = ReliefLayout::Kind::None;
	return layout;
}

/// Swaps the axes of a layout found on the transposed texture back to the texture's.
void untranspose(ReliefLayout& layout) {
	const unsigned w = layout.height, h = layout.width;   // the texture's size
	std::vector<std::int32_t> units(layout.units.size());
	for (unsigned y = 0; y < h; ++y) {
		for (unsigned x = 0; x < w; ++x) units[std::size_t(y) * w + x] = layout.units[std::size_t(x) * h + y];
	}
	layout.units = std::move(units);
	layout.width = w;
	layout.height = h;
	layout.transposed = true;
}

bool containsAny(const std::string& text, std::initializer_list<std::string_view> words) {
	return std::any_of(words.begin(), words.end(), [&](std::string_view w) { return text.find(w) != std::string::npos; });
}

/// A word at the start of one of the name's parts ("sign" in "metal/sign_01", not in "design").
bool startsAnyPart(const std::string& text, std::initializer_list<std::string_view> words) {
	for (std::size_t i = 0; i < text.size(); ++i) {
		if (i > 0 && std::isalpha(static_cast<unsigned char>(text[i - 1]))) continue;
		for (const auto word : words) {
			if (text.compare(i, word.size(), word) == 0) return true;
		}
	}
	return false;
}

struct CellBox {
	unsigned x0, y0, x1, y1;
};

/// A group of set cells (4-connected, not wrapping) of at least `share` of all cells that fills at
/// least `fill` of its bounding rectangle, three cells or more each way (a window pane): that
/// rectangle, else nullopt.
std::optional<CellBox> largeRectangle(const std::vector<bool>& cells, unsigned w, unsigned h, double share, double fill) {
	std::vector<bool> seen(cells.size());
	std::vector<std::size_t> stack;
	for (std::size_t start = 0; start < cells.size(); ++start) {
		if (!cells[start] || seen[start]) continue;
		std::size_t count = 0;
		unsigned x0 = w, y0 = h, x1 = 0, y1 = 0;
		stack.push_back(start);
		seen[start] = true;
		while (!stack.empty()) {
			const std::size_t i = stack.back();
			stack.pop_back();
			++count;
			const unsigned x = unsigned(i % w), y = unsigned(i / w);
			x0 = std::min(x0, x), x1 = std::max(x1, x), y0 = std::min(y0, y), y1 = std::max(y1, y);
			const auto visit = [&](unsigned nx, unsigned ny) {
				const std::size_t j = std::size_t(ny) * w + nx;
				if (cells[j] && !seen[j]) {
					seen[j] = true;
					stack.push_back(j);
				}
			};
			if (x + 1 < w) visit(x + 1, y);
			if (x > 0) visit(x - 1, y);
			if (y + 1 < h) visit(x, y + 1);
			if (y > 0) visit(x, y - 1);
		}
		const double box = double(x1 - x0 + 1) * (y1 - y0 + 1);
		if (double(count) >= share * double(cells.size()) && double(count) >= fill * box && x1 - x0 >= 2 && y1 - y0 >= 2) return CellBox{x0, y0, x1, y1};
	}
	return std::nullopt;
}

} // namespace

std::int32_t ReliefLayout::unitAt(int x, int y) const {
	if (width == 0 || height == 0 || units.size() < std::size_t(width) * height) return OUTSIDE;
	return units[std::size_t((y % int(height) + int(height)) % int(height)) * width + std::size_t((x % int(width) + int(width)) % int(width))];
}

int ReliefLayout::courses() const {
	return courseHeight > 0 ? int(std::lround((transposed ? width : height) / courseHeight)) : 0;
}

std::string ReliefLayout::describe() const {
	if (!found()) return "no regular layout";
	std::ostringstream out;
	const int perCourse = unitWidth > 0 ? int(std::lround((transposed ? height : width) / unitWidth)) : 0;
	switch (kind) {
		case Kind::RunningBond: out << "bricks in a running bond: " << courses() << " courses of " << perCourse; break;
		case Kind::Stacked: out << "tiles or blocks in a grid: " << courses() << " rows of " << perCourse; break;
		case Kind::Offset: out << "courses of units: " << courses() << " courses of " << perCourse; break;
		case Kind::Courses: out << courses() << (transposed ? " boards or courses, upright" : " boards or courses"); break;
		case Kind::None: break;
	}
	out << (lightJoints ? ", light joints" : ", dark joints");
	if (coverage < 0.95) out << ", " << int(std::lround((1.0 - coverage) * 100)) << " % of it flat (no pattern there)";
	return out.str();
}

ReliefLayout detectReliefLayout(const ImageRgba& baseTexture) {
	const Gray g = gray(baseTexture, MAX_SIZE);
	if (g.w < 16 || g.h < 16) return {};
	ReliefLayout across = analyse(g.lum, g.w, g.h);
	ReliefLayout upright = analyse(transpose(g.lum, g.w, g.h), g.h, g.w);
	// Courses across unless the upright ones are clearly better (boards standing up).
	const auto score = [](const ReliefLayout& l) { return l.found() ? l.confidence * (l.unitWidth > 0 ? 1.0 : 0.8) : -1.0; };
	if (upright.found() && score(upright) > score(across) * 1.25) {
		untranspose(upright);
		return upright;
	}
	return across;
}

std::optional<HeightMap> heightFromLayout(const ReliefLayout& layout, const ImageRgba& baseTexture) {
	if (!layout.found()) return std::nullopt;
	const Gray g = gray(baseTexture, MAX_SIZE);
	if (g.w != layout.width || g.h != layout.height) return std::nullopt;
	const unsigned w = g.w, h = g.h;
	const std::size_t n = std::size_t(w) * h;
	// In the layout's own axes (courses across): x along the course, y across.
	const unsigned lw = layout.transposed ? h : w, lh = layout.transposed ? w : h;
	const int courseCount = std::max(1, layout.courses());
	// Flat faces with a short, crisp bevel (a third of the way to the unit's middle, at most 1.2 % of
	// the texture, eased in and out) and a little of the texture's grain on top - Julius: no
	// "Noppen", no cushions; the rounded edges over half the brick (29.10.2026) made every brick a
	// pillow that caught the light all over and looked deeper than it was. Joints stay shallow:
	// mortar and grout sit only a little below the units - deep joints with steep sides threw a
	// black strip along every tile in the game and smeared bricks seen at a flat angle. Tiles
	// shallower still.
	const double faceHalf = 0.5 * std::min(layout.courseHeight - layout.bedJoint, layout.unitWidth > 0 ? layout.unitWidth - layout.headJoint : 1e9);
	const double bevel = std::clamp(0.35 * faceHalf, 1.0, std::max(1.0, std::max(w, h) * 0.012));
	const double jointHeight = layout.kind == ReliefLayout::Kind::Stacked ? 0.55 : 0.3;
	constexpr double GRAIN = 0.1;
	const auto around = boxMean(g.lum, w, h, std::max(2, int(std::lround(0.25 * layout.courseHeight))));
	std::vector<double> detail(n);
	for (std::size_t i = 0; i < n; ++i) detail[i] = g.lum[i] - around[i];
	std::vector<double> sorted = detail;
	std::sort(sorted.begin(), sorted.end());
	const double low = sorted[n / 100], high = std::max(sorted[n * 99 / 100], low + 1e-9);
	std::vector<double> heights(n, 1.0);
	for (unsigned ly = 0; ly < lh; ++ly) {
		const double v = double(ly) - layout.bedPhase;
		const int course = ((int(std::floor(v / layout.courseHeight)) % courseCount) + courseCount) % courseCount;
		const double bedEdge = wrapDistance(v, layout.courseHeight) - layout.bedJoint * 0.5;
		for (unsigned lx = 0; lx < lw; ++lx) {
			const unsigned x = layout.transposed ? ly : lx, y = layout.transposed ? lx : ly;
			const std::size_t i = std::size_t(y) * w + x;
			if (layout.units[i] == ReliefLayout::OUTSIDE) continue;   // flat on top
			double edge = bedEdge;
			if (layout.unitWidth > 0) {
				const double u = double(lx) - layout.headPhase[std::size_t(course)];
				edge = std::min(edge, wrapDistance(u, layout.unitWidth) - layout.headJoint * 0.5);
			}
			if (edge <= 0) {
				heights[i] = jointHeight;
				continue;
			}
			const double t = std::min(edge / bevel, 1.0);
			const double shape = t * t * (3 - 2 * t);
			const double grain = std::clamp((detail[i] - low) / (high - low), 0.0, 1.0);
			heights[i] = jointHeight + (1.0 - jointHeight) * shape * (1.0 - GRAIN + GRAIN * grain);
		}
	}
	// Softened over a texel: no stair steps at the joints' edges.
	HeightMap map;
	map.width = w;
	map.height = h;
	map.values.resize(n);
	const auto soft = boxMean(heights, w, h, 1);
	for (std::size_t i = 0; i < n; ++i) map.values[i] = float(std::clamp(0.5 * heights[i] + 0.5 * soft[i], 0.0, 1.0));
	return map;
}

std::optional<HeightMap> heightFromTexture(const ImageRgba& baseTexture, ReliefLayout* layoutOut, std::string* error) {
	ReliefLayout layout = detectReliefLayout(baseTexture);
	std::optional<HeightMap> map;
	if (layout.found()) map = heightFromLayout(layout, baseTexture);
	if (!map) map = heightFromColor(baseTexture, error);
	if (layoutOut) *layoutOut = std::move(layout);
	return map;
}

bool setUnitZone(const ReliefLayout& layout, std::vector<std::uint8_t>& zones, unsigned zonesWidth, unsigned zonesHeight, int x, int y, HeightZone zone) {
	const std::int32_t unit = layout.unitAt(x, y);
	if (unit < 0 || zonesWidth == 0 || zonesHeight == 0 || zones.size() < std::size_t(zonesWidth) * zonesHeight) return false;
	for (unsigned zy = 0; zy < zonesHeight; ++zy) {
		const int ly = int(std::size_t(zy) * layout.height / zonesHeight);
		for (unsigned zx = 0; zx < zonesWidth; ++zx) {
			const int lx = int(std::size_t(zx) * layout.width / zonesWidth);
			if (layout.unitAt(lx, ly) == unit) zones[std::size_t(zy) * zonesWidth + zx] = static_cast<std::uint8_t>(zone);
		}
	}
	return true;
}

// --- Which materials get relief -------------------------------------------------------------

ReliefFit reliefFitByName(std::string_view materialName, std::string_view baseTexture) {
	const std::string name = toLower(std::string(materialName) + " " + std::string(baseTexture));
	// Things on or in a wall - never stone-like, whatever else the name says.
	struct Rule {
		std::initializer_list<std::string_view> words;
		const char* reason;
	};
	static const Rule objects[] = {
		{{"window", "glass", "windshield", "mirror", "stainedglass", "skylight", "storefront", "shopfront"}, "window or glass"},
		{{"poster", "billboard", "advert", "banner", "graffiti"}, "poster or sign"},
		{{"grating", "chainlink", "railing", "fence"}, "grate or fence"},
		{{"shutter", "garagedoor"}, "door or shutter"},
		{{"monitor", "computer", "keyboard"}, "screen"},
		{{"curtain", "wallpaper", "carpet", "fabric"}, "cloth or paper"},
	};
	for (const auto& rule : objects) {
		if (containsAny(name, rule.words)) return {false, rule.reason};
	}
	// Signs anywhere in a word ("citysign"), but not a "design".
	for (std::size_t at = name.find("sign"); at != std::string::npos; at = name.find("sign", at + 1)) {
		if (at < 2 || name.compare(at - 2, 2, "de") != 0) return {false, "poster or sign"};
	}
	static const Rule parts[] = {
		{{"label", "logo", "sticker"}, "poster or sign"},
		{{"grate", "grill", "mesh", "vent"}, "grate or fence"},
		{{"door", "gate"}, "door or shutter"},
		{{"screen", "lamp", "lights/"}, "screen or light"},
		{{"pipe", "wire", "cable"}, "pipe or cable"},
		{{"cloth", "paper", "blind"}, "cloth or paper"},
	};
	for (const auto& rule : parts) {
		if (startsAnyPart(name, rule.words)) return {false, rule.reason};
	}
	// Smooth surfaces - unless the name also tells of bricks or stones laid in them.
	const bool stony = containsAny(name, {"brick", "stone", "cobble", "rock", "tile", "block", "cinder", "paver", "slate", "shingle"});
	if (!stony) {
		if (containsAny(name, {"metal", "steel", "corrugated", "aluminium", "aluminum"}) || startsAnyPart(name, {"sheet", "plate", "iron"})) {
			return {false, "metal sheet"};
		}
		if (containsAny(name, {"plaster", "stucco", "drywall"}) || startsAnyPart(name, {"paint", "rubber", "plastic"})) return {false, "smooth surface"};
	}
	return {};
}

ReliefFit reliefFitByLook(const ImageRgba& baseTexture) {
	const Gray g = gray(baseTexture, 128);
	if (g.w < 8 || g.h < 8) return {};
	const unsigned w = g.w, h = g.h;
	const std::size_t n = std::size_t(w) * h;
	std::size_t clear = 0;
	for (const double a : g.alpha) clear += a < 0.5 ? 1 : 0;
	if (clear * 5 > n) return {false, "see-through"};
	// Flat texels: hardly any change within a few texels (glass, paint, plaster, sheet metal).
	const auto around = boxMean(g.lum, w, h, 2);
	std::vector<double> diff(n);
	for (std::size_t i = 0; i < n; ++i) diff[i] = std::abs(g.lum[i] - around[i]);
	const auto rough = boxMean(diff, w, h, 2);
	std::vector<bool> flat(n);
	std::size_t flatCount = 0;
	for (std::size_t i = 0; i < n; ++i) {
		flat[i] = rough[i] < 0.012;
		flatCount += flat[i] ? 1 : 0;
	}
	if (flatCount * 10 > n * 8) return {false, "smooth surface"};
	// The largest flat area that fills its rectangle: a window pane or a panel.
	std::vector<std::int32_t> label(n, -1);
	std::vector<std::size_t> stack;
	for (std::size_t start = 0; start < n; ++start) {
		if (!flat[start] || label[start] >= 0) continue;
		std::size_t count = 0;
		unsigned x0 = w, y0 = h, x1 = 0, y1 = 0;
		stack.push_back(start);
		label[start] = 1;
		while (!stack.empty()) {
			const std::size_t i = stack.back();
			stack.pop_back();
			++count;
			const unsigned x = unsigned(i % w), y = unsigned(i / w);
			x0 = std::min(x0, x), x1 = std::max(x1, x), y0 = std::min(y0, y), y1 = std::max(y1, y);
			const std::size_t next[4] = {y * std::size_t(w) + (x + 1) % w, y * std::size_t(w) + (x + w - 1) % w, ((y + 1) % h) * std::size_t(w) + x,
			                             ((y + h - 1) % h) * std::size_t(w) + x};
			for (const std::size_t j : next) {
				if (flat[j] && label[j] < 0) {
					label[j] = 1;
					stack.push_back(j);
				}
			}
		}
		const double box = double(x1 - x0 + 1) * (y1 - y0 + 1);
		if (count * 10 > n * 3 && double(count) > 0.6 * box) return {false, "window or panel (large flat pane)"};
	}
	// A dark pane divided by bars (a window's grid between brick pillars - Julius, 29.10.2026): the
	// bars cut the glass into cells too small for the flat test above, so look at blocks of 8 x 8
	// texels. A block is glass when about half of it or more is dark and even - close to the
	// block's darker tone (a bar through the block is the rest). Bricks are not that dark, a joint
	// is too little of a block, and a texture dark all over is a dark wall (see below).
	// (At up to 256 texels: at 128 the thin bars of small panes blur into the glass.)
	constexpr unsigned BLOCK = 8;
	const Gray fine = baseTexture.width > w || baseTexture.height > h ? gray(baseTexture, 256) : g;
	if (fine.w >= BLOCK * 4 && fine.h >= BLOCK * 4) {
		const unsigned bw = fine.w / BLOCK, bh = fine.h / BLOCK;
		std::vector<bool> glass(std::size_t(bw) * bh);
		std::vector<double> tones;
		for (unsigned by = 0; by < bh; ++by) {
			for (unsigned bx = 0; bx < bw; ++bx) {
				tones.clear();
				for (unsigned y = by * BLOCK; y < (by + 1) * BLOCK; ++y) {
					for (unsigned x = bx * BLOCK; x < (bx + 1) * BLOCK; ++x) tones.push_back(fine.lum[std::size_t(y) * fine.w + x]);
				}
				std::nth_element(tones.begin(), tones.begin() + tones.size() / 4, tones.end());
				const double low = tones[tones.size() / 4];
				const auto pane = std::count_if(tones.begin(), tones.end(), [low](double t) { return t < 0.15 && std::abs(t - low) < 0.05; });
				glass[std::size_t(by) * bw + bx] = low < 0.15 && pane * 100 >= std::ptrdiff_t(tones.size()) * 45;
			}
		}
		// A large group of glass blocks that fills its rectangle (no wrapping: a pane has edges) and
		// leaves a wall around it (a texture dark all over is a dark wall, not a window).
		if (const auto box = largeRectangle(glass, bw, bh, 0.15, 0.6); box && double(box->x1 - box->x0 + 1) * (box->y1 - box->y0 + 1) < 0.9 * bw * bh) {
			return {false, "window (panes between bars)"};
		}
	}
	return {};
}

ReliefFit reliefFitByMask(const ImageRgba& envmapMask) {
	// A reflection mask bright over a large rectangle and dark around it: glass (a wall texture's
	// window reflects, its bricks do not). Bars in the glass leave dark lines in the mask: the
	// rectangle need only be half filled. A mask bright all over (shiny tiles, metal) is no window.
	const Gray g = gray(envmapMask, 64);
	if (g.w < 8 || g.h < 8) return {};
	// Bright texels, grown by a texel so that thin bars do not cut the pane into cells.
	std::vector<bool> shiny(g.lum.size());
	for (unsigned y = 0; y < g.h; ++y) {
		for (unsigned x = 0; x < g.w; ++x) {
			bool bright = false;
			for (int dy = -1; dy <= 1 && !bright; ++dy) {
				for (int dx = -1; dx <= 1 && !bright; ++dx) {
					const int nx = int(x) + dx, ny = int(y) + dy;
					bright = nx >= 0 && ny >= 0 && nx < int(g.w) && ny < int(g.h) && g.lum[std::size_t(ny) * g.w + std::size_t(nx)] > 0.45;
				}
			}
			shiny[std::size_t(y) * g.w + x] = bright;
		}
	}
	const auto box = largeRectangle(shiny, g.w, g.h, 0.15, 0.5);
	if (!box) return {};
	double inside = 0, outside = 0;
	std::size_t insideCount = 0, outsideCount = 0;
	for (unsigned y = 0; y < g.h; ++y) {
		for (unsigned x = 0; x < g.w; ++x) {
			const bool in = x >= box->x0 && x <= box->x1 && y >= box->y0 && y <= box->y1;
			(in ? inside : outside) += g.lum[std::size_t(y) * g.w + x];
			++(in ? insideCount : outsideCount);
		}
	}
	if (outsideCount * 10 < g.lum.size()) return {};   // the whole texture: not a window in a wall
	if (inside / double(insideCount) > 0.5 && outside / double(outsideCount) < 0.25) return {false, "window (reflecting pane)"};
	return {};
}

ReliefFit reliefFit(std::string_view materialName, const Material& material, const ImageRgba* baseTexture) {
	if (auto byName = reliefFitByName(materialName, material.param("$basetexture")); !byName.suits) return byName;
	if (material.flag("$translucent") || material.flag("$alphatest") || material.flag("$additive")) return {false, "see-through"};
	if (baseTexture) return reliefFitByLook(*baseTexture);
	return {};
}

ReliefFit reliefFitInGame(const GameFileSystem& fs, std::string_view materialName, const Material& material) {
	const ReliefFit early = reliefFit(materialName, material);
	if (!early.suits) return early;
	const std::string texture = toLower(material.param("$basetexture"));
	if (texture.empty()) return early;
	// Per texture file (the folder it comes from tells one game's from another's).
	std::string key = texture + "|" + toLower(material.param("$envmap").empty() ? std::string() : material.param("$envmapmask"));
	if (const auto mount = fs.locate("materials/" + texture + ".vtf")) key = displayPath(fs.mounts()[*mount].path) + "|" + texture;
	static std::mutex mutex;
	static std::map<std::string, ReliefFit> looks;
	{
		std::lock_guard lock(mutex);
		if (const auto it = looks.find(key); it != looks.end()) return it->second;
	}
	const auto image = loadTexture(fs, texture, 256);
	ReliefFit fit = image ? reliefFitByLook(*image) : ReliefFit{};
	// The reflection mask: a window's glass reflects, the wall around it does not.
	if (const std::string mask = toLower(material.param("$envmapmask")); fit.suits && !mask.empty() && !material.param("$envmap").empty()) {
		if (const auto maskImage = loadTexture(fs, mask, 64)) fit = reliefFitByMask(*maskImage);
	}
	std::lock_guard lock(mutex);
	looks[key] = fit;
	return fit;
}

} // namespace sme
