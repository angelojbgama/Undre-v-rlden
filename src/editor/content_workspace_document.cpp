#include "editor/content_workspace_document.h"

#include "game/content/builtin_content.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <sstream>
#include <system_error>

namespace underworld::editor {
namespace {

using game::content::AuthoredContentPack;
using game::content::ContentKind;

constexpr std::array<ContentDefinitionKind, 25> categories{
    ContentDefinitionKind::tileset, ContentDefinitionKind::projectile,
    ContentDefinitionKind::attack, ContentDefinitionKind::behavior,
    ContentDefinitionKind::enemy, ContentDefinitionKind::item,
    ContentDefinitionKind::object, ContentDefinitionKind::pickup,
    ContentDefinitionKind::npc, ContentDefinitionKind::npcVisual,
    ContentDefinitionKind::dialogue, ContentDefinitionKind::quest,
    ContentDefinitionKind::authoringDescriptor, ContentDefinitionKind::tileSemantic,
    ContentDefinitionKind::stamp, ContentDefinitionKind::playerProgression,
    ContentDefinitionKind::rewardProfile, ContentDefinitionKind::rewardGrant,
    ContentDefinitionKind::shop, ContentDefinitionKind::presentationEffect,
    ContentDefinitionKind::visualImage, ContentDefinitionKind::staticSprite,
    ContentDefinitionKind::animation, ContentDefinitionKind::enemyVisual,
    ContentDefinitionKind::objectVisual};

std::filesystem::path absoluteNormal(const std::filesystem::path& path) {
    std::error_code error;
    const auto absolute = std::filesystem::absolute(path, error);
    return (error ? path : absolute).lexically_normal();
}

bool safeRelativePath(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute() || !path.root_name().empty()) return false;
    const auto text = path.generic_string();
    if (text.empty() || text.find('\0') != std::string::npos) return false;
    if (text.size() >= 2 && std::isalpha(static_cast<unsigned char>(text[0])) && text[1] == ':') return false;
    for (const auto& part : path) if (part == "..") return false;
    return true;
}

bool inside(const std::filesystem::path& root, const std::filesystem::path& candidate) {
    std::error_code error;
    const auto relative = std::filesystem::relative(candidate, root, error);
    if (error || relative.empty()) return !error && candidate == root;
    for (const auto& component : relative) if (component == "..") return false;
    return true;
}

bool containsNoSymlink(const std::filesystem::path& root,
                       const std::filesystem::path& candidate) {
    std::error_code error;
    const auto relative = std::filesystem::relative(candidate, root, error);
    if (error) return false;
    auto current = root;
    for (const auto& component : relative) {
        current /= component;
        const auto status = std::filesystem::symlink_status(current, error);
        if (error == std::errc::no_such_file_or_directory) {
            error.clear();
            break;
        }
        if (error || std::filesystem::is_symlink(status)) return false;
    }
    return true;
}

std::optional<ContentKind> runtimeKind(ContentDefinitionKind kind) noexcept {
    switch (kind) {
    case ContentDefinitionKind::tileset: return ContentKind::tileset;
    case ContentDefinitionKind::projectile: return ContentKind::projectile;
    case ContentDefinitionKind::attack: return ContentKind::attack;
    case ContentDefinitionKind::behavior: return ContentKind::behavior;
    case ContentDefinitionKind::enemy: return ContentKind::enemy;
    case ContentDefinitionKind::item: return ContentKind::item;
    case ContentDefinitionKind::object: return ContentKind::object;
    case ContentDefinitionKind::pickup: return ContentKind::pickup;
    case ContentDefinitionKind::npc: return ContentKind::npc;
    case ContentDefinitionKind::npcVisual: return ContentKind::npcVisual;
    case ContentDefinitionKind::dialogue: return ContentKind::dialogue;
    case ContentDefinitionKind::quest: return ContentKind::quest;
    case ContentDefinitionKind::authoringDescriptor: return ContentKind::authoringDescriptor;
    case ContentDefinitionKind::tileSemantic: return ContentKind::tileSemantic;
    case ContentDefinitionKind::stamp: return ContentKind::stamp;
    case ContentDefinitionKind::playerProgression: return ContentKind::playerProgression;
    case ContentDefinitionKind::rewardProfile: return ContentKind::rewardProfile;
    case ContentDefinitionKind::rewardGrant: return ContentKind::rewardGrant;
    case ContentDefinitionKind::shop: return ContentKind::shop;
    case ContentDefinitionKind::presentationEffect: return ContentKind::presentationEffect;
    case ContentDefinitionKind::visualImage: return ContentKind::visualImage;
    case ContentDefinitionKind::staticSprite: return ContentKind::staticSprite;
    case ContentDefinitionKind::animation: return ContentKind::animation;
    case ContentDefinitionKind::enemyVisual: return ContentKind::enemyVisual;
    case ContentDefinitionKind::objectVisual: return ContentKind::objectVisual;
    }
    return std::nullopt;
}

template<class T>
bool containsId(const std::vector<T>& values, const simulation::DefinitionId& id) {
    return std::any_of(values.begin(), values.end(), [&](const auto& value) { return value.id == id; });
}

template<class T>
T* findId(std::vector<T>& values, const simulation::DefinitionId& id) {
    const auto it = std::find_if(values.begin(), values.end(),
        [&](auto& value) { return value.id == id; });
    return it == values.end() ? nullptr : &*it;
}

template<class T>
const T* findId(const std::vector<T>& values, const simulation::DefinitionId& id) {
    const auto it = std::find_if(values.begin(), values.end(),
        [&](const auto& value) { return value.id == id; });
    return it == values.end() ? nullptr : &*it;
}

} // namespace

const char* ContentWorkspaceDocument::categoryName(ContentDefinitionKind kind) noexcept {
    switch (kind) {
    case ContentDefinitionKind::tileset: return "tilesets";
    case ContentDefinitionKind::projectile: return "projectiles";
    case ContentDefinitionKind::attack: return "attacks";
    case ContentDefinitionKind::behavior: return "behaviors";
    case ContentDefinitionKind::enemy: return "enemies";
    case ContentDefinitionKind::item: return "items";
    case ContentDefinitionKind::object: return "objects";
    case ContentDefinitionKind::pickup: return "pickups";
    case ContentDefinitionKind::npc: return "npcs";
    case ContentDefinitionKind::npcVisual: return "npcVisuals";
    case ContentDefinitionKind::dialogue: return "dialogues";
    case ContentDefinitionKind::quest: return "quests";
    case ContentDefinitionKind::authoringDescriptor: return "authoringDescriptors";
    case ContentDefinitionKind::tileSemantic: return "tileSemantics";
    case ContentDefinitionKind::stamp: return "stamps";
    case ContentDefinitionKind::playerProgression: return "playerProgressions";
    case ContentDefinitionKind::rewardProfile: return "rewardProfiles";
    case ContentDefinitionKind::rewardGrant: return "rewardGrants";
    case ContentDefinitionKind::shop: return "shops";
    case ContentDefinitionKind::presentationEffect: return "presentationEffects";
    case ContentDefinitionKind::visualImage: return "visualImages";
    case ContentDefinitionKind::staticSprite: return "staticSprites";
    case ContentDefinitionKind::animation: return "animations";
    case ContentDefinitionKind::enemyVisual: return "enemyVisuals";
    case ContentDefinitionKind::objectVisual: return "objectVisuals";
    }
    return "";
}

const std::vector<ContentDefinitionKind>& ContentWorkspaceDocument::categoryOrder() noexcept {
    static const std::vector<ContentDefinitionKind> result(categories.begin(), categories.end());
    return result;
}

std::optional<ContentDefinitionKind> ContentWorkspaceDocument::categoryFromName(
    std::string_view name) noexcept {
    for (const auto kind : categories) if (name == categoryName(kind)) return kind;
    return std::nullopt;
}

std::optional<ContentWorkspaceDocument> ContentWorkspaceDocument::open(
    const std::filesystem::path& root, std::string& error) {
    ContentWorkspaceDocument document;
    document.root_ = absoluteNormal(root);
    std::error_code statusError;
    const auto status = std::filesystem::symlink_status(document.root_, statusError);
    if (statusError || !std::filesystem::is_directory(status) || std::filesystem::is_symlink(status)) {
        error = "content workspace root is not an accessible directory";
        return std::nullopt;
    }
    document.writable_ = true;
    const auto discovered = game::content::discoverContentWorkspaceFiles(document.root_);
    if (!discovered.files) {
        error = "content workspace discovery failed";
        return std::nullopt;
    }
    for (const auto& path : *discovered.files) {
        const auto parsed = game::content::readAuthoredContentJsonFile(path);
        if (!parsed.content) {
            error = "content workspace contains structurally invalid JSON: " + path.string();
            if (!parsed.diagnostics.empty()) {
                const auto& diagnostic = parsed.diagnostics.front();
                error += " [" + std::to_string(diagnostic.line) + ":" +
                         std::to_string(diagnostic.column) + "]";
                if (!diagnostic.path.empty()) error += " " + diagnostic.path;
                error += ": " + diagnostic.message;
            }
            return std::nullopt;
        }
        document.files_.push_back({absoluteNormal(path), std::move(*parsed.content), false});
        document.origins_.push_back(std::move(parsed.origins));
    }
    if (!document.rebuild(&error)) return std::nullopt;
    error.clear();
    return document;
}

ContentWorkspaceDocument ContentWorkspaceDocument::fromBuiltin(AuthoredContentPack authored) {
    ContentWorkspaceDocument document;
    document.root_ = "<builtin>";
    document.writable_ = false;
    document.files_.push_back({"<builtin>", std::move(authored), false});
    const auto encoded = game::content::encodeAuthoredContentJson(document.files_.front().authored);
    const auto parsed = game::content::decodeAuthoredContentJson(encoded);
    document.origins_.push_back(parsed.origins);
    (void)document.rebuild();
    return document;
}

bool ContentWorkspaceDocument::dirty() const noexcept {
    return std::any_of(files_.begin(), files_.end(), [](const auto& file) { return file.dirty; });
}

std::vector<ContentDefinitionKey> ContentWorkspaceDocument::index() const {
    std::vector<ContentDefinitionKey> result;
    result.reserve(sources_.definitions.size());
    for (const auto& source : sources_.definitions) {
        const auto kind = categoryFromName(source.category);
        if (kind) result.push_back({*kind, source.definitionId});
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        if (left.kind != right.kind) return static_cast<int>(left.kind) < static_cast<int>(right.kind);
        return left.id.value() < right.id.value();
    });
    return result;
}

const game::content::ContentSourceLocation* ContentWorkspaceDocument::sourceFor(
    const ContentDefinitionKey& key) const noexcept {
    const auto kind = runtimeKind(key.kind);
    return kind ? sources_.find(game::content::contentCategoryName(*kind), key.id) : nullptr;
}

std::filesystem::path ContentWorkspaceDocument::resolveDocumentPath(
    const std::filesystem::path& path) const {
    if (path.is_absolute()) return absoluteNormal(path);
    return (root_ / path).lexically_normal();
}

ContentFileDocument* ContentWorkspaceDocument::findFile(const std::filesystem::path& path) noexcept {
    const auto resolved = resolveDocumentPath(path);
    const auto it = std::find_if(files_.begin(), files_.end(), [&](auto& file) {
        return file.path.lexically_normal() == resolved;
    });
    return it == files_.end() ? nullptr : &*it;
}

const ContentFileDocument* ContentWorkspaceDocument::findFile(
    const std::filesystem::path& path) const noexcept {
    const auto resolved = resolveDocumentPath(path);
    const auto it = std::find_if(files_.begin(), files_.end(), [&](const auto& file) {
        return file.path.lexically_normal() == resolved;
    });
    return it == files_.end() ? nullptr : &*it;
}

ContentFileDocument* ContentWorkspaceDocument::fileForDefinition(
    ContentDefinitionKind kind, const simulation::DefinitionId& id) noexcept {
    const auto* source = sourceFor({kind, id});
    return source ? findFile(source->sourcePath) : nullptr;
}

bool ContentWorkspaceDocument::ensureWritable(std::string& error) const {
    if (!writable_) { error = "builtin content is read-only"; return false; }
    return true;
}

bool ContentWorkspaceDocument::rebuild(std::string* error) {
    std::vector<game::content::DecodedContentWorkspaceFile> decoded;
    decoded.reserve(files_.size());
    for (std::size_t i = 0; i < files_.size(); ++i) {
        decoded.push_back({files_[i].path, files_[i].authored, origins_[i]});
    }
    const auto built = game::content::buildContentWorkspace(decoded);
    mergedAuthored_ = built.mergedAuthored;
    sources_ = built.sources;
    diagnostics_ = built.diagnostics;
    if (built.workspace) compiledRegistry_ = built.workspace->registry;
    else compiledRegistry_.reset();
    if (error) {
        error->clear();
        if (diagnostics_.empty() && !compiledRegistry_) *error = "content workspace could not compile";
    }
    return true;
}

bool ContentWorkspaceDocument::createContentFile(const std::filesystem::path& relativePath,
                                                  std::string& error) {
    if (!ensureWritable(error)) return false;
    if (!safeRelativePath(relativePath) || relativePath.extension() != ".json") {
        error = "content file path must be a relative .json path inside the workspace";
        return false;
    }
    const auto path = resolveDocumentPath(relativePath);
    if (findFile(path)) { error = "content file already exists"; return false; }
    if (!inside(root_, path) || !containsNoSymlink(root_, path)) {
        error = "content file path escapes workspace root or crosses a symlink";
        return false;
    }
    std::error_code existsError;
    if (std::filesystem::exists(path, existsError)) { error = "content file already exists"; return false; }
    if (existsError) { error = "could not inspect content file path"; return false; }
    files_.push_back({path, {}, true});
    origins_.push_back({});
    ++revision_;
    (void)rebuild();
    error.clear();
    return true;
}

bool ContentWorkspaceDocument::saveAll(std::string& error) {
    if (!ensureWritable(error)) return false;
    for (auto& file : files_) {
        if (!file.dirty) continue;
        std::error_code directoryError;
        std::filesystem::create_directories(file.path.parent_path(), directoryError);
        if (directoryError) { error = "could not create content file directory"; return false; }
        if (!game::content::writeAuthoredContentJsonFile(file.path, file.authored, error)) return false;
        file.dirty = false;
    }
    error.clear();
    return true;
}

bool ContentWorkspaceDocument::validateWorkspace() {
    (void)rebuild();
    return valid();
}

bool ContentWorkspaceDocument::mutateFile(
    const std::filesystem::path& path,
    const std::function<bool(AuthoredContentPack&)>& mutation, std::string& error) {
    if (!ensureWritable(error)) return false;
    auto* file = findFile(path);
    if (!file) { error = "content source file is not open in this workspace"; return false; }
    auto candidate = file->authored;
    if (!mutation(candidate)) { error = "content mutation was rejected"; return false; }
    const auto encoded = game::content::encodeAuthoredContentJson(candidate);
    const auto parsed = game::content::decodeAuthoredContentJson(encoded);
    if (!parsed.content) { error = "content mutation produced structurally invalid data"; return false; }
    file->authored = *parsed.content;
    const auto fileIndex = static_cast<std::size_t>(file - files_.data());
    origins_[fileIndex] = parsed.origins;
    file->dirty = true;
    ++revision_;
    (void)rebuild();
    error.clear();
    return true;
}

bool ContentWorkspaceDocument::mutateDefinition(
    ContentDefinitionKind kind, const simulation::DefinitionId& id,
    const std::function<bool(AuthoredContentPack&)>& mutation, std::string& error) {
    const auto* source = sourceFor({kind, id});
    if (!source) { error = "definition is not present in this workspace"; return false; }
    return mutateFile(source->sourcePath, mutation, error);
}

bool ContentWorkspaceDocument::addDefinition(
    ContentDefinitionKind kind, const std::filesystem::path& path,
    const simulation::DefinitionId& id,
    const std::function<void(AuthoredContentPack&)>& mutation, std::string& error) {
    if (id.empty()) { error = "definition ID must not be empty"; return false; }
    auto* file = findFile(path);
    if (!file) { error = "content source file is not open in this workspace"; return false; }
    if ([&] {
        const auto* local = file;
        const auto category = categoryName(kind);
        if (std::string_view(category) == "visualImages") return containsId(local->authored.visualImages, id);
        if (std::string_view(category) == "staticSprites") return containsId(local->authored.staticSprites, id);
        if (std::string_view(category) == "animations") return containsId(local->authored.animations, id);
        return containsId(local->authored.enemyVisuals, id);
    }()) { error = "definition ID already exists"; return false; }
    return mutateFile(path, [&](AuthoredContentPack& pack) { mutation(pack); return true; }, error);
}

bool ContentWorkspaceDocument::addVisualImage(const std::filesystem::path& file,
                                              game::content::AuthoredVisualImage value,
                                              std::string& error) {
    const auto id = value.id;
    return addDefinition(ContentDefinitionKind::visualImage, file, id,
        [value = std::move(value)](AuthoredContentPack& pack) { pack.visualImages.push_back(value); }, error);
}

bool ContentWorkspaceDocument::updateVisualImage(const simulation::DefinitionId& id,
                                                 game::content::AuthoredVisualImage value,
                                                 std::string& error) {
    if (value.id != id) { error = "definition IDs are stable during editing"; return false; }
    return mutateDefinition(ContentDefinitionKind::visualImage, id,
        [value = std::move(value), id](AuthoredContentPack& pack) {
            auto* current = findId(pack.visualImages, id); if (!current) return false; *current = value; return true;
        }, error);
}

bool ContentWorkspaceDocument::removeVisualImage(const simulation::DefinitionId& id, std::string& error) {
    return removeDefinition({ContentDefinitionKind::visualImage, id}, error);
}

bool ContentWorkspaceDocument::addStaticSprite(const std::filesystem::path& file,
                                               game::content::AuthoredStaticSprite value,
                                               std::string& error) {
    const auto id = value.id;
    return addDefinition(ContentDefinitionKind::staticSprite, file, id,
        [value = std::move(value)](AuthoredContentPack& pack) { pack.staticSprites.push_back(value); }, error);
}

bool ContentWorkspaceDocument::updateStaticSprite(const simulation::DefinitionId& id,
                                                  game::content::AuthoredStaticSprite value,
                                                  std::string& error) {
    if (value.id != id) { error = "definition IDs are stable during editing"; return false; }
    return mutateDefinition(ContentDefinitionKind::staticSprite, id,
        [value = std::move(value), id](AuthoredContentPack& pack) {
            auto* current = findId(pack.staticSprites, id); if (!current) return false; *current = value; return true;
        }, error);
}

bool ContentWorkspaceDocument::removeStaticSprite(const simulation::DefinitionId& id, std::string& error) {
    return removeDefinition({ContentDefinitionKind::staticSprite, id}, error);
}

bool ContentWorkspaceDocument::addAnimation(const std::filesystem::path& file,
                                            game::content::AuthoredAnimation value,
                                            std::string& error) {
    const auto id = value.id;
    return addDefinition(ContentDefinitionKind::animation, file, id,
        [value = std::move(value)](AuthoredContentPack& pack) { pack.animations.push_back(value); }, error);
}

bool ContentWorkspaceDocument::updateAnimation(const simulation::DefinitionId& id,
                                               game::content::AuthoredAnimation value,
                                               std::string& error) {
    if (value.id != id) { error = "definition IDs are stable during editing"; return false; }
    return mutateDefinition(ContentDefinitionKind::animation, id,
        [value = std::move(value), id](AuthoredContentPack& pack) {
            auto* current = findId(pack.animations, id); if (!current) return false; *current = value; return true;
        }, error);
}

bool ContentWorkspaceDocument::removeAnimation(const simulation::DefinitionId& id, std::string& error) {
    return removeDefinition({ContentDefinitionKind::animation, id}, error);
}

bool ContentWorkspaceDocument::addAnimationFrame(const simulation::DefinitionId& id,
                                                 game::content::AuthoredAnimationFrame value,
                                                 std::string& error) {
    return mutateDefinition(ContentDefinitionKind::animation, id,
        [value = std::move(value), id](AuthoredContentPack& pack) {
            auto* animation = findId(pack.animations, id); if (!animation) return false;
            animation->frames.push_back(value); return true;
        }, error);
}

bool ContentWorkspaceDocument::updateAnimationFrame(const simulation::DefinitionId& id, std::size_t index,
                                                    game::content::AuthoredAnimationFrame value,
                                                    std::string& error) {
    return mutateDefinition(ContentDefinitionKind::animation, id,
        [value = std::move(value), id, index](AuthoredContentPack& pack) {
            auto* animation = findId(pack.animations, id);
            if (!animation || index >= animation->frames.size()) return false;
            animation->frames[index] = value; return true;
        }, error);
}

bool ContentWorkspaceDocument::removeAnimationFrame(const simulation::DefinitionId& id,
                                                    std::size_t index, std::string& error) {
    return mutateDefinition(ContentDefinitionKind::animation, id,
        [id, index](AuthoredContentPack& pack) {
            auto* animation = findId(pack.animations, id);
            if (!animation || index >= animation->frames.size()) return false;
            animation->frames.erase(animation->frames.begin() + static_cast<std::ptrdiff_t>(index)); return true;
        }, error);
}

bool ContentWorkspaceDocument::moveAnimationFrame(const simulation::DefinitionId& id,
                                                  std::size_t from, std::size_t to, std::string& error) {
    return mutateDefinition(ContentDefinitionKind::animation, id,
        [id, from, to](AuthoredContentPack& pack) {
            auto* animation = findId(pack.animations, id);
            if (!animation || from >= animation->frames.size() || to >= animation->frames.size()) return false;
            auto frame = animation->frames[from]; animation->frames.erase(animation->frames.begin() + static_cast<std::ptrdiff_t>(from));
            animation->frames.insert(animation->frames.begin() + static_cast<std::ptrdiff_t>(to), std::move(frame)); return true;
        }, error);
}

bool ContentWorkspaceDocument::addAnimationMarker(const simulation::DefinitionId& id, std::size_t frame,
                                                  std::string marker, std::string& error) {
    if (marker.empty()) { error = "animation marker must not be empty"; return false; }
    return mutateDefinition(ContentDefinitionKind::animation, id,
        [id, frame, marker = std::move(marker)](AuthoredContentPack& pack) {
            auto* animation = findId(pack.animations, id);
            if (!animation || frame >= animation->frames.size()) return false;
            animation->frames[frame].markers.push_back(marker); return true;
        }, error);
}

bool ContentWorkspaceDocument::updateAnimationMarker(const simulation::DefinitionId& id, std::size_t frame,
                                                     std::size_t marker, std::string value,
                                                     std::string& error) {
    if (value.empty()) { error = "animation marker must not be empty"; return false; }
    return mutateDefinition(ContentDefinitionKind::animation, id,
        [id, frame, marker, value = std::move(value)](AuthoredContentPack& pack) {
            auto* animation = findId(pack.animations, id);
            if (!animation || frame >= animation->frames.size() || marker >= animation->frames[frame].markers.size()) return false;
            animation->frames[frame].markers[marker] = value; return true;
        }, error);
}

bool ContentWorkspaceDocument::removeAnimationMarker(const simulation::DefinitionId& id, std::size_t frame,
                                                     std::size_t marker, std::string& error) {
    return mutateDefinition(ContentDefinitionKind::animation, id,
        [id, frame, marker](AuthoredContentPack& pack) {
            auto* animation = findId(pack.animations, id);
            if (!animation || frame >= animation->frames.size() || marker >= animation->frames[frame].markers.size()) return false;
            animation->frames[frame].markers.erase(animation->frames[frame].markers.begin() + static_cast<std::ptrdiff_t>(marker)); return true;
        }, error);
}

bool ContentWorkspaceDocument::addEnemyVisual(const std::filesystem::path& file,
                                              game::content::AuthoredEnemyVisual value,
                                              std::string& error) {
    const auto id = value.id;
    return addDefinition(ContentDefinitionKind::enemyVisual, file, id,
        [value = std::move(value)](AuthoredContentPack& pack) { pack.enemyVisuals.push_back(value); }, error);
}

bool ContentWorkspaceDocument::updateEnemyVisual(const simulation::DefinitionId& id,
                                                 game::content::AuthoredEnemyVisual value,
                                                 std::string& error) {
    if (value.id != id) { error = "definition IDs are stable during editing"; return false; }
    return mutateDefinition(ContentDefinitionKind::enemyVisual, id,
        [value = std::move(value), id](AuthoredContentPack& pack) {
            auto* current = findId(pack.enemyVisuals, id); if (!current) return false; *current = value; return true;
        }, error);
}

bool ContentWorkspaceDocument::removeEnemyVisual(const simulation::DefinitionId& id, std::string& error) {
    return removeDefinition({ContentDefinitionKind::enemyVisual, id}, error);
}

bool ContentWorkspaceDocument::addEnemyVisualAction(const simulation::DefinitionId& id,
                                                    game::content::AuthoredEnemyAttackVisual value,
                                                    std::string& error) {
    if (value.visualActionId.empty()) { error = "visual action ID must not be empty"; return false; }
    return mutateDefinition(ContentDefinitionKind::enemyVisual, id,
        [id, value = std::move(value)](AuthoredContentPack& pack) {
            auto* visual = findId(pack.enemyVisuals, id); if (!visual) return false;
            if (std::any_of(visual->attacks.begin(), visual->attacks.end(), [&](const auto& x) { return x.visualActionId == value.visualActionId; })) return false;
            visual->attacks.push_back(value); return true;
        }, error);
}

bool ContentWorkspaceDocument::removeEnemyVisualAction(const simulation::DefinitionId& id,
                                                       const simulation::DefinitionId& actionId,
                                                       std::string& error) {
    return mutateDefinition(ContentDefinitionKind::enemyVisual, id,
        [id, actionId](AuthoredContentPack& pack) {
            auto* visual = findId(pack.enemyVisuals, id); if (!visual) return false;
            const auto it = std::find_if(visual->attacks.begin(), visual->attacks.end(), [&](const auto& x) { return x.visualActionId == actionId; });
            if (it == visual->attacks.end()) return false;
            visual->attacks.erase(it);
            return true;
        }, error);
}

bool ContentWorkspaceDocument::removeDefinition(const ContentDefinitionKey& key, std::string& error) {
    return mutateDefinition(key.kind, key.id,
        [key](AuthoredContentPack& pack) {
            auto erase = [&](auto& values) {
                const auto it = std::find_if(values.begin(), values.end(), [&](const auto& value) { return value.id == key.id; });
                if (it == values.end()) return false;
                values.erase(it);
                return true;
            };
            switch (key.kind) {
            case ContentDefinitionKind::visualImage: return erase(pack.visualImages);
            case ContentDefinitionKind::staticSprite: return erase(pack.staticSprites);
            case ContentDefinitionKind::animation: return erase(pack.animations);
            case ContentDefinitionKind::enemyVisual: return erase(pack.enemyVisuals);
            default: return false;
            }
        }, error);
}

} // namespace underworld::editor
