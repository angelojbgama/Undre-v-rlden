#include "game/content/content_source.h"

#include <filesystem>
#include <iostream>
#include <string>

// Native workspace content validator. Structural and cross-reference
// validation only: game runtime requirements (required attack/projectile
// ids and so on) are enforced by the game startup, not by every authored
// workspace, since combat content is authored data.
int main(int argc, char** argv) {
    if (argc != 2 || argv[1] == nullptr || std::string(argv[1]).empty()) {
        std::cerr << "usage: content_check <workspace-directory>\n";
        return 2;
    }
    underworld::game::content::ContentSourceSelection selection;
    selection.kind = underworld::game::content::ContentSourceKind::workspaceDirectory;
    selection.workspaceRoot = std::filesystem::path(argv[1]);
    const auto result = underworld::game::content::loadContentSource(selection);
    if (!result) {
        for (const auto& diagnostic : result.diagnostics) {
            std::cerr << underworld::game::content::formatContentWorkspaceDiagnostic(diagnostic) << '\n';
        }
        return 1;
    }
    const auto& authored = result.content->authored;
    std::cout << "PASS\nfiles: " << result.content->sourceFileCount
              << "\ndefinitions: "
              << authored.tilesets.size() + authored.projectiles.size() + authored.attacks.size()
              + authored.behaviors.size() + authored.enemies.size() + authored.items.size()
              + authored.objects.size() + authored.pickups.size() + authored.npcVisuals.size()
              + authored.npcs.size() + authored.dialogues.size() + authored.quests.size()
              + authored.playerProgressions.size() + authored.rewardProfiles.size()
              + authored.rewardGrants.size() + authored.shops.size()
              + authored.craftingRecipes.size()
              + authored.authoringDescriptors.size() + authored.tileSemantics.size()
              + authored.stamps.size() + authored.presentationEffects.size()
              + authored.visualImages.size() + authored.staticSprites.size()
              + authored.animations.size() + authored.enemyVisuals.size()
              + authored.objectVisuals.size()
              + authored.players.size() + authored.playerVisuals.size()
              + authored.uiScreens.size()
              << "\n";
    return 0;
}
