#include "game/maps/authored_world.h"

#include "engine/data/json.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <sstream>
#include <unordered_set>

namespace underworld::game::maps {
namespace {
constexpr std::uint64_t maximumAuthoredWorldFileBytes = 256ULL * 1024ULL * 1024ULL;
using engine::data::JsonArray;
using engine::data::JsonObject;
using engine::data::JsonValue;

JsonValue stringValue(std::string value) { return {{}, std::move(value)}; }
JsonValue numberValue(unsigned value) { return {{}, engine::data::JsonNumber{std::to_string(value)}}; }
JsonValue arrayValue(JsonArray value) { return {{}, std::move(value)}; }
void put(JsonObject& object, std::string key, JsonValue value) {
    object.emplace_back(std::move(key), std::move(value));
}

const JsonValue* field(const JsonObject& object, std::string_view name) {
    for (const auto& [key, value] : object) if (key == name) return &value;
    return nullptr;
}

AuthoredWorldDiagnostic diagnostic(AuthoredWorldDiagnosticStage stage, std::string code,
                                   std::string message, std::string path = {}) {
    AuthoredWorldDiagnostic result;
    result.stage = stage; result.code = std::move(code); result.message = std::move(message);
    result.path = std::move(path); return result;
}

const JsonObject* object(const JsonValue& value) {
    return std::get_if<JsonObject>(&value.value);
}

bool readString(const JsonValue* value, std::string& output) {
    if (value == nullptr) return false;
    const auto* text = std::get_if<std::string>(&value->value);
    if (text == nullptr) return false;
    output = *text;
    return true;
}

bool readVersion(const JsonValue* value, unsigned& output) {
    if (value == nullptr) return false;
    const auto* number = std::get_if<engine::data::JsonNumber>(&value->value);
    if (number == nullptr) return false;
    try {
        std::size_t consumed = 0;
        const auto parsed = std::stoul(number->lexeme, &consumed);
        if (consumed != number->lexeme.size() || parsed > 0xffffffffUL) return false;
        output = static_cast<unsigned>(parsed);
        return true;
    } catch (...) { return false; }
}

void checkRequired(const JsonObject& root, std::string_view name,
                   std::vector<AuthoredWorldDiagnostic>& diagnostics) {
    if (field(root, name) == nullptr) diagnostics.push_back(diagnostic(
        AuthoredWorldDiagnosticStage::decode, "missing_field",
        "missing required field " + std::string(name), std::string(name)));
}

void validateCrossMapReferences(const AuthoredWorldSource& source,
                                std::vector<AuthoredWorldDiagnostic>& diagnostics) {
    std::unordered_set<std::string> ids;
    for (std::size_t index = 0; index < source.maps.size(); ++index) {
        const auto& map = source.maps[index];
        const auto id = std::string(map.geometry.id.value());
        if (id.empty()) {
            diagnostics.push_back(diagnostic(AuthoredWorldDiagnosticStage::validation,
                "empty_map_id", "map id must not be empty", "maps[" + std::to_string(index) + "].id"));
        } else if (!ids.emplace(id).second) {
            diagnostics.push_back(diagnostic(AuthoredWorldDiagnosticStage::validation,
                "duplicate_map_id", "duplicate map id " + id,
                "maps[" + std::to_string(index) + "].id"));
        }
    }
    const auto entry = std::string(source.entryMapId.value());
    if (source.maps.empty()) {
        diagnostics.push_back(diagnostic(AuthoredWorldDiagnosticStage::validation,
            "empty_world", "world project must contain at least one map", "maps"));
    } else if (entry.empty() || !ids.contains(entry)) {
        diagnostics.push_back(diagnostic(AuthoredWorldDiagnosticStage::validation,
            "missing_entry_map", "entryMapId does not reference an existing map", "entryMapId"));
    }
    for (std::size_t mapIndex = 0; mapIndex < source.maps.size(); ++mapIndex) {
        const auto& map = source.maps[mapIndex];
        for (std::size_t linkIndex = 0; linkIndex < map.geometry.links.size(); ++linkIndex) {
            const auto& link = map.geometry.links[linkIndex];
            const auto targetMap = std::string(link.targetMapId.value());
            const auto targetSpawn = std::string(link.targetSpawnId.value());
            const auto target = std::find_if(source.maps.begin(), source.maps.end(),
                [&](const auto& candidate) { return candidate.geometry.id.value() == link.targetMapId.value(); });
            const std::string path = "maps[" + std::to_string(mapIndex) + "].links[" +
                                     std::to_string(linkIndex) + "]";
            if (target == source.maps.end()) {
                auto issue = diagnostic(AuthoredWorldDiagnosticStage::validation,
                    "missing_target_map", std::string(map.geometry.id.value()) + ": link " + link.id +
                    " targets missing map " + targetMap, path);
                issue.mapId = std::string(map.geometry.id.value()); issue.linkId = link.id;
                issue.targetMapId = targetMap; issue.targetSpawnId = targetSpawn;
                diagnostics.push_back(std::move(issue));
                continue;
            }
            const bool spawnExists = std::any_of(target->geometry.playerSpawns.begin(),
                target->geometry.playerSpawns.end(), [&](const auto& spawn) {
                    return spawn.id.value() == link.targetSpawnId.value();
                });
            if (!spawnExists) {
                auto issue = diagnostic(AuthoredWorldDiagnosticStage::validation,
                    "missing_target_spawn", std::string(map.geometry.id.value()) + ": link " + link.id +
                    " targets missing spawn " + targetSpawn + " in " + targetMap, path);
                issue.mapId = std::string(map.geometry.id.value()); issue.linkId = link.id;
                issue.targetMapId = targetMap; issue.targetSpawnId = targetSpawn;
                diagnostics.push_back(std::move(issue));
            }
        }
    }
}
} // namespace

AuthoredWorldDecodeResult decodeAuthoredWorldJson(std::string_view json) {
    AuthoredWorldDecodeResult result;
    const auto parsed = engine::data::parseJson(json);
    for (const auto& issue : parsed.diagnostics) result.diagnostics.push_back({
        AuthoredWorldDiagnosticStage::decode, "invalid_json", issue.message, {}, {}, {}, {}, {},
        issue.location.line, issue.location.column});
    if (!parsed.value) return result;
    const auto* root = object(*parsed.value);
    if (!root) { result.diagnostics.push_back(diagnostic(AuthoredWorldDiagnosticStage::decode,
        "root_not_object", "world project root must be an object")); return result; }
    const std::unordered_set<std::string> allowed{"format", "version", "entryMapId", "maps"};
    for (const auto& [key, unused] : *root) if (!allowed.contains(key)) result.diagnostics.push_back(
        diagnostic(AuthoredWorldDiagnosticStage::decode, "unknown_field", "unknown field " + key, key));
    for (const auto name : {"format", "version", "entryMapId", "maps"}) checkRequired(*root, name, result.diagnostics);
    std::string format;
    unsigned version = 0;
    AuthoredWorldSource source;
    if (!readString(field(*root, "format"), format) || format != "dungeon-underworld-world-project")
        result.diagnostics.push_back(diagnostic(AuthoredWorldDiagnosticStage::decode,
            "invalid_format", "invalid world project format", "format"));
    if (!readVersion(field(*root, "version"), version) || version != authoredWorldVersion)
        result.diagnostics.push_back(diagnostic(AuthoredWorldDiagnosticStage::decode,
            "invalid_version", "unsupported world project version", "version"));
    std::string entry;
    if (!readString(field(*root, "entryMapId"), entry) || entry.empty())
        result.diagnostics.push_back(diagnostic(AuthoredWorldDiagnosticStage::decode,
            "invalid_entry_map", "entryMapId must be a non-empty string", "entryMapId"));
    // Keep malformed input diagnostic-only.  MapId rejects empty values, so do
    // not construct it until the required string has passed local decoding.
    if (!entry.empty()) source.entryMapId = simulation::MapId{entry};
    const auto* maps = field(*root, "maps");
    const auto* array = maps ? std::get_if<JsonArray>(&maps->value) : nullptr;
    if (!array) result.diagnostics.push_back(diagnostic(AuthoredWorldDiagnosticStage::decode,
        "invalid_maps", "maps must be an array", "maps"));
    if (array) for (std::size_t index = 0; index < array->size(); ++index) {
        const auto* mapObject = object((*array)[index]);
        if (!mapObject) { result.diagnostics.push_back(diagnostic(AuthoredWorldDiagnosticStage::decode,
            "invalid_map", "maps entries must be objects", "maps[" + std::to_string(index) + "]")); continue; }
        const auto encoded = engine::data::writeJson((*array)[index], false);
        auto decoded = decodeAuthoredMapJson(encoded);
        for (auto& issue : decoded.diagnostics) result.diagnostics.push_back({
            AuthoredWorldDiagnosticStage::decode, issue.code, issue.message,
            "maps[" + std::to_string(index) + "]." + issue.path, {}, {}, {}, {}, issue.line, issue.column});
        if (decoded.source) source.maps.push_back(std::move(*decoded.source));
    }
    if (result.diagnostics.empty()) {
        validateCrossMapReferences(source, result.diagnostics);
        if (result.diagnostics.empty()) result.source = std::move(source);
    }
    return result;
}

std::string encodeAuthoredWorldJson(const AuthoredWorldSource& source) {
    JsonObject root;
    put(root, "format", stringValue("dungeon-underworld-world-project"));
    put(root, "version", numberValue(authoredWorldVersion));
    put(root, "entryMapId", stringValue(std::string(source.entryMapId.value())));
    JsonArray maps;
    for (const auto& map : source.maps) {
        // Editor property overrides originate in a hash map. Canonicalize that
        // one collection at the world boundary while preserving authored order
        // for geometry, links, regions and encounters.
        auto canonicalMap = map;
        std::stable_sort(canonicalMap.placementOverrides.begin(),
                         canonicalMap.placementOverrides.end(),
                         [](const auto& left, const auto& right) {
            if (left.instanceId.value != right.instanceId.value) {
                return left.instanceId.value < right.instanceId.value;
            }
            return left.propertyId < right.propertyId;
        });
        const auto parsed = engine::data::parseJson(encodeAuthoredMapJson(canonicalMap));
        if (!parsed.value) throw std::invalid_argument("cannot encode authored map in world project");
        maps.push_back(*parsed.value);
    }
    put(root, "maps", arrayValue(std::move(maps)));
    return engine::data::writeJson(JsonValue{{}, std::move(root)});
}

AuthoredWorldDecodeResult readAuthoredWorldFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {{}, {diagnostic(AuthoredWorldDiagnosticStage::io, "open_failed",
        "could not open authored world project", path.generic_string())}};
    input.seekg(0, std::ios::end);
    const auto end = input.tellg();
    if (end < 0 || static_cast<std::uint64_t>(end) > maximumAuthoredWorldFileBytes) {
        return {{}, {diagnostic(AuthoredWorldDiagnosticStage::io, "file_too_large",
            "authored world project exceeds the size limit", path.generic_string())}};
    }
    input.seekg(0, std::ios::beg);
    std::string text(static_cast<std::size_t>(end), '\0');
    if (!text.empty()) input.read(text.data(), static_cast<std::streamsize>(text.size()));
    if (!input && !text.empty()) return {{}, {diagnostic(AuthoredWorldDiagnosticStage::io,
        "read_failed", "could not read authored world project", path.generic_string())}};
    auto result = decodeAuthoredWorldJson(text);
    for (auto& issue : result.diagnostics) issue.path = path.generic_string() +
        (issue.path.empty() ? "" : ":" + issue.path);
    return result;
}

WorldValidationResult validateAuthoredWorld(const AuthoredWorldSource& source) {
    WorldValidationResult result;
    validateCrossMapReferences(source, result.diagnostics);
    for (std::size_t index = 0; index < source.maps.size(); ++index) {
        if (const auto issue = validateAuthoredMapSource(source.maps[index])) {
            auto worldIssue = diagnostic(AuthoredWorldDiagnosticStage::validation, issue->code,
                issue->message, "maps[" + std::to_string(index) + "]." + issue->path);
            worldIssue.mapId = std::string(source.maps[index].geometry.id.value());
            result.diagnostics.push_back(std::move(worldIssue));
        }
    }
    return result;
}

WorldCompileResult compileAuthoredWorld(const AuthoredWorldSource& source,
                                        const game::GameContentRegistry& content) {
    WorldCompileResult result;
    const auto structural = validateAuthoredWorld(source);
    result.diagnostics = structural.diagnostics;
    if (!result.diagnostics.empty()) return result;
    for (const auto& authored : source.maps) {
        const auto compiled = compileAuthoredMap(authored, content);
        for (const auto& issue : compiled.diagnostics) {
            auto worldIssue = diagnostic(AuthoredWorldDiagnosticStage::compile, issue.code,
                issue.message, std::string(authored.geometry.id.value()) + ":" + issue.path);
            worldIssue.mapId = std::string(authored.geometry.id.value());
            result.diagnostics.push_back(std::move(worldIssue));
        }
        if (compiled.map) result.maps.push_back({authored.geometry.id, *compiled.map});
    }
    if (!result.diagnostics.empty()) result.maps.clear();
    return result;
}

bool writeAuthoredWorldFile(const std::filesystem::path& path,
                            const AuthoredWorldSource& source, std::string& error) {
    try {
        const auto validation = validateAuthoredWorld(source);
        if (!validation.valid()) { error = validation.diagnostics.front().message; return false; }
        const auto text = encodeAuthoredWorldJson(source);
        auto temporary = path; temporary += ".tmp";
        { std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
          if (!output) { error = "could not open temporary authored world"; return false; }
          output.write(text.data(), static_cast<std::streamsize>(text.size())); output.flush();
          if (!output) { std::error_code ignored; std::filesystem::remove(temporary, ignored);
              error = "could not write temporary authored world"; return false; } }
        auto backup = path; backup += ".bak"; std::error_code fsError;
        std::filesystem::remove(backup, fsError); fsError.clear();
        if (std::filesystem::exists(path, fsError)) { fsError.clear(); std::filesystem::rename(path, backup, fsError);
            if (fsError) { std::filesystem::remove(temporary, fsError); error = "could not back up authored world"; return false; } }
        fsError.clear(); std::filesystem::rename(temporary, path, fsError);
        if (fsError) { std::error_code restore; if (std::filesystem::exists(backup, restore)) std::filesystem::rename(backup, path, restore);
            std::filesystem::remove(temporary, restore); error = "could not replace authored world"; return false; }
        error.clear(); return true;
    } catch (const std::exception& exception) { error = exception.what(); return false; }
}

} // namespace underworld::game::maps
