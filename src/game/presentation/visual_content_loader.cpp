#include "game/presentation/visual_content_loader.h"

#include "engine/render/image.h"
#include "engine/render/sprite.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace underworld::game::presentation {
namespace {

void diagnostic(VisualContentLoadResult& result, VisualContentDiagnosticStage stage,
                std::string code, simulation::DefinitionId id, std::string path,
                std::string message,
                std::optional<VisualAssetRoot> assetRoot = std::nullopt) {
    result.diagnostics.push_back({stage, std::move(code), std::move(id), std::move(assetRoot),
                                  std::move(path), std::move(message)});
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

std::optional<std::filesystem::path> resolvePath(
    const VisualImageDefinition& definition,
    const VisualAssetRoots& roots,
    std::vector<VisualContentDiagnostic>& diagnostics) {
    VisualContentLoadResult result;
    const std::filesystem::path* root = nullptr;
    if (definition.root == VisualAssetRoot::gameAssets) {
        root = &roots.gameAssetsRoot;
    } else if (roots.contentWorkspaceRoot) {
        root = &*roots.contentWorkspaceRoot;
    } else {
        diagnostic(result, VisualContentDiagnosticStage::resolve, "workspace_root_missing",
                   definition.id, definition.relativePath,
                   "contentWorkspace visual asset requires a workspace root", definition.root);
        diagnostics.insert(diagnostics.end(), result.diagnostics.begin(), result.diagnostics.end());
        return std::nullopt;
    }
    if (root->empty()) {
        diagnostic(result, VisualContentDiagnosticStage::resolve, "asset_root_missing",
                   definition.id, definition.relativePath, "visual asset root is empty",
                   definition.root);
        diagnostics.insert(diagnostics.end(), result.diagnostics.begin(), result.diagnostics.end());
        return std::nullopt;
    }
    if (!safeRelativePath(definition.relativePath)) {
        diagnostic(result, VisualContentDiagnosticStage::resolve, "invalid_asset_path",
                   definition.id, definition.relativePath,
                   "visual asset path must be a normalized relative path", definition.root);
        diagnostics.insert(diagnostics.end(), result.diagnostics.begin(), result.diagnostics.end());
        return std::nullopt;
    }
    const auto absoluteRoot = std::filesystem::absolute(*root).lexically_normal();
    const auto candidate = (absoluteRoot / definition.relativePath).lexically_normal();
    if (!contained(absoluteRoot, candidate)) {
        diagnostic(result, VisualContentDiagnosticStage::resolve, "asset_path_escape",
                   definition.id, definition.relativePath,
                   "visual asset path escapes its selected asset root", definition.root);
        diagnostics.insert(diagnostics.end(), result.diagnostics.begin(), result.diagnostics.end());
        return std::nullopt;
    }
    if (definition.root == VisualAssetRoot::contentWorkspace) {
        std::error_code error;
        const auto rootStatus = std::filesystem::symlink_status(absoluteRoot, error);
        if (!error && std::filesystem::is_symlink(rootStatus)) {
            diagnostic(result, VisualContentDiagnosticStage::resolve,
                       "asset_symlink_rejected", definition.id,
                       definition.relativePath,
                       "content workspace visual assets may not use a symlink workspace root",
                       definition.root);
            diagnostics.insert(diagnostics.end(), result.diagnostics.begin(), result.diagnostics.end());
            return std::nullopt;
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
                           "content workspace visual assets may not use symlink path components",
                           definition.root);
                diagnostics.insert(diagnostics.end(), result.diagnostics.begin(), result.diagnostics.end());
                return std::nullopt;
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
        frames.push_back({{authored.source, authored.anchor, authored.drawOffset, authored.flipX},
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

const char* visualAssetRootName(VisualAssetRoot root) noexcept {
    switch (root) {
    case VisualAssetRoot::gameAssets: return "gameAssets";
    case VisualAssetRoot::contentWorkspace: return "contentWorkspace";
    }
    return "unknown";
}

std::optional<std::filesystem::path> VisualAssetResolver::resolve(
    const VisualImageDefinition& definition,
    std::vector<VisualContentDiagnostic>& diagnostics) const {
    return resolvePath(definition, roots_, diagnostics);
}

VisualImageDecodeResult decodeVisualImage(const VisualImageDefinition& definition,
                                          platform::ImageDecoder& decoder,
                                          const VisualAssetRoots& roots) {
    VisualImageDecodeResult result;
    result.path = VisualAssetResolver{roots}.resolve(definition, result.diagnostics);
    if (!result.path) return result;
    try {
        result.image = std::make_shared<const render::Image>(decoder.decode(*result.path));
    } catch (const std::exception& exception) {
        result.diagnostics.push_back({VisualContentDiagnosticStage::decode,
                                      "image_decode_failed", definition.id, definition.root,
                                      definition.relativePath, exception.what()});
    }
    return result;
}

std::shared_ptr<const render::AnimationClip> buildVisualAnimationClip(
    const AnimationDefinition& definition,
    const std::shared_ptr<const render::SpriteSheet>& sheet,
    const render::Image& image,
    std::vector<VisualContentDiagnostic>& diagnostics) {
    VisualContentLoadResult result;
    const auto clip = makeClip(definition, sheet, image, result);
    diagnostics.insert(diagnostics.end(), result.diagnostics.begin(), result.diagnostics.end());
    return clip;
}

std::optional<simulation::DefinitionId> resolveDirectionalAnimationId(
    const DirectionalAnimationRef& reference,
    gameplay::FacingDirection facing) noexcept {
    const std::optional<simulation::DefinitionId>* exact = nullptr;
    switch (facing) {
    case gameplay::FacingDirection::down: exact = &reference.down; break;
    case gameplay::FacingDirection::up: exact = &reference.up; break;
    case gameplay::FacingDirection::left:
        exact = reference.left ? &reference.left : &reference.side;
        break;
    case gameplay::FacingDirection::right:
        exact = reference.right ? &reference.right : &reference.side;
        break;
    }
    const std::optional<simulation::DefinitionId>* candidates[] = {
        exact, &reference.defaultAnimation, &reference.down, &reference.up,
        &reference.left, &reference.right, &reference.side};
    for (const auto* candidate : candidates) {
        if (candidate != nullptr && candidate->has_value()) return *candidate;
    }
    return std::nullopt;
}

std::optional<DirectionalAnimationClips> resolveDirectionalAnimationClips(
    const DirectionalAnimationRef& reference,
    const RuntimeAnimationCatalog& animations,
    std::vector<VisualContentDiagnostic>& diagnostics,
    const simulation::DefinitionId& ownerId) {
    DirectionalAnimationClips result{};
    const gameplay::FacingDirection directions[] = {
        gameplay::FacingDirection::down,
        gameplay::FacingDirection::up,
        gameplay::FacingDirection::left};
    for (std::size_t index = 0; index < std::size(directions); ++index) {
        const auto animationId = resolveDirectionalAnimationId(reference, directions[index]);
        if (!animationId) {
            diagnostics.push_back({VisualContentDiagnosticStage::compile,
                                   "missing_directional_animation", ownerId, std::nullopt, {},
                                   "directional binding has no authored animation"});
            return std::nullopt;
        }
        const auto* clip = animations.find(*animationId);
        if (!clip) {
            diagnostics.push_back({VisualContentDiagnosticStage::compile,
                                   "unknown_animation", ownerId, std::nullopt, {},
                                   "directional binding references an unavailable animation"});
            return std::nullopt;
        }
        result[index] = *clip;
    }
    return result;
}

std::optional<PlayerDirectionalClips> resolvePlayerDirectionalAnimationClips(
    const DirectionalAnimationRef& reference,
    const RuntimeAnimationCatalog& animations,
    std::vector<VisualContentDiagnostic>& diagnostics,
    const simulation::DefinitionId& ownerId) {
    PlayerDirectionalClips result{};
    const gameplay::FacingDirection directions[] = {
        gameplay::FacingDirection::down,
        gameplay::FacingDirection::up,
        gameplay::FacingDirection::left,
        gameplay::FacingDirection::right};
    for (std::size_t index = 0; index < std::size(directions); ++index) {
        const auto animationId =
            resolveDirectionalAnimationId(reference, directions[index]);
        if (!animationId) {
            diagnostics.push_back({
                VisualContentDiagnosticStage::compile,
                "missing_player_directional_animation",
                ownerId, std::nullopt, {},
                "Player visual binding has no animation for a direction"});
            return std::nullopt;
        }
        const auto* clip = animations.find(*animationId);
        if (!clip) {
            diagnostics.push_back({
                VisualContentDiagnosticStage::compile,
                "unknown_player_animation",
                ownerId, std::nullopt, {},
                "Player visual binding references an unavailable animation"});
            return std::nullopt;
        }
        result[index] = *clip;
    }
    return result;
}

std::string formatVisualContentDiagnostic(const VisualContentDiagnostic& diagnostic) {
    std::ostringstream output;
    output << '[' << visualContentStageName(diagnostic.stage) << '/'
           << diagnostic.code << ']';
    bool hasContext = false;
    if (!diagnostic.definitionId.empty()) {
        output << ' ' << diagnostic.definitionId.value();
        hasContext = true;
    }
    if (diagnostic.assetRoot) {
        output << " root=" << visualAssetRootName(*diagnostic.assetRoot);
        hasContext = true;
    }
    if (!diagnostic.relativePath.empty()) {
        output << " path=" << diagnostic.relativePath;
        hasContext = true;
    }
    if (hasContext) output << ':';
    output << ' ' << diagnostic.message;
    return output.str();
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
        const auto decoded = decodeVisualImage(entry->second, decoder_, roots);
        result.diagnostics.insert(result.diagnostics.end(), decoded.diagnostics.begin(),
                                  decoded.diagnostics.end());
        if (!decoded.image) continue;
        images.emplace(entry->first, decoded.image);
        sheets.emplace(entry->first, std::make_shared<const render::SpriteSheet>(decoded.image));
    }
    for (const auto* entry : orderedEntries(registry.animations().values())) {
        const auto image = images.find(entry->second.imageId);
        const auto sheet = sheets.find(entry->second.imageId);
        if (image == images.end() || sheet == sheets.end()) {
            diagnostic(result, VisualContentDiagnosticStage::compile, "unknown_image",
                       entry->first, {}, "animation image was not decoded");
            continue;
        }
        const auto clip = buildVisualAnimationClip(entry->second, sheet->second, *image->second,
                                                   result.diagnostics);
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

        for (const auto* entry : orderedEntries(registry.playerVisuals().values())) {
            PlayerVisualSet set;
            set.id = entry->first;

            const auto idle = resolvePlayerDirectionalAnimationClips(
                entry->second.idle, runtime.animations,
                result.diagnostics, entry->first);
            const auto walk = resolvePlayerDirectionalAnimationClips(
                entry->second.walk, runtime.animations,
                result.diagnostics, entry->first);
            if (!idle || !walk) continue;
            set.idle = *idle;
            set.walk = *walk;

            if (entry->second.hurt) {
                const auto hurt = resolvePlayerDirectionalAnimationClips(
                    *entry->second.hurt, runtime.animations,
                    result.diagnostics, entry->first);
                if (!hurt) continue;
                set.hurt = *hurt;
            }

            bool actionsValid = true;
            for (const auto& action : entry->second.actions) {
                const auto clips = resolvePlayerDirectionalAnimationClips(
                    action.clips, runtime.animations,
                    result.diagnostics, entry->first);
                if (!clips) {
                    actionsValid = false;
                    break;
                }
                set.actions.emplace(action.actionId, *clips);
            }
            if (!actionsValid) continue;
            runtime.players.add(std::move(set));
        }
        for (const auto* entry : orderedEntries(registry.enemyVisuals().values())) {
            EnemyVisualSet set;
            set.id = entry->first;
            const auto idle = resolveDirectionalAnimationClips(entry->second.idle,
                                                                runtime.animations,
                                                                result.diagnostics, entry->first);
            if (!idle) continue;
            set.idle = *idle;
            if (entry->second.move) {
                const auto move = resolveDirectionalAnimationClips(*entry->second.move,
                                                                    runtime.animations,
                                                                    result.diagnostics, entry->first);
                if (!move) continue;
                set.walk = *move;
            } else set.walk = set.idle;
            if (entry->second.death) {
                const auto death = resolveDirectionalAnimationClips(*entry->second.death,
                                                                      runtime.animations,
                                                                      result.diagnostics, entry->first);
                if (!death) continue;
                set.death = *death;
            } else set.death = set.idle;
            if (entry->second.hurt) {
                const auto hurt = resolveDirectionalAnimationClips(*entry->second.hurt,
                                                                    runtime.animations,
                                                                    result.diagnostics, entry->first);
                if (!hurt) continue;
                set.hurt = *hurt;
            }
            if (entry->second.dead) {
                const auto dead = resolveDirectionalAnimationClips(*entry->second.dead,
                                                                    runtime.animations,
                                                                    result.diagnostics, entry->first);
                if (!dead) continue;
                set.dead = *dead;
            }
            for (const auto& attack : entry->second.attacks) {
                const auto clips = resolveDirectionalAnimationClips(attack.clips,
                                                                      runtime.animations,
                                                                      result.diagnostics,
                                                                      entry->first);
                if (!clips) continue;
                set.attacks.emplace(attack.visualActionId, *clips);
            }
            runtime.enemies.add(std::move(set));
        }
        for (const auto* entry : orderedEntries(registry.objectVisuals().values())) {
            WorldObjectVisualSet set;
            set.id = entry->first;
            set.idle = runtime.animations.require(entry->second.idleAnimationId);
            if (entry->second.openedAnimationId) set.opened = runtime.animations.require(*entry->second.openedAnimationId);
            if (entry->second.damagedAnimationId) set.damaged = runtime.animations.require(*entry->second.damagedAnimationId);
            if (entry->second.destroyingAnimationId) set.destroying = runtime.animations.require(*entry->second.destroyingAnimationId);
            if (entry->second.activationInactiveAnimationId) set.activationInactive = runtime.animations.require(*entry->second.activationInactiveAnimationId);
            if (entry->second.activationActiveAnimationId) set.activationActive = runtime.animations.require(*entry->second.activationActiveAnimationId);
            if (entry->second.doorLockedAnimationId) set.doorLocked = runtime.animations.require(*entry->second.doorLockedAnimationId);
            if (entry->second.doorClosedAnimationId) set.doorClosed = runtime.animations.require(*entry->second.doorClosedAnimationId);
            if (entry->second.doorOpenAnimationId) set.doorOpen = runtime.animations.require(*entry->second.doorOpenAnimationId);
            if (entry->second.destroyedAnimationId) set.destroyed = runtime.animations.require(*entry->second.destroyedAnimationId);
            runtime.objects.add(std::move(set));
        }
        for (const auto* entry : orderedEntries(registry.npcVisuals().values())) {
            RuntimeNpcVisualSet set{entry->first, entry->second.markerColor, std::nullopt};
            if (entry->second.idle) {
                const auto idle = resolveDirectionalAnimationClips(*entry->second.idle,
                                                                    runtime.animations,
                                                                    result.diagnostics, entry->first);
                if (!idle) continue;
                set.idle = *idle;
            }
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
