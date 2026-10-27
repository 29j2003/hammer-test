// texture.h - Decodes VTF textures to RGBA8 images for previews and the viewport.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace sme {

class GameFileSystem;

/// 8-bit RGBA pixels, rows top to bottom.
struct ImageRgba {
	std::uint16_t width = 0;
	std::uint16_t height = 0;
	std::vector<std::byte> pixels;
};

/// Header data of a VTF (size, format, flags) without the pixels.
struct TextureInfo {
	std::uint16_t width = 0;
	std::uint16_t height = 0;
	std::string format;          ///< e.g. "DXT1"
	unsigned mipCount = 0;
	unsigned frameCount = 0;
	unsigned faceCount = 0;
	std::uint32_t flags = 0;
};

/// Reads header information only.
[[nodiscard]] std::optional<TextureInfo> readTextureInfo(std::span<const std::byte> vtfData, std::string* error = nullptr);

/// Decodes the first frame. With maxSize > 0 the smallest mip that is still at least
/// maxSize on its longer side is used (cheap thumbnails); 0 = full size.
[[nodiscard]] std::optional<ImageRgba> decodeTexture(std::span<const std::byte> vtfData, unsigned maxSize = 0, std::string* error = nullptr);

/// Loads "materials/<name>.vtf" through the file system and decodes it.
[[nodiscard]] std::optional<ImageRgba> loadTexture(const GameFileSystem& fs, std::string_view name, unsigned maxSize = 0, std::string* error = nullptr);

/// Encodes an image as PNG (used by the probe tool and tests).
[[nodiscard]] std::vector<std::byte> encodePng(const ImageRgba& image);

} // namespace sme
