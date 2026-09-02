#pragma once

#include "engine/core/image_data.h"

#include <filesystem>

namespace underworld::platform {

class ImageDecoder {
public:
    virtual ~ImageDecoder() = default;
    [[nodiscard]] virtual core::ImageData decode(const std::filesystem::path& path) = 0;
};

} // namespace underworld::platform
