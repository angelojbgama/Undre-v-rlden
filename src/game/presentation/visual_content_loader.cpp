#include "game/presentation/visual_content_loader.h"

#include "engine/render/image.h"
#include "engine/render/sprite.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace underworld::game::presentation {
namespace {

void diagnostic(VisualContentLoadResult& result, VisualContentDiagnosticStage stage,
                std::string code, simulation::DefinitionId id, std::string path,
                std::string message) {
    result.diagnostics.push_back({stage, std::move(code), std::move(id), std::move(path),
                                  std::move(message)});
}

bool contained(const std::filesystem::path& root, const std::filesystem::path& candidate) {
    const auto rootText = root.lexically_normal().generic_string();
    const auto candidateText = candidate.lexically_normal().generic_string();
    return candidateText == rootText ||
           (candidateText.size() > rootText.size() &&
            candidateText.compare(0, rootText.size(), rootText) == 0 &&
            candidateText[rootText.size()] == '/');
}

bool safeRelativePath(const std::string& value) {
    if (value.empty() || value.find('\0') != std::string::npos) return false;
    if (value.front() == '/' || value.front() == '\\' ||
        (value.size() > 1 && std::isalpha(static_cast<unsigned char>(value[0])) &&
         value[1] == ':')) return false;
    std::string component;
    for (const char character : value + '/') {
        if (character == '/' || character == '\\') {
            if (component.empty() || component == "." || component == "..") return false;
            component.clear();
        } else {
            component.push_back(character);
        }
    }
    return true;
}

std::filesystem::path resolvePath(const VisualImageDefinition& definition,
                                  const VisualAssetRoots& roots,
                                  VisualContentLoadResult& result) {
    const std::filesystem::path* root = nullptr;
    if (definition.root == VisualAssetRoot::gameAssets) {
        root = &roots.gameAssetsRoot;
    } else if (roots.contentWorkspaceRoot) {
        root = &*roots.contentWorkspaceRoot;
    } else {
        diagnostic(result, VisualContentDiagnosticStage::resolve, "workspace_root_missing",
                   definition.id, definition.relativePath,
                   "contentWorkspace visual asset requires a workspace root");
        return {};
    }
    if (root->empty()) {
        diagnostic(result, VisualContentDiagnosticStage::resolve, "asset_root_missing",
                   definition.id, definition.relativePath, "visual asset root is empty");
        return {};
    }
    if (!safeRelativePath(definition.relativePath)) {
        diagnostic(result, VisualContentDiagnosticStage::resolve, "invalid_asset_path",
                   definition.id, definition.relativePath,
                   "visual asset path must be a normalized relative path");
        return {};
    }
    const auto absoluteRoot = std::filesystem::absolute(*root).lexically_normal();
    const auto candidate = (absoluteRoot / definition.relativePath).lexically_normal();
    if (!contained(absoluteRoot, candidate)) {
        diagnostic(result, VisualContentDiagnosticStage::resolve, "asset_path_escape",
                   definition.id, definition.relativePath,
                   "visual asset path escapes its selected asset root");
        return {};
    }
    if (definition.root == VisualAssetRoot::contentWorkspace) {
        std::error_code error;
        const auto rootStatus = std::filesystem::symlink_status(absoluteRoot, error);
        if (!error && std::filesystem::is_symlink(rootStatus)) {
            diagnostic(result, VisualContentDiagnosticStage::resolve,
                       "asset_symlink_rejected", definition.id,
                       definition.relativePath,
                       "content workspace visual assets may not use a symlink workspace root");
            return {};
        }
        error.clear();
        auto current = absoluteRoot;
        for (const auto& component : std::filesystem::path(definition.relativePath)) {
            current /= component;
            const auto status = std::filesystem::symlink_status(current, error);
            if (!error && std::filesystem::is_symlink(status)) {
                diagnostic(result, VisualContentDiagnosticStage::resolve,
                           "asset_symlink_rejected", definition.id,
                           definition.relativePath,
                           "content workspace visual assets may not use symlink path components");
                return {};
            }
            error.clear();
        }
    }
    return candidate;
}

bool validSource(const core::RectI& source, const render::Image& image) {
    const std::int64_t right = static_cast<std::int64_t>(source.x) + source.width;
    const std::int64_t bottom = static_cast<std::int64_t>(source.y) + source.height;
    return !source.empty() && source.x >= 0 && source.y >= 0 && right <= image.width() &&
           bottom <= image.height();
}

template<class Map>
std::vector<const typename Map::value_type*> orderedEntries(const Map& values) {
    std::vector<const typename Map::value_type*> result;
    result.reserve(values.size());
    for (const auto& entry : values) result.push_back(&entry);
    std::sort(result.begin(), result.end(), [](const auto* left, const auto* right) {
        return left->first.value() < right->first.value();
    });
    return result;
}

std::shared_ptr<const render::AnimationClip> makeClip(
    const AnimationDefinition& definition, const std::shared_ptr<const render::SpriteSheet>& sheet,
    const render::Image& image, VisualContentLoadResult& result) {
    std::vector<render::AnimationFrame> frames;
    frames.reserve(definition.frames.size());
    bool valid = true;
    for (const auto& authored : definition.frames) {
        if (!validSource(authored.source, image)) {
            diagnostic(result, VisualContentDiagnosticStage::validate, "frame_out_of_bounds",
                       definition.id, {}, "animation frame source rectangle is outside decoded image");
            valid = false;
        }
        frames.push_back({{authored.source, authored.anchor, authored.drawOffset, false},
                          authored.durationTicks, authored.markers});
    }
    if (!valid) return {};
    try {
        return std::make_shared<const render::AnimationClip>(
            std::string(definition.id.value()), sheet, std::move(frames), definition.loop);
    } catch (const std::exception& exception) {
        diagnostic(result, VisualContentDiagnosticStage::compile, "animation_rejected",
                   definition.id, {}, exception.what());
        return {};
    }
}

DirectionalAnimationClips directional(const DirectionalAnimationRef& reference,
                                       const RuntimeAnimationCatalog& animations) {
    const std::optional<simulation::DefinitionId>* candidates[] = {
        &reference.defaultAnimation, &reference.down, &reference.up, &reference.side};
    const auto resolve = [&](std::size_t requested) {
        const std::optional<simulation::DefinitionId>* ordered[] = {
            candidates[requested], &reference.defaultAnimation, &reference.down,
            &reference.up, &reference.side};
        for (const auto* candidate : ordered) {
            if (candidate->has_value()) {
                if (const auto* clip = animations.find(**candidate)) return *clip;
            }
        }
        throw std::out_of_range("directional animation binding was not found");
    };
    return {resolve(1), resolve(2), resolve(3)};
}

std::size_t npcFacingIndex(gameplay::FacingDirection facing) noexcept {
    switch (facing) {
    case gameplay::FacingDirection::down: return 0;
    case gameplay::FacingDirection::up: return 1;
    case gameplay::FacingDirection::left:
    case gameplay::FacingDirection::right: return 2;
    }
    return 0;
}

} // namespace

const char* visualContentStageName(VisualContentDiagnosticStage stage) noexcept {
    switch (stage) {
    case VisualContentDiagnosticStage::resolve: return "resolve";
    case VisualContentDiagnosticStage::decode: return "decode";
    case VisualContentDiagnosticStage::validate: return "validate";
    case VisualContentDiagnosticStage::compile: return "compile";
    }
    return "unknown";
}

void RuntimeStaticSpriteCatalog::add(RuntimeStaticSprite sprite) {
    if (sprite.id.empty() || !sprite.sheet) throw std::invalid_argument("static sprite is incomplete");
    const auto [it, inserted] = sprites_.emplace(sprite.id, std::move(sprite));
    static_cast<void>(it);
    if (!inserted) throw std::logic_error("duplicate runtime static sprite");
}

const RuntimeStaticSprite* RuntimeStaticSpriteCatalog::find(
    const simulation::DefinitionId& id) const noexcept {
    const auto it = sprites_.find(id);
    return it == sprites_.end() ? nullptr : &it->second;
}

const RuntimeStaticSprite& RuntimeStaticSpriteCatalog::require(
    const simulation::DefinitionId& id) const {
    const auto* value = find(id);
    if (!value) throw std::out_of_range("runtime static sprite not found");
    return *value;
}

void RuntimeAnimationCatalog::add(simulation::DefinitionId id,
                                  std::shared_ptr<const render::AnimationClip> clip) {
    if (id.empty() || !clip) throw std::invalid_argument("runtime animation is incomplete");
    const auto [it, inserted] = clips_.emplace(std::move(id), std::move(clip));
    static_cast<void>(it);
    if (!inserted) throw std::logic_error("duplicate runtime animation");
}

const std::shared_ptr<const render::AnimationClip>* RuntimeAnimationCatalog::find(
    const simulation::DefinitionId& id) const noexcept {
    const auto it = clips_.find(id);
    return it == clips_.end() ? nullptr : &it->second;
}

const std::shared_ptr<const render::AnimationClip>& RuntimeAnimationCatalog::require(
    const simulation::DefinitionId& id) const {
    const auto* value = find(id);
    if (!value) throw std::out_of_range("runtime animation not found");
    return *value;
}

void RuntimeNpcVisualCatalog::add(RuntimeNpcVisualSet set) {
    if (set.id.empty()) throw std::invalid_argument("NPC runtime visual requires id");
    const auto [it, inserted] = sets_.emplace(set.id, std::move(set));
    static_cast<void>(it);
    if (!inserted) throw std::logic_error("duplicate NPC runtime visual");
}

RuntimeNpcVisualInstance::RuntimeNpcVisualInstance(
    simulation::EntityHandle handle, const RuntimeNpcVisualSet& visualSet)
    : handle_(handle), visualSet_(&visualSet) {
    if (!handle_) throw std::invalid_argument("NPC visual requires a valid handle");
}

void RuntimeNpcVisualInstance::update(const gameplay::npcs::NpcInstance& npc,
                                      std::uint64_t ticks) {
    if (npc.handle() != handle_) {
        throw std::invalid_argument("NPC visual updated with a different handle");
    }
    if (visualSet_->idle) {
        const auto facing = npc.facing();
        if (!initialized_ || facing != facing_) {
            animator_.play((*visualSet_->idle)[npcFacingIndex(facing)]);
            facing_ = facing;
            flipX_ = facing == gameplay::FacingDirection::right;
            initialized_ = true;
        }
        animator_.updateTicks(ticks);
    } else {
        initialized_ = true;
    }
}

const RuntimeNpcVisualSet* RuntimeNpcVisualCatalog::find(
    const simulation::DefinitionId& id) const noexcept {
    const auto it = sets_.find(id);
    return it == sets_.end() ? nullptr : &it->second;
}

const RuntimeNpcVisualSet& RuntimeNpcVisualCatalog::require(
    const simulation::DefinitionId& id) const {
    const auto* value = find(id);
    if (!value) throw std::out_of_range("NPC runtime visual not found");
    return *value;
}

VisualContentLoadResult VisualContentLoader::load(const GameContentRegistry& registry,
                                                  const VisualAssetRoots& roots) const {
    VisualContentLoadResult result;
    RuntimeVisualContent runtime;
    std::unordered_map<simulation::DefinitionId, std::shared_ptr<const render::Image>,
                       simulation::DefinitionIdHash> images;
    std::unordered_map<simulation::DefinitionId, std::shared_ptr<const render::SpriteSheet>,
                       simulation::DefinitionIdHash> sheets;

    for (const auto* entry : orderedEntries(registry.visualImages().values())) {
        const auto path = resolvePath(entry->second, roots, result);
        if (path.empty()) continue;
        try {
            auto image = std::make_shared<const render::Image>(decoder_.decode(path));
            images.emplace(entry->first, image);
            sheets.emplace(entry->first, std::make_shared<const render::SpriteSheet>(image));
        } catch (const std::exception& exception) {
            diagnostic(result, VisualContentDiagnosticStage::decode, "image_decode_failed",
                       entry->first, entry->second.relativePath, exception.what());
        }
    }
    for (const auto* entry : orderedEntries(registry.animations().values())) {
        const auto image = images.find(entry->second.imageId);
        const auto sheet = sheets.find(entry->second.imageId);
        if (image == images.end() || sheet == sheets.end()) {
            diagnostic(result, VisualContentDiagnosticStage::compile, "unknown_image",
                       entry->first, {}, "animation image was not decoded");
            continue;
        }
        const auto clip = makeClip(entry->second, sheet->second, *image->second, result);
        if (clip) runtime.animations.add(entry->first, clip);
    }
    for (const auto* entry : orderedEntries(registry.staticSprites().values())) {
        const auto image = images.find(entry->second.imageId);
        const auto sheet = sheets.find(entry->second.imageId);
        if (image == images.end() || sheet == sheets.end()) {
            diagnostic(result, VisualContentDiagnosticStage::compile, "unknown_image",
                       entry->first, {}, "static sprite image was not decoded");
            continue;
        }
        const core::RectI source = entry->second.source.value_or(
            core::RectI{0, 0, image->second->width(), image->second->height()});
        if (!validSource(source, *image->second)) {
            diagnostic(result, VisualContentDiagnosticStage::validate, "source_out_of_bounds",
                       entry->first, {}, "static sprite source rectangle is outside decoded image");
            continue;
        }
        runtime.staticSprites.add({entry->first, sheet->second,
                                   {source, entry->second.anchor, {}, false}});
    }
    try {
        for (const auto* entry : orderedEntries(registry.enemyVisuals().values())) {
            EnemyVisualSet set;
            set.id = entry->first;
            set.idle = directional(entry->second.idle, runtime.animations);
            set.walk = entry->second.move ? directional(*entry->second.move, runtime.animations) : set.idle;
            set.death = entry->second.death ? directional(*entry->second.death, runtime.animations) : set.idle;
            if (entry->second.hurt) set.hurt = directional(*entry->second.hurt, runtime.animations);
            if (entry->second.dead) set.dead = directional(*entry->second.dead, runtime.animations);
            for (const auto& attack : entry->second.attacks)
                set.attacks.emplace(attack.visualActionId, directional(attack.clips, runtime.animations));
            runtime.enemies.add(std::move(set));
        }
        for (const auto* entry : orderedEntries(registry.objectVisuals().values())) {
            WorldObjectVisualSet set;
            set.id = entry->first;
            set.idle = runtime.animations.require(entry->second.idleAnimationId);
            if (entry->second.openedAnimationId) set.opened = runtime.animations.require(*entry->second.openedAnimationId);
            if (entry->second.destroyingAnimationId) set.destroying = runtime.animations.require(*entry->second.destroyingAnimationId);
            if (entry->second.activationInactiveAnimationId) set.activationInactive = runtime.animations.require(*entry->second.activationInactiveAnimationId);
            if (entry->second.activationActiveAnimationId) set.activationActive = runtime.animations.require(*entry->second.activationActiveAnimationId);
            if (entry->second.doorLockedAnimationId) set.doorLocked = runtime.animations.require(*entry->second.doorLockedAnimationId);
            if (entry->second.doorClosedAnimationId) set.doorClosed = runtime.animations.require(*entry->second.doorClosedAnimationId);
            if (entry->second.doorOpenAnimationId) set.doorOpen = runtime.animations.require(*entry->second.doorOpenAnimationId);
            runtime.objects.add(std::move(set));
        }
        for (const auto* entry : orderedEntries(registry.npcVisuals().values())) {
            RuntimeNpcVisualSet set{entry->first, entry->second.markerColor, std::nullopt};
            if (entry->second.idle) set.idle = directional(*entry->second.idle, runtime.animations);
            runtime.npcs.add(std::move(set));
        }

        const auto requireStaticSprite = [&](const simulation::DefinitionId& id,
                                              const simulation::DefinitionId& visualId,
                                              const char* category) {
            if (!runtime.staticSprites.find(visualId)) {
                diagnostic(result, VisualContentDiagnosticStage::compile,
                           "missing_static_sprite", id, {},
                           std::string(category) + " visual references an unavailable static sprite");
            }
        };
        for (const auto* entry : orderedEntries(registry.projectiles().values()))
            requireStaticSprite(entry->first, entry->second.visualId, "projectile");
        for (const auto* entry : orderedEntries(registry.items().values()))
            requireStaticSprite(entry->first, entry->second.visualId, "item");
        for (const auto& entry : registry.pickups())
            requireStaticSprite(entry.id, entry.visualId, "pickup");
        for (const auto* entry : orderedEntries(registry.enemies().values())) {
            if (!runtime.enemies.find(entry->second.visualSetId)) {
                diagnostic(result, VisualContentDiagnosticStage::compile,
                           "missing_enemy_visual", entry->first, {},
                           "enemy visual set was not loaded");
            }
        }
        for (const auto* entry : orderedEntries(registry.objects().values())) {
            if (!runtime.objects.find(entry->second.visualSetId)) {
                diagnostic(result, VisualContentDiagnosticStage::compile,
                           "missing_object_visual", entry->first, {},
                           "world object visual set was not loaded");
            }
        }
        for (const auto* entry : orderedEntries(registry.npcs().values())) {
            if (!runtime.npcs.find(entry->second.visualSetId)) {
                diagnostic(result, VisualContentDiagnosticStage::compile,
                           "missing_npc_visual", entry->first, {},
                           "NPC visual set was not loaded");
            }
        }
    } catch (const std::exception& exception) {
        diagnostic(result, VisualContentDiagnosticStage::compile, "visual_catalog_rejected", {}, {}, exception.what());
    }
    if (result.diagnostics.empty()) result.content = std::move(runtime);
    return result;
}

} // namespace underworld::game::presentation
