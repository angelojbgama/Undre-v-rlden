#pragma once

#include "engine/core/pixel_buffer_view.h"

#include <filesystem>
#include <string>

namespace underworld::game::audit {

// Writes a bottom-up 32-bit BMP while preserving the logical framebuffer's RGBA8
// pixels as BGRA bytes in the file. No desktop/window capture is involved.
[[nodiscard]] bool writeBmp32(const std::filesystem::path& path,
                              core::PixelBufferView surface, std::string& error);

} // namespace underworld::game::audit
