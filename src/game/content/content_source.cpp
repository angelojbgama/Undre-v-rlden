#include "game/content/content_source.h"

#include "game/content/builtin_content.h"
#include "game/content/content_compiler.h"
#include "game/gameplay/attack_definitions.h"
#include "game/gameplay/creatures/creature_engine.h"
#include "game/gameplay/rpg/player_progression.h"

#include <algorithm>
#include <sstream>
#include <utility>

namespace underworld::game::content {
namespace {

void addCompileDiagnostics(ContentSourceLoadResult& result,
                           const ContentValidationReport& report) {
    for (const auto& diagnostic : report.diagnostics) {
        result.diagnostics.push_back({ContentWorkspaceDiagnosticStage::compile, {}, {}, 0, 0,
                                      diagnostic.field, diagnostic.code, {}, {},
                                      diagnostic.message});
    }
}

template<class T, class IdFn>
void overlayCategory(std::vector<T>& base, const std::vector<T>& authored, IdFn idOf) {
    for (const auto& value : authored) {
        const auto existing = std::find_if(base.begin(), base.end(), [&](const auto& candidate) {
            return idOf(candidate) == idOf(value);
        });
        if (existing == base.end()) base.push_back(value);
        else *existing = value;
    }
}

AuthoredContentPack overlayBuiltinContent(const AuthoredContentPack& authored) {
    auto result = makeBuiltinAuthoredContent();
#define OVERLAY_CATEGORY(member, idMember) \
    overlayCategory(result.member, authored.member, [](const auto& value) { return value.idMember; })
    OVERLAY_CATEGORY(tilesets, id);
    OVERLAY_CATEGORY(projectiles, id);
    OVERLAY_CATEGORY(attacks, id);
    OVERLAY_CATEGORY(behaviors, id);
    OVERLAY_CATEGORY(enemies, id);
    OVERLAY_CATEGORY(items, id);
    OVERLAY_CATEGORY(objects, id);
    OVERLAY_CATEGORY(pickups, id);
    OVERLAY_CATEGORY(npcs, id);
    OVERLAY_CATEGORY(npcVisuals, id);
    OVERLAY_CATEGORY(dialogues, id);
    OVERLAY_CATEGORY(quests, id);
    OVERLAY_CATEGORY(authoringDescriptors, definitionId);
    OVERLAY_CATEGORY(tileSemantics, id);
    OVERLAY_CATEGORY(stamps, id);
    OVERLAY_CATEGORY(players, id);
    OVERLAY_CATEGORY(playerProgressions, id);
    OVERLAY_CATEGORY(rewardProfiles, id);
    OVERLAY_CATEGORY(rewardGrants, id);
    OVERLAY_CATEGORY(shops, id);
    OVERLAY_CATEGORY(presentationEffects, id);
    OVERLAY_CATEGORY(visualImages, id);
    OVERLAY_CATEGORY(staticSprites, id);
    OVERLAY_CATEGORY(animations, id);
    OVERLAY_CATEGORY(enemyVisuals, id);
    OVERLAY_CATEGORY(objectVisuals, id);
    OVERLAY_CATEGORY(playerVisuals, id);
#undef OVERLAY_CATEGORY
    return result;
}

} // namespace

ContentSourceLoadResult loadContentSource(const ContentSourceSelection& selection) {
    if (selection.kind == ContentSourceKind::workspaceDirectory) {
        const auto discovered = discoverContentWorkspaceFiles(selection.workspaceRoot);
        if (!discovered.files) return {std::nullopt, discovered.diagnostics};
        const auto loaded = loadContentWorkspaceFiles(*discovered.files);
        if (!loaded.mergedAuthored) return {std::nullopt, loaded.diagnostics};

        auto authored = overlayBuiltinContent(*loaded.mergedAuthored);
        auto compiled = compileContent(authored);
        if (!compiled.registry) {
            ContentSourceLoadResult result;
            addCompileDiagnostics(result, compiled.report);
            return result;
        }
        return {LoadedContentBundle{ContentSourceKind::workspaceDirectory,
                                    selection.workspaceRoot.lexically_normal(),
                                    discovered.files->size(),
                                    std::move(authored),
                                    std::move(*compiled.registry),
                                    loaded.sources},
                {}};
    }

    const auto authored = makeBuiltinAuthoredContent();
    const auto compiled = compileContent(authored);
    if (!compiled.registry) {
        ContentSourceLoadResult result;
        addCompileDiagnostics(result, compiled.report);
        return result;
    }
    return {LoadedContentBundle{ContentSourceKind::builtin, {}, 0, authored,
                                std::move(*compiled.registry), {}}, {}};
}

std::string formatContentWorkspaceDiagnostic(
    const ContentWorkspaceDiagnostic& diagnostic) {
    const auto stageName = [](ContentWorkspaceDiagnosticStage stage) {
        switch (stage) {
        case ContentWorkspaceDiagnosticStage::io: return "io";
        case ContentWorkspaceDiagnosticStage::decode: return "decode";
        case ContentWorkspaceDiagnosticStage::merge: return "merge";
        case ContentWorkspaceDiagnosticStage::validation: return "validation";
        case ContentWorkspaceDiagnosticStage::compile: return "compile";
        }
        return "unknown";
    };
    std::ostringstream output;
    if (!diagnostic.sourcePath.empty()) {
        output << diagnostic.sourcePath.generic_string();
        if (diagnostic.line != 0) output << ':' << diagnostic.line << ':' << diagnostic.column;
        output << ' ';
    }
    output << '[' << stageName(diagnostic.stage) << '/' << diagnostic.code << ']';
    if (!diagnostic.category.empty()) output << " category=" << diagnostic.category;
    if (!diagnostic.definitionId.empty()) output << " definitionId=" << diagnostic.definitionId.value();
    if (!diagnostic.jsonPath.empty()) output << ' ' << diagnostic.jsonPath << ':';
    output << ' ' << diagnostic.message;
    if (!diagnostic.relatedSourcePath.empty()) {
        output << " (related: " << diagnostic.relatedSourcePath.generic_string() << ')';
    }
    return output.str();
}

std::vector<ContentWorkspaceDiagnostic>
validateCurrentRuntimeContentRequirements(const GameContentRegistry& registry) {
    std::vector<ContentWorkspaceDiagnostic> diagnostics;
    const auto require = [&](std::string category, const simulation::DefinitionId& id) {
        bool present = false;
        if (category == "tilesets") present = registry.tilesets().find(id) != nullptr;
        else if (category == "progressions") present = registry.progressions().find(id) != nullptr;
        else if (category == "players") present = registry.players().find(id) != nullptr;
        else if (category == "attacks") present = registry.attacks().find(id) != nullptr;
        else if (category == "projectiles") present = registry.projectiles().find(id) != nullptr;
        else if (category == "presentationEffects") present = registry.presentationEffects().find(id) != nullptr;
        if (!present) {
            diagnostics.push_back({ContentWorkspaceDiagnosticStage::compile, {}, {}, 0, 0, {},
                                   "runtime_requirement", category, id,
                                   "current game runtime requires this definition"});
        }
    };
    require("tilesets", {"tileset.dungeon"});
    require("progressions", gameplay::rpg::defaultPlayerProgressionId());
    require("players", gameplay::defaultPlayerDefinitionId());
    require("attacks", gameplay::playerSwordAttackId());
    require("attacks", gameplay::playerBowAttackId());
    require("attacks", gameplay::creatures::soldierSwordAttackId());
    require("attacks", gameplay::creatures::skullArrowAttackId());
    require("projectiles", gameplay::playerArrowProjectileId());
    require("projectiles", gameplay::creatures::skullArrowProjectileId());
    require("presentationEffects", {"effect.player.hit"});
    return diagnostics;
}

} // namespace underworld::game::content
