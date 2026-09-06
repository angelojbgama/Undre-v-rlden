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
    void error(const JsonValue& v, std::string path, std::string message) {
        diagnostics.push_back({v.span.begin.line, v.span.begin.column, std::move(path), std::move(message)});
    }
};

const JsonValue* findField(const JsonObject& o, std::string_view n) {
    for (const auto& m : o) if (m.first == n) return &m.second;
    return nullptr;
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
    const auto* v = required(p, o, n, path, c); if (!v) return false; std::string s; if (!stringValue(*v, pathOf(path, n), c, s)) return false; out = simulation::DefinitionId{std::move(s)}; return true;
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
bool npcVisual(const JsonValue&v,std::string_view p,Context&c,AuthoredNpcVisualSet&out){const JsonObject*o=nullptr;if(!object(v,p,c,o))return false;allowed(*o,{"id","markerColor"},p,c);AuthoredNpcVisualSet d{};bool ok=idField(v,*o,"id",p,c,d.id);const auto*m=required(v,*o,"markerColor",p,c);ok=m&&color(*m,pathOf(p,"markerColor"),c,d.markerColor)&&ok;if(ok)out=std::move(d);return ok;}
bool progression(const JsonValue&v,std::string_view p,Context&c,AuthoredPlayerProgression&out){const JsonObject*o=nullptr;if(!object(v,p,c,o))return false;allowed(*o,{"id","baseStats","cumulativeExperienceThresholds"},p,c);AuthoredPlayerProgression d{};bool ok=idField(v,*o,"id",p,c,d.id);const auto*s=required(v,*o,"baseStats",p,c);const auto*t=required(v,*o,"cumulativeExperienceThresholds",p,c);const JsonObject*so=nullptr;if(!s||!object(*s,pathOf(p,"baseStats"),c,so))ok=false;else{auto sp=pathOf(p,"baseStats");allowed(*so,{"maximumHealth"},sp,c);ok=signedField(*s,*so,"maximumHealth",sp,c,d.baseStats.maximumHealth)&&ok;}const JsonArray*ta=nullptr;if(!t||!array(*t,pathOf(p,"cumulativeExperienceThresholds"),c,ta))ok=false;else for(size_t i=0;i<ta->size();++i){uint64_t n{};if(u64((*ta)[i],pathOf(p,"cumulativeExperienceThresholds")+"["+std::to_string(i)+"]",c,n))d.cumulativeExperienceThresholds.push_back(n);else ok=false;}if(ok)out=std::move(d);return ok;}
bool loot(const JsonValue&v,std::string_view p,Context&c,AuthoredLootEntry&out){const JsonObject*o=nullptr;if(!object(v,p,c,o))return false;allowed(*o,{"pickupDefinitionId","chanceBasisPoints","minimumCount","maximumCount"},p,c);AuthoredLootEntry d{};bool ok=idField(v,*o,"pickupDefinitionId",p,c,d.pickupDefinitionId);ok=unsignedField(v,*o,"chanceBasisPoints",p,c,d.chanceBasisPoints)&&ok;ok=unsignedField(v,*o,"minimumCount",p,c,d.minimumCount)&&ok;ok=unsignedField(v,*o,"maximumCount",p,c,d.maximumCount)&&ok;if(ok)out=std::move(d);return ok;}
bool rewardProfile(const JsonValue&v,std::string_view p,Context&c,AuthoredRewardProfile&out){const JsonObject*o=nullptr;if(!object(v,p,c,o))return false;allowed(*o,{"id","experience","loot"},p,c);AuthoredRewardProfile d{};bool ok=idField(v,*o,"id",p,c,d.id);ok=unsignedField(v,*o,"experience",p,c,d.experience)&&ok;const auto*l=required(v,*o,"loot",p,c);const JsonArray*a=nullptr;if(!l||!array(*l,pathOf(p,"loot"),c,a))ok=false;else for(size_t i=0;i<a->size();++i){AuthoredLootEntry x{};if(loot((*a)[i],pathOf(p,"loot")+"["+std::to_string(i)+"]",c,x))d.loot.push_back(std::move(x));else ok=false;}if(ok)out=std::move(d);return ok;}
bool rewardItem(const JsonValue&v,std::string_view p,Context&c,AuthoredRewardItemGrant&out){const JsonObject*o=nullptr;if(!object(v,p,c,o))return false;allowed(*o,{"itemId","quantity"},p,c);AuthoredRewardItemGrant d{};bool ok=idField(v,*o,"itemId",p,c,d.itemId);ok=unsignedField(v,*o,"quantity",p,c,d.quantity)&&ok;if(ok)out=std::move(d);return ok;}
bool rewardGrant(const JsonValue&v,std::string_view p,Context&c,AuthoredRewardGrant&out){const JsonObject*o=nullptr;if(!object(v,p,c,o))return false;allowed(*o,{"id","experience","gold","items"},p,c);AuthoredRewardGrant d{};bool ok=idField(v,*o,"id",p,c,d.id);ok=unsignedField(v,*o,"experience",p,c,d.experience)&&ok;ok=unsignedField(v,*o,"gold",p,c,d.gold)&&ok;const auto*i=required(v,*o,"items",p,c);const JsonArray*a=nullptr;if(!i||!array(*i,pathOf(p,"items"),c,a))ok=false;else for(size_t n=0;n<a->size();++n){AuthoredRewardItemGrant x{};if(rewardItem((*a)[n],pathOf(p,"items")+"["+std::to_string(n)+"]",c,x))d.items.push_back(std::move(x));else ok=false;}if(ok)out=std::move(d);return ok;}
bool shopOffer(const JsonValue&v,std::string_view p,Context&c,AuthoredShopOffer&out){const JsonObject*o=nullptr;if(!object(v,p,c,o))return false;allowed(*o,{"itemId","playerBuyPrice","playerSellPrice"},p,c);AuthoredShopOffer d{};bool ok=idField(v,*o,"itemId",p,c,d.itemId);for(auto n:{"playerBuyPrice","playerSellPrice"}){const auto*x=findField(*o,n);if(!x||std::holds_alternative<std::nullptr_t>(x->value))continue;uint64_t q{};if(!u64(*x,pathOf(p,n),c,q))ok=false;else if(std::string_view(n)=="playerBuyPrice")d.playerBuyPrice=q;else d.playerSellPrice=q;}if(ok)out=std::move(d);return ok;}
bool shop(const JsonValue&v,std::string_view p,Context&c,AuthoredShop&out){const JsonObject*o=nullptr;if(!object(v,p,c,o))return false;allowed(*o,{"id","offers"},p,c);AuthoredShop d{};bool ok=idField(v,*o,"id",p,c,d.id);const auto*f=required(v,*o,"offers",p,c);const JsonArray*a=nullptr;if(!f||!array(*f,pathOf(p,"offers"),c,a))ok=false;else for(size_t i=0;i<a->size();++i){AuthoredShopOffer x{};if(shopOffer((*a)[i],pathOf(p,"offers")+"["+std::to_string(i)+"]",c,x))d.offers.push_back(std::move(x));else ok=false;}if(ok)out=std::move(d);return ok;}
bool descriptor(const JsonValue&v,std::string_view p,Context&c,AuthoringDescriptor&out){const JsonObject*o=nullptr;if(!object(v,p,c,o))return false;allowed(*o,{"definitionId","displayName","category","tags"},p,c);AuthoringDescriptor d{};bool ok=idField(v,*o,"definitionId",p,c,d.definitionId);const auto*n=required(v,*o,"displayName",p,c);const auto*k=required(v,*o,"category",p,c);const auto*t=required(v,*o,"tags",p,c);ok=n&&stringValue(*n,pathOf(p,"displayName"),c,d.displayName)&&ok;ok=k&&authoringCategory(*k,pathOf(p,"category"),c,d.category)&&ok;const JsonArray*a=nullptr;if(!t||!array(*t,pathOf(p,"tags"),c,a))ok=false;else for(size_t i=0;i<a->size();++i){std::string s;if(stringValue((*a)[i],pathOf(p,"tags")+"["+std::to_string(i)+"]",c,s))d.tags.push_back(std::move(s));else ok=false;}if(ok)out=std::move(d);return ok;}
template<class T> using Decoder=bool(*)(const JsonValue&,std::string_view,Context&,T&);
template<class T> void category(const JsonObject&r,std::string_view n,Decoder<T> d,std::vector<T>&out,Context&c){const auto*v=findField(r,n);if(!v)return;const JsonArray*a=nullptr;if(!array(*v,n,c,a))return;for(size_t i=0;i<a->size();++i){T x{};if(d((*a)[i],std::string(n)+"["+std::to_string(i)+"]",c,x))out.push_back(std::move(x));}}
}}

namespace underworld::game::content {
ContentJsonDecodeResult decodeAuthoredContentJson(std::string_view text){ContentJsonDecodeResult r;const auto p=engine::data::parseJson(text);for(const auto&d:p.diagnostics)r.diagnostics.push_back({d.location.line,d.location.column,{},d.message});if(!p.value||!r.diagnostics.empty())return r;const auto*root=std::get_if<engine::data::JsonObject>(&p.value->value);if(!root){r.diagnostics.push_back({p.value->span.begin.line,p.value->span.begin.column,{},"top-level JSON value must be an object"});return r;}Context c;allowed(*root,{"format","version","tilesets","projectiles","attacks","behaviors","enemies","items","objects","pickups","npcVisuals","npcs","dialogues","quests","playerProgressions","rewardProfiles","rewardGrants","shops","authoringDescriptors","tileSemantics","stamps"},"",c);const auto*f=required(*p.value,*root,"format","",c);std::string fs;if(f&&stringValue(*f,"format",c,fs)&&fs!="dungeon-underworld-content")c.error(*f,"format","invalid format identifier");const auto*v=required(*p.value,*root,"version","",c);if(v){uint64_t x{};if(u64(*v,"version",c,x)&&x!=1)c.error(*v,"version","unsupported schema version");}AuthoredContentPack out;category(*root,"tilesets",tileset,out.tilesets,c);category(*root,"behaviors",behavior,out.behaviors,c);category(*root,"items",item,out.items,c);category(*root,"npcVisuals",npcVisual,out.npcVisuals,c);category(*root,"playerProgressions",progression,out.playerProgressions,c);category(*root,"rewardProfiles",rewardProfile,out.rewardProfiles,c);category(*root,"rewardGrants",rewardGrant,out.rewardGrants,c);category(*root,"shops",shop,out.shops,c);category(*root,"authoringDescriptors",descriptor,out.authoringDescriptors,c);for(auto n:{"projectiles","attacks","enemies","objects","pickups","npcs","dialogues","quests","tileSemantics","stamps"}){const auto*x=findField(*root,n);if(!x)continue;const JsonArray*a=nullptr;if(!array(*x,n,c,a))continue;if(!a->empty())c.error(*x,n,"category decoding is not implemented in 13A1");}r.diagnostics.insert(r.diagnostics.end(),c.diagnostics.begin(),c.diagnostics.end());if(r.diagnostics.empty())r.content=std::move(out);return r;}
}
