#pragma once

#include "engine/platform/image_decoder.h"

#include <memory>

namespace underworld::platform::win32 {

// Pimpl keeps COM and WIC types out of every non-Win32 header.
class Win32ImageDecoder final : public ImageDecoder {
public:
    Win32ImageDecoder();
    ~Win32ImageDecoder() override;
    Win32ImageDecoder(const Win32ImageDecoder&) = delete;
    Win32ImageDecoder& operator=(const Win32ImageDecoder&) = delete;

    [[nodiscard]] core::ImageData decode(const std::filesystem::path& path) override;

private:
    struct Implementation;
    std::unique_ptr<Implementation> implementation_;
};

} // namespace underworld::platform::win32
