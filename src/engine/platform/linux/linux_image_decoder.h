#pragma once

#include "engine/platform/image_decoder.h"

namespace underworld::platform::linux {

class LinuxImageDecoder final : public ImageDecoder {
public:
    [[nodiscard]] core::ImageData decode(const std::filesystem::path& path) override;
};

} // namespace underworld::platform::linux
