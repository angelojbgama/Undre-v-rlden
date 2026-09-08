#pragma once

#include "editor/editor_commands.h"
#include "game/maps/authored_world.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace underworld::editor {

struct ProjectPlacementUsage final {
    simulation::MapId mapId{};
    PlacementUsage usage;
};

class WorldProjectDocument final {
public:
    explicit WorldProjectDocument(EditorDocument initial);
    WorldProjectDocument(const WorldProjectDocument&) = delete;
    WorldProjectDocument& operator=(const WorldProjectDocument&) = delete;
    WorldProjectDocument(WorldProjectDocument&&) noexcept = default;
    WorldProjectDocument& operator=(WorldProjectDocument&&) noexcept = default;

    static WorldProjectDocument newProject(EditorDocument initial);
    static WorldProjectDocument fromStandalone(EditorDocument map);
    static std::optional<WorldProjectDocument> open(const std::filesystem::path& path,
        const game::GameContentRegistry& content, std::string& error);

    bool createMap(simulation::MapId id, std::uint32_t width, std::uint32_t height,
                   std::uint16_t tileSize, bool includePlayerSpawn,
                   const game::GameContentRegistry& content, std::string& error);
    bool importMap(const std::filesystem::path& path,
                   const game::GameContentRegistry& content, std::string& error);
    bool removeMap(const simulation::MapId& id, std::string& error);
    bool setActiveMap(const simulation::MapId& id, std::string& error) noexcept;
    bool setEntryMap(const simulation::MapId& id, std::string& error);

    bool save(const game::GameContentRegistry& content, std::string& error);
    bool saveAs(const std::filesystem::path& path,
                const game::GameContentRegistry& content, std::string& error);
    bool exportDmaps(const std::filesystem::path& directory,
                     const game::GameContentRegistry& content, std::string& error) const;
    bool autosave(const game::GameContentRegistry& content, std::string& error) const;

    [[nodiscard]] bool validate(const game::GameContentRegistry& content,
                                std::string& error) const;
    [[nodiscard]] game::maps::WorldCompileResult compile(
        const game::GameContentRegistry& content) const;
    [[nodiscard]] game::maps::AuthoredWorldSource authoredSource() const;

    [[nodiscard]] bool projectMode() const noexcept { return projectMode_; }
    [[nodiscard]] bool dirty() const noexcept;
    [[nodiscard]] bool hasMap(const simulation::MapId& id) const noexcept;
    [[nodiscard]] EditorDocument& activeDocument() noexcept { return maps_[activeIndex_]; }
    [[nodiscard]] const EditorDocument& activeDocument() const noexcept { return maps_[activeIndex_]; }
    [[nodiscard]] EditorDocument* findMap(const simulation::MapId& id) noexcept;
    [[nodiscard]] const EditorDocument* findMap(const simulation::MapId& id) const noexcept;
    [[nodiscard]] const std::vector<EditorDocument>& maps() const noexcept { return maps_; }
    [[nodiscard]] std::vector<EditorDocument>& maps() noexcept { return maps_; }
    [[nodiscard]] const simulation::MapId& activeMapId() const noexcept { return maps_[activeIndex_].data().id; }
    [[nodiscard]] const simulation::MapId& entryMapId() const noexcept { return entryMapId_; }
    [[nodiscard]] const std::optional<std::filesystem::path>& filePath() const noexcept { return filePath_; }
    [[nodiscard]] std::uint64_t revision() const noexcept;
    [[nodiscard]] std::vector<ProjectPlacementUsage> findPlacementUsages(
        const ContentDefinitionKey& key) const;

private:
    bool saveProject(const std::filesystem::path& path,
                     const game::GameContentRegistry& content, std::string& error,
                     bool clearDirty);
    void markProjectDirty() noexcept { projectDirty_ = true; ++revision_; }

    std::vector<EditorDocument> maps_;
    std::size_t activeIndex_{};
    simulation::MapId entryMapId_{};
    std::optional<std::filesystem::path> filePath_;
    bool projectMode_{true};
    bool projectDirty_{true};
    std::uint64_t revision_{1};
};

} // namespace underworld::editor
