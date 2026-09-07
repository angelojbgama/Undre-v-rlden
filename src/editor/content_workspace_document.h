#pragma once

#include "game/content/content_workspace.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace underworld::editor {

enum class ContentDefinitionKind {
    tileset,
    projectile,
    attack,
    behavior,
    enemy,
    item,
    object,
    pickup,
    npc,
    npcVisual,
    dialogue,
    quest,
    authoringDescriptor,
    tileSemantic,
    stamp,
    playerProgression,
    rewardProfile,
    rewardGrant,
    shop,
    presentationEffect,
    visualImage,
    staticSprite,
    animation,
    enemyVisual,
    objectVisual
};

struct ContentDefinitionKey final {
    ContentDefinitionKind kind{ContentDefinitionKind::tileset};
    simulation::DefinitionId id{};
    [[nodiscard]] bool operator==(const ContentDefinitionKey&) const noexcept = default;
};

struct ContentFileDocument final {
    std::filesystem::path path;
    game::content::AuthoredContentPack authored;
    bool dirty{};
};

class ContentWorkspaceDocument final {
public:
    [[nodiscard]] static std::optional<ContentWorkspaceDocument> open(
        const std::filesystem::path& root, std::string& error);
    [[nodiscard]] static ContentWorkspaceDocument fromBuiltin(
        game::content::AuthoredContentPack authored);

    [[nodiscard]] const std::filesystem::path& root() const noexcept { return root_; }
    [[nodiscard]] bool writable() const noexcept { return writable_; }
    [[nodiscard]] bool builtinReadOnly() const noexcept { return !writable_; }
    [[nodiscard]] const std::vector<ContentFileDocument>& files() const noexcept { return files_; }
    [[nodiscard]] const std::optional<game::content::AuthoredContentPack>& mergedAuthored() const noexcept {
        return mergedAuthored_;
    }
    [[nodiscard]] const std::optional<game::GameContentRegistry>& compiledRegistry() const noexcept {
        return compiledRegistry_;
    }
    [[nodiscard]] const std::vector<game::content::ContentWorkspaceDiagnostic>& diagnostics() const noexcept {
        return diagnostics_;
    }
    [[nodiscard]] bool valid() const noexcept {
        return compiledRegistry_.has_value() && diagnostics_.empty();
    }
    [[nodiscard]] bool dirty() const noexcept;
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

    [[nodiscard]] std::vector<ContentDefinitionKey> index() const;
    [[nodiscard]] const game::content::ContentSourceLocation* sourceFor(
        const ContentDefinitionKey& key) const noexcept;
    [[nodiscard]] static const std::vector<ContentDefinitionKind>& categoryOrder() noexcept;
    [[nodiscard]] static const char* categoryName(ContentDefinitionKind kind) noexcept;
    [[nodiscard]] static std::optional<ContentDefinitionKind> categoryFromName(
        std::string_view name) noexcept;

    [[nodiscard]] bool createContentFile(const std::filesystem::path& relativePath,
                                          std::string& error);
    [[nodiscard]] bool saveAll(std::string& error);
    [[nodiscard]] bool validateWorkspace();

    [[nodiscard]] bool addVisualImage(const std::filesystem::path& file,
                                      game::content::AuthoredVisualImage value,
                                      std::string& error);
    [[nodiscard]] bool updateVisualImage(const simulation::DefinitionId& id,
                                         game::content::AuthoredVisualImage value,
                                         std::string& error);
    [[nodiscard]] bool removeVisualImage(const simulation::DefinitionId& id, std::string& error);

    [[nodiscard]] bool addStaticSprite(const std::filesystem::path& file,
                                       game::content::AuthoredStaticSprite value,
                                       std::string& error);
    [[nodiscard]] bool updateStaticSprite(const simulation::DefinitionId& id,
                                          game::content::AuthoredStaticSprite value,
                                          std::string& error);
    [[nodiscard]] bool removeStaticSprite(const simulation::DefinitionId& id, std::string& error);

    [[nodiscard]] bool addAnimation(const std::filesystem::path& file,
                                    game::content::AuthoredAnimation value,
                                    std::string& error);
    [[nodiscard]] bool updateAnimation(const simulation::DefinitionId& id,
                                       game::content::AuthoredAnimation value,
                                       std::string& error);
    [[nodiscard]] bool removeAnimation(const simulation::DefinitionId& id, std::string& error);
    [[nodiscard]] bool addAnimationFrame(const simulation::DefinitionId& id,
                                         game::content::AuthoredAnimationFrame value,
                                         std::string& error);
    [[nodiscard]] bool updateAnimationFrame(const simulation::DefinitionId& id, std::size_t index,
                                            game::content::AuthoredAnimationFrame value,
                                            std::string& error);
    [[nodiscard]] bool removeAnimationFrame(const simulation::DefinitionId& id,
                                            std::size_t index, std::string& error);
    [[nodiscard]] bool moveAnimationFrame(const simulation::DefinitionId& id,
                                          std::size_t from, std::size_t to, std::string& error);
    [[nodiscard]] bool addAnimationMarker(const simulation::DefinitionId& id, std::size_t frame,
                                          std::string marker, std::string& error);
    [[nodiscard]] bool updateAnimationMarker(const simulation::DefinitionId& id, std::size_t frame,
                                             std::size_t marker, std::string value,
                                             std::string& error);
    [[nodiscard]] bool removeAnimationMarker(const simulation::DefinitionId& id, std::size_t frame,
                                             std::size_t marker, std::string& error);

    [[nodiscard]] bool addEnemyVisual(const std::filesystem::path& file,
                                      game::content::AuthoredEnemyVisual value,
                                      std::string& error);
    [[nodiscard]] bool updateEnemyVisual(const simulation::DefinitionId& id,
                                         game::content::AuthoredEnemyVisual value,
                                         std::string& error);
    [[nodiscard]] bool removeEnemyVisual(const simulation::DefinitionId& id, std::string& error);
    [[nodiscard]] bool addEnemyVisualAction(const simulation::DefinitionId& id,
                                            game::content::AuthoredEnemyAttackVisual value,
                                            std::string& error);
    [[nodiscard]] bool removeEnemyVisualAction(const simulation::DefinitionId& id,
                                               const simulation::DefinitionId& actionId,
                                               std::string& error);

    [[nodiscard]] bool removeDefinition(const ContentDefinitionKey& key, std::string& error);

private:
    ContentWorkspaceDocument() = default;

    [[nodiscard]] bool rebuild(std::string* error = nullptr);
    [[nodiscard]] bool ensureWritable(std::string& error) const;
    [[nodiscard]] std::filesystem::path resolveDocumentPath(
        const std::filesystem::path& path) const;
    [[nodiscard]] ContentFileDocument* findFile(const std::filesystem::path& path) noexcept;
    [[nodiscard]] const ContentFileDocument* findFile(const std::filesystem::path& path) const noexcept;
    [[nodiscard]] ContentFileDocument* fileForDefinition(ContentDefinitionKind kind,
                                                          const simulation::DefinitionId& id) noexcept;
    [[nodiscard]] bool mutateFile(const std::filesystem::path& path,
                                  const std::function<bool(game::content::AuthoredContentPack&)>& mutation,
                                  std::string& error);
    [[nodiscard]] bool mutateDefinition(ContentDefinitionKind kind,
                                        const simulation::DefinitionId& id,
                                        const std::function<bool(game::content::AuthoredContentPack&)>& mutation,
                                        std::string& error);
    [[nodiscard]] bool addDefinition(ContentDefinitionKind kind, const std::filesystem::path& path,
                                     const simulation::DefinitionId& id,
                                     const std::function<void(game::content::AuthoredContentPack&)>& mutation,
                                     std::string& error);

    std::filesystem::path root_;
    bool writable_{};
    std::vector<ContentFileDocument> files_;
    std::vector<std::vector<game::content::ContentJsonDefinitionOrigin>> origins_;
    std::optional<game::content::AuthoredContentPack> mergedAuthored_;
    game::content::ContentSourceMap sources_;
    std::optional<game::GameContentRegistry> compiledRegistry_;
    std::vector<game::content::ContentWorkspaceDiagnostic> diagnostics_;
    std::uint64_t revision_{};
};

} // namespace underworld::editor
