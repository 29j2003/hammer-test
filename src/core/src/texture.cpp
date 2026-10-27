// texture.cpp - VTF decoding via vtfpp (see texture.h).

#include <sme/texture.h>

#include <sme/filesystem.h>
#include <sme/material.h>

#include <algorithm>

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

} // namespace

std::optional<TextureInfo> readTextureInfo(std::span<const std::byte> vtfData, std::string* error) {
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
	const vtfpp::VTF vtf(vtfData);
	if (!vtf || vtf.getMipCount() == 0) {
		if (error) {
			*error = "not a valid VTF";
		}
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
