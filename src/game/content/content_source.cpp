#include "game/content/content_source.h"

#include "game/content/builtin_content.h"
#include "game/content/content_compiler.h"
#include "game/gameplay/attack_definitions.h"
#include "game/gameplay/creatures/creature_engine.h"
#include "game/gameplay/rpg/player_progression.h"

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

} // namespace

ContentSourceLoadResult loadContentSource(const ContentSourceSelection& selection) {
    if (selection.kind == ContentSourceKind::workspaceDirectory) {
        const auto loaded = loadContentWorkspaceDirectory(selection.workspaceRoot);
        if (!loaded.workspace) return {std::nullopt, loaded.diagnostics};
        return {LoadedContentBundle{ContentSourceKind::workspaceDirectory,
                                    selection.workspaceRoot.lexically_normal(),
                                    loaded.sourceFileCount,
                                    std::move(loaded.workspace->authored),
                                    std::move(loaded.workspace->registry),
                                    std::move(loaded.workspace->sources)},
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
