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

const game::content::AuthoredVisualImage* ContentWorkspaceDocument::visualImage(
    const simulation::DefinitionId& id) const noexcept {
    return mergedAuthored_ ? findId(mergedAuthored_->visualImages, id) : nullptr;
}

const game::content::AuthoredStaticSprite* ContentWorkspaceDocument::staticSprite(
    const simulation::DefinitionId& id) const noexcept {
    return mergedAuthored_ ? findId(mergedAuthored_->staticSprites, id) : nullptr;
}

const game::content::AuthoredAnimation* ContentWorkspaceDocument::animation(
    const simulation::DefinitionId& id) const noexcept {
    return mergedAuthored_ ? findId(mergedAuthored_->animations, id) : nullptr;
}

const game::content::AuthoredEnemyVisual* ContentWorkspaceDocument::enemyVisual(
    const simulation::DefinitionId& id) const noexcept {
    return mergedAuthored_ ? findId(mergedAuthored_->enemyVisuals, id) : nullptr;
}

const game::content::AuthoredWorldObjectVisual* ContentWorkspaceDocument::objectVisual(
    const simulation::DefinitionId& id) const noexcept {
    return mergedAuthored_ ? findId(mergedAuthored_->objectVisuals, id) : nullptr;
}

const game::content::AuthoredNpcVisualSet* ContentWorkspaceDocument::npcVisual(
    const simulation::DefinitionId& id) const noexcept {
    return mergedAuthored_ ? findId(mergedAuthored_->npcVisuals, id) : nullptr;
}

const game::content::AuthoredTileset* ContentWorkspaceDocument::tileset(
    const simulation::DefinitionId& id) const noexcept {
    return mergedAuthored_ ? findId(mergedAuthored_->tilesets, id) : nullptr;
}

const game::content::AuthoringDescriptor* ContentWorkspaceDocument::authoringDescriptor(
    const simulation::DefinitionId& id) const noexcept {
    if (!mergedAuthored_) return nullptr;
    const auto it = std::find_if(mergedAuthored_->authoringDescriptors.begin(),
                                 mergedAuthored_->authoringDescriptors.end(),
                                 [&](const auto& value) { return value.definitionId == id; });
    return it == mergedAuthored_->authoringDescriptors.end() ? nullptr : &*it;
}

const game::content::AuthoredTileSemantic* ContentWorkspaceDocument::tileSemantic(
    const simulation::DefinitionId& id) const noexcept {
    return mergedAuthored_ ? findId(mergedAuthored_->tileSemantics, id) : nullptr;
}

const game::content::AuthoredStamp* ContentWorkspaceDocument::stamp(
    const simulation::DefinitionId& id) const noexcept {
    return mergedAuthored_ ? findId(mergedAuthored_->stamps, id) : nullptr;
}

#define UNDERWORLD_CONTENT_GETTER(name, member, type) \
const game::content::type* ContentWorkspaceDocument::name( \
    const simulation::DefinitionId& id) const noexcept { \
    return mergedAuthored_ ? findId(mergedAuthored_->member, id) : nullptr; \
}

UNDERWORLD_CONTENT_GETTER(projectile, projectiles, AuthoredProjectile)
UNDERWORLD_CONTENT_GETTER(attack, attacks, AuthoredAttack)
UNDERWORLD_CONTENT_GETTER(behavior, behaviors, AuthoredBehaviorProfile)
UNDERWORLD_CONTENT_GETTER(enemy, enemies, AuthoredEnemy)
UNDERWORLD_CONTENT_GETTER(item, items, AuthoredItem)
UNDERWORLD_CONTENT_GETTER(object, objects, AuthoredWorldObject)
UNDERWORLD_CONTENT_GETTER(pickup, pickups, AuthoredPickup)
UNDERWORLD_CONTENT_GETTER(npc, npcs, AuthoredNpc)
UNDERWORLD_CONTENT_GETTER(dialogue, dialogues, AuthoredDialogue)
UNDERWORLD_CONTENT_GETTER(quest, quests, AuthoredQuest)
UNDERWORLD_CONTENT_GETTER(playerProgression, playerProgressions, AuthoredPlayerProgression)
UNDERWORLD_CONTENT_GETTER(rewardProfile, rewardProfiles, AuthoredRewardProfile)
UNDERWORLD_CONTENT_GETTER(rewardGrant, rewardGrants, AuthoredRewardGrant)
UNDERWORLD_CONTENT_GETTER(shop, shops, AuthoredShop)
UNDERWORLD_CONTENT_GETTER(presentationEffect, presentationEffects, AuthoredPresentationEffect)

#undef UNDERWORLD_CONTENT_GETTER

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
        if (std::string_view(category) == "tilesets") return containsId(local->authored.tilesets, id);
        if (std::string_view(category) == "projectiles") return containsId(local->authored.projectiles, id);
        if (std::string_view(category) == "attacks") return containsId(local->authored.attacks, id);
        if (std::string_view(category) == "behaviors") return containsId(local->authored.behaviors, id);
        if (std::string_view(category) == "enemies") return containsId(local->authored.enemies, id);
        if (std::string_view(category) == "items") return containsId(local->authored.items, id);
        if (std::string_view(category) == "objects") return containsId(local->authored.objects, id);
        if (std::string_view(category) == "pickups") return containsId(local->authored.pickups, id);
        if (std::string_view(category) == "npcs") return containsId(local->authored.npcs, id);
        if (std::string_view(category) == "npcVisuals") return containsId(local->authored.npcVisuals, id);
        if (std::string_view(category) == "dialogues") return containsId(local->authored.dialogues, id);
        if (std::string_view(category) == "quests") return containsId(local->authored.quests, id);
        if (std::string_view(category) == "authoringDescriptors") {
            return std::any_of(local->authored.authoringDescriptors.begin(),
                               local->authored.authoringDescriptors.end(),
                               [&](const auto& value) { return value.definitionId == id; });
        }
        if (std::string_view(category) == "tileSemantics") return containsId(local->authored.tileSemantics, id);
        if (std::string_view(category) == "stamps") return containsId(local->authored.stamps, id);
        if (std::string_view(category) == "playerProgressions") return containsId(local->authored.playerProgressions, id);
        if (std::string_view(category) == "rewardProfiles") return containsId(local->authored.rewardProfiles, id);
        if (std::string_view(category) == "rewardGrants") return containsId(local->authored.rewardGrants, id);
        if (std::string_view(category) == "shops") return containsId(local->authored.shops, id);
        if (std::string_view(category) == "presentationEffects") return containsId(local->authored.presentationEffects, id);
        if (std::string_view(category) == "visualImages") return containsId(local->authored.visualImages, id);
        if (std::string_view(category) == "staticSprites") return containsId(local->authored.staticSprites, id);
        if (std::string_view(category) == "animations") return containsId(local->authored.animations, id);
        if (std::string_view(category) == "enemyVisuals") return containsId(local->authored.enemyVisuals, id);
        if (std::string_view(category) == "objectVisuals") return containsId(local->authored.objectVisuals, id);
        return false;
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

bool ContentWorkspaceDocument::addObjectVisual(
    const std::filesystem::path& file, game::content::AuthoredWorldObjectVisual value,
    std::string& error) {
    const auto id = value.id;
    return addDefinition(ContentDefinitionKind::objectVisual, file, id,
        [value = std::move(value)](AuthoredContentPack& pack) {
            pack.objectVisuals.push_back(value);
        }, error);
}

bool ContentWorkspaceDocument::updateObjectVisual(
    const simulation::DefinitionId& id, game::content::AuthoredWorldObjectVisual value,
    std::string& error) {
    if (value.id != id) { error = "definition IDs are stable during editing"; return false; }
    return mutateDefinition(ContentDefinitionKind::objectVisual, id,
        [value = std::move(value), id](AuthoredContentPack& pack) {
            auto* current = findId(pack.objectVisuals, id);
            if (!current) return false;
            *current = value;
            return true;
        }, error);
}

bool ContentWorkspaceDocument::removeObjectVisual(const simulation::DefinitionId& id,
                                                  std::string& error) {
    return removeDefinition({ContentDefinitionKind::objectVisual, id}, error);
}

bool ContentWorkspaceDocument::addNpcVisual(
    const std::filesystem::path& file, game::content::AuthoredNpcVisualSet value,
    std::string& error) {
    const auto id = value.id;
    return addDefinition(ContentDefinitionKind::npcVisual, file, id,
        [value = std::move(value)](AuthoredContentPack& pack) {
            pack.npcVisuals.push_back(value);
        }, error);
}

bool ContentWorkspaceDocument::updateNpcVisual(
    const simulation::DefinitionId& id, game::content::AuthoredNpcVisualSet value,
    std::string& error) {
    if (value.id != id) { error = "definition IDs are stable during editing"; return false; }
    return mutateDefinition(ContentDefinitionKind::npcVisual, id,
        [value = std::move(value), id](AuthoredContentPack& pack) {
            auto* current = findId(pack.npcVisuals, id);
            if (!current) return false;
            *current = value;
            return true;
        }, error);
}

bool ContentWorkspaceDocument::removeNpcVisual(const simulation::DefinitionId& id,
                                               std::string& error) {
    return removeDefinition({ContentDefinitionKind::npcVisual, id}, error);
}

bool ContentWorkspaceDocument::addTileset(const std::filesystem::path& file,
                                          game::content::AuthoredTileset value,
                                          std::string& error) {
    const auto id = value.id;
    return addDefinition(ContentDefinitionKind::tileset, file, id,
        [value = std::move(value)](AuthoredContentPack& pack) {
            pack.tilesets.push_back(value);
        }, error);
}

bool ContentWorkspaceDocument::updateTileset(const simulation::DefinitionId& id,
                                             game::content::AuthoredTileset value,
                                             std::string& error) {
    if (value.id != id) { error = "definition IDs are stable during editing"; return false; }
    return mutateDefinition(ContentDefinitionKind::tileset, id,
        [value = std::move(value), id](AuthoredContentPack& pack) {
            auto* current = findId(pack.tilesets, id);
            if (!current) return false;
            *current = value;
            return true;
        }, error);
}

bool ContentWorkspaceDocument::removeTileset(const simulation::DefinitionId& id,
                                             std::string& error) {
    return removeDefinition({ContentDefinitionKind::tileset, id}, error);
}

bool ContentWorkspaceDocument::addAuthoringDescriptor(
    const std::filesystem::path& file, game::content::AuthoringDescriptor value,
    std::string& error) {
    const auto id = value.definitionId;
    return addDefinition(ContentDefinitionKind::authoringDescriptor, file, id,
        [value = std::move(value)](AuthoredContentPack& pack) {
            pack.authoringDescriptors.push_back(value);
        }, error);
}

bool ContentWorkspaceDocument::updateAuthoringDescriptor(
    const simulation::DefinitionId& id, game::content::AuthoringDescriptor value,
    std::string& error) {
    if (value.definitionId != id) { error = "definition IDs are stable during editing"; return false; }
    return mutateDefinition(ContentDefinitionKind::authoringDescriptor, id,
        [value = std::move(value), id](AuthoredContentPack& pack) {
            const auto it = std::find_if(pack.authoringDescriptors.begin(),
                                         pack.authoringDescriptors.end(),
                                         [&](const auto& current) { return current.definitionId == id; });
            if (it == pack.authoringDescriptors.end()) return false;
            *it = value;
            return true;
        }, error);
}

bool ContentWorkspaceDocument::removeAuthoringDescriptor(
    const simulation::DefinitionId& id, std::string& error) {
    return removeDefinition({ContentDefinitionKind::authoringDescriptor, id}, error);
}

bool ContentWorkspaceDocument::addTileSemantic(const std::filesystem::path& file,
                                               game::content::AuthoredTileSemantic value,
                                               std::string& error) {
    const auto id = value.id;
    return addDefinition(ContentDefinitionKind::tileSemantic, file, id,
        [value = std::move(value)](AuthoredContentPack& pack) {
            pack.tileSemantics.push_back(value);
        }, error);
}

bool ContentWorkspaceDocument::updateTileSemantic(const simulation::DefinitionId& id,
                                                  game::content::AuthoredTileSemantic value,
                                                  std::string& error) {
    if (value.id != id) { error = "definition IDs are stable during editing"; return false; }
    return mutateDefinition(ContentDefinitionKind::tileSemantic, id,
        [value = std::move(value), id](AuthoredContentPack& pack) {
            auto* current = findId(pack.tileSemantics, id);
            if (!current) return false;
            *current = value;
            return true;
        }, error);
}

bool ContentWorkspaceDocument::removeTileSemantic(const simulation::DefinitionId& id,
                                                  std::string& error) {
    return removeDefinition({ContentDefinitionKind::tileSemantic, id}, error);
}

bool ContentWorkspaceDocument::addStamp(const std::filesystem::path& file,
                                        game::content::AuthoredStamp value,
                                        std::string& error) {
    const auto id = value.id;
    return addDefinition(ContentDefinitionKind::stamp, file, id,
        [value = std::move(value)](AuthoredContentPack& pack) {
            pack.stamps.push_back(value);
        }, error);
}

bool ContentWorkspaceDocument::updateStamp(const simulation::DefinitionId& id,
                                           game::content::AuthoredStamp value,
                                           std::string& error) {
    if (value.id != id) { error = "definition IDs are stable during editing"; return false; }
    return mutateDefinition(ContentDefinitionKind::stamp, id,
        [value = std::move(value), id](AuthoredContentPack& pack) {
            auto* current = findId(pack.stamps, id);
            if (!current) return false;
            *current = value;
            return true;
        }, error);
}

bool ContentWorkspaceDocument::removeStamp(const simulation::DefinitionId& id,
                                           std::string& error) {
    return removeDefinition({ContentDefinitionKind::stamp, id}, error);
}

#define UNDERWORLD_CONTENT_EDITORS(methodName, kindName, memberName, typeName) \
bool ContentWorkspaceDocument::add##methodName(const std::filesystem::path& file, \
                                             game::content::typeName value, std::string& error) { \
    const auto id = value.id; \
    return addDefinition(ContentDefinitionKind::kindName, file, id, \
        [value = std::move(value)](AuthoredContentPack& pack) { pack.memberName.push_back(value); }, error); \
} \
bool ContentWorkspaceDocument::update##methodName(const simulation::DefinitionId& id, \
                                                game::content::typeName value, std::string& error) { \
    if (value.id != id) { error = "definition IDs are stable during editing"; return false; } \
    return mutateDefinition(ContentDefinitionKind::kindName, id, \
        [value = std::move(value), id](AuthoredContentPack& pack) { \
            auto* current = findId(pack.memberName, id); if (!current) return false; *current = value; return true; \
        }, error); \
} \
bool ContentWorkspaceDocument::remove##methodName(const simulation::DefinitionId& id, std::string& error) { \
    return removeDefinition({ContentDefinitionKind::kindName, id}, error); \
}

UNDERWORLD_CONTENT_EDITORS(Projectile, projectile, projectiles, AuthoredProjectile)
bool ContentWorkspaceDocument::addAttack(const std::filesystem::path& file,
                                         game::content::AuthoredAttack value,
                                         std::string& error) {
    const auto id = value.id;
    return addDefinition(ContentDefinitionKind::attack, file, id,
        [value = std::move(value)](AuthoredContentPack& pack) {
            pack.attacks.push_back(value);
        }, error);
}

bool ContentWorkspaceDocument::updateAttack(const simulation::DefinitionId& id,
                                            game::content::AuthoredAttack value,
                                            std::string& error) {
    if (value.id != id) { error = "definition IDs are stable during editing"; return false; }
    return mutateDefinition(ContentDefinitionKind::attack, id,
        [value = std::move(value), id](AuthoredContentPack& pack) {
            auto* current = findId(pack.attacks, id);
            if (!current) return false;
            *current = value;
            return true;
        }, error);
}

bool ContentWorkspaceDocument::removeAttack(const simulation::DefinitionId& id,
                                            std::string& error) {
    // The typed mutation remains available for repair workflows that inspect
    // diagnostics after intentionally removing a referenced definition. The
    // Content Studio DELETE action uses removeDefinition(), which performs the
    // safe reference check before reaching this lower-level operation.
    return mutateDefinition(ContentDefinitionKind::attack, id,
        [id](AuthoredContentPack& pack) {
            const auto it = std::find_if(pack.attacks.begin(), pack.attacks.end(),
                                         [&](const auto& value) { return value.id == id; });
            if (it == pack.attacks.end()) return false;
            pack.attacks.erase(it);
            return true;
        }, error);
}
UNDERWORLD_CONTENT_EDITORS(Behavior, behavior, behaviors, AuthoredBehaviorProfile)
UNDERWORLD_CONTENT_EDITORS(Enemy, enemy, enemies, AuthoredEnemy)
UNDERWORLD_CONTENT_EDITORS(Item, item, items, AuthoredItem)
UNDERWORLD_CONTENT_EDITORS(Pickup, pickup, pickups, AuthoredPickup)
UNDERWORLD_CONTENT_EDITORS(Object, object, objects, AuthoredWorldObject)
UNDERWORLD_CONTENT_EDITORS(Npc, npc, npcs, AuthoredNpc)
UNDERWORLD_CONTENT_EDITORS(Dialogue, dialogue, dialogues, AuthoredDialogue)
UNDERWORLD_CONTENT_EDITORS(Quest, quest, quests, AuthoredQuest)
UNDERWORLD_CONTENT_EDITORS(PlayerProgression, playerProgression, playerProgressions, AuthoredPlayerProgression)
UNDERWORLD_CONTENT_EDITORS(RewardProfile, rewardProfile, rewardProfiles, AuthoredRewardProfile)
UNDERWORLD_CONTENT_EDITORS(RewardGrant, rewardGrant, rewardGrants, AuthoredRewardGrant)
UNDERWORLD_CONTENT_EDITORS(Shop, shop, shops, AuthoredShop)
UNDERWORLD_CONTENT_EDITORS(PresentationEffect, presentationEffect, presentationEffects, AuthoredPresentationEffect)

#undef UNDERWORLD_CONTENT_EDITORS

bool ContentWorkspaceDocument::duplicateDefinition(
    const ContentDefinitionKey& source, simulation::DefinitionId duplicateId,
    std::string& error) {
    if (!ensureWritable(error)) return false;
    if (duplicateId.empty()) { error = "definition ID must not be empty"; return false; }
    if (source.id.empty()) { error = "source definition ID must not be empty"; return false; }
    const auto* sourceLocation = sourceFor(source);
    if (!sourceLocation) { error = "source definition is not present in this workspace"; return false; }
    if (sourceFor({source.kind, duplicateId})) {
        error = "definition ID already exists";
        return false;
    }

    const auto sourcePath = sourceLocation->sourcePath;
    return addDefinition(source.kind, sourcePath, duplicateId,
        [&](AuthoredContentPack& pack) {
            const auto copy = [&](auto& values) {
                const auto* original = findId(values, source.id);
                if (!original) return false;
                auto duplicate = *original;
                duplicate.id = duplicateId;
                values.push_back(std::move(duplicate));
                return true;
            };
            switch (source.kind) {
            case ContentDefinitionKind::tileset: return copy(pack.tilesets);
            case ContentDefinitionKind::projectile: return copy(pack.projectiles);
            case ContentDefinitionKind::attack: return copy(pack.attacks);
            case ContentDefinitionKind::behavior: return copy(pack.behaviors);
            case ContentDefinitionKind::enemy: return copy(pack.enemies);
            case ContentDefinitionKind::item: return copy(pack.items);
            case ContentDefinitionKind::object: return copy(pack.objects);
            case ContentDefinitionKind::pickup: return copy(pack.pickups);
            case ContentDefinitionKind::npc: return copy(pack.npcs);
            case ContentDefinitionKind::npcVisual: return copy(pack.npcVisuals);
            case ContentDefinitionKind::dialogue: return copy(pack.dialogues);
            case ContentDefinitionKind::quest: return copy(pack.quests);
            case ContentDefinitionKind::tileSemantic: return copy(pack.tileSemantics);
            case ContentDefinitionKind::stamp: return copy(pack.stamps);
            case ContentDefinitionKind::playerProgression: return copy(pack.playerProgressions);
            case ContentDefinitionKind::rewardProfile: return copy(pack.rewardProfiles);
            case ContentDefinitionKind::rewardGrant: return copy(pack.rewardGrants);
            case ContentDefinitionKind::shop: return copy(pack.shops);
            case ContentDefinitionKind::presentationEffect: return copy(pack.presentationEffects);
            case ContentDefinitionKind::visualImage: return copy(pack.visualImages);
            case ContentDefinitionKind::staticSprite: return copy(pack.staticSprites);
            case ContentDefinitionKind::animation: return copy(pack.animations);
            case ContentDefinitionKind::enemyVisual: return copy(pack.enemyVisuals);
            case ContentDefinitionKind::objectVisual: return copy(pack.objectVisuals);
            case ContentDefinitionKind::authoringDescriptor: {
                const auto it = std::find_if(pack.authoringDescriptors.begin(),
                                             pack.authoringDescriptors.end(),
                                             [&](const auto& value) {
                                                 return value.definitionId == source.id;
                                             });
                if (it == pack.authoringDescriptors.end()) return false;
                auto duplicate = *it;
                duplicate.definitionId = duplicateId;
                pack.authoringDescriptors.push_back(std::move(duplicate));
                return true;
            }
            }
            return false;
        }, error);
}

bool ContentWorkspaceDocument::removeDefinition(const ContentDefinitionKey& key, std::string& error) {
    if (!ensureWritable(error)) return false;
    if (!sourceFor(key)) { error = "definition is not present in this workspace"; return false; }
    const auto* pack = mergedAuthored_ ? &*mergedAuthored_ : nullptr;
    if (!pack) { error = "content workspace has no merged authored data"; return false; }
    const auto& id = key.id;
    std::string referencedBy;
    const auto refText = [](std::string_view category, const simulation::DefinitionId& definition) {
        return std::string(category) + " " + std::string(definition.value());
    };
    switch (key.kind) {
    case ContentDefinitionKind::visualImage:
        for (const auto& value : pack->staticSprites) if (value.imageId == id) { referencedBy = refText("staticSprite", value.id); break; }
        if (referencedBy.empty()) for (const auto& value : pack->animations) if (value.imageId == id) { referencedBy = refText("animation", value.id); break; }
        break;
    case ContentDefinitionKind::staticSprite:
        for (const auto& value : pack->pickups) if (value.visualId == id) { referencedBy = refText("pickup", value.id); break; }
        break;
    case ContentDefinitionKind::animation: {
        const auto matches = [&](const auto& ref) {
            return (ref.defaultAnimation && *ref.defaultAnimation == id) ||
                   (ref.down && *ref.down == id) ||
                   (ref.up && *ref.up == id) ||
                   (ref.side && *ref.side == id);
        };
        for (const auto& value : pack->enemyVisuals) {
            if (matches(value.idle) ||
                (value.move && matches(*value.move)) ||
                (value.hurt && matches(*value.hurt)) ||
                (value.death && matches(*value.death)) ||
                (value.dead && matches(*value.dead)) ||
                std::any_of(value.attacks.begin(), value.attacks.end(), [&](const auto& attack) {
                    return matches(attack.clips);
                })) { referencedBy = refText("enemyVisual", value.id); break; }
        }
        if (referencedBy.empty()) for (const auto& value : pack->objectVisuals) {
            if (value.idleAnimationId == id ||
                (value.openedAnimationId && *value.openedAnimationId == id) ||
                (value.damagedAnimationId && *value.damagedAnimationId == id) ||
                (value.destroyingAnimationId && *value.destroyingAnimationId == id) ||
                (value.activationInactiveAnimationId && *value.activationInactiveAnimationId == id) ||
                (value.activationActiveAnimationId && *value.activationActiveAnimationId == id) ||
                (value.doorLockedAnimationId && *value.doorLockedAnimationId == id) ||
                (value.doorClosedAnimationId && *value.doorClosedAnimationId == id) ||
                (value.doorOpenAnimationId && *value.doorOpenAnimationId == id) ||
                (value.destroyedAnimationId && *value.destroyedAnimationId == id)) {
                referencedBy = refText("objectVisual", value.id); break;
            }
        }
        if (referencedBy.empty()) for (const auto& value : pack->npcVisuals) {
            if (value.idle && matches(*value.idle)) { referencedBy = refText("npcVisual", value.id); break; }
        }
        break;
    }
    case ContentDefinitionKind::tileset:
        for (const auto& value : pack->tileSemantics) if (value.tilesetId == id) { referencedBy = refText("tileSemantic", value.id); break; }
        break;
    case ContentDefinitionKind::projectile:
        for (const auto& value : pack->attacks) if (value.projectileDefinitionId && *value.projectileDefinitionId == id) { referencedBy = refText("attack", value.id); break; }
        break;
    case ContentDefinitionKind::attack:
        for (const auto& value : pack->enemies) if (std::find(value.attackIds.begin(), value.attackIds.end(), id) != value.attackIds.end()) { referencedBy = refText("enemy", value.id); break; }
        break;
    case ContentDefinitionKind::behavior:
        for (const auto& value : pack->enemies) if (value.behaviorProfileId == id) { referencedBy = refText("enemy", value.id); break; }
        break;
    case ContentDefinitionKind::enemyVisual:
        for (const auto& value : pack->enemies) if (value.visualSetId == id) { referencedBy = refText("enemy", value.id); break; }
        break;
    case ContentDefinitionKind::objectVisual:
        for (const auto& value : pack->objects) if (value.visualSetId == id) { referencedBy = refText("object", value.id); break; }
        break;
    case ContentDefinitionKind::npcVisual:
        for (const auto& value : pack->npcs) if (value.visualSetId == id) { referencedBy = refText("npc", value.id); break; }
        break;
    case ContentDefinitionKind::item:
        for (const auto& value : pack->pickups) if (const auto* item = std::get_if<game::content::AuthoredItemPickup>(&value.payload); item && item->itemId == id) { referencedBy = refText("pickup", value.id); break; }
        if (referencedBy.empty()) for (const auto& value : pack->rewardGrants) if (std::any_of(value.items.begin(), value.items.end(), [&](const auto& item) { return item.itemId == id; })) { referencedBy = refText("rewardGrant", value.id); break; }
        if (referencedBy.empty()) for (const auto& value : pack->shops) if (std::any_of(value.offers.begin(), value.offers.end(), [&](const auto& offer) { return offer.itemId == id; })) { referencedBy = refText("shop", value.id); break; }
        break;
    case ContentDefinitionKind::pickup:
        for (const auto& value : pack->rewardProfiles) if (std::any_of(value.loot.begin(), value.loot.end(), [&](const auto& entry) { return entry.pickupDefinitionId == id; })) { referencedBy = refText("rewardProfile", value.id); break; }
        break;
    case ContentDefinitionKind::rewardProfile:
        for (const auto& value : pack->enemies) if (value.rewardProfileId && *value.rewardProfileId == id) { referencedBy = refText("enemy", value.id); break; }
        break;
    case ContentDefinitionKind::rewardGrant:
        for (const auto& value : pack->quests) if (value.rewardGrantId && *value.rewardGrantId == id) { referencedBy = refText("quest", value.id); break; }
        break;
    case ContentDefinitionKind::dialogue:
        for (const auto& value : pack->npcs) if (value.defaultDialogueId == id) { referencedBy = refText("npc", value.id); break; }
        break;
    default: break;
    }
    if (!referencedBy.empty()) {
        error = "cannot delete definition: referenced by " + referencedBy;
        return false;
    }
    return mutateDefinition(key.kind, key.id,
        [key](AuthoredContentPack& pack) {
            auto erase = [&](auto& values) {
                const auto it = std::find_if(values.begin(), values.end(), [&](const auto& value) { return value.id == key.id; });
                if (it == values.end()) return false;
                values.erase(it);
                return true;
            };
            switch (key.kind) {
            case ContentDefinitionKind::tileset: return erase(pack.tilesets);
            case ContentDefinitionKind::visualImage: return erase(pack.visualImages);
            case ContentDefinitionKind::staticSprite: return erase(pack.staticSprites);
            case ContentDefinitionKind::animation: return erase(pack.animations);
            case ContentDefinitionKind::enemyVisual: return erase(pack.enemyVisuals);
            case ContentDefinitionKind::objectVisual: return erase(pack.objectVisuals);
            case ContentDefinitionKind::npcVisual: return erase(pack.npcVisuals);
            case ContentDefinitionKind::projectile: return erase(pack.projectiles);
            case ContentDefinitionKind::attack: return erase(pack.attacks);
            case ContentDefinitionKind::behavior: return erase(pack.behaviors);
            case ContentDefinitionKind::enemy: return erase(pack.enemies);
            case ContentDefinitionKind::item: return erase(pack.items);
            case ContentDefinitionKind::object: return erase(pack.objects);
            case ContentDefinitionKind::pickup: return erase(pack.pickups);
            case ContentDefinitionKind::npc: return erase(pack.npcs);
            case ContentDefinitionKind::dialogue: return erase(pack.dialogues);
            case ContentDefinitionKind::quest: return erase(pack.quests);
            case ContentDefinitionKind::authoringDescriptor: {
                const auto it = std::find_if(pack.authoringDescriptors.begin(),
                                             pack.authoringDescriptors.end(),
                                             [&](const auto& value) { return value.definitionId == key.id; });
                if (it == pack.authoringDescriptors.end()) return false;
                pack.authoringDescriptors.erase(it);
                return true;
            }
            case ContentDefinitionKind::tileSemantic: return erase(pack.tileSemantics);
            case ContentDefinitionKind::stamp: return erase(pack.stamps);
            case ContentDefinitionKind::playerProgression: return erase(pack.playerProgressions);
            case ContentDefinitionKind::rewardProfile: return erase(pack.rewardProfiles);
            case ContentDefinitionKind::rewardGrant: return erase(pack.rewardGrants);
            case ContentDefinitionKind::shop: return erase(pack.shops);
            case ContentDefinitionKind::presentationEffect: return erase(pack.presentationEffects);
            default: return false;
            }
        }, error);
}

} // namespace underworld::editor
