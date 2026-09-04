#include "engine/serialization/byte_io.h"

#include <limits>
#include <stdexcept>

namespace underworld::serialization {

void ByteWriter::writeU8(std::uint8_t value) { bytes_.push_back(value); }
void ByteWriter::writeU16(std::uint16_t value) {
    for (unsigned shift = 0; shift < 16; shift += 8) { writeU8(static_cast<std::uint8_t>(value >> shift)); }
}
void ByteWriter::writeU32(std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) { writeU8(static_cast<std::uint8_t>(value >> shift)); }
}
void ByteWriter::writeU64(std::uint64_t value) {
    for (unsigned shift = 0; shift < 64; shift += 8) { writeU8(static_cast<std::uint8_t>(value >> shift)); }
}
void ByteWriter::writeI32(std::int32_t value) { writeU32(static_cast<std::uint32_t>(value)); }
void ByteWriter::writeBytes(std::span<const std::uint8_t> value) {
    bytes_.insert(bytes_.end(), value.begin(), value.end());
}
void ByteWriter::writeString(std::string_view value) {
    if (value.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("serialized string exceeds u32 length");
    }
    writeU32(static_cast<std::uint32_t>(value.size()));
    writeBytes({reinterpret_cast<const std::uint8_t*>(value.data()), value.size()});
}

bool ByteReader::canRead(std::size_t size) noexcept {
    if (failed_ || size > bytes_.size() - offset_) { failed_ = true; return false; }
    return true;
}
bool ByteReader::readU8(std::uint8_t& value) noexcept {
    if (!canRead(1)) { return false; }
    value = bytes_[offset_++]; return true;
}
bool ByteReader::readU16(std::uint16_t& value) noexcept {
    if (!canRead(2)) { return false; }
    value = static_cast<std::uint16_t>(bytes_[offset_]) |
            static_cast<std::uint16_t>(bytes_[offset_ + 1] << 8U);
    offset_ += 2; return true;
}
bool ByteReader::readU32(std::uint32_t& value) noexcept {
    if (!canRead(4)) { return false; }
    value = 0;
    for (unsigned index = 0; index < 4; ++index) {
        value |= static_cast<std::uint32_t>(bytes_[offset_ + index]) << (index * 8U);
    }
    offset_ += 4; return true;
}
bool ByteReader::readU64(std::uint64_t& value) noexcept {
    if (!canRead(8)) { return false; }
    value = 0;
    for (unsigned index = 0; index < 8; ++index) {
        value |= static_cast<std::uint64_t>(bytes_[offset_ + index]) << (index * 8U);
    }
    offset_ += 8; return true;
}
bool ByteReader::readI32(std::int32_t& value) noexcept {
    std::uint32_t bits{}; if (!readU32(bits)) { return false; }
    value = static_cast<std::int32_t>(bits); return true;
}
bool ByteReader::readBytes(std::size_t size, std::span<const std::uint8_t>& value) noexcept {
    if (!canRead(size)) { return false; }
    value = bytes_.subspan(offset_, size); offset_ += size; return true;
}
bool ByteReader::readString(std::string& value, std::uint32_t maximumBytes) noexcept {
    std::uint32_t size{};
    if (!readU32(size) || size > maximumBytes || !canRead(size)) { failed_ = true; return false; }
    value.assign(reinterpret_cast<const char*>(bytes_.data() + offset_), size);
    offset_ += size; return true;
}
bool ByteReader::skip(std::size_t size) noexcept {
    if (!canRead(size)) { return false; }
    offset_ += size; return true;
}

} // namespace underworld::serialization
