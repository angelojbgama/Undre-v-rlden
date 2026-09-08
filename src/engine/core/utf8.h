#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace underworld::core {

// Reads one Unicode scalar value from a UTF-8 string. Invalid sequences consume
// one byte and return nullopt, allowing callers to apply a deterministic fallback.
[[nodiscard]] std::optional<std::uint32_t> decodeUtf8Codepoint(
    std::string_view text, std::size_t& offset) noexcept;

[[nodiscard]] bool appendUtf8Codepoint(std::string& text, std::uint32_t codepoint);
[[nodiscard]] bool eraseLastUtf8Codepoint(std::string& text) noexcept;
[[nodiscard]] std::size_t utf8CodepointCount(std::string_view text) noexcept;

} // namespace underworld::core
