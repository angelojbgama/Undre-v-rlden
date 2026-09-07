#include "game/content/content_workspace.h"

#include "game/content/content_compiler.h"

#include <algorithm>
#include <fstream>
#include <system_error>
#include <sstream>
#include <string_view>

namespace underworld::game::content {
namespace {

std::filesystem::path normalized(const std::filesystem::path& path) {
    return path.lexically_normal();
}

std::string categoryOf(ContentKind kind) {
    switch (kind) {
    case ContentKind::tileset: return "tilesets";
    case ContentKind::projectile: return "projectiles";
    case ContentKind::attack: return "attacks";
    case ContentKind::behavior: return "behaviors";
    case ContentKind::enemy: return "enemies";
    case ContentKind::item: return "items";
    case ContentKind::object: return "objects";
    case ContentKind::pickup: return "pickups";
    case ContentKind::npc: return "npcs";
    case ContentKind::npcVisual: return "npcVisuals";
    case ContentKind::dialogue: return "dialogues";
    case ContentKind::quest: return "quests";
    case ContentKind::authoringDescriptor: return "authoringDescriptors";
    case ContentKind::tileSemantic: return "tileSemantics";
    case ContentKind::stamp: return "stamps";
    case ContentKind::playerProgression: return "playerProgressions";
    case ContentKind::rewardProfile: return "rewardProfiles";
    case ContentKind::rewardGrant: return "rewardGrants";
    case ContentKind::shop: return "shops";
    case ContentKind::presentationEffect: return "presentationEffects";
    }
    return {};
}

struct DecodedFile final {
    std::filesystem::path path;
    AuthoredContentPack content;
    std::vector<ContentJsonDefinitionOrigin> origins;
};

void addDiagnostic(ContentWorkspaceLoadResult& result,
                   ContentWorkspaceDiagnosticStage stage,
                   const std::filesystem::path& path, std::size_t line,
                   std::size_t column, std::string jsonPath, std::string code,
                   std::string message, std::string category = {},
                   simulation::DefinitionId id = {},
                   std::filesystem::path related = {}) {
    result.diagnostics.push_back({stage, path, std::move(related), line, column,
                                  std::move(jsonPath), std::move(code),
                                  std::move(category), std::move(id), std::move(message)});
}

template<class T, class IdFn>
void mergeCategory(const DecodedFile& file, std::string_view category, const std::vector<T>& values,
                   std::vector<T>& destination, ContentSourceMap& sources,
                   ContentWorkspaceLoadResult& result, IdFn idFn) {
    std::size_t originIndex = 0;
    for (const auto& value : values) {
        while (originIndex < file.origins.size() && file.origins[originIndex].category != category) ++originIndex;
        if (originIndex == file.origins.size()) continue;
        const auto& origin = file.origins[originIndex++];
        const auto id = idFn(value);
        const auto* first = sources.find(category, id);
        const ContentSourceLocation current{file.path, std::string(category), id,
                                            origin.line, origin.column, origin.jsonPath};
        if (first) {
            addDiagnostic(result, ContentWorkspaceDiagnosticStage::merge, file.path,
                          current.line, current.column, current.jsonPath,
                          "duplicate_definition", std::string(id.value()) +
                          " is already defined; first definition is in " +
                          first->sourcePath.generic_string() + ":" +
                          std::to_string(first->line) + ":" + std::to_string(first->column),
                          std::string(category), id, first->sourcePath);
            continue;
        }
        destination.push_back(value);
        sources.definitions.push_back(current);
    }
}

std::string fieldPath(const ContentSourceLocation& origin, std::string_view field) {
    if (field.empty() || field == "definition" || field == "range" || field == "offer" || field == "payload")
        return origin.jsonPath;
    return origin.jsonPath + "." + std::string(field);
}

} // namespace

const ContentSourceLocation* ContentSourceMap::find(
    std::string_view category, const simulation::DefinitionId& id) const noexcept {
    const auto it = std::find_if(definitions.begin(), definitions.end(),
        [&](const auto& value) { return value.category == category && value.definitionId == id; });
    return it == definitions.end() ? nullptr : &*it;
}

ContentWorkspaceLoadResult loadContentWorkspaceFiles(
    std::span<const std::filesystem::path> files) {
    ContentWorkspaceLoadResult result;
    if (files.empty()) {
        addDiagnostic(result, ContentWorkspaceDiagnosticStage::merge, {}, 0, 0, {},
                      "empty_workspace", "at least one content source file is required");
        return result;
    }

    std::vector<std::filesystem::path> paths;
    paths.reserve(files.size());
    for (const auto& path : files) paths.push_back(normalized(path));
    std::sort(paths.begin(), paths.end(), [](const auto& a, const auto& b) {
        return a.generic_string() < b.generic_string();
    });
    for (std::size_t i = 1; i < paths.size(); ++i) {
        if (paths[i] == paths[i - 1]) {
            addDiagnostic(result, ContentWorkspaceDiagnosticStage::merge, paths[i], 0, 0, {},
                          "duplicate_source_file", "content source path is listed more than once");
        }
    }
    if (!result.diagnostics.empty()) return result;

    std::vector<DecodedFile> decoded;
    for (const auto& path : paths) {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            addDiagnostic(result, ContentWorkspaceDiagnosticStage::io, path, 0, 0, {},
                          "file_open_failed", "could not open content source file");
            continue;
        }
        std::ostringstream text;
        text << file.rdbuf();
        const auto parsed = decodeAuthoredContentJson(text.str());
        if (!parsed.content) {
            for (const auto& diagnostic : parsed.diagnostics) {
                addDiagnostic(result, ContentWorkspaceDiagnosticStage::decode, path,
                              diagnostic.line, diagnostic.column, diagnostic.path,
                              "json_decode", diagnostic.message);
            }
            continue;
        }
        decoded.push_back({path, std::move(*parsed.content), std::move(parsed.origins)});
    }
    if (!result.diagnostics.empty()) return result;

    AuthoredContentPack merged;
    ContentSourceMap sources;
    for (const auto& file : decoded) {
        mergeCategory(file, "tilesets", file.content.tilesets, merged.tilesets, sources, result, [](const auto& v) { return v.id; });
        mergeCategory(file, "projectiles", file.content.projectiles, merged.projectiles, sources, result, [](const auto& v) { return v.id; });
        mergeCategory(file, "attacks", file.content.attacks, merged.attacks, sources, result, [](const auto& v) { return v.id; });
        mergeCategory(file, "behaviors", file.content.behaviors, merged.behaviors, sources, result, [](const auto& v) { return v.id; });
        mergeCategory(file, "enemies", file.content.enemies, merged.enemies, sources, result, [](const auto& v) { return v.id; });
        mergeCategory(file, "items", file.content.items, merged.items, sources, result, [](const auto& v) { return v.id; });
        mergeCategory(file, "objects", file.content.objects, merged.objects, sources, result, [](const auto& v) { return v.id; });
        mergeCategory(file, "pickups", file.content.pickups, merged.pickups, sources, result, [](const auto& v) { return v.id; });
        mergeCategory(file, "npcVisuals", file.content.npcVisuals, merged.npcVisuals, sources, result, [](const auto& v) { return v.id; });
        mergeCategory(file, "npcs", file.content.npcs, merged.npcs, sources, result, [](const auto& v) { return v.id; });
        mergeCategory(file, "dialogues", file.content.dialogues, merged.dialogues, sources, result, [](const auto& v) { return v.id; });
        mergeCategory(file, "quests", file.content.quests, merged.quests, sources, result, [](const auto& v) { return v.id; });
        mergeCategory(file, "playerProgressions", file.content.playerProgressions, merged.playerProgressions, sources, result, [](const auto& v) { return v.id; });
        mergeCategory(file, "rewardProfiles", file.content.rewardProfiles, merged.rewardProfiles, sources, result, [](const auto& v) { return v.id; });
        mergeCategory(file, "rewardGrants", file.content.rewardGrants, merged.rewardGrants, sources, result, [](const auto& v) { return v.id; });
        mergeCategory(file, "shops", file.content.shops, merged.shops, sources, result, [](const auto& v) { return v.id; });
        mergeCategory(file, "authoringDescriptors", file.content.authoringDescriptors, merged.authoringDescriptors, sources, result, [](const auto& v) { return v.definitionId; });
        mergeCategory(file, "tileSemantics", file.content.tileSemantics, merged.tileSemantics, sources, result, [](const auto& v) { return v.id; });
        mergeCategory(file, "stamps", file.content.stamps, merged.stamps, sources, result, [](const auto& v) { return v.id; });
        mergeCategory(file, "presentationEffects", file.content.presentationEffects, merged.presentationEffects, sources, result, [](const auto& v) { return v.id; });
    }
    if (!result.diagnostics.empty()) return result;

    const auto compiled = compileContent(merged);
    if (!compiled.registry) {
        for (const auto& diagnostic : compiled.report.diagnostics) {
            const auto category = categoryOf(diagnostic.kind);
            const auto* origin = sources.find(category, diagnostic.definitionId);
            addDiagnostic(result, ContentWorkspaceDiagnosticStage::validation,
                          origin ? origin->sourcePath : std::filesystem::path{},
                          origin ? origin->line : 0, origin ? origin->column : 0,
                          origin ? fieldPath(*origin, diagnostic.field) : diagnostic.field,
                          diagnostic.code, diagnostic.message, category, diagnostic.definitionId);
        }
        return result;
    }
    result.workspace = LoadedContentWorkspace{std::move(merged), *compiled.registry,
                                              std::move(sources), decoded.size()};
    return result;
}

ContentWorkspaceDiscoveryResult discoverContentWorkspaceFiles(
    const std::filesystem::path& root) {
    ContentWorkspaceDiscoveryResult result;
    if (root.empty()) {
        result.diagnostics.push_back({ContentWorkspaceDiagnosticStage::io, root, {}, 0, 0, {},
                                      "workspace_root_missing", {}, {},
                                      "workspace root is empty"});
        return result;
    }
    std::error_code error;
    const auto status = std::filesystem::symlink_status(root, error);
    if (error || !std::filesystem::exists(status)) {
        result.diagnostics.push_back({ContentWorkspaceDiagnosticStage::io, root, {}, 0, 0, {},
                                      "workspace_root_missing", {}, {},
                                      "workspace root does not exist"});
        return result;
    }
    if (std::filesystem::is_symlink(status) || !std::filesystem::is_directory(status)) {
        result.diagnostics.push_back({ContentWorkspaceDiagnosticStage::io, root, {}, 0, 0, {},
                                      "workspace_root_not_directory", {}, {},
                                      "workspace root is not a directory"});
        return result;
    }

    std::vector<std::filesystem::path> files;
    std::filesystem::recursive_directory_iterator iterator(
        root, std::filesystem::directory_options::skip_permission_denied, error);
    if (error) {
        result.diagnostics.push_back({ContentWorkspaceDiagnosticStage::io, root, {}, 0, 0, {},
                                      "workspace_scan_failed", {}, {}, error.message()});
        return result;
    }
    const std::filesystem::recursive_directory_iterator end;
    while (iterator != end) {
        const auto path = iterator->path();
        std::error_code entryError;
        const auto entryStatus = iterator->symlink_status(entryError);
        if (entryError) {
            result.diagnostics.push_back({ContentWorkspaceDiagnosticStage::io, path, {}, 0, 0,
                                          {}, "workspace_scan_failed", {}, {},
                                          entryError.message()});
        } else if (!std::filesystem::is_symlink(entryStatus)) {
            if (std::filesystem::is_directory(entryStatus)) {
                // The iterator does not recurse into symlink directories because they
                // are skipped above; regular directories are traversed normally.
            } else if (std::filesystem::is_regular_file(entryStatus) &&
                       path.extension() == ".json") {
                files.push_back(path.lexically_normal());
            }
        }
        iterator.increment(error);
        if (error) {
            result.diagnostics.push_back({ContentWorkspaceDiagnosticStage::io, root, {}, 0, 0,
                                          {}, "workspace_scan_failed", {}, {},
                                          error.message()});
            error.clear();
        }
    }
    std::sort(files.begin(), files.end(), [](const auto& left, const auto& right) {
        return left.generic_string() < right.generic_string();
    });
    if (!result.diagnostics.empty()) return result;
    if (files.empty()) {
        result.diagnostics.push_back({ContentWorkspaceDiagnosticStage::merge, root, {}, 0, 0, {},
                                      "empty_workspace", {}, {},
                                      "workspace contains no JSON source files"});
        return result;
    }
    result.files = std::move(files);
    return result;
}

ContentWorkspaceDirectoryLoadResult loadContentWorkspaceDirectory(
    const std::filesystem::path& root) {
    const auto discovered = discoverContentWorkspaceFiles(root);
    if (!discovered.files) {
        return {std::nullopt, discovered.diagnostics, 0};
    }
    const auto loaded = loadContentWorkspaceFiles(*discovered.files);
    return {std::move(loaded.workspace), std::move(loaded.diagnostics), discovered.files->size()};
}

} // namespace underworld::game::content
