#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace underworld::editor {

enum class EditorLanguage {
    portugueseBrazil,
    englishUnitedStates,
};

[[nodiscard]] std::string_view editorLanguageCode(EditorLanguage language) noexcept;
[[nodiscard]] std::string_view editorLanguageNativeName(EditorLanguage language) noexcept;

enum class EditorTextId {
    file, edit, view, settings, language,
    newMap, openMap, save, saveAs, saveAll, exit, undo, redo,
    mapMode, contentMode, grid, frameMap, validateWorkspace,
    tiles, semantics, stamps, entities, layers, rules, encounters,
    select, pencil, erase, rectangle, fill, eyedropper, collision, region,
    properties, categories, definitions, inspector, status, add, remove,
    moveUp, moveDown, open, create, update, clear, set, placeInMap, findInMap,
    saveChangesBeforeContinuing, contentStudio, mapMaker, builtinContent,
    contentWorkspace, workspaceInvalid, compileUnavailable, playtestUnavailable,
    definitionCreated, definitionUpdated, definitionDeleted, workspaceValidated,
    id, displayName, visual, behavior, faction, health, movementSpeed,
    collisionBox, hurtbox, reward, attacks, stackLimit, category, quantity,
    dialogue, quest, objective, gold, experience, buyPrice, sellPrice,
    duration, priority, animation, source, anchor, frame, loop, image,
    tilesets, projectiles, attacksCategory, behaviors, enemies, items, objects,
    pickups, npcVisuals, npcs, dialogues, quests, playerProgressions,
    rewardProfiles, rewardGrants, shops, authoringDescriptors, tileSemantics,
    visualImages, staticSprites, animations, enemyVisuals, objectVisuals,
    missingDefinition, readOnlyCategory, fontUnavailable, initializationError,
    languagePortugueseBrazil, languageEnglish, unknown,
};

class EditorLocalization final {
public:
    explicit EditorLocalization(EditorLanguage language = EditorLanguage::portugueseBrazil) noexcept
        : language_(language) {}

    void setLanguage(EditorLanguage language) noexcept { language_ = language; }
    [[nodiscard]] EditorLanguage language() const noexcept { return language_; }
    [[nodiscard]] std::string_view text(EditorTextId id) const noexcept;
    [[nodiscard]] std::string localize(std::string_view englishText) const;

    [[nodiscard]] static bool catalogComplete(EditorLanguage language) noexcept;
    [[nodiscard]] static const std::vector<EditorTextId>& allTextIds() noexcept;

private:
    EditorLanguage language_;
};

} // namespace underworld::editor
