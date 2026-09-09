#include "game/content/content_source.h"
#include "game/maps/authored_world.h"
#include "game/maps/dmap.h"

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>

namespace {

std::string safeFileName(const underworld::simulation::MapId& id) {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string result;
    for (const unsigned char byte : std::string(id.value())) {
        if ((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
            (byte >= '0' && byte <= '9') || byte == '.' || byte == '_' || byte == '-') {
            result.push_back(static_cast<char>(byte));
        } else {
            result.push_back('%');
            result.push_back(hex[(byte >> 4U) & 0x0fU]);
            result.push_back(hex[byte & 0x0fU]);
        }
    }
    if (result.empty()) result = "map";
    return result + ".dmap";
}

const char* stageName(underworld::game::maps::AuthoredWorldDiagnosticStage stage) {
    using Stage = underworld::game::maps::AuthoredWorldDiagnosticStage;
    switch (stage) {
    case Stage::decode: return "decode";
    case Stage::validation: return "validation";
    case Stage::compile: return "compile";
    case Stage::io: return "io";
    }
    return "unknown";
}

void printDiagnostic(const underworld::game::maps::AuthoredWorldDiagnostic& diagnostic) {
    if (!diagnostic.path.empty()) {
        std::cerr << diagnostic.path;
        if (diagnostic.line != 0) std::cerr << ':' << diagnostic.line << ':' << diagnostic.column;
        std::cerr << ' ';
    }
    std::cerr << '[' << stageName(diagnostic.stage) << '/' << diagnostic.code << ']';
    if (!diagnostic.mapId.empty()) std::cerr << " map=" << diagnostic.mapId;
    if (!diagnostic.linkId.empty()) std::cerr << " link=" << diagnostic.linkId;
    if (!diagnostic.message.empty()) std::cerr << ' ' << diagnostic.message;
    std::cerr << '\n';
}

} // namespace

int main(int argc, char** argv) {
    std::optional<std::filesystem::path> contentRoot;
    std::filesystem::path source;
    std::filesystem::path output;
    std::string error;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index] == nullptr ? "" : argv[index];
        if (argument == "--content") {
            if (++index >= argc || argv[index] == nullptr || std::string(argv[index]).empty()) {
                error = "--content requires a directory";
                break;
            }
            contentRoot = std::filesystem::path(argv[index]);
        } else if (argument.rfind("--content=", 0) == 0) {
            contentRoot = std::filesystem::path(argument.substr(10));
            if (contentRoot->empty()) { error = "--content requires a directory"; break; }
        } else if (!argument.empty() && argument[0] == '-') {
            error = "unknown option: " + argument;
            break;
        } else if (source.empty()) {
            source = argument;
        } else if (output.empty()) {
            output = argument;
        } else {
            error = "too many positional arguments";
            break;
        }
    }
    if (source.empty() || output.empty()) error = error.empty() ? "source and output are required" : error;
    if (!error.empty()) {
        std::cerr << "usage: world_compile [--content <workspace>] <source.uworld> <output-directory>\n";
        std::cerr << error << '\n';
        return 2;
    }

    const auto authored = underworld::game::maps::readAuthoredWorldFile(source);
    if (!authored.source) {
        for (const auto& diagnostic : authored.diagnostics) printDiagnostic(diagnostic);
        return 1;
    }
    const underworld::game::content::ContentSourceSelection selection{
        contentRoot ? underworld::game::content::ContentSourceKind::workspaceDirectory
                    : underworld::game::content::ContentSourceKind::builtin,
        contentRoot.value_or(std::filesystem::path{})};
    const auto content = underworld::game::content::loadContentSource(selection);
    if (!content.content) {
        for (const auto& diagnostic : content.diagnostics) {
            std::cerr << underworld::game::content::formatContentWorkspaceDiagnostic(diagnostic) << '\n';
        }
        return 1;
    }
    const auto compiled = underworld::game::maps::compileAuthoredWorld(
        *authored.source, content.content->registry);
    if (!compiled.valid()) {
        for (const auto& diagnostic : compiled.diagnostics) printDiagnostic(diagnostic);
        return 1;
    }
    std::error_code filesystemError;
    std::filesystem::create_directories(output, filesystemError);
    if (filesystemError) {
        std::cerr << "[io/create_output] " << filesystemError.message() << '\n';
        return 1;
    }
    const auto catalogs = underworld::game::mapValidationCatalogs(content.content->registry);
    for (const auto& map : compiled.maps) {
        const auto outputPath = output / safeFileName(map.id);
        if (!underworld::game::maps::writeDmap(outputPath, map.data, error)) {
            std::cerr << "[io/write_dmap] " << error << '\n';
            return 1;
        }
        const auto verified = underworld::game::maps::readDmap(outputPath, &catalogs);
        if (!verified) {
            std::cerr << "[io/verify_dmap] " << outputPath << ": " << verified.error << '\n';
            return 1;
        }
    }
    std::cout << "PASS\nworld: " << source.string() << "\nmaps: " << compiled.maps.size() << '\n';
    return 0;
}
