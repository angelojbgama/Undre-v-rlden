#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace underworld::engine::data {
struct JsonSourceLocation final { std::size_t offset{}; std::size_t line{1}; std::size_t column{1}; };
struct JsonSourceSpan final { JsonSourceLocation begin{}; JsonSourceLocation end{}; };
struct JsonNumber final { std::string lexeme; };
struct JsonValue;
using JsonArray = std::vector<JsonValue>;
using JsonObjectMember = std::pair<std::string, JsonValue>;
using JsonObject = std::vector<JsonObjectMember>;
struct JsonValue final {
    JsonSourceSpan span{};
    std::variant<std::nullptr_t, bool, JsonNumber, std::string, JsonArray, JsonObject> value{nullptr};
    [[nodiscard]] const JsonValue* find(std::string_view key) const noexcept;
};
struct JsonParseDiagnostic final { JsonSourceLocation location{}; std::string message; };
struct JsonParseResult final { std::unique_ptr<JsonValue> value; std::vector<JsonParseDiagnostic> diagnostics; };
[[nodiscard]] JsonParseResult parseJson(std::string_view text);
[[nodiscard]] std::string writeJson(const JsonValue& value, bool pretty = true);
} // namespace underworld::engine::data
