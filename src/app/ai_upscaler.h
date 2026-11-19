// ai_upscaler.h - The AI upscaler for Enhance textures: Real-ESRGAN ncnn Vulkan, a program that
// ships next to Mapsmith (tools/realesrgan/), run once per texture with its full path (never
// through PATH or a shell), on a PNG in a temporary folder of Mapsmith's own. It needs a GPU with
// Vulkan; without one it fails and the built-in scaling is used (sme/texture_enhance.h).

#pragma once

#include <QString>

#include <sme/texture_enhance.h>

namespace sme::app {

/// The folder with the program and its models, or empty when it is not there. Next to the
/// executable; SME_REALESRGAN_DIR may point elsewhere (developers, tests).
[[nodiscard]] QString realEsrganFolder();
/// Whether the AI upscaler can be offered.
[[nodiscard]] inline bool aiUpscalerAvailable() { return !realEsrganFolder().isEmpty(); }
/// The upscaler as Enhance textures calls it (from a worker thread). Empty when there is no program.
[[nodiscard]] AiUpscaler makeAiUpscaler();

} // namespace sme::app
