#pragma once

#include <cstdint>
#include <vector>

namespace underworld::simulation {

struct EntityHandle final {
    std::uint32_t index{};
    std::uint32_t generation{};
    [[nodiscard]] constexpr bool operator==(const EntityHandle&) const noexcept = default;
    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return generation != 0;
    }
};

// Runtime identity only. This is deliberately not a component registry.
class EntityHandlePool final {
public:
    [[nodiscard]] EntityHandle create();
    [[nodiscard]] bool destroy(EntityHandle handle) noexcept;
    [[nodiscard]] bool valid(EntityHandle handle) const noexcept;

private:
    struct Slot final {
        std::uint32_t generation{1};
        bool alive{};
    };
    std::vector<Slot> slots_;
    std::vector<std::uint32_t> free_;
};

} // namespace underworld::simulation
