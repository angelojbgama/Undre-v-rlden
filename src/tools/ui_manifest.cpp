#include "game/content/builtin_content.h"
#include "game/content/content_compiler.h"
#include "game/content/content_json.h"
#include "game/ui/ui_screens.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

// Emits the UI registry manifest as JSON on stdout, or --screens <file>
// writes the builtin authored UI screens (plus the visual definitions they
// reference) as a canonical content v7 pack. The Content Studio UI Composer
// consumes the file to list builtin screens and create workspace overrides.
int main(int argc, char** argv) {
    if (argc >= 3 && std::string(argv[1]) == "--screens") {
        auto builtin = underworld::game::content::makeBuiltinAuthoredContent();
        const auto compiled = underworld::game::content::compileContent(builtin);
        if (!compiled) {
            for (const auto& diagnostic : compiled.report.diagnostics) {
                if (diagnostic.severity != underworld::game::content::ContentDiagnosticSeverity::error) {
                    continue;
                }
                std::cerr << "[" << diagnostic.code << "] " << diagnostic.message
                          << " (id=" << diagnostic.definitionId.value()
                          << " field=" << diagnostic.field << ")\n";
            }
            std::cerr << "builtin ui screens failed to compile\n";
            return 1;
        }
        underworld::game::content::AuthoredContentPack exportPack;
        exportPack.uiScreens = std::move(builtin.uiScreens);
        exportPack.visualImages = std::move(builtin.visualImages);
        exportPack.staticSprites = std::move(builtin.staticSprites);
        exportPack.animations = std::move(builtin.animations);
        const std::filesystem::path output{argv[2]};
        std::ofstream file(output, std::ios::binary | std::ios::trunc);
        if (!file) {
            std::cerr << "could not open " << output.string() << " for writing\n";
            return 1;
        }
        file << underworld::game::content::encodeAuthoredContentJson(exportPack);
        file.flush();
        if (!file) {
            std::cerr << "could not write " << output.string() << "\n";
            return 1;
        }
        std::cout << "wrote " << output.string() << "\n";
        return 0;
    }
    if (argc != 1) {
        std::cerr << "usage: ui_manifest [--screens <output.json>]\n";
        return 2;
    }
    std::cout << underworld::game::ui::emitUiManifestJson();
    return 0;
}
