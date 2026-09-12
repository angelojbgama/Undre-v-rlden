#include "game/content/content_json.h"
#include "engine/data/json.h"
#include <charconv>
#include <fstream>
#include <limits>
#include <sstream>
#include <type_traits>

namespace underworld::game::content {
namespace {
using engine::data::JsonArray; using engine::data::JsonObject; using engine::data::JsonNumber; using engine::data::JsonValue;
JsonValue S(std::string v){return {{},std::move(v)};} JsonValue N(std::string v){return {{},JsonNumber{std::move(v)}};}
JsonValue U(std::uint64_t v){return N(std::to_string(v));} JsonValue I(std::int64_t v){return N(std::to_string(v));}
JsonValue B(bool v){return {{},v};} JsonValue O(JsonObject v){return {{},std::move(v)};} JsonValue A(JsonArray v){return {{},std::move(v)};}
void p(JsonObject& o,std::string k,JsonValue v){o.emplace_back(std::move(k),std::move(v));}
std::string sid(const simulation::DefinitionId& v){return std::string(v.value());}
JsonValue id(const simulation::DefinitionId& v){return S(sid(v));}
JsonValue point(core::PointI v){JsonObject o;p(o,"x",I(v.x));p(o,"y",I(v.y));return O(std::move(o));}
JsonValue point(core::WorldPointI v){JsonObject o;p(o,"x",I(v.x));p(o,"y",I(v.y));return O(std::move(o));}
JsonValue box(world::AabbI v){JsonObject o;p(o,"x",I(v.x));p(o,"y",I(v.y));p(o,"width",I(v.width));p(o,"height",I(v.height));return O(std::move(o));}
JsonValue actor(const gameplay::creatures::ActorBoxDefinition& v){JsonObject o;p(o,"offsetX",I(v.offsetX));p(o,"offsetY",I(v.offsetY));p(o,"width",I(v.width));p(o,"height",I(v.height));return O(std::move(o));}
JsonValue damage(gameplay::DamageSpec v){JsonObject o;p(o,"amount",I(v.amount));p(o,"knockbackPixels",I(v.knockbackPixels));return O(std::move(o));}
JsonValue offsets(const gameplay::DirectionalOffsets& v){static const char* n[]={"down","up","left","right"};JsonObject o;for(int i=0;i<4;++i)p(o,n[i],point(v.values[i]));return O(std::move(o));}
JsonValue boxes(const gameplay::DirectionalBoxes& v){static const char* n[]={"down","up","left","right"};JsonObject o;for(int i=0;i<4;++i){JsonObject q;p(q,"offsetX",I(v.values[i].offsetX));p(q,"offsetY",I(v.values[i].offsetY));p(q,"width",I(v.values[i].width));p(q,"height",I(v.values[i].height));p(o,n[i],O(std::move(q)));}return O(std::move(o));}
template<class T> JsonValue strings(const std::vector<T>& v){JsonArray a;for(const auto& x:v)a.push_back(S(x));return A(std::move(a));}
template<class T,class F> JsonValue arrayOf(const std::vector<T>& v,F f){JsonArray a;for(const auto& x:v)a.push_back(f(x));return A(std::move(a));}
const char* facing(gameplay::FacingDirection v){switch(v){case gameplay::FacingDirection::down:return "down";case gameplay::FacingDirection::up:return "up";case gameplay::FacingDirection::left:return "left";case gameplay::FacingDirection::right:return "right";}return "?";}
const char* attackKind(gameplay::AttackKind v){return v==gameplay::AttackKind::meleeHitbox?"meleeHitbox":"projectile";}
const char* faction(gameplay::Faction v){switch(v){case gameplay::Faction::player:return "player";case gameplay::Faction::enemy:return "enemy";case gameplay::Faction::environment:return "environment";case gameplay::Faction::neutral:return "neutral";}return "?";}
const char* doorState(gameplay::DoorState v){switch(v){case gameplay::DoorState::locked:return "locked";case gameplay::DoorState::closed:return "closed";case gameplay::DoorState::open:return "open";}return "?";}
const char* activationMode(gameplay::ObjectActivationMode v){return v==gameplay::ObjectActivationMode::interactToggle?"interactToggle":"playerPressure";}
const char* itemCat(gameplay::ItemCategory v){switch(v){case gameplay::ItemCategory::consumable:return "consumable";case gameplay::ItemCategory::equipment:return "equipment";case gameplay::ItemCategory::key:return "key";case gameplay::ItemCategory::misc:return "misc";}return "?";}
const char* useKind(gameplay::ItemUseKind){return "restoreHealth";}
const char* timeline(gameplay::AttackTimelineEventKind v){switch(v){case gameplay::AttackTimelineEventKind::activateHitbox:return "activateHitbox";case gameplay::AttackTimelineEventKind::deactivateHitbox:return "deactivateHitbox";case gameplay::AttackTimelineEventKind::spawnProjectile:return "spawnProjectile";}return "?";}
const char* equip(AuthoredEquipmentSlot v){return v==AuthoredEquipmentSlot::armor?"armor":"accessory";}
const char* cond(gameplay::dialogue::DialogueConditionKind v){return v==gameplay::dialogue::DialogueConditionKind::flagSet?"flagSet":"flagNotSet";}
const char* action(gameplay::dialogue::DialogueActionKind v){switch(v){case gameplay::dialogue::DialogueActionKind::setFlag:return "setFlag";case gameplay::dialogue::DialogueActionKind::clearFlag:return "clearFlag";case gameplay::dialogue::DialogueActionKind::startQuest:return "startQuest";case gameplay::dialogue::DialogueActionKind::openShop:return "openShop";}return "?";}
const char* objective(gameplay::quests::QuestObjectiveKind v){switch(v){case gameplay::quests::QuestObjectiveKind::talk:return "talk";case gameplay::quests::QuestObjectiveKind::kill:return "kill";case gameplay::quests::QuestObjectiveKind::pickup:return "pickup";case gameplay::quests::QuestObjectiveKind::enter:return "enter";case gameplay::quests::QuestObjectiveKind::open:return "open";case gameplay::quests::QuestObjectiveKind::deliver:return "deliver";}return "?";}
const char* category(AuthoringCategory v){switch(v){case AuthoringCategory::enemy:return "enemy";case AuthoringCategory::object:return "object";case AuthoringCategory::pickup:return "pickup";case AuthoringCategory::npc:return "npc";case AuthoringCategory::player:return "player";case AuthoringCategory::item:return "item";case AuthoringCategory::rewardProfile:return "rewardProfile";case AuthoringCategory::rewardGrant:return "rewardGrant";case AuthoringCategory::shop:return "shop";}return "?";}
const char* role(authoring::TileRole v){switch(v){case authoring::TileRole::floor:return "floor";case authoring::TileRole::wall:return "wall";case authoring::TileRole::corner:return "corner";case authoring::TileRole::ledge:return "ledge";case authoring::TileRole::opening:return "opening";case authoring::TileRole::detail:return "detail";case authoring::TileRole::unknown:return "unknown";}return "?";}
const char* topology(authoring::TileTopology v){switch(v){case authoring::TileTopology::unknown:return "unknown";case authoring::TileTopology::interior:return "interior";case authoring::TileTopology::straightHorizontal:return "straightHorizontal";case authoring::TileTopology::straightVertical:return "straightVertical";case authoring::TileTopology::outerCorner:return "outerCorner";case authoring::TileTopology::innerCorner:return "innerCorner";case authoring::TileTopology::cap:return "cap";case authoring::TileTopology::junction:return "junction";case authoring::TileTopology::architecturalDetail:return "architecturalDetail";}return "?";}
const char* edge(authoring::EdgeProfile v){switch(v){case authoring::EdgeProfile::unknown:return "unknown";case authoring::EdgeProfile::floor:return "floor";case authoring::EdgeProfile::masonry:return "masonry";case authoring::EdgeProfile::voidEdge:return "voidEdge";case authoring::EdgeProfile::terminal:return "terminal";}return "?";}
const char* confidence(authoring::SemanticConfidence v){switch(v){case authoring::SemanticConfidence::confirmed:return "confirmed";case authoring::SemanticConfidence::probable:return "probable";case authoring::SemanticConfidence::unverified:return "unverified";}return "?";}
const char* effectLifetime(presentation::PresentationEffectLifetime v){return v==presentation::PresentationEffectLifetime::transient?"transient":"persistent";}
const char* overlayMode(presentation::PresentationOverlayMode v){switch(v){case presentation::PresentationOverlayMode::constant:return "constant";case presentation::PresentationOverlayMode::linearFadeOut:return "linearFadeOut";case presentation::PresentationOverlayMode::pulse:return "pulse";}return "?";}
const char* compositionLayer(presentation::PresentationCompositionLayer v){return v==presentation::PresentationCompositionLayer::world?"world":"final";}
JsonValue rgba(core::ColorRGBA8 v){JsonObject o;p(o,"r",U(v.r));p(o,"g",U(v.g));p(o,"b",U(v.b));p(o,"a",U(v.a));return O(std::move(o));}
JsonValue presentationEffect(const AuthoredPresentationEffect& v){JsonObject o;p(o,"id",id(v.id));p(o,"lifetime",S(effectLifetime(v.lifetime)));p(o,"durationTicks",U(v.durationTicks));p(o,"priority",I(v.priority));if(v.cameraShake){JsonObject q;p(q,"amplitudePixels",I(v.cameraShake->amplitudePixels));p(o,"cameraShake",O(std::move(q)));}if(v.overlay){JsonObject q;p(q,"color",rgba(v.overlay->color));p(q,"mode",S(overlayMode(v.overlay->mode)));p(q,"pulsePeriodTicks",U(v.overlay->pulsePeriodTicks));p(q,"layer",S(compositionLayer(v.overlay->layer)));p(o,"overlay",O(std::move(q)));}if(v.visionMask){JsonObject q;p(q,"innerRadiusPixels",I(v.visionMask->innerRadiusPixels));p(q,"outerRadiusPixels",I(v.visionMask->outerRadiusPixels));p(q,"outsideAlpha",U(v.visionMask->outsideAlpha));p(q,"color",rgba(v.visionMask->color));p(o,"visionMask",O(std::move(q)));}if(v.fade){JsonObject q;p(q,"color",rgba(v.fade->color));p(q,"startAlpha",U(v.fade->startAlpha));p(q,"endAlpha",U(v.fade->endAlpha));p(o,"fade",O(std::move(q)));}return O(std::move(o));}
JsonValue item(const AuthoredItem& v){JsonObject o;p(o,"id",id(v.id));p(o,"visualId",id(v.visualId));p(o,"category",S(itemCat(v.category)));p(o,"stackLimit",U(v.stackLimit));if(v.use){JsonObject q;p(q,"kind",S(useKind(v.use->kind)));p(q,"amount",I(v.use->amount));p(o,"use",O(std::move(q)));}if(v.equipment){JsonObject q,m;p(m,"maximumHealthBonus",I(v.equipment->modifiers.maximumHealthBonus));p(m,"playerAttackDamageBonus",I(v.equipment->modifiers.playerAttackDamageBonus));p(q,"slot",S(equip(v.equipment->slot)));p(q,"modifiers",O(std::move(m)));p(o,"equipment",O(std::move(q)));}return O(std::move(o));}
JsonValue projectile(const AuthoredProjectile& v){JsonObject o;p(o,"id",id(v.id));p(o,"visualId",id(v.visualId));p(o,"canonicalFacing",S(facing(v.canonicalFacing)));p(o,"speedPixelsPerTick",I(v.speedPixelsPerTick));p(o,"lifetimeTicks",U(v.lifetimeTicks));p(o,"hitboxWidth",I(v.hitboxWidth));p(o,"hitboxHeight",I(v.hitboxHeight));p(o,"spawnOffsets",offsets(v.spawnOffsets));return O(std::move(o));}
JsonValue attackShapeFrame(const AuthoredAttackShapeFrame& v){JsonObject o;p(o,"frameIndex",U(v.frameIndex));p(o,"tick",U(v.tick));p(o,"width",U(v.width));p(o,"height",U(v.height));p(o,"cells",arrayOf(v.cells,[](auto x){return U(x);}));return O(std::move(o));}
JsonValue attackShape(const AuthoredDirectionalAttackShape& v){JsonObject o;p(o,"facing",S(facing(v.facing)));p(o,"frames",arrayOf(v.frames,attackShapeFrame));return O(std::move(o));}
JsonValue attack(const AuthoredAttack& v){JsonObject o;p(o,"id",id(v.id));p(o,"kind",S(attackKind(v.kind)));p(o,"damage",damage(v.damage));p(o,"totalTicks",U(v.totalTicks));p(o,"cooldownTicks",U(v.cooldownTicks));p(o,"minimumRangePixels",I(v.minimumRangePixels));p(o,"maximumRangePixels",I(v.maximumRangePixels));p(o,"visualActionId",id(v.visualActionId));if(v.meleeHitboxes)p(o,"meleeHitboxes",boxes(*v.meleeHitboxes));if(v.projectileDefinitionId)p(o,"projectileDefinitionId",id(*v.projectileDefinitionId));p(o,"timeline",arrayOf(v.timeline,[](auto& x){JsonObject q;p(q,"tick",U(x.tick));p(q,"kind",S(timeline(x.kind)));return O(std::move(q));}));if(!v.shapes.empty())p(o,"shapes",arrayOf(v.shapes,attackShape));return O(std::move(o));}
JsonValue actorField(const gameplay::creatures::ActorBoxDefinition& v){return actor(v);}
JsonValue enemy(const AuthoredEnemy& v){JsonObject o;p(o,"id",id(v.id));p(o,"visualSetId",id(v.visualSetId));p(o,"behaviorProfileId",id(v.behaviorProfileId));p(o,"faction",S(faction(v.faction)));p(o,"maximumHealth",I(v.maximumHealth));p(o,"movementSpeedSubpixelsPerTick",I(v.movementSpeedSubpixelsPerTick));p(o,"collisionBody",actorField(v.collisionBody));p(o,"hurtbox",actorField(v.hurtbox));p(o,"attackIds",arrayOf(v.attackIds,[](auto& x){return id(x);}));if(v.rewardProfileId)p(o,"rewardProfileId",id(*v.rewardProfileId));return O(std::move(o));}
JsonValue objectCollision(const AuthoredObjectCollision& v){JsonObject o;p(o,"width",U(v.width));p(o,"height",U(v.height));p(o,"origin",point(v.origin));p(o,"cells",arrayOf(v.cells,[](auto x){return U(x);}));return O(std::move(o));}
JsonValue objectOcclusion(const gameplay::ObjectOcclusionDefinition& v){JsonObject o;p(o,"width",U(v.width));p(o,"height",U(v.height));p(o,"origin",point(v.origin));p(o,"cells",arrayOf(v.cells,[](auto x){return U(x);}));return O(std::move(o));}
JsonValue objectDef(const AuthoredWorldObject& v){JsonObject o;p(o,"id",id(v.id));p(o,"visualSetId",id(v.visualSetId));if(v.collision)p(o,"collision",objectCollision(*v.collision));p(o,"depthAnchor",point(v.depthAnchor));if(v.occlusion)p(o,"occlusion",objectOcclusion(*v.occlusion));if(v.interactable)p(o,"interactable",box(v.interactable->bounds));if(v.container){JsonObject q;p(q,"capacity",U(v.container->capacity));p(o,"container",O(std::move(q)));}if(v.destructible){JsonObject q;p(q,"maximumHealth",I(v.destructible->maximumHealth));p(q,"hurtbox",box(v.destructible->hurtbox));p(q,"destructionDurationTicks",U(v.destructible->destructionDurationTicks));p(q,"damageDurationTicks",U(v.destructible->damageDurationTicks));if(v.destructible->rewardProfileId)p(q,"rewardProfileId",id(*v.destructible->rewardProfileId));p(q,"leaveDestroyedResidue",B(v.destructible->leaveDestroyedResidue));p(o,"destructible",O(std::move(q)));}if(v.bankAccess)p(o,"bankAccess",O({}));if(v.door){JsonObject q;p(q,"initialState",S(doorState(v.door->initialState)));if(v.door->hasBlockingBounds)p(q,"blockingBounds",box(v.door->blockingBounds));p(o,"door",O(std::move(q)));}if(v.activation){JsonObject q;p(q,"mode",S(activationMode(v.activation->mode)));if(v.activation->mode==gameplay::ObjectActivationMode::interactToggle)p(q,"initialActive",B(v.activation->initialActive));if(v.activation->activationBounds)p(q,"bounds",box(*v.activation->activationBounds));p(o,"activation",O(std::move(q)));}return O(std::move(o));}
JsonValue pickup(const AuthoredPickup& v){JsonObject o;p(o,"id",id(v.id));p(o,"visualId",id(v.visualId));p(o,"collectionBounds",box(v.collectionBounds));JsonObject q;std::visit([&](auto& x){using T=std::decay_t<decltype(x)>;if constexpr(std::is_same_v<T,AuthoredHealthPickup>){p(q,"kind",S("health"));p(q,"amount",I(x.amount));}else if constexpr(std::is_same_v<T,AuthoredCurrencyPickup>){p(q,"kind",S("currency"));p(q,"amount",U(x.amount));}else{p(q,"kind",S("item"));p(q,"itemId",id(x.itemId));p(q,"quantity",U(x.quantity));}},v.payload);p(o,"payload",O(std::move(q)));return O(std::move(o));}
JsonValue dialogue(const AuthoredDialogue& v){JsonObject o;p(o,"id",id(v.id));p(o,"entryNodeId",id(v.entryNodeId));p(o,"nodes",arrayOf(v.nodes,[](auto& n){JsonObject q;p(q,"id",id(n.id));p(q,"speaker",S(n.speaker));p(q,"pages",strings(n.pages));p(q,"nextNodeId",id(n.nextNodeId));p(q,"choices",arrayOf(n.choices,[](auto& c){JsonObject z;p(z,"label",S(c.label));p(z,"targetNodeId",id(c.targetNodeId));p(z,"conditions",arrayOf(c.conditions,[](auto& x){JsonObject a;p(a,"kind",S(cond(x.kind)));p(a,"flagId",id(x.flagId));return O(std::move(a));}));p(z,"actions",arrayOf(c.actions,[](auto& x){JsonObject a;p(a,"kind",S(action(x.kind)));p(a,"targetId",id(x.targetId));return O(std::move(a));}));return O(std::move(z));}));return O(std::move(q));}));return O(std::move(o));}
JsonValue quest(const AuthoredQuest& v){JsonObject o;p(o,"id",id(v.id));p(o,"title",S(v.title));p(o,"objectives",arrayOf(v.objectives,[](auto& x){JsonObject q;p(q,"id",id(x.id));p(q,"kind",S(objective(x.kind)));p(q,"targetId",id(x.targetId));p(q,"requiredCount",U(x.requiredCount));p(q,"description",S(x.description));return O(std::move(q));}));p(o,"tags",strings(v.tags));if(v.rewardGrantId)p(o,"rewardGrantId",id(*v.rewardGrantId));return O(std::move(o));}
JsonValue shop(const AuthoredShop& v){JsonObject o;p(o,"id",id(v.id));p(o,"offers",arrayOf(v.offers,[](auto& x){JsonObject q;p(q,"itemId",id(x.itemId));if(x.playerBuyPrice)p(q,"playerBuyPrice",U(*x.playerBuyPrice));if(x.playerSellPrice)p(q,"playerSellPrice",U(*x.playerSellPrice));return O(std::move(q));}));return O(std::move(o));}
JsonValue tile(const AuthoredTileSemantic& v){JsonObject o;p(o,"id",id(v.id));p(o,"tilesetId",id(v.tilesetId));p(o,"sourceIndex",U(v.sourceIndex));p(o,"family",S(v.family));p(o,"role",S(role(v.role)));p(o,"topology",S(topology(v.topology)));p(o,"north",S(edge(v.north)));p(o,"east",S(edge(v.east)));p(o,"south",S(edge(v.south)));p(o,"west",S(edge(v.west)));p(o,"preferredLayer",S(v.preferredLayer));p(o,"flipXAllowed",B(v.flipXAllowed));p(o,"visualConfidence",S(confidence(v.visualConfidence)));p(o,"semanticConfidence",S(confidence(v.semanticConfidence)));p(o,"gameplayConfidence",S(confidence(v.gameplayConfidence)));p(o,"variantWeight",U(v.variantWeight));return O(std::move(o));}
JsonValue stamp(const AuthoredStamp& v){JsonObject o;p(o,"id",id(v.id));p(o,"displayName",S(v.displayName));p(o,"width",U(v.width));p(o,"height",U(v.height));p(o,"cells",arrayOf(v.cells,[](auto& x){JsonObject q;p(q,"x",I(x.x));p(q,"y",I(x.y));p(q,"tileId",id(x.tileId));return O(std::move(q));}));p(o,"anchor",point(v.anchor));p(o,"flipXAllowed",B(v.flipXAllowed));p(o,"atomic",B(v.atomic));p(o,"confidence",S(confidence(v.confidence)));return O(std::move(o));}
const char* visualRoot(presentation::VisualAssetRoot v){return v==presentation::VisualAssetRoot::gameAssets?"gameAssets":"contentWorkspace";}
JsonValue rect(core::RectI v){JsonObject o;p(o,"x",I(v.x));p(o,"y",I(v.y));p(o,"width",I(v.width));p(o,"height",I(v.height));return O(std::move(o));}
JsonValue directional(const presentation::DirectionalAnimationRef& v){JsonObject o;if(v.defaultAnimation)p(o,"default",id(*v.defaultAnimation));if(v.down)p(o,"down",id(*v.down));if(v.up)p(o,"up",id(*v.up));if(v.side)p(o,"side",id(*v.side));return O(std::move(o));}
JsonValue visualImage(const AuthoredVisualImage& v){JsonObject o;p(o,"id",id(v.id));p(o,"root",S(visualRoot(v.root)));p(o,"relativePath",S(v.relativePath));return O(std::move(o));}
JsonValue staticSprite(const AuthoredStaticSprite& v){JsonObject o;p(o,"id",id(v.id));p(o,"imageId",id(v.imageId));if(v.source)p(o,"source",rect(*v.source));p(o,"anchor",point(v.anchor));return O(std::move(o));}
JsonValue animation(const AuthoredAnimation& v){JsonObject o;p(o,"id",id(v.id));p(o,"imageId",id(v.imageId));p(o,"loop",B(v.loop));p(o,"frames",arrayOf(v.frames,[](const auto& f){JsonObject q;p(q,"source",rect(f.source));p(q,"anchor",point(f.anchor));p(q,"drawOffset",point(f.drawOffset));p(q,"durationTicks",U(f.durationTicks));p(q,"markers",strings(f.markers));if(f.flipX)p(q,"flipX",B(true));return O(std::move(q));}));return O(std::move(o));}
JsonValue playerMovementCollision(const AuthoredPlayerMovementCollision& v){JsonObject o;p(o,"down",objectCollision(v.down));p(o,"up",objectCollision(v.up));p(o,"side",objectCollision(v.side));return O(std::move(o));}
JsonValue playerDef(const AuthoredPlayer& v){JsonObject o;p(o,"id",id(v.id));p(o,"visualSetId",id(v.visualSetId));p(o,"progressionId",id(v.progressionId));if(v.movementCollision)p(o,"movementCollision",playerMovementCollision(*v.movementCollision));return O(std::move(o));}
JsonValue playerVisualAction(const AuthoredPlayerActionVisual& v){JsonObject o;p(o,"actionId",S(v.actionId));p(o,"clips",directional(v.clips));return O(std::move(o));}
JsonValue playerVisual(const AuthoredPlayerVisual& v){JsonObject o;p(o,"id",id(v.id));p(o,"idle",directional(v.idle));p(o,"walk",directional(v.walk));if(v.hurt)p(o,"hurt",directional(*v.hurt));p(o,"actions",arrayOf(v.actions,playerVisualAction));return O(std::move(o));}
JsonValue enemyVisual(const AuthoredEnemyVisual& v){JsonObject o;p(o,"id",id(v.id));p(o,"idle",directional(v.idle));if(v.move)p(o,"move",directional(*v.move));if(v.hurt)p(o,"hurt",directional(*v.hurt));if(v.death)p(o,"death",directional(*v.death));if(v.dead)p(o,"dead",directional(*v.dead));p(o,"actions",arrayOf(v.attacks,[](const auto& a){JsonObject q;p(q,"visualActionId",id(a.visualActionId));p(q,"clips",directional(a.clips));return O(std::move(q));}));return O(std::move(o));}
JsonValue objectVisual(const AuthoredWorldObjectVisual& v){JsonObject o;p(o,"id",id(v.id));p(o,"idleAnimationId",id(v.idleAnimationId));const auto add=[&](const char* n,const auto& x){if(x)p(o,n,id(*x));};add("openedAnimationId",v.openedAnimationId);add("destroyingAnimationId",v.destroyingAnimationId);add("activationInactiveAnimationId",v.activationInactiveAnimationId);add("activationActiveAnimationId",v.activationActiveAnimationId);add("doorLockedAnimationId",v.doorLockedAnimationId);add("doorClosedAnimationId",v.doorClosedAnimationId);add("doorOpenAnimationId",v.doorOpenAnimationId);add("destroyedAnimationId",v.destroyedAnimationId);add("damagedAnimationId",v.damagedAnimationId);return O(std::move(o));}
}

std::string encodeAuthoredContentJson(const AuthoredContentPack& c){JsonObject r;p(r,"format",S("dungeon-underworld-content"));p(r,"version",U(5));JsonArray a;for(auto& x:c.tilesets){JsonObject o;p(o,"id",id(x.id));p(o,"displayName",S(x.displayName));p(o,"relativeAssetPath",S(x.relativeAssetPath));p(o,"tileSize",U(x.tileSize));p(o,"columns",U(x.columns));p(o,"rows",U(x.rows));a.push_back(O(std::move(o)));}p(r,"tilesets",A(std::move(a)));p(r,"projectiles",arrayOf(c.projectiles,projectile));p(r,"attacks",arrayOf(c.attacks,attack));JsonArray b;for(auto& x:c.behaviors){JsonObject o;p(o,"id",id(x.id));p(o,"detectionRangePixels",I(x.detectionRangePixels));p(o,"disengageRangePixels",I(x.disengageRangePixels));p(o,"idleDurationTicks",U(x.idleDurationTicks));p(o,"wanderDurationTicks",U(x.wanderDurationTicks));b.push_back(O(std::move(o)));}p(r,"behaviors",A(std::move(b)));p(r,"enemies",arrayOf(c.enemies,enemy));p(r,"items",arrayOf(c.items,item));p(r,"objects",arrayOf(c.objects,objectDef));p(r,"pickups",arrayOf(c.pickups,pickup));JsonArray nv;for(auto& x:c.npcVisuals){JsonObject o,q;p(q,"r",U(x.markerColor.r));p(q,"g",U(x.markerColor.g));p(q,"b",U(x.markerColor.b));p(q,"a",U(x.markerColor.a));p(o,"id",id(x.id));p(o,"markerColor",O(std::move(q)));if(x.idle)p(o,"idle",directional(*x.idle));nv.push_back(O(std::move(o)));}p(r,"npcVisuals",A(std::move(nv)));JsonArray ns;for(auto& x:c.npcs){JsonObject o;p(o,"id",id(x.id));p(o,"visualSetId",id(x.visualSetId));p(o,"interaction",box(x.interaction.bounds));p(o,"interactionEnabled",B(x.interaction.enabled));p(o,"defaultDialogueId",id(x.defaultDialogueId));p(o,"tags",strings(x.tags));ns.push_back(O(std::move(o)));}p(r,"npcs",A(std::move(ns)));p(r,"dialogues",arrayOf(c.dialogues,dialogue));p(r,"quests",arrayOf(c.quests,quest));p(r,"players",arrayOf(c.players,playerDef));JsonArray pp;for(auto& x:c.playerProgressions){JsonObject o,q;p(q,"maximumHealth",I(x.baseStats.maximumHealth));p(o,"id",id(x.id));p(o,"baseStats",O(std::move(q)));p(o,"cumulativeExperienceThresholds",arrayOf(x.cumulativeExperienceThresholds,[](auto& n){return U(n);}));pp.push_back(O(std::move(o)));}p(r,"playerProgressions",A(std::move(pp)));JsonArray rp;for(auto& x:c.rewardProfiles){JsonObject o;p(o,"id",id(x.id));p(o,"experience",U(x.experience));p(o,"loot",arrayOf(x.loot,[](auto& z){JsonObject q;p(q,"pickupDefinitionId",id(z.pickupDefinitionId));p(q,"chanceBasisPoints",U(z.chanceBasisPoints));p(q,"minimumCount",U(z.minimumCount));p(q,"maximumCount",U(z.maximumCount));return O(std::move(q));}));rp.push_back(O(std::move(o)));}p(r,"rewardProfiles",A(std::move(rp)));JsonArray rg;for(auto& x:c.rewardGrants){JsonObject o;p(o,"id",id(x.id));p(o,"experience",U(x.experience));p(o,"gold",U(x.gold));p(o,"items",arrayOf(x.items,[](auto& z){JsonObject q;p(q,"itemId",id(z.itemId));p(q,"quantity",U(z.quantity));return O(std::move(q));}));rg.push_back(O(std::move(o)));}p(r,"rewardGrants",A(std::move(rg)));p(r,"shops",arrayOf(c.shops,shop));JsonArray ad;for(auto& x:c.authoringDescriptors){JsonObject o;p(o,"definitionId",id(x.definitionId));p(o,"displayName",S(x.displayName));p(o,"category",S(category(x.category)));p(o,"tags",strings(x.tags));ad.push_back(O(std::move(o)));}p(r,"authoringDescriptors",A(std::move(ad)));p(r,"tileSemantics",arrayOf(c.tileSemantics,tile));p(r,"stamps",arrayOf(c.stamps,stamp));p(r,"presentationEffects",arrayOf(c.presentationEffects,presentationEffect));p(r,"visualImages",arrayOf(c.visualImages,visualImage));p(r,"staticSprites",arrayOf(c.staticSprites,staticSprite));p(r,"animations",arrayOf(c.animations,animation));p(r,"enemyVisuals",arrayOf(c.enemyVisuals,enemyVisual));p(r,"objectVisuals",arrayOf(c.objectVisuals,objectVisual));p(r,"playerVisuals",arrayOf(c.playerVisuals,playerVisual));return engine::data::writeJson(O(std::move(r)));}

} // namespace underworld::game::content

namespace underworld::game::content {
ContentJsonDecodeResult readAuthoredContentJsonFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return {{}, {{0, 0, path.string(), "could not open content file"}}, {}};
    std::ostringstream text;
    text << file.rdbuf();
    return decodeAuthoredContentJson(text.str());
}

bool writeAuthoredContentJsonFile(const std::filesystem::path& path,
                                  const AuthoredContentPack& content,
                                  std::string& error) {
    auto temporary = path;
    temporary += ".tmp";
    auto backup = path;
    backup += ".bak";
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    if (!file) { error = "could not open content file for writing"; return false; }
    file << encodeAuthoredContentJson(content);
    file.flush();
    if (!file) {
        error = "could not write content file";
        file.close();
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return false;
    }
    file.close();
    std::error_code errorCode;
    std::filesystem::remove(backup, errorCode);
    if (errorCode) {
        error = "could not remove stale content backup";
        std::error_code cleanupError;
        std::filesystem::remove(temporary, cleanupError);
        return false;
    }
    errorCode.clear();
    const bool originalExists = std::filesystem::exists(path, errorCode);
    if (errorCode) {
        error = "could not inspect existing content file";
        std::error_code cleanupError;
        std::filesystem::remove(temporary, cleanupError);
        return false;
    }
    if (originalExists) {
        std::filesystem::rename(path, backup, errorCode);
        if (errorCode) {
            error = "could not preserve existing content file";
            std::error_code cleanupError;
            std::filesystem::remove(temporary, cleanupError);
            return false;
        }
    }
    errorCode.clear();
    std::filesystem::rename(temporary, path, errorCode);
    if (errorCode) {
        error = "could not replace content file";
        std::error_code restoreError;
        const bool backupExists = std::filesystem::exists(backup, restoreError);
        if (!restoreError && backupExists) {
            std::filesystem::rename(backup, path, restoreError);
        }
        if (restoreError) error += "; could not restore previous content file";
        std::error_code cleanupError;
        std::filesystem::remove(temporary, cleanupError);
        return false;
    }
    std::filesystem::remove(backup, errorCode);
    if (errorCode) {
        error = "could not remove content backup after save";
        return false;
    }
    error.clear();
    return true;
}
} // namespace underworld::game::content
