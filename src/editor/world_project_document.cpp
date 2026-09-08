#include "editor/world_project_document.h"

#include "game/maps/dmap.h"

#include <algorithm>
#include <stdexcept>

namespace underworld::editor {
namespace {

std::string dmapFileName(const simulation::MapId& id) {
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
    result += ".dmap";
    return result;
}

} // namespace

WorldProjectDocument::WorldProjectDocument(EditorDocument initial) {
    if (initial.data().id.empty()) throw std::invalid_argument("world project map id cannot be empty");
    entryMapId_ = initial.data().id;
    maps_.push_back(std::move(initial));
}

WorldProjectDocument WorldProjectDocument::newProject(EditorDocument initial) {
    WorldProjectDocument project(std::move(initial));
    project.projectMode_ = true;
    project.projectDirty_ = true;
    return project;
}

WorldProjectDocument WorldProjectDocument::fromStandalone(EditorDocument map) {
    WorldProjectDocument project(std::move(map));
    project.projectMode_ = false;
    project.projectDirty_ = false;
    project.filePath_ = project.maps_.front().filePath();
    return project;
}

std::optional<WorldProjectDocument> WorldProjectDocument::open(
    const std::filesystem::path& path, const game::GameContentRegistry&, std::string& error) {
    const auto loaded = game::maps::readAuthoredWorldFile(path);
    if (!loaded.source) {
        error = loaded.diagnostics.empty() ? "could not decode authored world project" :
            loaded.diagnostics.front().message;
        return std::nullopt;
    }
    try {
        if (loaded.source->maps.empty()) { error = "world project contains no maps"; return std::nullopt; }
        WorldProjectDocument project(EditorDocument::fromAuthoredSource(
            loaded.source->maps.front()));
        project.maps_.clear();
        for (const auto& map : loaded.source->maps) project.maps_.push_back(
            EditorDocument::fromAuthoredSource(map));
        project.entryMapId_ = loaded.source->entryMapId;
        project.filePath_ = path;
        project.projectMode_ = true;
        project.projectDirty_ = false;
        project.activeIndex_ = 0;
        error.clear();
        return project;
    } catch (const std::exception& exception) { error = exception.what(); return std::nullopt; }
}

bool WorldProjectDocument::hasMap(const simulation::MapId& id) const noexcept {
    return findMap(id) != nullptr;
}

EditorDocument* WorldProjectDocument::findMap(const simulation::MapId& id) noexcept {
    const auto found = std::find_if(maps_.begin(), maps_.end(),
        [&](auto& map) { return map.data().id == id; });
    return found == maps_.end() ? nullptr : &*found;
}

const EditorDocument* WorldProjectDocument::findMap(const simulation::MapId& id) const noexcept {
    const auto found = std::find_if(maps_.begin(), maps_.end(),
        [&](const auto& map) { return map.data().id == id; });
    return found == maps_.end() ? nullptr : &*found;
}

bool WorldProjectDocument::createMap(simulation::MapId id, std::uint32_t width,
                                     std::uint32_t height, std::uint16_t tileSize,
                                     bool includePlayerSpawn,
                                     const game::GameContentRegistry& content,
                                     std::string& error) {
    if (id.empty()) { error = "MapId cannot be empty"; return false; }
    if (hasMap(id)) { error = "MapId already exists: " + std::string(id.value()); return false; }
    try {
        maps_.push_back(EditorDocument::newAuthoredMap(std::move(id), width, height, tileSize,
                                                        content, includePlayerSpawn));
        activeIndex_ = maps_.size() - 1;
        if (entryMapId_.empty()) entryMapId_ = maps_.back().data().id;
        projectMode_ = true; markProjectDirty(); error.clear(); return true;
    } catch (const std::exception& exception) { error = exception.what(); return false; }
}

bool WorldProjectDocument::importMap(const std::filesystem::path& path,
                                     const game::GameContentRegistry& content,
                                     std::string& error) {
    auto loaded = EditorDocument::open(path, content, error);
    if (!loaded) return false;
    if (hasMap(loaded->data().id)) { error = "MapId already exists: " +
        std::string(loaded->data().id.value()); return false; }
    const bool wasStandalone = !projectMode_;
    maps_.push_back(std::move(*loaded));
    activeIndex_ = maps_.size() - 1;
    projectMode_ = true;
    // A standalone UMAP path cannot become the persistence path of an embedded
    // multi-map project. Force an explicit Save As to avoid overwriting it with
    // UWORLD data.
    if (wasStandalone) filePath_.reset();
    markProjectDirty(); error.clear(); return true;
}

bool WorldProjectDocument::removeMap(const simulation::MapId& id, std::string& error) {
    if (maps_.size() <= 1) { error = "a world project must keep at least one map"; return false; }
    if (id == entryMapId_) { error = "cannot remove the entry map"; return false; }
    const auto found = std::find_if(maps_.begin(), maps_.end(),
        [&](const auto& map) { return map.data().id == id; });
    if (found == maps_.end()) { error = "map is not in the project"; return false; }
    for (const auto& map : maps_) for (const auto& link : map.data().links) {
        if (link.targetMapId == id) { error = "cannot remove map " + std::string(id.value()) +
            "; link " + link.id + " in " + std::string(map.data().id.value()) + " targets it"; return false; }
    }
    const auto removedIndex = static_cast<std::size_t>(std::distance(maps_.begin(), found));
    maps_.erase(found);
    if (activeIndex_ >= maps_.size()) activeIndex_ = maps_.size() - 1;
    else if (activeIndex_ > removedIndex) --activeIndex_;
    markProjectDirty(); error.clear(); return true;
}

bool WorldProjectDocument::setActiveMap(const simulation::MapId& id, std::string& error) noexcept {
    const auto found = std::find_if(maps_.begin(), maps_.end(),
        [&](const auto& map) { return map.data().id == id; });
    if (found == maps_.end()) { error = "map is not in the project"; return false; }
    activeIndex_ = static_cast<std::size_t>(std::distance(maps_.begin(), found));
    error.clear(); return true;
}

bool WorldProjectDocument::setEntryMap(const simulation::MapId& id, std::string& error) {
    if (!hasMap(id)) { error = "entry map is not in the project"; return false; }
    if (entryMapId_ != id) { entryMapId_ = id; markProjectDirty(); }
    error.clear(); return true;
}

game::maps::AuthoredWorldSource WorldProjectDocument::authoredSource() const {
    game::maps::AuthoredWorldSource source;
    source.entryMapId = entryMapId_;
    source.maps.reserve(maps_.size());
    for (const auto& map : maps_) source.maps.push_back(map.authoredSource());
    return source;
}

game::maps::WorldCompileResult WorldProjectDocument::compile(
    const game::GameContentRegistry& content) const {
    return game::maps::compileAuthoredWorld(authoredSource(), content);
}

bool WorldProjectDocument::validate(const game::GameContentRegistry& content,
                                    std::string& error) const {
    const auto result = compile(content);
    if (!result.valid()) {
        const auto& issue = result.diagnostics.front();
        error = issue.mapId.empty() ? issue.message : issue.mapId + ": " + issue.message;
        return false;
    }
    error.clear(); return true;
}

bool WorldProjectDocument::saveProject(const std::filesystem::path& path,
                                       const game::GameContentRegistry& content,
                                       std::string& error, bool clearDirty) {
    const auto compiled = compile(content);
    if (!compiled.valid()) { error = compiled.diagnostics.front().message; return false; }
    if (!game::maps::writeAuthoredWorldFile(path, authoredSource(), error)) return false;
    if (clearDirty) {
        for (auto& map : maps_) map.markSaved();
        projectDirty_ = false; filePath_ = path;
    }
    error.clear(); return true;
}

bool WorldProjectDocument::save(const game::GameContentRegistry& content, std::string& error) {
    if (!projectMode_) return maps_.front().save(content, error);
    if (!filePath_) { error = "world project has no file path; use Save As"; return false; }
    return saveProject(*filePath_, content, error, true);
}

bool WorldProjectDocument::saveAs(const std::filesystem::path& path,
                                  const game::GameContentRegistry& content, std::string& error) {
    if (!projectMode_) {
        if (path.extension() != ".uworld") return maps_.front().saveAs(path, content, error);
        projectMode_ = true;
        const bool saved = saveProject(path, content, error, true);
        if (!saved) projectMode_ = false;
        return saved;
    }
    if (path.extension() != ".uworld") { error = "world project Save As requires a .uworld path"; return false; }
    return saveProject(path, content, error, true);
}

bool WorldProjectDocument::exportDmaps(const std::filesystem::path& directory,
                                       const game::GameContentRegistry& content,
                                       std::string& error) const {
    if (directory.empty()) { error = "DMAP export directory cannot be empty"; return false; }
    const auto compiled = compile(content);
    if (!compiled.valid()) {
        error = compiled.diagnostics.empty() ? "world project compilation failed" :
            compiled.diagnostics.front().message;
        return false;
    }
    std::error_code filesystemError;
    std::filesystem::create_directories(directory, filesystemError);
    if (filesystemError) {
        error = "could not create DMAP export directory: " + filesystemError.message();
        return false;
    }
    for (const auto& map : compiled.maps) {
        if (!game::maps::writeDmap(directory / dmapFileName(map.id), map.data, error)) {
            error = "could not export " + std::string(map.id.value()) + ": " + error;
            return false;
        }
    }
    error.clear();
    return true;
}

bool WorldProjectDocument::autosave(const game::GameContentRegistry& content,
                                    std::string& error) const {
    error.clear();
    if (!dirty()) return true;
    if (!projectMode_) {
        const auto path = maps_.front().autosavePath();
        return path ? maps_.front().saveBackup(*path, content, error) : true;
    }
    if (!filePath_) return true;
    auto path = *filePath_; path += ".autosave.uworld";
    const auto compiled = compile(content);
    if (!compiled.valid()) { error = compiled.diagnostics.front().message; return false; }
    return game::maps::writeAuthoredWorldFile(path, authoredSource(), error);
}

bool WorldProjectDocument::dirty() const noexcept {
    if (projectDirty_) return true;
    return std::any_of(maps_.begin(), maps_.end(), [](const auto& map) { return map.dirty(); });
}

std::uint64_t WorldProjectDocument::revision() const noexcept {
    std::uint64_t result = revision_;
    for (const auto& map : maps_) {
        result ^= map.revision() + 0x9e3779b97f4a7c15ULL + (result << 6U) + (result >> 2U);
    }
    return result;
}

std::vector<ProjectPlacementUsage> WorldProjectDocument::findPlacementUsages(
    const ContentDefinitionKey& key) const {
    std::vector<ProjectPlacementUsage> result;
    for (const auto& map : maps_) for (const auto& usage : editor::findPlacementUsages(map, key))
        result.push_back({map.data().id, usage});
    return result;
}

} // namespace underworld::editor
