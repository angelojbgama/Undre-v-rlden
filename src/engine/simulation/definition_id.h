#pragma once

#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace underworld::simulation {

// Stable content/type identity. Unlike EntityHandle, this value is suitable for
// definitions and future serialized references; an empty value is only a sentinel.
class DefinitionId final {
public:
    DefinitionId() = default;
    explicit DefinitionId(std::string value) : value_(std::move(value)) {
        if (value_.empty()) {
            throw std::invalid_argument("definition id cannot be empty");
        }
    }

    [[nodiscard]] bool empty() const noexcept { return value_.empty(); }
    [[nodiscard]] std::string_view value() const noexcept { return value_; }
    [[nodiscard]] bool operator==(const DefinitionId&) const noexcept = default;

private:
    std::string value_;
};

struct DefinitionIdHash final {
    [[nodiscard]] std::size_t operator()(const DefinitionId& id) const noexcept {
        return std::hash<std::string_view>{}(id.value());
    }
};

} // namespace underworld::simulation
