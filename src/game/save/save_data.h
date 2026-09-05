#pragma once

#include "engine/core/coordinates.h"
#include "engine/simulation/persistent_id.h"
#include "game/gameplay/facing_direction.h"
#include "game/gameplay/dialogue/dialogue_flags.h"
#include "game/gameplay/items.h"
#include "game/gameplay/player_items.h"
#include "game/gameplay/player.h"
#include "game/gameplay/quests/quest_state.h"
#include "game/maps/map_data.h"
#include "game/maps/runtime_world.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <span>
#include <vector>

namespace underworld::game::save {

struct SavedPlayer final {
    simulation::MapId currentMapId{};
    core::WorldPointI position{};
    gameplay::FacingDirection facing{gameplay::FacingDirection::down};
    int health{};
    std::array<std::optional<gameplay::ItemStack>, gameplay::PlayerInventory::slotCount> inventory{};
    std::uint64_t gold{};
    std::array<std::optional<simulation::DefinitionId>, gameplay::QuickSlotBindings::slotCount> quickSlots{};
};

struct ObjectDelta final {
    simulation::PersistentEntityKey key{};
    bool opened{};
    bool destroyed{};
    std::vector<gameplay::ItemStack> remainingContents;
};

struct PickupDelta final {
    simulation::PersistentEntityKey key{};
    bool collected{};
    std::optional<std::uint64_t> remainingQuantity;
};

struct SessionWorldState final {
    std::vector<ObjectDelta> objects;
    std::vector<PickupDelta> pickups;
    void set(ObjectDelta delta);
    void set(PickupDelta delta);
    [[nodiscard]] const ObjectDelta* findObject(const simulation::PersistentEntityKey& key) const noexcept;
    [[nodiscard]] const PickupDelta* findPickup(const simulation::PersistentEntityKey& key) const noexcept;
};

struct SaveData final {
    SavedPlayer player;
    SessionWorldState world;
    gameplay::dialogue::DialogueFlagSet dialogueFlags;
    gameplay::quests::QuestStateStore quests;
};

struct SaveValidationCatalogs final {
    const gameplay::ItemCatalog* items{};
    std::vector<const maps::MapData*> maps;
    const gameplay::quests::QuestCatalog* quests{};
};

struct SaveResult final {
    bool success{};
    SaveData data{};
    std::string error;
    [[nodiscard]] explicit operator bool() const noexcept { return success; }
};

inline constexpr std::uint16_t saveMajorVersion = 1;
// Minor 1 added FLGS; minor 2 adds the optional QSTS chunk. Older saves remain readable.
inline constexpr std::uint16_t saveMinorVersion = 2;

[[nodiscard]] std::string validateSaveData(const SaveData& data,
                                           const SaveValidationCatalogs& catalogs);
[[nodiscard]] std::vector<std::uint8_t> serializeSave(const SaveData& data);
[[nodiscard]] SaveResult deserializeSave(std::span<const std::uint8_t> bytes,
                                         const SaveValidationCatalogs& catalogs);
[[nodiscard]] bool writeSaveAtomic(const std::filesystem::path& path, const SaveData& data,
                                   std::string& error);
[[nodiscard]] SaveResult readSave(const std::filesystem::path& path,
                                  const SaveValidationCatalogs& catalogs);
[[nodiscard]] bool applyWorldState(const SessionWorldState& state, maps::RuntimeWorld& world,
                                   simulation::EntityHandlePool& handles,
                                   const gameplay::ItemCatalog& items, std::string& error);
void captureWorldState(const maps::MapData& original, const maps::RuntimeWorld& world,
                       SessionWorldState& state);
[[nodiscard]] SavedPlayer capturePlayer(const gameplay::Player& player,
                                        const gameplay::PlayerItems& items,
                                        const simulation::MapId& currentMapId);
[[nodiscard]] bool applyPlayer(const SavedPlayer& saved, gameplay::Player& player,
                               gameplay::PlayerItems& items, const gameplay::ItemCatalog& catalog,
                               std::string& error);

} // namespace underworld::game::save
