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
    [[nodiscard]] const game::content::AuthoredVisualImage* visualImage(
        const simulation::DefinitionId& id) const noexcept;
    [[nodiscard]] const game::content::AuthoredStaticSprite* staticSprite(
        const simulation::DefinitionId& id) const noexcept;
    [[nodiscard]] const game::content::AuthoredAnimation* animation(
        const simulation::DefinitionId& id) const noexcept;
    [[nodiscard]] const game::content::AuthoredEnemyVisual* enemyVisual(
        const simulation::DefinitionId& id) const noexcept;
    [[nodiscard]] const game::content::AuthoredWorldObjectVisual* objectVisual(
        const simulation::DefinitionId& id) const noexcept;
    [[nodiscard]] const game::content::AuthoredNpcVisualSet* npcVisual(
        const simulation::DefinitionId& id) const noexcept;
    [[nodiscard]] const game::content::AuthoredTileset* tileset(
        const simulation::DefinitionId& id) const noexcept;
    [[nodiscard]] const game::content::AuthoringDescriptor* authoringDescriptor(
        const simulation::DefinitionId& id) const noexcept;
    [[nodiscard]] const game::content::AuthoredTileSemantic* tileSemantic(
        const simulation::DefinitionId& id) const noexcept;
    [[nodiscard]] const game::content::AuthoredStamp* stamp(
        const simulation::DefinitionId& id) const noexcept;
    [[nodiscard]] const game::content::AuthoredProjectile* projectile(
        const simulation::DefinitionId& id) const noexcept;
    [[nodiscard]] const game::content::AuthoredAttack* attack(
        const simulation::DefinitionId& id) const noexcept;
    [[nodiscard]] const game::content::AuthoredBehaviorProfile* behavior(
        const simulation::DefinitionId& id) const noexcept;
    [[nodiscard]] const game::content::AuthoredEnemy* enemy(
        const simulation::DefinitionId& id) const noexcept;
    [[nodiscard]] const game::content::AuthoredItem* item(
        const simulation::DefinitionId& id) const noexcept;
    [[nodiscard]] const game::content::AuthoredWorldObject* object(
        const simulation::DefinitionId& id) const noexcept;
    [[nodiscard]] const game::content::AuthoredPickup* pickup(
        const simulation::DefinitionId& id) const noexcept;
    [[nodiscard]] const game::content::AuthoredNpc* npc(
        const simulation::DefinitionId& id) const noexcept;
    [[nodiscard]] const game::content::AuthoredDialogue* dialogue(
        const simulation::DefinitionId& id) const noexcept;
    [[nodiscard]] const game::content::AuthoredQuest* quest(
        const simulation::DefinitionId& id) const noexcept;
    [[nodiscard]] const game::content::AuthoredPlayerProgression* playerProgression(
        const simulation::DefinitionId& id) const noexcept;
    [[nodiscard]] const game::content::AuthoredRewardProfile* rewardProfile(
        const simulation::DefinitionId& id) const noexcept;
    [[nodiscard]] const game::content::AuthoredRewardGrant* rewardGrant(
        const simulation::DefinitionId& id) const noexcept;
    [[nodiscard]] const game::content::AuthoredShop* shop(
        const simulation::DefinitionId& id) const noexcept;
    [[nodiscard]] const game::content::AuthoredPresentationEffect* presentationEffect(
        const simulation::DefinitionId& id) const noexcept;
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

    [[nodiscard]] bool addObjectVisual(const std::filesystem::path& file,
                                       game::content::AuthoredWorldObjectVisual value,
                                       std::string& error);
    [[nodiscard]] bool updateObjectVisual(const simulation::DefinitionId& id,
                                          game::content::AuthoredWorldObjectVisual value,
                                          std::string& error);
    [[nodiscard]] bool removeObjectVisual(const simulation::DefinitionId& id, std::string& error);

    [[nodiscard]] bool addNpcVisual(const std::filesystem::path& file,
                                    game::content::AuthoredNpcVisualSet value,
                                    std::string& error);
    [[nodiscard]] bool updateNpcVisual(const simulation::DefinitionId& id,
                                       game::content::AuthoredNpcVisualSet value,
                                       std::string& error);
    [[nodiscard]] bool removeNpcVisual(const simulation::DefinitionId& id, std::string& error);

    [[nodiscard]] bool addTileset(const std::filesystem::path& file,
                                  game::content::AuthoredTileset value,
                                  std::string& error);
    [[nodiscard]] bool updateTileset(const simulation::DefinitionId& id,
                                     game::content::AuthoredTileset value,
                                     std::string& error);
    [[nodiscard]] bool removeTileset(const simulation::DefinitionId& id, std::string& error);
    [[nodiscard]] bool addAuthoringDescriptor(const std::filesystem::path& file,
                                              game::content::AuthoringDescriptor value,
                                              std::string& error);
    [[nodiscard]] bool updateAuthoringDescriptor(const simulation::DefinitionId& id,
                                                 game::content::AuthoringDescriptor value,
                                                 std::string& error);
    [[nodiscard]] bool removeAuthoringDescriptor(const simulation::DefinitionId& id,
                                                 std::string& error);
    [[nodiscard]] bool addTileSemantic(const std::filesystem::path& file,
                                       game::content::AuthoredTileSemantic value,
                                       std::string& error);
    [[nodiscard]] bool updateTileSemantic(const simulation::DefinitionId& id,
                                          game::content::AuthoredTileSemantic value,
                                          std::string& error);
    [[nodiscard]] bool removeTileSemantic(const simulation::DefinitionId& id,
                                          std::string& error);
    [[nodiscard]] bool addStamp(const std::filesystem::path& file,
                                game::content::AuthoredStamp value,
                                std::string& error);
    [[nodiscard]] bool updateStamp(const simulation::DefinitionId& id,
                                   game::content::AuthoredStamp value,
                                   std::string& error);
    [[nodiscard]] bool removeStamp(const simulation::DefinitionId& id, std::string& error);

    [[nodiscard]] bool addProjectile(const std::filesystem::path& file,
                                     game::content::AuthoredProjectile value,
                                     std::string& error);
    [[nodiscard]] bool updateProjectile(const simulation::DefinitionId& id,
                                        game::content::AuthoredProjectile value,
                                        std::string& error);
    [[nodiscard]] bool removeProjectile(const simulation::DefinitionId& id, std::string& error);
    [[nodiscard]] bool addAttack(const std::filesystem::path& file,
                                 game::content::AuthoredAttack value, std::string& error);
    [[nodiscard]] bool updateAttack(const simulation::DefinitionId& id,
                                    game::content::AuthoredAttack value, std::string& error);
    [[nodiscard]] bool removeAttack(const simulation::DefinitionId& id, std::string& error);
    [[nodiscard]] bool addBehavior(const std::filesystem::path& file,
                                   game::content::AuthoredBehaviorProfile value,
                                   std::string& error);
    [[nodiscard]] bool updateBehavior(const simulation::DefinitionId& id,
                                      game::content::AuthoredBehaviorProfile value,
                                      std::string& error);
    [[nodiscard]] bool removeBehavior(const simulation::DefinitionId& id, std::string& error);
    [[nodiscard]] bool addEnemy(const std::filesystem::path& file,
                                game::content::AuthoredEnemy value, std::string& error);
    [[nodiscard]] bool updateEnemy(const simulation::DefinitionId& id,
                                   game::content::AuthoredEnemy value, std::string& error);
    [[nodiscard]] bool removeEnemy(const simulation::DefinitionId& id, std::string& error);
    [[nodiscard]] bool addItem(const std::filesystem::path& file,
                               game::content::AuthoredItem value, std::string& error);
    [[nodiscard]] bool updateItem(const simulation::DefinitionId& id,
                                  game::content::AuthoredItem value, std::string& error);
    [[nodiscard]] bool removeItem(const simulation::DefinitionId& id, std::string& error);
    [[nodiscard]] bool addPickup(const std::filesystem::path& file,
                                 game::content::AuthoredPickup value, std::string& error);
    [[nodiscard]] bool updatePickup(const simulation::DefinitionId& id,
                                    game::content::AuthoredPickup value, std::string& error);
    [[nodiscard]] bool removePickup(const simulation::DefinitionId& id, std::string& error);
    [[nodiscard]] bool addObject(const std::filesystem::path& file,
                                 game::content::AuthoredWorldObject value, std::string& error);
    [[nodiscard]] bool updateObject(const simulation::DefinitionId& id,
                                    game::content::AuthoredWorldObject value, std::string& error);
    [[nodiscard]] bool removeObject(const simulation::DefinitionId& id, std::string& error);
    [[nodiscard]] bool addNpc(const std::filesystem::path& file,
                              game::content::AuthoredNpc value, std::string& error);
    [[nodiscard]] bool updateNpc(const simulation::DefinitionId& id,
                                 game::content::AuthoredNpc value, std::string& error);
    [[nodiscard]] bool removeNpc(const simulation::DefinitionId& id, std::string& error);
    [[nodiscard]] bool addDialogue(const std::filesystem::path& file,
                                   game::content::AuthoredDialogue value, std::string& error);
    [[nodiscard]] bool updateDialogue(const simulation::DefinitionId& id,
                                      game::content::AuthoredDialogue value, std::string& error);
    [[nodiscard]] bool removeDialogue(const simulation::DefinitionId& id, std::string& error);
    [[nodiscard]] bool addQuest(const std::filesystem::path& file,
                                game::content::AuthoredQuest value, std::string& error);
    [[nodiscard]] bool updateQuest(const simulation::DefinitionId& id,
                                   game::content::AuthoredQuest value, std::string& error);
    [[nodiscard]] bool removeQuest(const simulation::DefinitionId& id, std::string& error);
    [[nodiscard]] bool addPlayerProgression(const std::filesystem::path& file,
                                             game::content::AuthoredPlayerProgression value,
                                             std::string& error);
    [[nodiscard]] bool updatePlayerProgression(const simulation::DefinitionId& id,
                                                game::content::AuthoredPlayerProgression value,
                                                std::string& error);
    [[nodiscard]] bool removePlayerProgression(const simulation::DefinitionId& id,
                                               std::string& error);
    [[nodiscard]] bool addRewardProfile(const std::filesystem::path& file,
                                        game::content::AuthoredRewardProfile value,
                                        std::string& error);
    [[nodiscard]] bool updateRewardProfile(const simulation::DefinitionId& id,
                                           game::content::AuthoredRewardProfile value,
                                           std::string& error);
    [[nodiscard]] bool removeRewardProfile(const simulation::DefinitionId& id, std::string& error);
    [[nodiscard]] bool addRewardGrant(const std::filesystem::path& file,
                                      game::content::AuthoredRewardGrant value,
                                      std::string& error);
    [[nodiscard]] bool updateRewardGrant(const simulation::DefinitionId& id,
                                         game::content::AuthoredRewardGrant value,
                                         std::string& error);
    [[nodiscard]] bool removeRewardGrant(const simulation::DefinitionId& id, std::string& error);
    [[nodiscard]] bool addShop(const std::filesystem::path& file,
                               game::content::AuthoredShop value, std::string& error);
    [[nodiscard]] bool updateShop(const simulation::DefinitionId& id,
                                  game::content::AuthoredShop value, std::string& error);
    [[nodiscard]] bool removeShop(const simulation::DefinitionId& id, std::string& error);
    [[nodiscard]] bool addPresentationEffect(const std::filesystem::path& file,
                                             game::content::AuthoredPresentationEffect value,
                                             std::string& error);
    [[nodiscard]] bool updatePresentationEffect(const simulation::DefinitionId& id,
                                                game::content::AuthoredPresentationEffect value,
                                                std::string& error);
    [[nodiscard]] bool removePresentationEffect(const simulation::DefinitionId& id,
                                                std::string& error);

    [[nodiscard]] bool duplicateDefinition(const ContentDefinitionKey& source,
                                           simulation::DefinitionId duplicateId,
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
