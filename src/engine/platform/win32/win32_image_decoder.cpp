#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include "engine/platform/win32/win32_image_decoder.h"

#include <filesystem>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

namespace underworld::platform::win32 {

namespace {

using Microsoft::WRL::ComPtr;

std::runtime_error wicError(const std::filesystem::path& path, const char* operation,
                            HRESULT result) {
    std::ostringstream message;
    message << "PNG load failed for " << path.string() << ": " << operation
            << " (HRESULT 0x" << std::hex << static_cast<unsigned long>(result) << ')';
    return std::runtime_error(message.str());
}

} // namespace

struct Win32ImageDecoder::Implementation final {
    ComPtr<IWICImagingFactory> factory;
    bool ownsComInitialization{};
};

Win32ImageDecoder::Win32ImageDecoder() : implementation_(std::make_unique<Implementation>()) {
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(comResult)) {
        implementation_->ownsComInitialization = true;
    } else if (comResult != RPC_E_CHANGED_MODE) {
        throw std::runtime_error("WIC initialization failed: CoInitializeEx failed");
    }

    const HRESULT factoryResult = CoCreateInstance(
        CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(implementation_->factory.ReleaseAndGetAddressOf()));
    if (FAILED(factoryResult)) {
        if (implementation_->ownsComInitialization) {
            CoUninitialize();
            implementation_->ownsComInitialization = false;
        }
        throw std::runtime_error("WIC initialization failed: imaging factory creation failed");
    }
}

Win32ImageDecoder::~Win32ImageDecoder() {
    if (implementation_) {
        implementation_->factory.Reset();
        if (implementation_->ownsComInitialization) {
            CoUninitialize();
        }
    }
}

core::ImageData Win32ImageDecoder::decode(const std::filesystem::path& path) {
    std::error_code fileError;
    if (!std::filesystem::is_regular_file(path, fileError)) {
        throw std::runtime_error("PNG file not found: " + path.string());
    }

    ComPtr<IWICBitmapDecoder> decoder;
    HRESULT result = implementation_->factory->CreateDecoderFromFilename(
        path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad,
        decoder.ReleaseAndGetAddressOf());
    if (FAILED(result)) {
        throw wicError(path, "decoder creation failed or image is unsupported", result);
    }

    ComPtr<IWICBitmapFrameDecode> frame;
    result = decoder->GetFrame(0, frame.ReleaseAndGetAddressOf());
    if (FAILED(result)) {
        throw wicError(path, "could not read image frame 0", result);
    }

    ComPtr<IWICFormatConverter> converter;
    result = implementation_->factory->CreateFormatConverter(converter.ReleaseAndGetAddressOf());
    if (FAILED(result)) {
        throw wicError(path, "format converter creation failed", result);
    }
    result = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
                                   WICBitmapDitherTypeNone, nullptr, 0.0,
                                   WICBitmapPaletteTypeCustom);
    if (FAILED(result)) {
        throw wicError(path, "conversion to RGBA8 failed", result);
    }

    UINT width = 0;
    UINT height = 0;
    result = converter->GetSize(&width, &height);
    if (FAILED(result)) {
        throw wicError(path, "dimension query failed", result);
    }
    if (width == 0 || height == 0 || width > static_cast<UINT>(std::numeric_limits<int>::max()) ||
        height > static_cast<UINT>(std::numeric_limits<int>::max())) {
        throw std::runtime_error("PNG has invalid or unsupported dimensions: " + path.string());
    }

    constexpr std::size_t bytesPerPixel = 4;
    const std::size_t sizeWidth = width;
    const std::size_t sizeHeight = height;
    if (sizeWidth > std::numeric_limits<std::size_t>::max() / bytesPerPixel) {
        throw std::length_error("PNG row stride overflows: " + path.string());
    }
    const std::size_t stride = sizeWidth * bytesPerPixel;
    if (sizeHeight > std::numeric_limits<std::size_t>::max() / stride) {
        throw std::length_error("PNG pixel buffer size overflows: " + path.string());
    }
    const std::size_t bufferSize = stride * sizeHeight;
    if (stride > std::numeric_limits<UINT>::max() || bufferSize > std::numeric_limits<UINT>::max()) {
        throw std::length_error("PNG is too large for the WIC CopyPixels contract: " + path.string());
    }

    core::ImageData output{static_cast<int>(width), static_cast<int>(height), stride,
                           std::vector<std::uint8_t>(bufferSize)};
    result = converter->CopyPixels(nullptr, static_cast<UINT>(stride),
                                   static_cast<UINT>(bufferSize), output.pixels.data());
    if (FAILED(result)) {
        throw wicError(path, "pixel copy failed", result);
    }
    return output;
}

} // namespace underworld::platform::win32
