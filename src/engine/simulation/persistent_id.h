#pragma once

#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace underworld::simulation {

class MapId final {
public:
    MapId() = default;
    explicit MapId(std::string value) : value_(std::move(value)) {
        if (value_.empty()) { throw std::invalid_argument("map id cannot be empty"); }
    }
    [[nodiscard]] bool empty() const noexcept { return value_.empty(); }
    [[nodiscard]] std::string_view value() const noexcept { return value_; }
    [[nodiscard]] bool operator==(const MapId&) const noexcept = default;
private:
    std::string value_;
};

class SpawnId final {
public:
    SpawnId() = default;
    explicit SpawnId(std::string value) : value_(std::move(value)) {
        if (value_.empty()) { throw std::invalid_argument("spawn id cannot be empty"); }
    }
    [[nodiscard]] bool empty() const noexcept { return value_.empty(); }
    [[nodiscard]] std::string_view value() const noexcept { return value_; }
    [[nodiscard]] bool operator==(const SpawnId&) const noexcept = default;
private:
    std::string value_;
};

struct PersistentInstanceId final {
    std::uint64_t value{};
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return value != 0; }
    [[nodiscard]] constexpr bool operator==(const PersistentInstanceId&) const noexcept = default;
};

struct PersistentEntityKey final {
    MapId mapId{};
    PersistentInstanceId instanceId{};
    [[nodiscard]] bool operator==(const PersistentEntityKey&) const noexcept = default;
};

struct MapIdHash final {
    [[nodiscard]] std::size_t operator()(const MapId& id) const noexcept {
        return std::hash<std::string_view>{}(id.value());
    }
};

struct PersistentEntityKeyHash final {
    [[nodiscard]] std::size_t operator()(const PersistentEntityKey& key) const noexcept {
        const std::size_t a = MapIdHash{}(key.mapId);
        const std::size_t b = std::hash<std::uint64_t>{}(key.instanceId.value);
        return a ^ (b + static_cast<std::size_t>(0x9e3779b9U) + (a << 6U) + (a >> 2U));
    }
};

} // namespace underworld::simulation
