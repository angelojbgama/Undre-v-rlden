#include "game/content/content_source.h"
#include "game/maps/authored_map.h"
#include "game/maps/dmap.h"
#include "tools/map_compile_options.h"

#include <iostream>
#include <string>

namespace {

const char* stageName(underworld::game::maps::AuthoredMapDiagnosticStage stage) {
    using Stage = underworld::game::maps::AuthoredMapDiagnosticStage;
    switch (stage) {
    case Stage::decode: return "decode";
    case Stage::validation: return "validation";
    case Stage::compile: return "compile";
    case Stage::io: return "io";
    }
    return "unknown";
}

void printDiagnostic(const underworld::game::maps::AuthoredMapDiagnostic& diagnostic) {
    if (!diagnostic.path.empty()) {
        std::cerr << diagnostic.path;
        if (diagnostic.line != 0) std::cerr << ':' << diagnostic.line << ':' << diagnostic.column;
        std::cerr << ' ';
    }
    std::cerr << '[' << stageName(diagnostic.stage) << '/' << diagnostic.code << ']';
    if (!diagnostic.message.empty()) std::cerr << ' ' << diagnostic.message;
    std::cerr << '\n';
}

} // namespace

int main(int argc, char** argv) {
    std::string parseError;
    const auto options = underworld::tools::parseMapCompileOptions(argc, argv, parseError);
    if (!options) {
        std::cerr << "usage: map_compile [--content <workspace>] <source.umap> <output.dmap>\n";
        std::cerr << parseError << '\n';
        return 2;
    }
    const auto source = underworld::game::maps::readAuthoredMapFile(options->source);
    if (!source.source) {
        for (const auto& diagnostic : source.diagnostics) {
            printDiagnostic(diagnostic);
        }
        return 1;
    }
    const underworld::game::content::ContentSourceSelection selection{
        options->contentRoot ? underworld::game::content::ContentSourceKind::workspaceDirectory
                             : underworld::game::content::ContentSourceKind::builtin,
        options->contentRoot.value_or(std::filesystem::path{})};
    const auto loadedContent = underworld::game::content::loadContentSource(selection);
    if (!loadedContent.content) {
        for (const auto& diagnostic : loadedContent.diagnostics) {
            std::cerr << underworld::game::content::formatContentWorkspaceDiagnostic(diagnostic) << '\n';
        }
        return 1;
    }
    const auto compiled = underworld::game::maps::compileAuthoredMap(
        *source.source, loadedContent.content->registry);
    if (!compiled.map) {
        for (const auto& diagnostic : compiled.diagnostics) {
            printDiagnostic(diagnostic);
        }
        return 1;
    }
    std::string error;
    if (!underworld::game::maps::writeDmap(options->output, *compiled.map, error)) {
        std::cerr << "[io/write_dmap] " << error << "\n";
        return 1;
    }
    const auto& registry = loadedContent.content->registry;
    const auto catalogs = underworld::game::mapValidationCatalogs(registry);
    const auto verified = underworld::game::maps::readDmap(options->output, &catalogs);
    if (!verified) {
        std::cerr << "[io/verify_dmap] production DMAP reader rejected the compiled output: "
                  << verified.error << "\n";
        return 1;
    }
    std::cout << "PASS\nmap: " << compiled.map->id.value()
              << "\nregions: " << compiled.map->regions.size()
              << "\nrules: " << compiled.map->worldRules.size()
              << "\nencounters: " << compiled.map->encounters.size() << "\n";
    return 0;
}
