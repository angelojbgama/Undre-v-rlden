#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace underworld::serialization {

class ByteWriter final {
public:
    void writeU8(std::uint8_t value);
    void writeU16(std::uint16_t value);
    void writeU32(std::uint32_t value);
    void writeU64(std::uint64_t value);
    void writeI32(std::int32_t value);
    void writeBytes(std::span<const std::uint8_t> value);
    void writeString(std::string_view value);
    [[nodiscard]] const std::vector<std::uint8_t>& bytes() const noexcept { return bytes_; }
    [[nodiscard]] std::vector<std::uint8_t> take() && noexcept { return std::move(bytes_); }
private:
    std::vector<std::uint8_t> bytes_;
};

class ByteReader final {
public:
    explicit ByteReader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}
    [[nodiscard]] bool readU8(std::uint8_t& value) noexcept;
    [[nodiscard]] bool readU16(std::uint16_t& value) noexcept;
    [[nodiscard]] bool readU32(std::uint32_t& value) noexcept;
    [[nodiscard]] bool readU64(std::uint64_t& value) noexcept;
    [[nodiscard]] bool readI32(std::int32_t& value) noexcept;
    [[nodiscard]] bool readBytes(std::size_t size, std::span<const std::uint8_t>& value) noexcept;
    [[nodiscard]] bool readString(std::string& value, std::uint32_t maximumBytes) noexcept;
    [[nodiscard]] bool skip(std::size_t size) noexcept;
    [[nodiscard]] std::size_t offset() const noexcept { return offset_; }
    [[nodiscard]] std::size_t remaining() const noexcept { return bytes_.size() - offset_; }
    [[nodiscard]] bool failed() const noexcept { return failed_; }
private:
    [[nodiscard]] bool canRead(std::size_t size) noexcept;
    std::span<const std::uint8_t> bytes_;
    std::size_t offset_{};
    bool failed_{};
};

} // namespace underworld::serialization
