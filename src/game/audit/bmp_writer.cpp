#include "game/audit/bmp_writer.h"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>

namespace underworld::game::audit {
namespace {

constexpr std::uint32_t bitmapInfoHeaderSize = 40;
constexpr std::uint32_t bytesPerPixel = 4;

void writeU16(std::ofstream& file, std::uint16_t value) {
    const char bytes[] = {static_cast<char>(value & 0xffU),
                          static_cast<char>((value >> 8U) & 0xffU)};
    file.write(bytes, sizeof(bytes));
}

void writeU32(std::ofstream& file, std::uint32_t value) {
    const char bytes[] = {static_cast<char>(value & 0xffU),
                          static_cast<char>((value >> 8U) & 0xffU),
                          static_cast<char>((value >> 16U) & 0xffU),
                          static_cast<char>((value >> 24U) & 0xffU)};
    file.write(bytes, sizeof(bytes));
}

bool checkedSizes(core::PixelBufferView surface, std::size_t& rowBytes,
                  std::size_t& imageBytes, std::string& error) {
    if (!surface.isValid() || surface.format != core::PixelFormat::rgba8) {
        error = "BMP source must be a valid RGBA8 pixel buffer";
        return false;
    }
    const auto width = static_cast<std::size_t>(surface.width);
    const auto height = static_cast<std::size_t>(surface.height);
    if (width > std::numeric_limits<std::size_t>::max() / bytesPerPixel) {
        error = "BMP row size overflows";
        return false;
    }
    rowBytes = width * bytesPerPixel;
    if (height > std::numeric_limits<std::size_t>::max() / rowBytes) {
        error = "BMP image size overflows";
        return false;
    }
    imageBytes = rowBytes * height;
    constexpr auto maximumFileSize = std::numeric_limits<std::uint32_t>::max();
    if (imageBytes > maximumFileSize ||
        imageBytes > static_cast<std::size_t>(maximumFileSize - 54U)) {
        error = "BMP image is too large for the supported format";
        return false;
    }
    return true;
}

} // namespace

bool writeBmp32(const std::filesystem::path& path,
                core::PixelBufferView surface, std::string& error) {
    std::size_t rowBytes{};
    std::size_t imageBytes{};
    if (!checkedSizes(surface, rowBytes, imageBytes, error)) { return false; }

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        error = "could not open BMP output: " + path.string();
        return false;
    }

    const auto fileSize = static_cast<std::uint32_t>(54U + imageBytes);
    const auto width = static_cast<std::uint32_t>(surface.width);
    const auto height = static_cast<std::uint32_t>(surface.height);
    writeU16(file, 0x4d42U); // BM
    writeU32(file, fileSize);
    writeU16(file, 0);
    writeU16(file, 0);
    writeU32(file, 54U);
    writeU32(file, bitmapInfoHeaderSize);
    writeU32(file, width);
    writeU32(file, height);
    writeU16(file, 1);
    writeU16(file, 32);
    writeU32(file, 0); // BI_RGB
    writeU32(file, static_cast<std::uint32_t>(imageBytes));
    writeU32(file, 0);
    writeU32(file, 0);
    writeU32(file, 0);
    writeU32(file, 0);

    const auto* source = reinterpret_cast<const std::uint8_t*>(surface.pixels);
    for (int y = surface.height - 1; y >= 0; --y) {
        const auto* row = source + static_cast<std::size_t>(y) * surface.strideBytes;
        for (int x = 0; x < surface.width; ++x) {
            const auto* pixel = row + static_cast<std::size_t>(x) * bytesPerPixel;
            const char bgra[] = {static_cast<char>(pixel[2]), static_cast<char>(pixel[1]),
                                 static_cast<char>(pixel[0]), static_cast<char>(pixel[3])};
            file.write(bgra, sizeof(bgra));
        }
    }
    if (!file) {
        error = "could not write BMP output: " + path.string();
        return false;
    }
    return true;
}

} // namespace underworld::game::audit
