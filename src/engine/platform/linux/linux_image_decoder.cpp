#include "engine/platform/linux/linux_image_decoder.h"

#include <png.h>

#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace underworld::platform::linux {
namespace {

struct PngFile final {
    FILE* file{};
    png_structp png{};
    png_infop info{};

    ~PngFile() {
        if (png != nullptr) { png_destroy_read_struct(&png, &info, nullptr); }
        if (file != nullptr) { std::fclose(file); }
    }
};

[[noreturn]] void fail(const std::filesystem::path& path, const char* message) {
    throw std::runtime_error("PNG load failed for " + path.string() + ": " + message);
}

} // namespace

core::ImageData LinuxImageDecoder::decode(const std::filesystem::path& path) {
    PngFile input{std::fopen(path.c_str(), "rb")};
    if (input.file == nullptr) { fail(path, "file could not be opened"); }

    std::uint8_t signature[8]{};
    if (std::fread(signature, 1, sizeof(signature), input.file) != sizeof(signature) ||
        png_sig_cmp(signature, 0, sizeof(signature)) != 0) {
        fail(path, "file is not a PNG");
    }

    input.png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (input.png == nullptr) { fail(path, "decoder initialization failed"); }
    input.info = png_create_info_struct(input.png);
    if (input.info == nullptr) { fail(path, "decoder initialization failed"); }
    if (setjmp(png_jmpbuf(input.png)) != 0) { fail(path, "PNG data is invalid"); }

    png_init_io(input.png, input.file);
    png_set_sig_bytes(input.png, sizeof(signature));
    png_read_info(input.png, input.info);

    const png_uint_32 width = png_get_image_width(input.png, input.info);
    const png_uint_32 height = png_get_image_height(input.png, input.info);
    if (width == 0 || height == 0 || width > static_cast<png_uint_32>(std::numeric_limits<int>::max()) ||
        height > static_cast<png_uint_32>(std::numeric_limits<int>::max())) {
        fail(path, "invalid image dimensions");
    }

    const int colorType = png_get_color_type(input.png, input.info);
    const int bitDepth = png_get_bit_depth(input.png, input.info);
    const bool hasTransparency = colorType == PNG_COLOR_TYPE_RGBA ||
                                 colorType == PNG_COLOR_TYPE_GRAY_ALPHA ||
                                 png_get_valid(input.png, input.info, PNG_INFO_tRNS) != 0;
    if (bitDepth == 16) { png_set_strip_16(input.png); }
    if (colorType == PNG_COLOR_TYPE_PALETTE) { png_set_palette_to_rgb(input.png); }
    if (colorType == PNG_COLOR_TYPE_GRAY && bitDepth < 8) { png_set_expand_gray_1_2_4_to_8(input.png); }
    if (hasTransparency && png_get_valid(input.png, input.info, PNG_INFO_tRNS) != 0) {
        png_set_tRNS_to_alpha(input.png);
    }
    if (colorType == PNG_COLOR_TYPE_GRAY || colorType == PNG_COLOR_TYPE_GRAY_ALPHA) {
        png_set_gray_to_rgb(input.png);
    }
    if (!hasTransparency) { png_set_filler(input.png, 0xff, PNG_FILLER_AFTER); }
    png_read_update_info(input.png, input.info);

    const png_size_t rowBytes = png_get_rowbytes(input.png, input.info);
    const std::size_t expectedStride = static_cast<std::size_t>(width) * 4U;
    if (rowBytes != expectedStride || static_cast<std::size_t>(height) >
            std::numeric_limits<std::size_t>::max() / expectedStride) {
        fail(path, "unsupported pixel layout or image size");
    }

    core::ImageData output{static_cast<int>(width), static_cast<int>(height), expectedStride,
                           std::vector<std::uint8_t>(expectedStride * static_cast<std::size_t>(height))};
    std::vector<png_bytep> rows(static_cast<std::size_t>(height));
    for (std::size_t row = 0; row < rows.size(); ++row) {
        rows[row] = output.pixels.data() + row * expectedStride;
    }
    png_read_image(input.png, rows.data());
    png_read_end(input.png, nullptr);
    return output;
}

} // namespace underworld::platform::linux
