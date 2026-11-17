// texture.cpp - VTF decoding via vtfpp (see texture.h).

#include <sme/texture.h>

#include <sme/filesystem.h>
#include <sme/material.h>

#include <algorithm>
#include <cstdint>
#include <cstring>

#include <vtfpp/ImageConversion.h>
#include <vtfpp/VTF.h>

namespace sme {

namespace {

std::string formatName(vtfpp::ImageFormat format) {
	using enum vtfpp::ImageFormat;
	switch (format) {
		case RGBA8888: return "RGBA8888";
		case ABGR8888: return "ABGR8888";
		case RGB888: return "RGB888";
		case BGR888: return "BGR888";
		case RGB565: return "RGB565";
		case I8: return "I8";
		case IA88: return "IA88";
		case A8: return "A8";
		case ARGB8888: return "ARGB8888";
		case BGRA8888: return "BGRA8888";
		case BGRX8888: return "BGRX8888";
		case DXT1: return "DXT1";
		case DXT1_ONE_BIT_ALPHA: return "DXT1A";
		case DXT3: return "DXT3";
		case DXT5: return "DXT5";
		case UV88: return "UV88";
		case RGBA16161616F: return "RGBA16161616F";
		case RGBA16161616: return "RGBA16161616";
		case ATI2N: return "ATI2N";
		case ATI1N: return "ATI1N";
		default: return "format " + std::to_string(static_cast<int>(format));
	}
}

/// The engine allows at most 32 resources (MAX_RSRC_DICTIONARY_ENTRIES); vtfpp reserves room for
/// as many as the header claims, so a damaged count asked for gigabytes before failing.
constexpr std::uint32_t MAX_VTF_RESOURCES = 32;

/// What vtfpp may read: a VTF 7.3+ header's resource count must be plausible (and fit the file).
bool plausibleVtfHeader(std::span<const std::byte> data) {
	if (data.size() < 16 || std::memcmp(data.data(), "VTF\0", 4) != 0) return true;   // not ours to judge (other signatures)
	std::uint32_t major = 0, minor = 0;
	std::memcpy(&major, data.data() + 4, 4);
	std::memcpy(&minor, data.data() + 8, 4);
	if (major != 7 || minor < 3) return true;
	if (data.size() < 72) return false;
	std::uint32_t resources = 0;
	std::memcpy(&resources, data.data() + 68, 4);
	return resources <= MAX_VTF_RESOURCES && 80 + std::uint64_t(resources) * 8 <= data.size();
}

} // namespace

static std::optional<TextureInfo> readTextureInfoUnchecked(std::span<const std::byte> vtfData, std::string* error);
static std::optional<ImageRgba> decodeTextureUnchecked(std::span<const std::byte> vtfData, unsigned maxSize, std::string* error);

std::optional<TextureInfo> readTextureInfo(std::span<const std::byte> vtfData, std::string* error) {
	// vtfpp throws on some damaged files (offsets past the end): a bad texture in a mod must not
	// take the editor down.
	try {
		return readTextureInfoUnchecked(vtfData, error);
	} catch (const std::exception& e) {
		if (error) *error = std::string("damaged VTF: ") + e.what();
		return std::nullopt;
	}
}

static std::optional<TextureInfo> readTextureInfoUnchecked(std::span<const std::byte> vtfData, std::string* error) {
	if (!plausibleVtfHeader(vtfData)) {
		if (error) *error = "damaged VTF header";
		return std::nullopt;
	}
	const vtfpp::VTF vtf(vtfData, true);
	if (!vtf) {
		if (error) {
			*error = "not a valid VTF";
		}
		return std::nullopt;
	}
	TextureInfo info;
	info.width = vtf.getWidth();
	info.height = vtf.getHeight();
	info.format = formatName(vtf.getFormat());
	info.mipCount = vtf.getMipCount();
	info.frameCount = vtf.getFrameCount();
	info.faceCount = vtf.getFaceCount();
	info.flags = vtf.getFlags();
	return info;
}

std::optional<ImageRgba> decodeTexture(std::span<const std::byte> vtfData, unsigned maxSize, std::string* error) {
	try {
		return decodeTextureUnchecked(vtfData, maxSize, error);
	} catch (const std::exception& e) {
		if (error) *error = std::string("damaged VTF: ") + e.what();
		return std::nullopt;
	}
}

static std::optional<ImageRgba> decodeTextureUnchecked(std::span<const std::byte> vtfData, unsigned maxSize, std::string* error) {
	if (!plausibleVtfHeader(vtfData)) {
		if (error) *error = "damaged VTF header";
		return std::nullopt;
	}
	const vtfpp::VTF vtf(vtfData);
	if (!vtf || vtf.getMipCount() == 0) {
		if (error) {
			*error = "not a valid VTF";
		}
		return std::nullopt;
	}
	// getDataPosition walks every frame, face, slice and mip the header claims: a damaged one
	// (65280 frames x 65535 slices) kept a loader thread busy for minutes (found by fuzzing).
	// Real textures stay far below these limits.
	if (vtf.getMipCount() > 16 || vtf.getFrameCount() > 4096 || vtf.getDepth() > 2048 ||
	    std::uint64_t(vtf.getFrameCount()) * vtf.getFaceCount() * std::max<std::uint16_t>(vtf.getDepth(), 1) > 65536) {
		if (error) *error = "damaged VTF header (frames, slices or mip levels)";
		return std::nullopt;
	}
	std::uint8_t mip = 0;
	if (maxSize > 0) {
		// Mip 0 is the largest; walk down while the next one is still big enough.
		while (mip + 1 < vtf.getMipCount() && std::max(vtf.getWidth(mip + 1), vtf.getHeight(mip + 1)) >= maxSize) {
			++mip;
		}
	}
	ImageRgba image;
	image.width = vtf.getWidth(mip);
	image.height = vtf.getHeight(mip);
	// A damaged file can hold less data than its size and format need; the decompressors of
	// vtfpp read past the end then (a crash on the first damaged texture of a mod).
	// (vtfpp's getImageDataRaw cuts its span out of the image resource without checking that the
	// mip lies inside it, so the position is checked here first.)
	const auto* resource = vtf.getResource(vtfpp::Resource::TYPE_IMAGE_DATA);
	std::uint32_t offset = 0, length = 0;
	const bool inside = resource &&
	                    vtfpp::ImageFormatDetails::getDataPosition(offset, length, vtf.getFormat(), mip, vtf.getMipCount(), 0, vtf.getFrameCount(), 0,
	                                                               vtf.getFaceCount(), vtf.getWidth(), vtf.getHeight(), 0, vtf.getDepth()) &&
	                    std::uint64_t(offset) + length <= resource->data.size();
	if (!inside || image.width == 0 || image.height == 0 ||
	    length < vtfpp::ImageFormatDetails::getDataLength(vtf.getFormat(), image.width, image.height)) {
		if (error) *error = "VTF image data is incomplete";
		return std::nullopt;
	}
	image.pixels = vtf.getImageDataAsRGBA8888(mip);
	if (image.pixels.size() != static_cast<std::size_t>(image.width) * image.height * 4) {
		if (error) {
			*error = "VTF image data could not be decoded (" + formatName(vtf.getFormat()) + ")";
		}
		return std::nullopt;
	}
	return image;
}

std::optional<ImageRgba> loadTexture(const GameFileSystem& fs, std::string_view name, unsigned maxSize, std::string* error) {
	const auto path = "materials/" + normalizeMaterialName(name) + ".vtf";
	const auto data = fs.read(path);
	if (!data) {
		if (error) {
			*error = path + " not found";
		}
		return std::nullopt;
	}
	return decodeTexture(*data, maxSize, error);
}

std::vector<std::byte> encodePng(const ImageRgba& image) {
	return vtfpp::ImageConversion::convertImageDataToFile(image.pixels, vtfpp::ImageFormat::RGBA8888, image.width, image.height, vtfpp::ImageConversion::FileFormat::PNG);
}

} // namespace sme
