#include "game/content/content_json.h"
#include "engine/data/json.h"
#include <charconv>
#include <initializer_list>
#include <limits>
#include <string>
#include <utility>

namespace underworld::game::content { namespace {
using engine::data::JsonArray; using engine::data::JsonObject; using engine::data::JsonNumber; using engine::data::JsonValue;

struct Context final {
    std::vector<ContentJsonDiagnostic> diagnostics;
    std::vector<ContentJsonDefinitionOrigin> origins;
    std::uint64_t schemaVersion{1};
    void error(const JsonValue& v, std::string path, std::string message) {
        diagnostics.push_back({v.span.begin.line, v.span.begin.column, std::move(path), std::move(message)});
    }
};

const JsonValue* findField(const JsonObject& o, std::string_view n) {
    for (const auto& m : o) if (m.first == n) return &m.second;
    return nullptr;
}
bool isNull(const JsonValue* value) {
    return value != nullptr && std::holds_alternative<std::nullptr_t>(value->value);
}
std::string pathOf(std::string_view p, std::string_view n) {
    return p.empty() ? std::string(n) : std::string(p) + "." + std::string(n);
}
const JsonValue* required(const JsonValue& parent, const JsonObject& o, std::string_view n, std::string_view p, Context& c) {
    if (const auto* v = findField(o, n)) return v;
    c.error(parent, pathOf(p, n), "missing required field"); return nullptr;
}
void allowed(const JsonObject& o, std::initializer_list<std::string_view> names, std::string_view p, Context& c) {
    for (const auto& m : o) { bool known = false; for (auto n : names) if (m.first == n) { known = true; break; } if (!known) c.error(m.second, pathOf(p, m.first), "unknown field"); }
}
bool object(const JsonValue& v, std::string_view p, Context& c, const JsonObject*& o) {
    o = std::get_if<JsonObject>(&v.value); if (!o) c.error(v, std::string(p), "must be an object"); return o != nullptr;
}
bool array(const JsonValue& v, std::string_view p, Context& c, const JsonArray*& a) {
    a = std::get_if<JsonArray>(&v.value); if (!a) c.error(v, std::string(p), "must be an array"); return a != nullptr;
}
bool stringValue(const JsonValue& v, std::string_view p, Context& c, std::string& out) {
    if (const auto* s = std::get_if<std::string>(&v.value)) { out = *s; return true; }
    c.error(v, std::string(p), "must be a string"); return false;
}
bool u64(const JsonValue& v, std::string_view p, Context& c, std::uint64_t& out) {
    const auto* n = std::get_if<JsonNumber>(&v.value); if (!n) { c.error(v, std::string(p), "must be an unsigned integer"); return false; }
    const auto b = n->lexeme.data(); const auto e = b + n->lexeme.size(); const auto [end, ec] = std::from_chars(b, e, out);
    if (ec != std::errc{} || end != e) { c.error(v, std::string(p), "invalid unsigned integer"); return false; } return true;
}
bool i64(const JsonValue& v, std::string_view p, Context& c, std::int64_t& out) {
    const auto* n = std::get_if<JsonNumber>(&v.value); if (!n) { c.error(v, std::string(p), "must be a signed integer"); return false; }
    const auto b = n->lexeme.data(); const auto e = b + n->lexeme.size(); const auto [end, ec] = std::from_chars(b, e, out);
    if (ec != std::errc{} || end != e) { c.error(v, std::string(p), "invalid signed integer"); return false; } return true;
}
template<class T> bool unsignedField(const JsonValue& p, const JsonObject& o, std::string_view n, std::string_view path, Context& c, T& out) {
    const auto* v = required(p, o, n, path, c); if (!v) return false; std::uint64_t x{}; if (!u64(*v, pathOf(path, n), c, x)) return false;
    if (x > std::numeric_limits<T>::max()) { c.error(*v, pathOf(path, n), "outside field range"); return false; } out = static_cast<T>(x); return true;
}
template<class T> bool signedField(const JsonValue& p, const JsonObject& o, std::string_view n, std::string_view path, Context& c, T& out) {
    const auto* v = required(p, o, n, path, c); if (!v) return false; std::int64_t x{}; if (!i64(*v, pathOf(path, n), c, x)) return false;
    if (x < std::numeric_limits<T>::min() || x > std::numeric_limits<T>::max()) { c.error(*v, pathOf(path, n), "outside field range"); return false; } out = static_cast<T>(x); return true;
}
bool idField(const JsonValue& p, const JsonObject& o, std::string_view n, std::string_view path, Context& c, simulation::DefinitionId& out) {
    const auto* v = required(p, o, n, path, c); if (!v) return false; std::string s; if (!stringValue(*v, pathOf(path, n), c, s)) return false;
    out = s.empty() ? simulation::DefinitionId{} : simulation::DefinitionId{std::move(s)}; return true;
}
bool itemCategory(const JsonValue& v, std::string_view p, Context& c, gameplay::ItemCategory& out) {
    std::string s; if (!stringValue(v, p, c, s)) return false;
    if (s == "consumable") out = gameplay::ItemCategory::consumable; else if (s == "equipment") out = gameplay::ItemCategory::equipment;
    else if (s == "key") out = gameplay::ItemCategory::key; else if (s == "misc") out = gameplay::ItemCategory::misc;
    else { c.error(v, std::string(p), "unknown ItemCategory"); return false; } return true;
}
bool itemUseKind(const JsonValue& v, std::string_view p, Context& c, gameplay::ItemUseKind& out) {
    std::string s; if (!stringValue(v, p, c, s)) return false; if (s != "restoreHealth") { c.error(v, std::string(p), "unknown ItemUseKind"); return false; }
    out = gameplay::ItemUseKind::restoreHealth; return true;
}
bool equipmentSlot(const JsonValue& v, std::string_view p, Context& c, AuthoredEquipmentSlot& out) {
    std::string s; if (!stringValue(v, p, c, s)) return false; if (s == "armor") out = AuthoredEquipmentSlot::armor;
    else if (s == "accessory") out = AuthoredEquipmentSlot::accessory;
    else { c.error(v, std::string(p), "unknown AuthoredEquipmentSlot"); return false; }
    return true;
}
bool authoringCategory(const JsonValue& v, std::string_view p, Context& c, AuthoringCategory& out) {
    std::string s; if (!stringValue(v, p, c, s)) return false;
    if (s == "enemy") out = AuthoringCategory::enemy; else if (s == "object") out = AuthoringCategory::object; else if (s == "pickup") out = AuthoringCategory::pickup;
    else if (s == "npc") out = AuthoringCategory::npc; else if (s == "item") out = AuthoringCategory::item; else if (s == "rewardProfile") out = AuthoringCategory::rewardProfile;
    else if (s == "rewardGrant") out = AuthoringCategory::rewardGrant; else if (s == "shop") out = AuthoringCategory::shop;
    else { c.error(v, std::string(p), "unknown AuthoringCategory"); return false; } return true;
}
bool color(const JsonValue& v, std::string_view p, Context& c, core::ColorRGBA8& out) {
    const JsonObject* o = nullptr; if (!object(v, p, c, o)) return false; allowed(*o, {"r","g","b","a"}, p, c);
    return unsignedField(v,*o,"r",p,c,out.r) && unsignedField(v,*o,"g",p,c,out.g) && unsignedField(v,*o,"b",p,c,out.b) && unsignedField(v,*o,"a",p,c,out.a);
}
bool itemUse(const JsonValue& v, std::string_view p, Context& c, gameplay::ItemUseDefinition& out) {
    const JsonObject* o = nullptr; if (!object(v,p,c,o)) return false; allowed(*o,{"kind","amount"},p,c);
    const auto* k = required(v,*o,"kind",p,c); gameplay::ItemUseKind kind{}; int amount{};
    const bool ko = k && itemUseKind(*k,pathOf(p,"kind"),c,kind); const bool ao = signedField(v,*o,"amount",p,c,amount);
    if (ko && ao) out = gameplay::ItemUseDefinition{kind, static_cast<int>(amount)};
    return ko && ao;
}
bool equipment(const JsonValue& v, std::string_view p, Context& c, AuthoredEquipment& out) {
    const JsonObject* o = nullptr; if (!object(v,p,c,o)) return false; allowed(*o,{"slot","modifiers"},p,c);
    const auto* s = required(v,*o,"slot",p,c); const auto* m = required(v,*o,"modifiers",p,c); AuthoredEquipment decoded{}; bool ok = s && equipmentSlot(*s,pathOf(p,"slot"),c,decoded.slot);
    const JsonObject* mo = nullptr; if (!m || !object(*m,pathOf(p,"modifiers"),c,mo)) ok = false; else { const auto mp=pathOf(p,"modifiers"); allowed(*mo,{"maximumHealthBonus","playerAttackDamageBonus"},mp,c); ok=signedField(*m,*mo,"maximumHealthBonus",mp,c,decoded.modifiers.maximumHealthBonus)&&ok; ok=signedField(*m,*mo,"playerAttackDamageBonus",mp,c,decoded.modifiers.playerAttackDamageBonus)&&ok; }
    if (ok) out=decoded;
    return ok;
}
bool item(const JsonValue& v, std::string_view p, Context& c, AuthoredItem& out) {
    const JsonObject* o=nullptr; if(!object(v,p,c,o)) return false; allowed(*o,{"id","visualId","category","stackLimit","use","equipment"},p,c); AuthoredItem d{}; bool ok=idField(v,*o,"id",p,c,d.id);
    ok=idField(v,*o,"visualId",p,c,d.visualId)&&ok; const auto* cat=required(v,*o,"category",p,c); ok=cat&&itemCategory(*cat,pathOf(p,"category"),c,d.category)&&ok; ok=unsignedField(v,*o,"stackLimit",p,c,d.stackLimit)&&ok;
    if(const auto* u=findField(*o,"use");u&&!std::holds_alternative<std::nullptr_t>(u->value)){gameplay::ItemUseDefinition x{};if(itemUse(*u,pathOf(p,"use"),c,x))d.use=x;else ok=false;}
    if(const auto* e=findField(*o,"equipment");e&&!std::holds_alternative<std::nullptr_t>(e->value)){AuthoredEquipment x{};if(equipment(*e,pathOf(p,"equipment"),c,x))d.equipment=x;else ok=false;}
    if(ok)out=std::move(d);
    return ok;
}
bool tileset(const JsonValue&v,std::string_view p,Context&c,AuthoredTileset&out){const JsonObject*o=nullptr;if(!object(v,p,c,o))return false;allowed(*o,{"id","displayName","relativeAssetPath","tileSize","columns","rows"},p,c);AuthoredTileset d{};bool ok=idField(v,*o,"id",p,c,d.id);const auto*a=required(v,*o,"displayName",p,c);const auto*b=required(v,*o,"relativeAssetPath",p,c);ok=a&&stringValue(*a,pathOf(p,"displayName"),c,d.displayName)&&ok;ok=b&&stringValue(*b,pathOf(p,"relativeAssetPath"),c,d.relativeAssetPath)&&ok;ok=unsignedField(v,*o,"tileSize",p,c,d.tileSize)&&ok;ok=unsignedField(v,*o,"columns",p,c,d.columns)&&ok;ok=unsignedField(v,*o,"rows",p,c,d.rows)&&ok;if(ok)out=std::move(d);return ok;}
bool behavior(const JsonValue&v,std::string_view p,Context&c,AuthoredBehaviorProfile&out){const JsonObject*o=nullptr;if(!object(v,p,c,o))return false;allowed(*o,{"id","detectionRangePixels","disengageRangePixels","idleDurationTicks","wanderDurationTicks"},p,c);AuthoredBehaviorProfile d{};bool ok=idField(v,*o,"id",p,c,d.id);ok=signedField(v,*o,"detectionRangePixels",p,c,d.detectionRangePixels)&&ok;ok=signedField(v,*o,"disengageRangePixels",p,c,d.disengageRangePixels)&&ok;ok=unsignedField(v,*o,"idleDurationTicks",p,c,d.idleDurationTicks)&&ok;ok=unsignedField(v,*o,"wanderDurationTicks",p,c,d.wanderDurationTicks)&&ok;if(ok)out=std::move(d);return ok;}
bool directional(const JsonValue&,std::string_view,Context&,presentation::DirectionalAnimationRef&);
bool npcVisual(const JsonValue&v,std::string_view p,Context&c,AuthoredNpcVisualSet&out){const JsonObject*o=nullptr;if(!object(v,p,c,o))return false;allowed(*o,{"id","markerColor","idle"},p,c);AuthoredNpcVisualSet d{};bool ok=idField(v,*o,"id",p,c,d.id);const auto*m=required(v,*o,"markerColor",p,c);ok=m&&color(*m,pathOf(p,"markerColor"),c,d.markerColor)&&ok;if(const auto*x=findField(*o,"idle");x&&!isNull(x)){presentation::DirectionalAnimationRef refs{};if(directional(*x,pathOf(p,"idle"),c,refs))d.idle=refs;else ok=false;}if(ok)out=std::move(d);return ok;}
bool progression(const JsonValue&v,std::string_view p,Context&c,AuthoredPlayerProgression&out){const JsonObject*o=nullptr;if(!object(v,p,c,o))return false;allowed(*o,{"id","baseStats","cumulativeExperienceThresholds"},p,c);AuthoredPlayerProgression d{};bool ok=idField(v,*o,"id",p,c,d.id);const auto*s=required(v,*o,"baseStats",p,c);const auto*t=required(v,*o,"cumulativeExperienceThresholds",p,c);const JsonObject*so=nullptr;if(!s||!object(*s,pathOf(p,"baseStats"),c,so))ok=false;else{auto sp=pathOf(p,"baseStats");allowed(*so,{"maximumHealth"},sp,c);ok=signedField(*s,*so,"maximumHealth",sp,c,d.baseStats.maximumHealth)&&ok;}const JsonArray*ta=nullptr;if(!t||!array(*t,pathOf(p,"cumulativeExperienceThresholds"),c,ta))ok=false;else for(size_t i=0;i<ta->size();++i){uint64_t n{};if(u64((*ta)[i],pathOf(p,"cumulativeExperienceThresholds")+"["+std::to_string(i)+"]",c,n))d.cumulativeExperienceThresholds.push_back(n);else ok=false;}if(ok)out=std::move(d);return ok;}
bool loot(const JsonValue&v,std::string_view p,Context&c,AuthoredLootEntry&out){const JsonObject*o=nullptr;if(!object(v,p,c,o))return false;allowed(*o,{"pickupDefinitionId","chanceBasisPoints","minimumCount","maximumCount"},p,c);AuthoredLootEntry d{};bool ok=idField(v,*o,"pickupDefinitionId",p,c,d.pickupDefinitionId);ok=unsignedField(v,*o,"chanceBasisPoints",p,c,d.chanceBasisPoints)&&ok;ok=unsignedField(v,*o,"minimumCount",p,c,d.minimumCount)&&ok;ok=unsignedField(v,*o,"maximumCount",p,c,d.maximumCount)&&ok;if(ok)out=std::move(d);return ok;}
bool rewardProfile(const JsonValue&v,std::string_view p,Context&c,AuthoredRewardProfile&out){const JsonObject*o=nullptr;if(!object(v,p,c,o))return false;allowed(*o,{"id","experience","loot"},p,c);AuthoredRewardProfile d{};bool ok=idField(v,*o,"id",p,c,d.id);ok=unsignedField(v,*o,"experience",p,c,d.experience)&&ok;const auto*l=required(v,*o,"loot",p,c);const JsonArray*a=nullptr;if(!l||!array(*l,pathOf(p,"loot"),c,a))ok=false;else for(size_t i=0;i<a->size();++i){AuthoredLootEntry x{};if(loot((*a)[i],pathOf(p,"loot")+"["+std::to_string(i)+"]",c,x))d.loot.push_back(std::move(x));else ok=false;}if(ok)out=std::move(d);return ok;}
bool rewardItem(const JsonValue&v,std::string_view p,Context&c,AuthoredRewardItemGrant&out){const JsonObject*o=nullptr;if(!object(v,p,c,o))return false;allowed(*o,{"itemId","quantity"},p,c);AuthoredRewardItemGrant d{};bool ok=idField(v,*o,"itemId",p,c,d.itemId);ok=unsignedField(v,*o,"quantity",p,c,d.quantity)&&ok;if(ok)out=std::move(d);return ok;}
bool rewardGrant(const JsonValue&v,std::string_view p,Context&c,AuthoredRewardGrant&out){const JsonObject*o=nullptr;if(!object(v,p,c,o))return false;allowed(*o,{"id","experience","gold","items"},p,c);AuthoredRewardGrant d{};bool ok=idField(v,*o,"id",p,c,d.id);ok=unsignedField(v,*o,"experience",p,c,d.experience)&&ok;ok=unsignedField(v,*o,"gold",p,c,d.gold)&&ok;const auto*i=required(v,*o,"items",p,c);const JsonArray*a=nullptr;if(!i||!array(*i,pathOf(p,"items"),c,a))ok=false;else for(size_t n=0;n<a->size();++n){AuthoredRewardItemGrant x{};if(rewardItem((*a)[n],pathOf(p,"items")+"["+std::to_string(n)+"]",c,x))d.items.push_back(std::move(x));else ok=false;}if(ok)out=std::move(d);return ok;}
bool shopOffer(const JsonValue&v,std::string_view p,Context&c,AuthoredShopOffer&out){const JsonObject*o=nullptr;if(!object(v,p,c,o))return false;allowed(*o,{"itemId","playerBuyPrice","playerSellPrice"},p,c);AuthoredShopOffer d{};bool ok=idField(v,*o,"itemId",p,c,d.itemId);for(auto n:{"playerBuyPrice","playerSellPrice"}){const auto*x=findField(*o,n);if(!x||std::holds_alternative<std::nullptr_t>(x->value))continue;uint64_t q{};if(!u64(*x,pathOf(p,n),c,q))ok=false;else if(std::string_view(n)=="playerBuyPrice")d.playerBuyPrice=q;else d.playerSellPrice=q;}if(ok)out=std::move(d);return ok;}
bool shop(const JsonValue&v,std::string_view p,Context&c,AuthoredShop&out){const JsonObject*o=nullptr;if(!object(v,p,c,o))return false;allowed(*o,{"id","offers"},p,c);AuthoredShop d{};bool ok=idField(v,*o,"id",p,c,d.id);const auto*f=required(v,*o,"offers",p,c);const JsonArray*a=nullptr;if(!f||!array(*f,pathOf(p,"offers"),c,a))ok=false;else for(size_t i=0;i<a->size();++i){AuthoredShopOffer x{};if(shopOffer((*a)[i],pathOf(p,"offers")+"["+std::to_string(i)+"]",c,x))d.offers.push_back(std::move(x));else ok=false;}if(ok)out=std::move(d);return ok;}
bool descriptor(const JsonValue&v,std::string_view p,Context&c,AuthoringDescriptor&out){const JsonObject*o=nullptr;if(!object(v,p,c,o))return false;allowed(*o,{"definitionId","displayName","category","tags"},p,c);AuthoringDescriptor d{};bool ok=idField(v,*o,"definitionId",p,c,d.definitionId);const auto*n=required(v,*o,"displayName",p,c);const auto*k=required(v,*o,"category",p,c);const auto*t=required(v,*o,"tags",p,c);ok=n&&stringValue(*n,pathOf(p,"displayName"),c,d.displayName)&&ok;ok=k&&authoringCategory(*k,pathOf(p,"category"),c,d.category)&&ok;const JsonArray*a=nullptr;if(!t||!array(*t,pathOf(p,"tags"),c,a))ok=false;else for(size_t i=0;i<a->size();++i){std::string s;if(stringValue((*a)[i],pathOf(p,"tags")+"["+std::to_string(i)+"]",c,s))d.tags.push_back(std::move(s));else ok=false;}if(ok)out=std::move(d);return ok;}
bool boolValue(const JsonValue&v,std::string_view p,Context&c,bool&out){if(const auto*b=std::get_if<bool>(&v.value)){out=*b;return true;}c.error(v,std::string(p),"must be a boolean");return false;}
bool facing(const JsonValue&v,std::string_view p,Context&c,gameplay::FacingDirection&out){std::string s;if(!stringValue(v,p,c,s))return false;if(s=="down")out=gameplay::FacingDirection::down;else if(s=="up")out=gameplay::FacingDirection::up;else if(s=="left")out=gameplay::FacingDirection::left;else if(s=="right")out=gameplay::FacingDirection::right;else{c.error(v,std::string(p),"unknown FacingDirection");return false;}return true;}
bool doorState(const JsonValue&v,std::string_view p,Context&c,gameplay::DoorState&out){std::string s;if(!stringValue(v,p,c,s))return false;if(s=="locked")out=gameplay::DoorState::locked;else if(s=="closed")out=gameplay::DoorState::closed;else if(s=="open")out=gameplay::DoorState::open;else{c.error(v,std::string(p),"unknown DoorState");return false;}return true;}
bool attackKind(const JsonValue&v,std::string_view p,Context&c,gameplay::AttackKind&out){std::string s;if(!stringValue(v,p,c,s))return false;if(s=="meleeHitbox")out=gameplay::AttackKind::meleeHitbox;else if(s=="projectile")out=gameplay::AttackKind::projectile;else{c.error(v,std::string(p),"unknown AttackKind");return false;}return true;}
bool faction(const JsonValue&v,std::string_view p,Context&c,gameplay::Faction&out){std::string s;if(!stringValue(v,p,c,s))return false;if(s=="player")out=gameplay::Faction::player;else if(s=="enemy")out=gameplay::Faction::enemy;else if(s=="environment")out=gameplay::Faction::environment;else if(s=="neutral")out=gameplay::Faction::neutral;else{c.error(v,std::string(p),"unknown Faction");return false;}return true;}
bool timelineKind(const JsonValue&v,std::string_view p,Context&c,gameplay::AttackTimelineEventKind&out){std::string s;if(!stringValue(v,p,c,s))return false;if(s=="activateHitbox")out=gameplay::AttackTimelineEventKind::activateHitbox;else if(s=="deactivateHitbox")out=gameplay::AttackTimelineEventKind::deactivateHitbox;else if(s=="spawnProjectile")out=gameplay::AttackTimelineEventKind::spawnProjectile;else{c.error(v,std::string(p),"unknown AttackTimelineEventKind");return false;}return true;}
bool point(const JsonValue&v,std::string_view p,Context&c,core::WorldPointI&out){const JsonObject*o=nullptr;if(!object(v,p,c,o))return false;allowed(*o,{"x","y"},p,c);bool ok=signedField(v,*o,"x",p,c,out.x);ok=signedField(v,*o,"y",p,c,out.y)&&ok;return ok;}
bool aabb(const JsonValue&v,std::string_view p,Context&c,world::AabbI&out){const JsonObject*o=nullptr;if(!object(v,p,c,o))return false;allowed(*o,{"x","y","width","height"},p,c);bool ok=signedField(v,*o,"x",p,c,out.x);ok=signedField(v,*o,"y",p,c,out.y)&&ok;ok=signedField(v,*o,"width",p,c,out.width)&&ok;ok=signedField(v,*o,"height",p,c,out.height)&&ok;return ok;}
bool actorBox(const JsonValue&v,std::string_view p,Context&c,gameplay::creatures::ActorBoxDefinition&out){const JsonObject*o=nullptr;if(!object(v,p,c,o))return false;allowed(*o,{"offsetX","offsetY","width","height"},p,c);bool ok=signedField(v,*o,"offsetX",p,c,out.offsetX);ok=signedField(v,*o,"offsetY",p,c,out.offsetY)&&ok;ok=signedField(v,*o,"width",p,c,out.width)&&ok;ok=signedField(v,*o,"height",p,c,out.height)&&ok;return ok;}
bool offsets(const JsonValue&v,std::string_view p,Context&c,gameplay::DirectionalOffsets&out){const JsonObject*o=nullptr;if(!object(v,p,c,o))return false;allowed(*o,{"down","up","left","right"},p,c);bool ok=true;for(size_t i=0;i<4;++i){const char*names[]={"down","up","left","right"};const auto*x=required(v,*o,names[i],p,c);if(x)ok=point(*x,pathOf(p,names[i]),c,out.values[i])&&ok;else ok=false;}return ok;}
bool boxes(const JsonValue&v,std::string_view p,Context&c,gameplay::DirectionalBoxes&out){const JsonObject*o=nullptr;if(!object(v,p,c,o))return false;allowed(*o,{"down","up","left","right"},p,c);bool ok=true;for(size_t i=0;i<4;++i){const char*names[]={"down","up","left","right"};const auto*x=required(v,*o,names[i],p,c);if(!x){ok=false;continue;}const JsonObject*b=nullptr;if(!object(*x,pathOf(p,names[i]),c,b)){ok=false;continue;}const auto bp=pathOf(p,names[i]);allowed(*b,{"offsetX","offsetY","width","height"},bp,c);ok=signedField(*x,*b,"offsetX",bp,c,out.values[i].offsetX)&&ok;ok=signedField(*x,*b,"offsetY",bp,c,out.values[i].offsetY)&&ok;ok=signedField(*x,*b,"width",bp,c,out.values[i].width)&&ok;ok=signedField(*x,*b,"height",bp,c,out.values[i].height)&&ok;}return ok;}
bool damage(const JsonValue&v,std::string_view p,Context&c,gameplay::DamageSpec&out){const JsonObject*o=nullptr;if(!object(v,p,c,o))return false;allowed(*o,{"amount","knockbackPixels"},p,c);bool ok=signedField(v,*o,"amount",p,c,out.amount);ok=signedField(v,*o,"knockbackPixels",p,c,out.knockbackPixels)&&ok;return ok;}
bool projectile(const JsonValue&v,std::string_view p,Context&c,AuthoredProjectile&out){const JsonObject*o=nullptr;if(!object(v,p,c,o))return false;allowed(*o,{"id","visualId","canonicalFacing","speedPixelsPerTick","lifetimeTicks","hitboxWidth","hitboxHeight","spawnOffsets"},p,c);AuthoredProjectile d{};bool ok=idField(v,*o,"id",p,c,d.id);ok=idField(v,*o,"visualId",p,c,d.visualId)&&ok;const auto*f=required(v,*o,"canonicalFacing",p,c);const auto*s=required(v,*o,"spawnOffsets",p,c);ok=f&&facing(*f,pathOf(p,"canonicalFacing"),c,d.canonicalFacing)&&ok;ok=s&&offsets(*s,pathOf(p,"spawnOffsets"),c,d.spawnOffsets)&&ok;ok=signedField(v,*o,"speedPixelsPerTick",p,c,d.speedPixelsPerTick)&&ok;ok=unsignedField(v,*o,"lifetimeTicks",p,c,d.lifetimeTicks)&&ok;ok=signedField(v,*o,"hitboxWidth",p,c,d.hitboxWidth)&&ok;ok=signedField(v,*o,"hitboxHeight",p,c,d.hitboxHeight)&&ok;if(ok)out=std::move(d);return ok;}
bool attackEvent(const JsonValue&v,std::string_view p,Context&c,gameplay::AttackTimelineEvent&out){const JsonObject*o=nullptr;if(!object(v,p,c,o))return false;allowed(*o,{"tick","kind"},p,c);gameplay::AttackTimelineEvent d{};bool ok=unsignedField(v,*o,"tick",p,c,d.tick);const auto*k=required(v,*o,"kind",p,c);ok=k&&timelineKind(*k,pathOf(p,"kind"),c,d.kind)&&ok;if(ok)out=d;return ok;}
bool uint8Array(const JsonValue&v,std::string_view p,Context&c,std::vector<std::uint8_t>&out){const JsonArray*a=nullptr;if(!array(v,p,c,a))return false;bool ok=true;for(std::size_t i=0;i<a->size();++i){std::uint64_t n{};if(!u64((*a)[i],pathOf(p,"["+std::to_string(i)+"]"),c,n)||n>1){if(n>1)c.error((*a)[i],pathOf(p,"["+std::to_string(i)+"]"),"attack mask cells must be 0 or 1");ok=false;}else out.push_back(static_cast<std::uint8_t>(n));}return ok;}
bool attackShapeFrame(const JsonValue&v,std::string_view p,Context&c,AuthoredAttackShapeFrame&out){const JsonObject*o=nullptr;if(!object(v,p,c,o))return false;allowed(*o,{"frameIndex","tick","width","height","cells"},p,c);AuthoredAttackShapeFrame d{};bool ok=unsignedField(v,*o,"frameIndex",p,c,d.frameIndex);ok=unsignedField(v,*o,"tick",p,c,d.tick)&&ok;ok=unsignedField(v,*o,"width",p,c,d.width)&&ok;ok=unsignedField(v,*o,"height",p,c,d.height)&&ok;const auto*cells=required(v,*o,"cells",p,c);if(!cells||!uint8Array(*cells,pathOf(p,"cells"),c,d.cells))ok=false;if(d.width==0||d.height==0){if(cells)c.error(*cells,pathOf(p,"cells"),"attack mask dimensions must be positive");ok=false;}else if(d.width>std::numeric_limits<std::size_t>::max()/d.height||d.cells.size()!=static_cast<std::size_t>(d.width)*d.height){if(cells)c.error(*cells,pathOf(p,"cells"),"attack mask cell count must match width * height");ok=false;}if(ok)out=std::move(d);return ok;}
bool attackShape(const JsonValue&v,std::string_view p,Context&c,AuthoredDirectionalAttackShape&out){const JsonObject*o=nullptr;if(!object(v,p,c,o))return false;allowed(*o,{"facing","frames"},p,c);AuthoredDirectionalAttackShape d{};const auto*f=required(v,*o,"facing",p,c);const auto*frames=required(v,*o,"frames",p,c);bool ok=f&&facing(*f,pathOf(p,"facing"),c,d.facing);const JsonArray*a=nullptr;if(!frames||!array(*frames,pathOf(p,"frames"),c,a))ok=false;else for(std::size_t i=0;i<a->size();++i){AuthoredAttackShapeFrame frame{};if(attackShapeFrame((*a)[i],pathOf(p,"frames")+"["+std::to_string(i)+"]",c,frame))d.frames.push_back(std::move(frame));else ok=false;}if(ok)out=std::move(d);return ok;}
bool attack(const JsonValue&v,std::string_view p,Context&c,AuthoredAttack&out){const JsonObject*o=nullptr;if(!object(v,p,c,o))return false;allowed(*o,{"id","kind","damage","totalTicks","cooldownTicks","minimumRangePixels","maximumRangePixels","visualActionId","meleeHitboxes","projectileDefinitionId","timeline","shapes"},p,c);AuthoredAttack d{};bool ok=idField(v,*o,"id",p,c,d.id);const auto*k=required(v,*o,"kind",p,c);const auto*dm=required(v,*o,"damage",p,c);ok=k&&attackKind(*k,pathOf(p,"kind"),c,d.kind)&&ok;ok=dm&&damage(*dm,pathOf(p,"damage"),c,d.damage)&&ok;ok=unsignedField(v,*o,"totalTicks",p,c,d.totalTicks)&&ok;ok=unsignedField(v,*o,"cooldownTicks",p,c,d.cooldownTicks)&&ok;ok=signedField(v,*o,"minimumRangePixels",p,c,d.minimumRangePixels)&&ok;ok=signedField(v,*o,"maximumRangePixels",p,c,d.maximumRangePixels)&&ok;ok=idField(v,*o,"visualActionId",p,c,d.visualActionId)&&ok;const auto*t=required(v,*o,"timeline",p,c);const JsonArray*ta=nullptr;if(!t||!array(*t,pathOf(p,"timeline"),c,ta))ok=false;else for(std::size_t i=0;i<ta->size();++i){gameplay::AttackTimelineEvent e{};if(attackEvent((*ta)[i],pathOf(p,"timeline")+"["+std::to_string(i)+"]",c,e))d.timeline.push_back(e);else ok=false;}if(const auto*m=findField(*o,"meleeHitboxes");m&&!std::holds_alternative<std::nullptr_t>(m->value)){gameplay::DirectionalBoxes b{};if(boxes(*m,pathOf(p,"meleeHitboxes"),c,b))d.meleeHitboxes=b;else ok=false;}if(const auto*pr=findField(*o,"projectileDefinitionId");pr){std::string s;if(!stringValue(*pr,pathOf(p,"projectileDefinitionId"),c,s))ok=false;else d.projectileDefinitionId=simulation::DefinitionId{std::move(s)};}if(const auto*sh=findField(*o,"shapes");sh&&!isNull(sh)){const JsonArray*a=nullptr;if(!array(*sh,pathOf(p,"shapes"),c,a))ok=false;else for(std::size_t i=0;i<a->size();++i){AuthoredDirectionalAttackShape shape{};if(attackShape((*a)[i],pathOf(p,"shapes")+"["+std::to_string(i)+"]",c,shape))d.shapes.push_back(std::move(shape));else ok=false;}}if(ok)out=std::move(d);return ok;}
bool enemy(const JsonValue&v,std::string_view p,Context&c,AuthoredEnemy&out){const JsonObject*o=nullptr;if(!object(v,p,c,o))return false;allowed(*o,{"id","visualSetId","behaviorProfileId","faction","maximumHealth","movementSpeedSubpixelsPerTick","collisionBody","hurtbox","attackIds","rewardProfileId"},p,c);AuthoredEnemy d{};bool ok=idField(v,*o,"id",p,c,d.id);ok=idField(v,*o,"visualSetId",p,c,d.visualSetId)&&ok;ok=idField(v,*o,"behaviorProfileId",p,c,d.behaviorProfileId)&&ok;const auto*f=required(v,*o,"faction",p,c);ok=f&&faction(*f,pathOf(p,"faction"),c,d.faction)&&ok;ok=signedField(v,*o,"maximumHealth",p,c,d.maximumHealth)&&ok;ok=signedField(v,*o,"movementSpeedSubpixelsPerTick",p,c,d.movementSpeedSubpixelsPerTick)&&ok;const auto*cb=required(v,*o,"collisionBody",p,c);const auto*hb=required(v,*o,"hurtbox",p,c);ok=cb&&actorBox(*cb,pathOf(p,"collisionBody"),c,d.collisionBody)&&ok;ok=hb&&actorBox(*hb,pathOf(p,"hurtbox"),c,d.hurtbox)&&ok;const auto*ids=required(v,*o,"attackIds",p,c);const JsonArray*a=nullptr;if(!ids||!array(*ids,pathOf(p,"attackIds"),c,a))ok=false;else for(size_t i=0;i<a->size();++i){std::string s;if(stringValue((*a)[i],pathOf(p,"attackIds")+"["+std::to_string(i)+"]",c,s))d.attackIds.emplace_back(std::move(s));else ok=false;}if(const auto*r=findField(*o,"rewardProfileId");r&&!std::holds_alternative<std::nullptr_t>(r->value)){std::string s;if(stringValue(*r,pathOf(p,"rewardProfileId"),c,s))d.rewardProfileId=simulation::DefinitionId{std::move(s)};else ok=false;}if(ok)out=std::move(d);return ok;}
bool worldObject(const JsonValue& v, std::string_view p, Context& c, AuthoredWorldObject& out) {
    const JsonObject* o = nullptr;
    if (!object(v, p, c, o)) return false;
    allowed(*o, {"id", "visualSetId", "interactable", "container", "destructible", "bankAccess", "door", "activation"}, p, c);
    AuthoredWorldObject d{};
    bool ok = idField(v, *o, "id", p, c, d.id);
    ok = idField(v, *o, "visualSetId", p, c, d.visualSetId) && ok;
    if (const auto* x = findField(*o, "interactable"); x && !std::holds_alternative<std::nullptr_t>(x->value)) {
        world::AabbI bounds{};
        if (aabb(*x, pathOf(p, "interactable"), c, bounds)) d.interactable = gameplay::ObjectInteractionDefinition{bounds};
        else ok = false;
    }
    if (const auto* x = findField(*o, "container"); x && !std::holds_alternative<std::nullptr_t>(x->value)) {
        const JsonObject* q = nullptr;
        const auto containerPath = pathOf(p, "container");
        if (!object(*x, containerPath, c, q)) ok = false;
        else {
            allowed(*q, {"capacity"}, containerPath, c);
            const auto* capacity = required(*x, *q, "capacity", containerPath, c);
            std::uint64_t number{};
            if (!capacity || !u64(*capacity, pathOf(containerPath, "capacity"), c, number)) ok = false;
            else if (number > std::numeric_limits<std::size_t>::max()) {
                c.error(*capacity, pathOf(containerPath, "capacity"), "outside field range");
                ok = false;
            } else d.container = gameplay::ObjectContainerDefinition{static_cast<std::size_t>(number)};
        }
    }
    if (const auto* x = findField(*o, "destructible"); x && !std::holds_alternative<std::nullptr_t>(x->value)) {
        const JsonObject* q = nullptr;
        const auto destructiblePath = pathOf(p, "destructible");
        if (!object(*x, destructiblePath, c, q)) ok = false;
        else {
            allowed(*q, {"maximumHealth", "hurtbox", "destructionDurationTicks"}, destructiblePath, c);
            gameplay::ObjectDestructibleDefinition decoded{};
            ok = signedField(*x, *q, "maximumHealth", destructiblePath, c, decoded.maximumHealth) && ok;
            const auto* hurtbox = required(*x, *q, "hurtbox", destructiblePath, c);
            if (!hurtbox || !aabb(*hurtbox, pathOf(destructiblePath, "hurtbox"), c, decoded.hurtbox)) ok = false;
            ok = unsignedField(*x, *q, "destructionDurationTicks", destructiblePath, c, decoded.destructionDurationTicks) && ok;
            if (ok) d.destructible = decoded;
        }
    }
    if (const auto* x = findField(*o, "bankAccess"); x && !std::holds_alternative<std::nullptr_t>(x->value)) {
        const JsonObject* q = nullptr;
        const auto bankPath = pathOf(p, "bankAccess");
        if (!object(*x, bankPath, c, q)) ok = false;
        else { allowed(*q, {}, bankPath, c); d.bankAccess = AuthoredObjectBankAccess{}; }
    }
    if (const auto* x = findField(*o, "door"); x) {
        const JsonObject* q = nullptr;
        const auto doorPath = pathOf(p, "door");
        if (c.schemaVersion == 1) {
            c.error(*x, doorPath, "door capability requires content schema version 2");
            ok = false;
        }
        if (!object(*x, doorPath, c, q)) ok = false;
        else {
            allowed(*q, {"initialState", "blockingBounds"}, doorPath, c);
            gameplay::ObjectDoorDefinition decoded{};
            const auto* state = required(*x, *q, "initialState", doorPath, c);
            const auto* bounds = required(*x, *q, "blockingBounds", doorPath, c);
            ok = state && doorState(*state, pathOf(doorPath, "initialState"), c,
                                    decoded.initialState) && ok;
            ok = bounds && aabb(*bounds, pathOf(doorPath, "blockingBounds"), c,
                                decoded.blockingBounds) && ok;
            if (ok) d.door = decoded;
        }
    }
    if (const auto* x = findField(*o, "activation"); x) {
        const auto activationPath = pathOf(p, "activation");
        if (c.schemaVersion < 4) {
            c.error(*x, activationPath, "activation capability requires content schema version 4");
            ok = false;
        }
        const JsonObject* q = nullptr;
        if (!object(*x, activationPath, c, q)) ok = false;
        else {
            allowed(*q, {"mode", "initialActive", "bounds"}, activationPath, c);
            gameplay::ObjectActivationDefinition decoded{};
            const auto* mode = required(*x, *q, "mode", activationPath, c);
            std::string modeName;
            if (!mode || !stringValue(*mode, pathOf(activationPath, "mode"), c, modeName)) ok = false;
            else if (modeName == "interactToggle") decoded.mode = gameplay::ObjectActivationMode::interactToggle;
            else if (modeName == "playerPressure") decoded.mode = gameplay::ObjectActivationMode::playerPressure;
            else { c.error(*mode, pathOf(activationPath, "mode"), "unknown ObjectActivationMode"); ok = false; }
            const auto* initial = findField(*q, "initialActive");
            if (decoded.mode == gameplay::ObjectActivationMode::interactToggle) {
                if (!initial || !boolValue(*initial, pathOf(activationPath, "initialActive"), c, decoded.initialActive)) ok = false;
                if (findField(*q, "bounds")) {
                    c.error(*findField(*q, "bounds"), pathOf(activationPath, "bounds"), "interact-toggle activation cannot have pressure bounds");
                    ok = false;
                }
            } else if (decoded.mode == gameplay::ObjectActivationMode::playerPressure) {
                if (initial) {
                    c.error(*initial, pathOf(activationPath, "initialActive"), "player-pressure activation does not have an authored initial state");
                    ok = false;
                }
                const auto* bounds = required(*x, *q, "bounds", activationPath, c);
                world::AabbI parsed{};
                if (!bounds || !aabb(*bounds, pathOf(activationPath, "bounds"), c, parsed)) ok = false;
                else decoded.activationBounds = parsed;
            }
            if (ok) d.activation = decoded;
        }
    }
    if (ok) out = std::move(d);
    return ok;
}
bool pickup(const JsonValue&v,std::string_view p,Context&c,AuthoredPickup&out){const JsonObject*o=nullptr;if(!object(v,p,c,o))return false;allowed(*o,{"id","visualId","collectionBounds","payload"},p,c);AuthoredPickup d{};bool ok=idField(v,*o,"id",p,c,d.id);ok=idField(v,*o,"visualId",p,c,d.visualId)&&ok;const auto*b=required(v,*o,"collectionBounds",p,c);const auto*pl=required(v,*o,"payload",p,c);ok=b&&aabb(*b,pathOf(p,"collectionBounds"),c,d.collectionBounds)&&ok;const JsonObject*q=nullptr;if(!pl||!object(*pl,pathOf(p,"payload"),c,q))ok=false;else{auto pp=pathOf(p,"payload");const auto*k=required(*pl,*q,"kind",pp,c);std::string kind;if(k&&stringValue(*k,pathOf(pp,"kind"),c,kind)){if(kind=="health"){allowed(*q,{"kind","amount"},pp,c);AuthoredHealthPickup x{};ok=signedField(*pl,*q,"amount",pp,c,x.amount)&&ok;if(ok)d.payload=x;}else if(kind=="currency"){allowed(*q,{"kind","amount"},pp,c);AuthoredCurrencyPickup x{};ok=unsignedField(*pl,*q,"amount",pp,c,x.amount)&&ok;if(ok)d.payload=x;}else if(kind=="item"){allowed(*q,{"kind","itemId","quantity"},pp,c);AuthoredItemPickup x{};ok=idField(*pl,*q,"itemId",pp,c,x.itemId)&&ok;ok=unsignedField(*pl,*q,"quantity",pp,c,x.quantity)&&ok;if(ok)d.payload=x;}else{c.error(*k,pathOf(pp,"kind"),"unknown pickup payload kind");ok=false;}}else ok=false;}if(ok)out=std::move(d);return ok;}
bool npc(const JsonValue&v,std::string_view p,Context&c,AuthoredNpc&out){const JsonObject*o=nullptr;if(!object(v,p,c,o))return false;allowed(*o,{"id","visualSetId","interaction","interactionEnabled","defaultDialogueId","tags"},p,c);AuthoredNpc d{};bool ok=idField(v,*o,"id",p,c,d.id);ok=idField(v,*o,"visualSetId",p,c,d.visualSetId)&&ok;const auto*i=required(v,*o,"interaction",p,c);const auto*e=required(v,*o,"interactionEnabled",p,c);const auto*dialogue=required(v,*o,"defaultDialogueId",p,c);const auto*t=required(v,*o,"tags",p,c);ok=i&&aabb(*i,pathOf(p,"interaction"),c,d.interaction.bounds)&&ok;if(e)ok=boolValue(*e,pathOf(p,"interactionEnabled"),c,d.interaction.enabled)&&ok;else ok=false;ok=dialogue&&idField(v,*o,"defaultDialogueId",p,c,d.defaultDialogueId)&&ok;const JsonArray*a=nullptr;if(!t||!array(*t,pathOf(p,"tags"),c,a))ok=false;else for(size_t n=0;n<a->size();++n){std::string s;if(stringValue((*a)[n],pathOf(p,"tags")+"["+std::to_string(n)+"]",c,s))d.tags.push_back(std::move(s));else ok=false;}if(ok)out=std::move(d);return ok;}
bool stringArray(const JsonValue& v, std::string_view p, Context& c, std::vector<std::string>& out) {
    const JsonArray* a = nullptr;
    if (!array(v, p, c, a)) return false;
    bool ok = true;
    for (std::size_t i = 0; i < a->size(); ++i) {
        std::string value;
        if (stringValue((*a)[i], std::string(p) + "[" + std::to_string(i) + "]", c, value)) out.push_back(std::move(value));
        else ok = false;
    }
    return ok;
}
bool conditionKind(const JsonValue& v, std::string_view p, Context& c, gameplay::dialogue::DialogueConditionKind& out) {
    std::string s; if (!stringValue(v, p, c, s)) return false;
    if (s == "flagSet") out = gameplay::dialogue::DialogueConditionKind::flagSet;
    else if (s == "flagNotSet") out = gameplay::dialogue::DialogueConditionKind::flagNotSet;
    else { c.error(v, std::string(p), "unknown DialogueConditionKind"); return false; }
    return true;
}
bool actionKind(const JsonValue& v, std::string_view p, Context& c, gameplay::dialogue::DialogueActionKind& out) {
    std::string s; if (!stringValue(v, p, c, s)) return false;
    if (s == "setFlag") out = gameplay::dialogue::DialogueActionKind::setFlag;
    else if (s == "clearFlag") out = gameplay::dialogue::DialogueActionKind::clearFlag;
    else if (s == "startQuest") out = gameplay::dialogue::DialogueActionKind::startQuest;
    else if (s == "openShop") out = gameplay::dialogue::DialogueActionKind::openShop;
    else { c.error(v, std::string(p), "unknown DialogueActionKind"); return false; }
    return true;
}
bool objectiveKind(const JsonValue& v, std::string_view p, Context& c, gameplay::quests::QuestObjectiveKind& out) {
    std::string s; if (!stringValue(v, p, c, s)) return false;
    if (s == "talk") out = gameplay::quests::QuestObjectiveKind::talk;
    else if (s == "kill") out = gameplay::quests::QuestObjectiveKind::kill;
    else if (s == "pickup") out = gameplay::quests::QuestObjectiveKind::pickup;
    else if (s == "enter") out = gameplay::quests::QuestObjectiveKind::enter;
    else if (s == "open") out = gameplay::quests::QuestObjectiveKind::open;
    else if (s == "deliver") out = gameplay::quests::QuestObjectiveKind::deliver;
    else { c.error(v, std::string(p), "unknown QuestObjectiveKind"); return false; }
    return true;
}
bool semanticConfidence(const JsonValue& v, std::string_view p, Context& c, authoring::SemanticConfidence& out) {
    std::string s; if (!stringValue(v, p, c, s)) return false;
    if (s == "confirmed") out = authoring::SemanticConfidence::confirmed;
    else if (s == "probable") out = authoring::SemanticConfidence::probable;
    else if (s == "unverified") out = authoring::SemanticConfidence::unverified;
    else { c.error(v, std::string(p), "unknown SemanticConfidence"); return false; }
    return true;
}
bool tileRole(const JsonValue& v, std::string_view p, Context& c, authoring::TileRole& out) {
    std::string s; if (!stringValue(v, p, c, s)) return false;
    if (s == "floor") out = authoring::TileRole::floor; else if (s == "wall") out = authoring::TileRole::wall;
    else if (s == "corner") out = authoring::TileRole::corner; else if (s == "ledge") out = authoring::TileRole::ledge;
    else if (s == "opening") out = authoring::TileRole::opening; else if (s == "detail") out = authoring::TileRole::detail;
    else if (s == "unknown") out = authoring::TileRole::unknown;
    else { c.error(v, std::string(p), "unknown TileRole"); return false; }
    return true;
}
bool tileTopology(const JsonValue& v, std::string_view p, Context& c, authoring::TileTopology& out) {
    std::string s; if (!stringValue(v, p, c, s)) return false;
    if (s == "unknown") out = authoring::TileTopology::unknown; else if (s == "interior") out = authoring::TileTopology::interior;
    else if (s == "straightHorizontal") out = authoring::TileTopology::straightHorizontal; else if (s == "straightVertical") out = authoring::TileTopology::straightVertical;
    else if (s == "outerCorner") out = authoring::TileTopology::outerCorner; else if (s == "innerCorner") out = authoring::TileTopology::innerCorner;
    else if (s == "cap") out = authoring::TileTopology::cap; else if (s == "junction") out = authoring::TileTopology::junction;
    else if (s == "architecturalDetail") out = authoring::TileTopology::architecturalDetail;
    else { c.error(v, std::string(p), "unknown TileTopology"); return false; }
    return true;
}
bool edgeProfile(const JsonValue& v, std::string_view p, Context& c, authoring::EdgeProfile& out) {
    std::string s; if (!stringValue(v, p, c, s)) return false;
    if (s == "unknown") out = authoring::EdgeProfile::unknown; else if (s == "floor") out = authoring::EdgeProfile::floor;
    else if (s == "masonry") out = authoring::EdgeProfile::masonry; else if (s == "voidEdge") out = authoring::EdgeProfile::voidEdge;
    else if (s == "terminal") out = authoring::EdgeProfile::terminal;
    else { c.error(v, std::string(p), "unknown EdgeProfile"); return false; }
    return true;
}
bool pointI(const JsonValue& v, std::string_view p, Context& c, core::PointI& out) {
    const JsonObject* o = nullptr; if (!object(v, p, c, o)) return false; allowed(*o, {"x", "y"}, p, c);
    bool ok = signedField(v, *o, "x", p, c, out.x); ok = signedField(v, *o, "y", p, c, out.y) && ok; return ok;
}
bool dialogueCondition(const JsonValue& v, std::string_view p, Context& c, AuthoredDialogueCondition& out) {
    const JsonObject* o = nullptr; if (!object(v, p, c, o)) return false; allowed(*o, {"kind", "flagId"}, p, c);
    AuthoredDialogueCondition d{}; const auto* k = required(v, *o, "kind", p, c); bool ok = k && conditionKind(*k, pathOf(p, "kind"), c, d.kind);
    ok = idField(v, *o, "flagId", p, c, d.flagId) && ok; if (ok) out = std::move(d); return ok;
}
bool dialogueAction(const JsonValue& v, std::string_view p, Context& c, AuthoredDialogueAction& out) {
    const JsonObject* o = nullptr; if (!object(v, p, c, o)) return false; allowed(*o, {"kind", "targetId"}, p, c);
    AuthoredDialogueAction d{}; const auto* k = required(v, *o, "kind", p, c); bool ok = k && actionKind(*k, pathOf(p, "kind"), c, d.kind);
    ok = idField(v, *o, "targetId", p, c, d.targetId) && ok; if (ok) out = std::move(d); return ok;
}
bool dialogueChoice(const JsonValue& v, std::string_view p, Context& c, AuthoredDialogueChoice& out) {
    const JsonObject* o = nullptr; if (!object(v, p, c, o)) return false; allowed(*o, {"label", "targetNodeId", "conditions", "actions"}, p, c);
    AuthoredDialogueChoice d{}; bool ok = true; const auto* label = required(v, *o, "label", p, c); const auto* target = required(v, *o, "targetNodeId", p, c);
    const auto* conditions = required(v, *o, "conditions", p, c); const auto* actions = required(v, *o, "actions", p, c);
    ok = label && stringValue(*label, pathOf(p, "label"), c, d.label) && ok; ok = target && idField(v, *o, "targetNodeId", p, c, d.targetNodeId) && ok;
    const JsonArray* ca = nullptr; if (!conditions || !array(*conditions, pathOf(p, "conditions"), c, ca)) ok = false; else for (std::size_t i = 0; i < ca->size(); ++i) { AuthoredDialogueCondition x{}; if (dialogueCondition((*ca)[i], pathOf(p, "conditions") + "[" + std::to_string(i) + "]", c, x)) d.conditions.push_back(std::move(x)); else ok = false; }
    const JsonArray* aa = nullptr; if (!actions || !array(*actions, pathOf(p, "actions"), c, aa)) ok = false; else for (std::size_t i = 0; i < aa->size(); ++i) { AuthoredDialogueAction x{}; if (dialogueAction((*aa)[i], pathOf(p, "actions") + "[" + std::to_string(i) + "]", c, x)) d.actions.push_back(std::move(x)); else ok = false; }
    if (ok) out = std::move(d);
    return ok;
}
bool dialogueNode(const JsonValue& v, std::string_view p, Context& c, AuthoredDialogueNode& out) {
    const JsonObject* o = nullptr; if (!object(v, p, c, o)) return false; allowed(*o, {"id", "speaker", "pages", "nextNodeId", "choices"}, p, c);
    AuthoredDialogueNode d{}; bool ok = true; ok = idField(v, *o, "id", p, c, d.id) && ok; const auto* speaker = required(v, *o, "speaker", p, c); ok = speaker && stringValue(*speaker, pathOf(p, "speaker"), c, d.speaker) && ok;
    const auto* pages = required(v, *o, "pages", p, c); ok = pages && stringArray(*pages, pathOf(p, "pages"), c, d.pages) && ok; ok = idField(v, *o, "nextNodeId", p, c, d.nextNodeId) && ok;
    const auto* choices = required(v, *o, "choices", p, c); const JsonArray* a = nullptr; if (!choices || !array(*choices, pathOf(p, "choices"), c, a)) ok = false; else for (std::size_t i = 0; i < a->size(); ++i) { AuthoredDialogueChoice x{}; if (dialogueChoice((*a)[i], pathOf(p, "choices") + "[" + std::to_string(i) + "]", c, x)) d.choices.push_back(std::move(x)); else ok = false; }
    if (ok) out = std::move(d);
    return ok;
}
bool dialogue(const JsonValue& v, std::string_view p, Context& c, AuthoredDialogue& out) {
    const JsonObject* o = nullptr; if (!object(v, p, c, o)) return false; allowed(*o, {"id", "entryNodeId", "nodes"}, p, c); AuthoredDialogue d{}; bool ok = idField(v, *o, "id", p, c, d.id); ok = idField(v, *o, "entryNodeId", p, c, d.entryNodeId) && ok;
    const auto* nodes = required(v, *o, "nodes", p, c); const JsonArray* a = nullptr; if (!nodes || !array(*nodes, pathOf(p, "nodes"), c, a)) ok = false; else for (std::size_t i = 0; i < a->size(); ++i) { AuthoredDialogueNode x{}; if (dialogueNode((*a)[i], pathOf(p, "nodes") + "[" + std::to_string(i) + "]", c, x)) d.nodes.push_back(std::move(x)); else ok = false; }
    if (ok) out = std::move(d);
    return ok;
}
bool questObjective(const JsonValue& v, std::string_view p, Context& c, AuthoredQuestObjective& out) {
    const JsonObject* o = nullptr; if (!object(v, p, c, o)) return false; allowed(*o, {"id", "kind", "targetId", "requiredCount", "description"}, p, c); AuthoredQuestObjective d{}; bool ok = idField(v, *o, "id", p, c, d.id); const auto* k = required(v, *o, "kind", p, c); ok = k && objectiveKind(*k, pathOf(p, "kind"), c, d.kind) && ok; ok = idField(v, *o, "targetId", p, c, d.targetId) && ok; ok = unsignedField(v, *o, "requiredCount", p, c, d.requiredCount) && ok; const auto* desc = required(v, *o, "description", p, c); ok = desc && stringValue(*desc, pathOf(p, "description"), c, d.description) && ok; if (ok) out = std::move(d); return ok;
}
bool quest(const JsonValue& v, std::string_view p, Context& c, AuthoredQuest& out) {
    const JsonObject* o = nullptr; if (!object(v, p, c, o)) return false; allowed(*o, {"id", "title", "objectives", "tags", "rewardGrantId"}, p, c); AuthoredQuest d{}; bool ok = idField(v, *o, "id", p, c, d.id); const auto* title = required(v, *o, "title", p, c); ok = title && stringValue(*title, pathOf(p, "title"), c, d.title) && ok;
    const auto* objectives = required(v, *o, "objectives", p, c); const JsonArray* a = nullptr; if (!objectives || !array(*objectives, pathOf(p, "objectives"), c, a)) ok = false; else for (std::size_t i = 0; i < a->size(); ++i) { AuthoredQuestObjective x{}; if (questObjective((*a)[i], pathOf(p, "objectives") + "[" + std::to_string(i) + "]", c, x)) d.objectives.push_back(std::move(x)); else ok = false; }
    const auto* tags = required(v, *o, "tags", p, c); ok = tags && stringArray(*tags, pathOf(p, "tags"), c, d.tags) && ok; if (const auto* reward = findField(*o, "rewardGrantId")) { if (!std::holds_alternative<std::nullptr_t>(reward->value)) { std::string s; if (stringValue(*reward, pathOf(p, "rewardGrantId"), c, s)) d.rewardGrantId = simulation::DefinitionId{std::move(s)}; else ok = false; } }
    if (ok) out = std::move(d);
    return ok;
}
bool tileSemantic(const JsonValue& v, std::string_view p, Context& c, AuthoredTileSemantic& out) {
    const JsonObject* o = nullptr; if (!object(v, p, c, o)) return false; allowed(*o, {"id","tilesetId","sourceIndex","family","role","topology","north","east","south","west","preferredLayer","flipXAllowed","visualConfidence","semanticConfidence","gameplayConfidence","variantWeight"}, p, c); AuthoredTileSemantic d{}; bool ok = idField(v,*o,"id",p,c,d.id); ok = idField(v,*o,"tilesetId",p,c,d.tilesetId) && ok; ok = unsignedField(v,*o,"sourceIndex",p,c,d.sourceIndex) && ok; const auto* family=required(v,*o,"family",p,c); ok=family&&stringValue(*family,pathOf(p,"family"),c,d.family)&&ok; const auto* roleV=required(v,*o,"role",p,c); ok=roleV&&tileRole(*roleV,pathOf(p,"role"),c,d.role)&&ok; const auto* topologyV=required(v,*o,"topology",p,c); ok=topologyV&&tileTopology(*topologyV,pathOf(p,"topology"),c,d.topology)&&ok;
    const char* edgeNames[] = {"north","east","south","west"}; authoring::EdgeProfile* edgeValues[] = {&d.north,&d.east,&d.south,&d.west}; for (int i=0;i<4;++i) { const auto* e=required(v,*o,edgeNames[i],p,c); if (!e || !edgeProfile(*e,pathOf(p,edgeNames[i]),c,*edgeValues[i])) ok=false; }
    const auto* layer=required(v,*o,"preferredLayer",p,c); ok=layer&&stringValue(*layer,pathOf(p,"preferredLayer"),c,d.preferredLayer)&&ok; const auto* flip=required(v,*o,"flipXAllowed",p,c); ok=flip&&boolValue(*flip,pathOf(p,"flipXAllowed"),c,d.flipXAllowed)&&ok;
    const char* confNames[] = {"visualConfidence","semanticConfidence","gameplayConfidence"}; authoring::SemanticConfidence* confValues[] = {&d.visualConfidence,&d.semanticConfidence,&d.gameplayConfidence}; for (int i=0;i<3;++i) { const auto* q=required(v,*o,confNames[i],p,c); if (!q || !semanticConfidence(*q,pathOf(p,confNames[i]),c,*confValues[i])) ok=false; } if (findField(*o,"variantWeight")) ok=unsignedField(v,*o,"variantWeight",p,c,d.variantWeight)&&ok; if (ok) out=std::move(d); return ok;
}
bool stampCell(const JsonValue& v, std::string_view p, Context& c, AuthoredStampCell& out) { const JsonObject* o=nullptr; if(!object(v,p,c,o))return false; allowed(*o,{"x","y","tileId"},p,c); AuthoredStampCell d{}; bool ok=signedField(v,*o,"x",p,c,d.x); ok=signedField(v,*o,"y",p,c,d.y)&&ok; ok=idField(v,*o,"tileId",p,c,d.tileId)&&ok; if(ok)out=std::move(d); return ok; }
bool stamp(const JsonValue& v, std::string_view p, Context& c, AuthoredStamp& out) { const JsonObject* o=nullptr; if(!object(v,p,c,o))return false; allowed(*o,{"id","displayName","width","height","cells","anchor","flipXAllowed","atomic","confidence"},p,c); AuthoredStamp d{}; bool ok=idField(v,*o,"id",p,c,d.id); const auto* name=required(v,*o,"displayName",p,c); ok=name&&stringValue(*name,pathOf(p,"displayName"),c,d.displayName)&&ok; ok=unsignedField(v,*o,"width",p,c,d.width)&&ok; ok=unsignedField(v,*o,"height",p,c,d.height)&&ok; const auto* cells=required(v,*o,"cells",p,c); const JsonArray* a=nullptr; if(!cells||!array(*cells,pathOf(p,"cells"),c,a))ok=false; else for(size_t i=0;i<a->size();++i){AuthoredStampCell x{};if(stampCell((*a)[i],pathOf(p,"cells")+"["+std::to_string(i)+"]",c,x))d.cells.push_back(std::move(x));else ok=false;} const auto* anchor=required(v,*o,"anchor",p,c); ok=anchor&&pointI(*anchor,pathOf(p,"anchor"),c,d.anchor)&&ok; const auto* flip=required(v,*o,"flipXAllowed",p,c); ok=flip&&boolValue(*flip,pathOf(p,"flipXAllowed"),c,d.flipXAllowed)&&ok; const auto* atomic=required(v,*o,"atomic",p,c); ok=atomic&&boolValue(*atomic,pathOf(p,"atomic"),c,d.atomic)&&ok; const auto* confidence=required(v,*o,"confidence",p,c); ok=confidence&&semanticConfidence(*confidence,pathOf(p,"confidence"),c,d.confidence)&&ok; if(ok)out=std::move(d); return ok; }
bool presentationLifetime(const JsonValue& v,std::string_view p,Context&c,presentation::PresentationEffectLifetime&out){std::string s;if(!stringValue(v,p,c,s))return false;if(s=="transient")out=presentation::PresentationEffectLifetime::transient;else if(s=="persistent")out=presentation::PresentationEffectLifetime::persistent;else{c.error(v,std::string(p),"unknown PresentationEffectLifetime");return false;}return true;}
bool presentationMode(const JsonValue& v,std::string_view p,Context&c,presentation::PresentationOverlayMode&out){std::string s;if(!stringValue(v,p,c,s))return false;if(s=="constant")out=presentation::PresentationOverlayMode::constant;else if(s=="linearFadeOut")out=presentation::PresentationOverlayMode::linearFadeOut;else if(s=="pulse")out=presentation::PresentationOverlayMode::pulse;else{c.error(v,std::string(p),"unknown PresentationOverlayMode");return false;}return true;}
bool presentationLayer(const JsonValue& v,std::string_view p,Context&c,presentation::PresentationCompositionLayer&out){std::string s;if(!stringValue(v,p,c,s))return false;if(s=="world")out=presentation::PresentationCompositionLayer::world;else if(s=="final")out=presentation::PresentationCompositionLayer::final;else{c.error(v,std::string(p),"unknown PresentationCompositionLayer");return false;}return true;}
bool presentationEffect(const JsonValue&v,std::string_view p,Context&c,AuthoredPresentationEffect&out){const JsonObject*o=nullptr;if(!object(v,p,c,o))return false;allowed(*o,{"id","lifetime","durationTicks","priority","cameraShake","overlay","visionMask","fade"},p,c);AuthoredPresentationEffect d{};bool ok=idField(v,*o,"id",p,c,d.id);const auto*l=required(v,*o,"lifetime",p,c);ok=l&&presentationLifetime(*l,pathOf(p,"lifetime"),c,d.lifetime)&&ok;ok=unsignedField(v,*o,"durationTicks",p,c,d.durationTicks)&&ok;ok=signedField(v,*o,"priority",p,c,d.priority)&&ok;if(const auto*x=findField(*o,"cameraShake")){const JsonObject*q=nullptr;const auto cp=pathOf(p,"cameraShake");if(!object(*x,cp,c,q))ok=false;else{allowed(*q,{"amplitudePixels"},cp,c);presentation::CameraShakeDefinition value{};ok=signedField(*x,*q,"amplitudePixels",cp,c,value.amplitudePixels)&&ok;if(ok)d.cameraShake=value;}}if(const auto*x=findField(*o,"overlay")){const JsonObject*q=nullptr;const auto op=pathOf(p,"overlay");if(!object(*x,op,c,q))ok=false;else{allowed(*q,{"color","mode","pulsePeriodTicks","layer"},op,c);presentation::ColorOverlayDefinition value{};const auto*colorValue=required(*x,*q,"color",op,c);const auto*mode=required(*x,*q,"mode",op,c);ok=colorValue&&color(*colorValue,pathOf(op,"color"),c,value.color)&&ok;ok=mode&&presentationMode(*mode,pathOf(op,"mode"),c,value.mode)&&ok;ok=unsignedField(*x,*q,"pulsePeriodTicks",op,c,value.pulsePeriodTicks)&&ok;const auto*layer=required(*x,*q,"layer",op,c);ok=layer&&presentationLayer(*layer,pathOf(op,"layer"),c,value.layer)&&ok;if(ok)d.overlay=value;}}if(const auto*x=findField(*o,"visionMask")){const JsonObject*q=nullptr;const auto vp=pathOf(p,"visionMask");if(!object(*x,vp,c,q))ok=false;else{allowed(*q,{"innerRadiusPixels","outerRadiusPixels","outsideAlpha","color"},vp,c);presentation::VisionMaskDefinition value{};ok=signedField(*x,*q,"innerRadiusPixels",vp,c,value.innerRadiusPixels)&&ok;ok=signedField(*x,*q,"outerRadiusPixels",vp,c,value.outerRadiusPixels)&&ok;ok=unsignedField(*x,*q,"outsideAlpha",vp,c,value.outsideAlpha)&&ok;const auto*colorValue=required(*x,*q,"color",vp,c);ok=colorValue&&color(*colorValue,pathOf(vp,"color"),c,value.color)&&ok;if(ok)d.visionMask=value;}}if(const auto*x=findField(*o,"fade")){const JsonObject*q=nullptr;const auto fp=pathOf(p,"fade");if(!object(*x,fp,c,q))ok=false;else{allowed(*q,{"color","startAlpha","endAlpha"},fp,c);presentation::FadeDefinition value{};const auto*colorValue=required(*x,*q,"color",fp,c);ok=colorValue&&color(*colorValue,pathOf(fp,"color"),c,value.color)&&ok;ok=unsignedField(*x,*q,"startAlpha",fp,c,value.startAlpha)&&ok;ok=unsignedField(*x,*q,"endAlpha",fp,c,value.endAlpha)&&ok;if(ok)d.fade=value;}}if(ok)out=std::move(d);return ok;}
bool visualRoot(const JsonValue& v,std::string_view p,Context& c,presentation::VisualAssetRoot& out){std::string s;if(!stringValue(v,p,c,s))return false;if(s=="gameAssets")out=presentation::VisualAssetRoot::gameAssets;else if(s=="contentWorkspace")out=presentation::VisualAssetRoot::contentWorkspace;else{c.error(v,std::string(p),"unknown VisualAssetRoot");return false;}return true;}
bool rect(const JsonValue&v,std::string_view p,Context&c,core::RectI&out){const JsonObject*o=nullptr;if(!object(v,p,c,o))return false;allowed(*o,{"x","y","width","height"},p,c);bool ok=signedField(v,*o,"x",p,c,out.x);ok=signedField(v,*o,"y",p,c,out.y)&&ok;ok=signedField(v,*o,"width",p,c,out.width)&&ok;ok=signedField(v,*o,"height",p,c,out.height)&&ok;return ok;}
bool directional(const JsonValue&v,std::string_view p,Context&c,presentation::DirectionalAnimationRef&out){const JsonObject*o=nullptr;if(!object(v,p,c,o))return false;allowed(*o,{"default","down","up","side"},p,c);bool ok=true;for(const auto n:{"default","down","up","side"}){if(const auto*x=findField(*o,n);x&&!isNull(x)){simulation::DefinitionId idValue{};if(!idField(v,*o,n,p,c,idValue))ok=false;else if(std::string_view(n)=="default")out.defaultAnimation=idValue;else if(std::string_view(n)=="down")out.down=idValue;else if(std::string_view(n)=="up")out.up=idValue;else out.side=idValue;}}if(!out.defaultAnimation&&!out.down&&!out.up&&!out.side){c.error(v,std::string(p),"directional binding must contain at least one animation");ok=false;}return ok;}
bool visualImage(const JsonValue&v,std::string_view p,Context&c,AuthoredVisualImage&out){const JsonObject*o=nullptr;if(!object(v,p,c,o))return false;allowed(*o,{"id","root","relativePath"},p,c);AuthoredVisualImage d{};bool ok=idField(v,*o,"id",p,c,d.id);const auto*r=required(v,*o,"root",p,c);const auto*f=required(v,*o,"relativePath",p,c);ok=r&&visualRoot(*r,pathOf(p,"root"),c,d.root)&&ok;ok=f&&stringValue(*f,pathOf(p,"relativePath"),c,d.relativePath)&&ok;if(ok)out=std::move(d);return ok;}
bool staticSprite(const JsonValue&v,std::string_view p,Context&c,AuthoredStaticSprite&out){const JsonObject*o=nullptr;if(!object(v,p,c,o))return false;allowed(*o,{"id","imageId","source","anchor"},p,c);AuthoredStaticSprite d{};bool ok=idField(v,*o,"id",p,c,d.id);ok=idField(v,*o,"imageId",p,c,d.imageId)&&ok;const auto*a=required(v,*o,"anchor",p,c);ok=a&&pointI(*a,pathOf(p,"anchor"),c,d.anchor)&&ok;if(const auto*s=findField(*o,"source");s&&!isNull(s)){core::RectI r{};if(rect(*s,pathOf(p,"source"),c,r))d.source=r;else ok=false;}if(ok)out=std::move(d);return ok;}
bool animationFrame(const JsonValue&v,std::string_view p,Context&c,AuthoredAnimationFrame&out){const JsonObject*o=nullptr;if(!object(v,p,c,o))return false;allowed(*o,{"source","anchor","drawOffset","durationTicks","markers"},p,c);AuthoredAnimationFrame d{};bool ok=true;const auto*s=required(v,*o,"source",p,c);const auto*a=required(v,*o,"anchor",p,c);const auto*off=required(v,*o,"drawOffset",p,c);const auto*dur=required(v,*o,"durationTicks",p,c);const auto*m=required(v,*o,"markers",p,c);ok=s&&rect(*s,pathOf(p,"source"),c,d.source)&&ok;ok=a&&pointI(*a,pathOf(p,"anchor"),c,d.anchor)&&ok;ok=off&&pointI(*off,pathOf(p,"drawOffset"),c,d.drawOffset)&&ok;ok=dur&&unsignedField(*dur,*o,"durationTicks",p,c,d.durationTicks)&&ok;ok=m&&stringArray(*m,pathOf(p,"markers"),c,d.markers)&&ok;if(ok)out=std::move(d);return ok;}
bool animation(const JsonValue&v,std::string_view p,Context&c,AuthoredAnimation&out){const JsonObject*o=nullptr;if(!object(v,p,c,o))return false;allowed(*o,{"id","imageId","frames","loop"},p,c);AuthoredAnimation d{};bool ok=idField(v,*o,"id",p,c,d.id);ok=idField(v,*o,"imageId",p,c,d.imageId)&&ok;const auto*loop=required(v,*o,"loop",p,c);ok=loop&&boolValue(*loop,pathOf(p,"loop"),c,d.loop)&&ok;const auto*f=required(v,*o,"frames",p,c);const JsonArray*a=nullptr;if(!f||!array(*f,pathOf(p,"frames"),c,a))ok=false;else for(size_t i=0;i<a->size();++i){AuthoredAnimationFrame x{};if(animationFrame((*a)[i],pathOf(p,"frames")+"["+std::to_string(i)+"]",c,x))d.frames.push_back(std::move(x));else ok=false;}if(ok)out=std::move(d);return ok;}
bool enemyVisual(const JsonValue&v,std::string_view p,Context&c,AuthoredEnemyVisual&out){const JsonObject*o=nullptr;if(!object(v,p,c,o))return false;allowed(*o,{"id","idle","move","hurt","death","dead","actions","walk","attacks"},p,c);AuthoredEnemyVisual d{};bool ok=idField(v,*o,"id",p,c,d.id);const auto*idleValue=required(v,*o,"idle",p,c);if(!idleValue||!directional(*idleValue,pathOf(p,"idle"),c,d.idle))ok=false;const auto readOptional=[&](const char*n,std::optional<presentation::DirectionalAnimationRef>&target){if(const auto*x=findField(*o,n);x&&!isNull(x)){presentation::DirectionalAnimationRef refs{};if(directional(*x,pathOf(p,n),c,refs))target=std::move(refs);else ok=false;}};readOptional("move",d.move);readOptional("hurt",d.hurt);readOptional("death",d.death);readOptional("dead",d.dead);if(!d.move&&findField(*o,"walk")&&!isNull(findField(*o,"walk")))readOptional("walk",d.move);const auto*aa=findField(*o,"actions");if(!aa)aa=findField(*o,"attacks");if(aa&&!isNull(aa)){const JsonArray*a=nullptr;const auto actionsPath=aa==findField(*o,"attacks")?pathOf(p,"attacks"):pathOf(p,"actions");if(!array(*aa,actionsPath,c,a))ok=false;else for(size_t actionIndex=0;actionIndex<a->size();++actionIndex){const JsonObject*q=nullptr;const auto ap=actionsPath+"["+std::to_string(actionIndex)+"]";if(!object((*a)[actionIndex],ap,c,q)){ok=false;continue;}allowed(*q,{"visualActionId","clips"},ap,c);AuthoredEnemyAttackVisual x{};ok=idField((*a)[actionIndex],*q,"visualActionId",ap,c,x.visualActionId)&&ok;const auto*cl=required((*a)[actionIndex],*q,"clips",ap,c);ok=cl&&directional(*cl,pathOf(ap,"clips"),c,x.clips)&&ok;if(ok)d.attacks.push_back(std::move(x));}}if(ok)out=std::move(d);return ok;}
bool objectVisual(const JsonValue&v,std::string_view p,Context&c,AuthoredWorldObjectVisual&out){const JsonObject*o=nullptr;if(!object(v,p,c,o))return false;allowed(*o,{"id","idleAnimationId","openedAnimationId","destroyingAnimationId","activationInactiveAnimationId","activationActiveAnimationId","doorLockedAnimationId","doorClosedAnimationId","doorOpenAnimationId","destroyedAnimationId"},p,c);AuthoredWorldObjectVisual d{};bool ok=idField(v,*o,"id",p,c,d.id);ok=idField(v,*o,"idleAnimationId",p,c,d.idleAnimationId)&&ok;for(auto n:{"openedAnimationId","destroyingAnimationId","activationInactiveAnimationId","activationActiveAnimationId","doorLockedAnimationId","doorClosedAnimationId","doorOpenAnimationId","destroyedAnimationId"}){if(const auto*x=findField(*o,n)){std::string s;if(stringValue(*x,pathOf(p,n),c,s)){simulation::DefinitionId value{std::move(s)};if(std::string_view(n)=="openedAnimationId")d.openedAnimationId=value;else if(std::string_view(n)=="destroyingAnimationId")d.destroyingAnimationId=value;else if(std::string_view(n)=="activationInactiveAnimationId")d.activationInactiveAnimationId=value;else if(std::string_view(n)=="activationActiveAnimationId")d.activationActiveAnimationId=value;else if(std::string_view(n)=="doorLockedAnimationId")d.doorLockedAnimationId=value;else if(std::string_view(n)=="doorClosedAnimationId")d.doorClosedAnimationId=value;else if(std::string_view(n)=="doorOpenAnimationId")d.doorOpenAnimationId=value;else d.destroyedAnimationId=value;}else ok=false;}}if(ok)out=std::move(d);return ok;}
template<class T> using Decoder=bool(*)(const JsonValue&,std::string_view,Context&,T&);
template<class T> const simulation::DefinitionId& definitionIdOf(const T& value) {
    if constexpr (requires { value.id; }) return value.id;
    else return value.definitionId;
}
template<class T> void category(const JsonObject&r,std::string_view n,Decoder<T> d,std::vector<T>&out,Context&c){const auto*v=findField(r,n);if(!v)return;const JsonArray*a=nullptr;if(!array(*v,n,c,a))return;for(size_t i=0;i<a->size();++i){T x{};const auto itemPath=std::string(n)+"["+std::to_string(i)+"]";if(d((*a)[i],itemPath,c,x)){out.push_back(x);c.origins.push_back({std::string(n),definitionIdOf(out.back()),(*a)[i].span.begin.line,(*a)[i].span.begin.column,itemPath});}}}
}}

namespace underworld::game::content {
ContentJsonDecodeResult decodeAuthoredContentJson(std::string_view text){ContentJsonDecodeResult r;const auto p=engine::data::parseJson(text);for(const auto&d:p.diagnostics)r.diagnostics.push_back({d.location.line,d.location.column,{},d.message});if(!p.value||!r.diagnostics.empty())return r;const auto*root=std::get_if<engine::data::JsonObject>(&p.value->value);if(!root){r.diagnostics.push_back({p.value->span.begin.line,p.value->span.begin.column,{},"top-level JSON value must be an object"});return r;}Context c;allowed(*root,{"format","version","tilesets","projectiles","attacks","behaviors","enemies","items","objects","pickups","npcVisuals","npcs","dialogues","quests","playerProgressions","rewardProfiles","rewardGrants","shops","authoringDescriptors","tileSemantics","stamps","presentationEffects","visualImages","staticSprites","animations","enemyVisuals","objectVisuals"},"",c);const auto*f=required(*p.value,*root,"format","",c);std::string fs;if(f&&stringValue(*f,"format",c,fs)&&fs!="dungeon-underworld-content")c.error(*f,"format","invalid format identifier");const auto*v=required(*p.value,*root,"version","",c);if(v){uint64_t x{};if(u64(*v,"version",c,x)){c.schemaVersion=x;if(x<1||x>5)c.error(*v,"version","unsupported schema version");if(x<3&&findField(*root,"presentationEffects"))c.error(*v,"version","presentationEffects require schema version 3");if(x<5&&(findField(*root,"visualImages")||findField(*root,"staticSprites")||findField(*root,"animations")||findField(*root,"enemyVisuals")||findField(*root,"objectVisuals")))c.error(*v,"version","visual content categories require schema version 5");}}AuthoredContentPack out;category(*root,"tilesets",tileset,out.tilesets,c);category(*root,"projectiles",projectile,out.projectiles,c);category(*root,"attacks",attack,out.attacks,c);category(*root,"behaviors",behavior,out.behaviors,c);category(*root,"enemies",enemy,out.enemies,c);category(*root,"items",item,out.items,c);category(*root,"objects",worldObject,out.objects,c);category(*root,"pickups",pickup,out.pickups,c);category(*root,"npcVisuals",npcVisual,out.npcVisuals,c);category(*root,"npcs",npc,out.npcs,c);category(*root,"dialogues",dialogue,out.dialogues,c);category(*root,"quests",quest,out.quests,c);category(*root,"playerProgressions",progression,out.playerProgressions,c);category(*root,"rewardProfiles",rewardProfile,out.rewardProfiles,c);category(*root,"rewardGrants",rewardGrant,out.rewardGrants,c);category(*root,"shops",shop,out.shops,c);category(*root,"authoringDescriptors",descriptor,out.authoringDescriptors,c);category(*root,"tileSemantics",tileSemantic,out.tileSemantics,c);category(*root,"stamps",stamp,out.stamps,c);category(*root,"presentationEffects",presentationEffect,out.presentationEffects,c);category(*root,"visualImages",visualImage,out.visualImages,c);category(*root,"staticSprites",staticSprite,out.staticSprites,c);category(*root,"animations",animation,out.animations,c);category(*root,"enemyVisuals",enemyVisual,out.enemyVisuals,c);category(*root,"objectVisuals",objectVisual,out.objectVisuals,c);r.diagnostics.insert(r.diagnostics.end(),c.diagnostics.begin(),c.diagnostics.end());r.origins=std::move(c.origins);if(r.diagnostics.empty())r.content=std::move(out);else r.origins.clear();return r;}
}
