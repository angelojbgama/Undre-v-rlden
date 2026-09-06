#include "game/content/builtin_content.h"
#include "game/maps/authored_map.h"
#include "game/maps/dmap.h"

#include <iostream>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: map_compile <source.umap> <output.dmap>\n";
        return 2;
    }
    const auto source = underworld::game::maps::readAuthoredMapFile(argv[1]);
    if (!source.source) {
        for (const auto& diagnostic : source.diagnostics) {
            std::cerr << diagnostic.path << ":" << diagnostic.line << ":" << diagnostic.column
                      << " [decode/" << diagnostic.code << "] " << diagnostic.message << "\n";
        }
        return 1;
    }
    const auto content = underworld::game::content::compileBuiltinContentOrThrow();
    const auto compiled = underworld::game::maps::compileAuthoredMap(*source.source, content);
    if (!compiled.map) {
        for (const auto& diagnostic : compiled.diagnostics) {
            std::cerr << diagnostic.path << ":" << diagnostic.line << ":" << diagnostic.column
                      << " [compile/" << diagnostic.code << "] " << diagnostic.message << "\n";
        }
        return 1;
    }
    std::string error;
    if (!underworld::game::maps::writeDmap(argv[2], *compiled.map, error)) {
        std::cerr << "[io/write_dmap] " << error << "\n";
        return 1;
    }
    std::cout << "PASS\nmap: " << compiled.map->id.value()
              << "\nregions: " << compiled.map->regions.size()
              << "\nrules: " << compiled.map->worldRules.size()
              << "\nencounters: " << compiled.map->encounters.size() << "\n";
    return 0;
}
