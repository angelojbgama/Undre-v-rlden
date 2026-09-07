#include "game/maps/authored_map.h"

#include "engine/data/json.h"

#include <charconv>
#include <fstream>
#include <limits>
#include <sstream>
#include <system_error>
#include <unordered_set>

namespace underworld::game::maps {
namespace {

using engine::data::JsonArray;
using engine::data::JsonNumber;
using engine::data::JsonObject;
using engine::data::JsonValue;

JsonValue pointValue(core::WorldPointI value);
JsonValue areaValue(world::AabbI value);

JsonValue stringValue(std::string value) { return {{}, std::move(value)}; }
JsonValue signedValue(std::int64_t value) {
    return {{}, JsonNumber{std::to_string(value)}};
}
JsonValue unsignedValue(std::uint64_t value) {
    return {{}, JsonNumber{std::to_string(value)}};
}
JsonValue boolValue(bool value) { return {{}, value}; }
JsonValue objectValue(JsonObject value) { return {{}, std::move(value)}; }
JsonValue arrayValue(JsonArray value) { return {{}, std::move(value)}; }

void put(JsonObject& object, std::string key, JsonValue value) {
    object.emplace_back(std::move(key), std::move(value));
}
JsonValue idValue(std::string_view value) { return stringValue(std::string(value)); }

const JsonValue* field(const JsonObject& object, std::string_view name) {
    for (const auto& [key, value] : object) {
        if (key == name) return &value;
    }
    return nullptr;
}

struct Reader final {
    std::vector<AuthoredMapDiagnostic> diagnostics;

    void error(const JsonValue& value, std::string path, std::string code,
               std::string message) {
        diagnostics.push_back({AuthoredMapDiagnosticStage::decode, std::move(code),
                               std::move(message), std::move(path),
                               value.span.begin.line, value.span.begin.column});
    }
};

void allowed(const JsonObject& object, Reader& reader, std::string_view path,
             std::initializer_list<std::string_view> names) {
    for (const auto& [key, value] : object) {
        bool known = false;
        for (const auto name : names) {
            if (key == name) {
                known = true;
                break;
            }
        }
        if (!known) {
            const std::string full = path.empty() ? key : std::string(path) + "." + key;
            reader.error(value, full, "unknown_field", "unknown field");
        }
    }
}

const JsonObject* object(const JsonValue& value, Reader& reader, std::string_view path) {
    const auto* result = std::get_if<JsonObject>(&value.value);
    if (result == nullptr) reader.error(value, std::string(path), "wrong_type",
                                         "must be an object");
    return result;
}

const JsonArray* array(const JsonValue& value, Reader& reader, std::string_view path) {
    const auto* result = std::get_if<JsonArray>(&value.value);
    if (result == nullptr) reader.error(value, std::string(path), "wrong_type",
                                         "must be an array");
    return result;
}

const JsonValue* required(const JsonObject& objectValue, const JsonValue& parent,
                          Reader& reader, std::string_view path, std::string_view name) {
    const auto* result = field(objectValue, name);
    if (result == nullptr) {
        const std::string full = path.empty() ? std::string(name)
                                              : std::string(path) + "." + std::string(name);
        reader.error(parent, full, "missing_required", "missing required field");
    }
    return result;
}

bool parseUnsigned(const JsonValue& value, std::uint64_t& output) {
    const auto* number = std::get_if<JsonNumber>(&value.value);
    if (number == nullptr) return false;
    const auto result = std::from_chars(number->lexeme.data(),
                                        number->lexeme.data() + number->lexeme.size(), output);
    return result.ec == std::errc{} &&
           result.ptr == number->lexeme.data() + number->lexeme.size();
}

bool parseSigned(const JsonValue& value, std::int64_t& output) {
    const auto* number = std::get_if<JsonNumber>(&value.value);
    if (number == nullptr) return false;
    const auto result = std::from_chars(number->lexeme.data(),
                                        number->lexeme.data() + number->lexeme.size(), output);
    return result.ec == std::errc{} &&
           result.ptr == number->lexeme.data() + number->lexeme.size();
}

bool readString(const JsonValue& value, Reader& reader, std::string_view path,
                std::string& output, bool nonEmpty = true) {
    const auto* text = std::get_if<std::string>(&value.value);
    if (text == nullptr || (nonEmpty && text->empty())) {
        reader.error(value, std::string(path), "wrong_type",
                     nonEmpty ? "must be a non-empty string" : "must be a string");
        return false;
    }
    output = *text;
    return true;
}

bool readBool(const JsonValue& value, Reader& reader, std::string_view path, bool& output) {
    const auto* boolean = std::get_if<bool>(&value.value);
    if (boolean == nullptr) {
        reader.error(value, std::string(path), "wrong_type", "must be a boolean");
        return false;
    }
    output = *boolean;
    return true;
}

bool readId(const JsonValue& value, Reader& reader, std::string_view path,
            simulation::DefinitionId& output) {
    std::string text;
    if (!readString(value, reader, path, text)) return false;
    output = simulation::DefinitionId{std::move(text)};
    return true;
}

bool readMapId(const JsonValue& value, Reader& reader, std::string_view path,
               simulation::MapId& output) {
    std::string text;
    if (!readString(value, reader, path, text)) return false;
    output = simulation::MapId{std::move(text)};
    return true;
}

bool readPersistentId(const JsonValue& value, Reader& reader, std::string_view path,
                      simulation::PersistentInstanceId& output) {
    std::uint64_t number{};
    if (!parseUnsigned(value, number) || number == 0) {
        reader.error(value, std::string(path), "invalid_id",
                     "must be a positive unsigned integer");
        return false;
    }
    output = {number};
    return true;
}

bool readU32(const JsonValue& value, Reader& reader, std::string_view path,
             std::uint32_t& output, std::uint32_t maximum = std::numeric_limits<std::uint32_t>::max()) {
    std::uint64_t number{};
    if (!parseUnsigned(value, number) || number > maximum) {
        reader.error(value, std::string(path), "out_of_range",
                     "unsigned integer is outside the destination range");
        return false;
    }
    output = static_cast<std::uint32_t>(number);
    return true;
}

bool readU16(const JsonValue& value, Reader& reader, std::string_view path,
             std::uint16_t& output) {
    std::uint32_t number{};
    if (!readU32(value, reader, path, number, std::numeric_limits<std::uint16_t>::max())) {
        return false;
    }
    output = static_cast<std::uint16_t>(number);
    return true;
}

bool readI32(const JsonValue& value, Reader& reader, std::string_view path, int& output) {
    std::int64_t number{};
    if (!parseSigned(value, number) || number < std::numeric_limits<int>::min() ||
        number > std::numeric_limits<int>::max()) {
        reader.error(value, std::string(path), "out_of_range",
                     "signed integer is outside the destination range");
        return false;
    }
    output = static_cast<int>(number);
    return true;
}

bool readPoint(const JsonValue& value, Reader& reader, std::string_view path,
               core::WorldPointI& output) {
    const auto* objectValue = object(value, reader, path);
    if (objectValue == nullptr) return false;
    allowed(*objectValue, reader, path, {"x", "y"});
    const auto* x = required(*objectValue, value, reader, path, "x");
    const auto* y = required(*objectValue, value, reader, path, "y");
    int xValue{}, yValue{};
    const bool good = x != nullptr && readI32(*x, reader, std::string(path) + ".x", xValue) &&
                      y != nullptr && readI32(*y, reader, std::string(path) + ".y", yValue);
    if (good) output = {xValue, yValue};
    return good;
}

bool readArea(const JsonValue& value, Reader& reader, std::string_view path,
              world::AabbI& output) {
    const auto* objectValue = object(value, reader, path);
    if (objectValue == nullptr) return false;
    allowed(*objectValue, reader, path, {"x", "y", "width", "height"});
    const char* names[] = {"x", "y", "width", "height"};
    int values[4]{};
    bool good = true;
    for (std::size_t index = 0; index < 4; ++index) {
        const auto* valueAt = required(*objectValue, value, reader, path, names[index]);
        if (valueAt == nullptr ||
            !readI32(*valueAt, reader, std::string(path) + "." + names[index], values[index])) {
            good = false;
        }
    }
    if (good) output = {values[0], values[1], values[2], values[3]};
    return good;
}

std::string facingName(gameplay::FacingDirection value) {
    switch (value) {
    case gameplay::FacingDirection::down: return "down";
    case gameplay::FacingDirection::up: return "up";
    case gameplay::FacingDirection::left: return "left";
    case gameplay::FacingDirection::right: return "right";
    }
    return "down";
}

bool readFacing(const JsonValue& value, Reader& reader, std::string_view path,
                gameplay::FacingDirection& output) {
    std::string text;
    if (!readString(value, reader, path, text)) return false;
    if (text == "down") output = gameplay::FacingDirection::down;
    else if (text == "up") output = gameplay::FacingDirection::up;
    else if (text == "left") output = gameplay::FacingDirection::left;
    else if (text == "right") output = gameplay::FacingDirection::right;
    else {
        reader.error(value, std::string(path), "unknown_enum", "unknown facing direction");
        return false;
    }
    return true;
}

template<class Id>
JsonValue persistentValue(Id id) {
    return unsignedValue(id.value);
}

JsonValue encodeStack(const gameplay::ItemStack& stack) {
    JsonObject objectValue;
    put(objectValue, "itemId", idValue(stack.itemId.value()));
    put(objectValue, "quantity", unsignedValue(stack.quantity));
    return ::underworld::game::maps::objectValue(std::move(objectValue));
}

bool decodeStack(const JsonValue& value, Reader& reader, std::string_view path,
                 gameplay::ItemStack& output) {
    const auto* objectValue = object(value, reader, path);
    if (objectValue == nullptr) return false;
    allowed(*objectValue, reader, path, {"itemId", "quantity"});
    const auto* item = required(*objectValue, value, reader, path, "itemId");
    const auto* quantity = required(*objectValue, value, reader, path, "quantity");
    bool good = item != nullptr && readId(*item, reader, std::string(path) + ".itemId",
                                          output.itemId);
    good = quantity != nullptr && readU32(*quantity, reader, std::string(path) + ".quantity",
                                          output.quantity) && good;
    return good;
}

JsonValue encodePlacement(const EnemyPlacement& value) {
    JsonObject objectValue;
    put(objectValue, "id", persistentValue(value.id));
    put(objectValue, "definitionId", idValue(value.definitionId.value()));
    put(objectValue, "position", pointValue(value.position));
    put(objectValue, "facing", stringValue(facingName(value.facing)));
    return ::underworld::game::maps::objectValue(std::move(objectValue));
}

JsonValue encodePlacement(const NpcPlacement& value) {
    JsonObject objectValue;
    put(objectValue, "id", persistentValue(value.id));
    put(objectValue, "definitionId", idValue(value.definitionId.value()));
    put(objectValue, "position", pointValue(value.position));
    put(objectValue, "facing", stringValue(facingName(value.facing)));
    return ::underworld::game::maps::objectValue(std::move(objectValue));
}

template<class Placement>
bool decodePlacement(const JsonValue& value, Reader& reader, std::string_view path,
                     Placement& output) {
    const auto* objectValue = object(value, reader, path);
    if (objectValue == nullptr) return false;
    allowed(*objectValue, reader, path, {"id", "definitionId", "position", "facing"});
    const auto* id = required(*objectValue, value, reader, path, "id");
    const auto* definition = required(*objectValue, value, reader, path, "definitionId");
    const auto* position = required(*objectValue, value, reader, path, "position");
    const auto* facing = required(*objectValue, value, reader, path, "facing");
    bool good = id != nullptr && readPersistentId(*id, reader, std::string(path) + ".id",
                                                  output.id);
    good = definition != nullptr &&
           readId(*definition, reader, std::string(path) + ".definitionId",
                  output.definitionId) && good;
    good = position != nullptr &&
           readPoint(*position, reader, std::string(path) + ".position", output.position) && good;
    good = facing != nullptr &&
           readFacing(*facing, reader, std::string(path) + ".facing", output.facing) && good;
    return good;
}

JsonValue encodeRegion(const MapRegionDefinition& value) {
    JsonObject objectValue;
    put(objectValue, "id", idValue(value.id.value()));
    put(objectValue, "bounds", areaValue(value.bounds));
    if (value.environmentEffectId) {
        put(objectValue, "environmentEffectId", idValue(value.environmentEffectId->value()));
    }
    return ::underworld::game::maps::objectValue(std::move(objectValue));
}

bool decodeRegion(const JsonValue& value, Reader& reader, std::string_view path,
                  MapRegionDefinition& output) {
    const auto* objectValue = object(value, reader, path);
    if (objectValue == nullptr) return false;
    allowed(*objectValue, reader, path, {"id", "bounds", "environmentEffectId"});
    const auto* id = required(*objectValue, value, reader, path, "id");
    const auto* bounds = required(*objectValue, value, reader, path, "bounds");
    bool good = id != nullptr && readId(*id, reader, std::string(path) + ".id", output.id);
    good = bounds != nullptr &&
           readArea(*bounds, reader, std::string(path) + ".bounds", output.bounds) && good;
    if (const auto* effect = field(*objectValue, "environmentEffectId")) {
        simulation::DefinitionId decoded;
        if (readId(*effect, reader, std::string(path) + ".environmentEffectId", decoded)) {
            output.environmentEffectId = std::move(decoded);
        } else {
            good = false;
        }
    }
    return good;
}

const char* triggerName(WorldTriggerKind value) {
    switch (value) {
    case WorldTriggerKind::mapEntered: return "mapEntered";
    case WorldTriggerKind::regionEntered: return "regionEntered";
    case WorldTriggerKind::regionExited: return "regionExited";
    case WorldTriggerKind::encounterStarted: return "encounterStarted";
    case WorldTriggerKind::encounterCompleted: return "encounterCompleted";
    case WorldTriggerKind::objectOpened: return "objectOpened";
    case WorldTriggerKind::objectActivated: return "objectActivated";
    case WorldTriggerKind::objectDeactivated: return "objectDeactivated";
    }
    return "mapEntered";
}
const char* conditionName(WorldConditionKind value) {
    switch (value) {
    case WorldConditionKind::flagSet: return "flagSet";
    case WorldConditionKind::flagNotSet: return "flagNotSet";
    case WorldConditionKind::encounterCompleted: return "encounterCompleted";
    case WorldConditionKind::encounterNotCompleted: return "encounterNotCompleted";
    case WorldConditionKind::doorState: return "doorState";
    case WorldConditionKind::objectActive: return "objectActive";
    case WorldConditionKind::objectInactive: return "objectInactive";
    }
    return "flagSet";
}
const char* actionName(WorldActionKind value) {
    switch (value) {
    case WorldActionKind::setFlag: return "setFlag";
    case WorldActionKind::clearFlag: return "clearFlag";
    case WorldActionKind::startEncounter: return "startEncounter";
    case WorldActionKind::setDoorState: return "setDoorState";
    case WorldActionKind::playPresentationEffect: return "playPresentationEffect";
    }
    return "setFlag";
}
const char* doorName(gameplay::DoorState value) {
    switch (value) {
    case gameplay::DoorState::locked: return "locked";
    case gameplay::DoorState::closed: return "closed";
    case gameplay::DoorState::open: return "open";
    }
    return "closed";
}

bool readDoorState(const JsonValue& value, Reader& reader, std::string_view path,
                   gameplay::DoorState& output) {
    std::string text;
    if (!readString(value, reader, path, text)) return false;
    if (text == "locked") output = gameplay::DoorState::locked;
    else if (text == "closed") output = gameplay::DoorState::closed;
    else if (text == "open") output = gameplay::DoorState::open;
    else {
        reader.error(value, std::string(path), "unknown_enum", "unknown door state");
        return false;
    }
    return true;
}

bool decodeTarget(const JsonObject& objectValue, const JsonValue& parent, Reader& reader,
                  std::string_view path, bool instance, simulation::DefinitionId& definition,
                  simulation::PersistentInstanceId& instanceId) {
    const auto* value = required(objectValue, parent, reader, path,
                                  instance ? "instanceTarget" : "target");
    if (value == nullptr) return false;
    if (instance) return readPersistentId(*value, reader,
                                          std::string(path) + ".instanceTarget", instanceId);
    return readId(*value, reader, std::string(path) + ".target", definition);
}

bool decodeTrigger(const JsonValue& value, Reader& reader, std::string_view path,
                   WorldTrigger& output) {
    const auto* objectValue = object(value, reader, path);
    if (objectValue == nullptr) return false;
    allowed(*objectValue, reader, path, {"kind", "target", "instanceTarget"});
    const auto* kind = required(*objectValue, value, reader, path, "kind");
    std::string text;
    bool good = kind != nullptr && readString(*kind, reader, std::string(path) + ".kind", text);
    if (good && text == "mapEntered") output.kind = WorldTriggerKind::mapEntered;
    else if (good && text == "regionEntered") output.kind = WorldTriggerKind::regionEntered;
    else if (good && text == "regionExited") output.kind = WorldTriggerKind::regionExited;
    else if (good && text == "encounterStarted") output.kind = WorldTriggerKind::encounterStarted;
    else if (good && text == "encounterCompleted") output.kind = WorldTriggerKind::encounterCompleted;
    else if (good && text == "objectOpened") output.kind = WorldTriggerKind::objectOpened;
    else if (good && text == "objectActivated") output.kind = WorldTriggerKind::objectActivated;
    else if (good && text == "objectDeactivated") output.kind = WorldTriggerKind::objectDeactivated;
    else if (good) {
        reader.error(*kind, std::string(path) + ".kind", "unknown_enum",
                     "unknown world trigger");
        good = false;
    }
    const bool wantsInstance = output.kind == WorldTriggerKind::objectOpened ||
                               output.kind == WorldTriggerKind::objectActivated ||
                               output.kind == WorldTriggerKind::objectDeactivated;
    const auto* target = field(*objectValue, "target");
    const auto* instance = field(*objectValue, "instanceTarget");
    if (target != nullptr && instance != nullptr) {
        reader.error(*instance, std::string(path), "ambiguous_target",
                     "target and instanceTarget cannot both be present");
        good = false;
    } else if (wantsInstance) {
        good = instance != nullptr && decodeTarget(*objectValue, value, reader, path, true,
                                                    output.definitionTarget, output.instanceTarget) &&
               good;
    } else if (output.kind != WorldTriggerKind::mapEntered || target != nullptr ||
               instance != nullptr) {
        good = target != nullptr && decodeTarget(*objectValue, value, reader, path, false,
                                                 output.definitionTarget, output.instanceTarget) &&
               good;
    }
    return good;
}

bool decodeCondition(const JsonValue& value, Reader& reader, std::string_view path,
                     WorldCondition& output) {
    const auto* objectValue = object(value, reader, path);
    if (objectValue == nullptr) return false;
    allowed(*objectValue, reader, path, {"kind", "target", "instanceTarget", "doorState"});
    const auto* kind = required(*objectValue, value, reader, path, "kind");
    std::string text;
    bool good = kind != nullptr && readString(*kind, reader, std::string(path) + ".kind", text);
    if (good && text == "flagSet") output.kind = WorldConditionKind::flagSet;
    else if (good && text == "flagNotSet") output.kind = WorldConditionKind::flagNotSet;
    else if (good && text == "encounterCompleted") output.kind = WorldConditionKind::encounterCompleted;
    else if (good && text == "encounterNotCompleted") output.kind = WorldConditionKind::encounterNotCompleted;
    else if (good && text == "doorState") output.kind = WorldConditionKind::doorState;
    else if (good && text == "objectActive") output.kind = WorldConditionKind::objectActive;
    else if (good && text == "objectInactive") output.kind = WorldConditionKind::objectInactive;
    else if (good) {
        reader.error(*kind, std::string(path) + ".kind", "unknown_enum",
                     "unknown world condition");
        good = false;
    }
    const bool wantsInstance = output.kind == WorldConditionKind::doorState ||
                               output.kind == WorldConditionKind::objectActive ||
                               output.kind == WorldConditionKind::objectInactive;
    const auto* target = field(*objectValue, "target");
    const auto* instance = field(*objectValue, "instanceTarget");
    if (target != nullptr && instance != nullptr) {
        reader.error(*instance, std::string(path), "ambiguous_target",
                     "target and instanceTarget cannot both be present");
        good = false;
    } else {
        good = decodeTarget(*objectValue, value, reader, path, wantsInstance,
                            output.definitionTarget, output.instanceTarget) && good;
    }
    if (output.kind == WorldConditionKind::doorState) {
        const auto* state = required(*objectValue, value, reader, path, "doorState");
        good = state != nullptr && readDoorState(*state, reader,
                                                 std::string(path) + ".doorState",
                                                 output.doorState) && good;
    } else if (const auto* state = field(*objectValue, "doorState")) {
        reader.error(*state, std::string(path) + ".doorState", "unexpected_field",
                     "doorState is only valid for doorState conditions");
        good = false;
    }
    return good;
}

bool decodeAction(const JsonValue& value, Reader& reader, std::string_view path,
                  WorldAction& output) {
    const auto* objectValue = object(value, reader, path);
    if (objectValue == nullptr) return false;
    allowed(*objectValue, reader, path, {"kind", "target", "instanceTarget", "doorState"});
    const auto* kind = required(*objectValue, value, reader, path, "kind");
    std::string text;
    bool good = kind != nullptr && readString(*kind, reader, std::string(path) + ".kind", text);
    if (good && text == "setFlag") output.kind = WorldActionKind::setFlag;
    else if (good && text == "clearFlag") output.kind = WorldActionKind::clearFlag;
    else if (good && text == "startEncounter") output.kind = WorldActionKind::startEncounter;
    else if (good && text == "setDoorState") output.kind = WorldActionKind::setDoorState;
    else if (good && text == "playPresentationEffect") output.kind = WorldActionKind::playPresentationEffect;
    else if (good) {
        reader.error(*kind, std::string(path) + ".kind", "unknown_enum",
                     "unknown world action");
        good = false;
    }
    const bool wantsInstance = output.kind == WorldActionKind::setDoorState;
    good = decodeTarget(*objectValue, value, reader, path, wantsInstance,
                        output.definitionTarget, output.instanceTarget) && good;
    if (output.kind == WorldActionKind::setDoorState) {
        const auto* state = required(*objectValue, value, reader, path, "doorState");
        good = state != nullptr && readDoorState(*state, reader,
                                                 std::string(path) + ".doorState",
                                                 output.doorState) && good;
    } else if (const auto* state = field(*objectValue, "doorState")) {
        reader.error(*state, std::string(path) + ".doorState", "unexpected_field",
                     "doorState is only valid for setDoorState actions");
        good = false;
    }
    return good;
}

JsonValue encodeRule(const WorldRuleDefinition& value) {
    JsonObject rule;
    put(rule, "id", idValue(value.id.value()));
    put(rule, "once", boolValue(value.once));
    JsonObject trigger;
    put(trigger, "kind", stringValue(triggerName(value.trigger.kind)));
    if (value.trigger.instanceTarget) {
        put(trigger, "instanceTarget", unsignedValue(value.trigger.instanceTarget.value));
    } else if (value.trigger.kind != WorldTriggerKind::mapEntered) {
        put(trigger, "target", idValue(value.trigger.definitionTarget.value()));
    }
    put(rule, "trigger", objectValue(std::move(trigger)));
    JsonArray conditions;
    for (const auto& condition : value.conditions) {
        JsonObject item;
        put(item, "kind", stringValue(conditionName(condition.kind)));
        if (condition.instanceTarget) put(item, "instanceTarget",
                                          unsignedValue(condition.instanceTarget.value));
        else put(item, "target", idValue(condition.definitionTarget.value()));
        if (condition.kind == WorldConditionKind::doorState) {
            put(item, "doorState", stringValue(doorName(condition.doorState)));
        }
        conditions.push_back(objectValue(std::move(item)));
    }
    put(rule, "conditions", arrayValue(std::move(conditions)));
    JsonArray actions;
    for (const auto& action : value.actions) {
        JsonObject item;
        put(item, "kind", stringValue(actionName(action.kind)));
        if (action.instanceTarget) put(item, "instanceTarget",
                                       unsignedValue(action.instanceTarget.value));
        else put(item, "target", idValue(action.definitionTarget.value()));
        if (action.kind == WorldActionKind::setDoorState) {
            put(item, "doorState", stringValue(doorName(action.doorState)));
        }
        actions.push_back(objectValue(std::move(item)));
    }
    put(rule, "actions", arrayValue(std::move(actions)));
    return objectValue(std::move(rule));
}

JsonValue pointValue(core::WorldPointI value) {
    JsonObject objectValue;
    put(objectValue, "x", signedValue(value.x));
    put(objectValue, "y", signedValue(value.y));
    return ::underworld::game::maps::objectValue(std::move(objectValue));
}

JsonValue areaValue(world::AabbI value) {
    JsonObject objectValue;
    put(objectValue, "x", signedValue(value.x));
    put(objectValue, "y", signedValue(value.y));
    put(objectValue, "width", signedValue(value.width));
    put(objectValue, "height", signedValue(value.height));
    return ::underworld::game::maps::objectValue(std::move(objectValue));
}

bool decodeRule(const JsonValue& value, Reader& reader, std::string_view path,
                WorldRuleDefinition& output) {
    const auto* objectValue = object(value, reader, path);
    if (objectValue == nullptr) return false;
    allowed(*objectValue, reader, path, {"id", "once", "trigger", "conditions", "actions"});
    const auto* id = required(*objectValue, value, reader, path, "id");
    const auto* once = required(*objectValue, value, reader, path, "once");
    const auto* trigger = required(*objectValue, value, reader, path, "trigger");
    const auto* conditions = required(*objectValue, value, reader, path, "conditions");
    const auto* actions = required(*objectValue, value, reader, path, "actions");
    bool good = id != nullptr && readId(*id, reader, std::string(path) + ".id", output.id);
    good = once != nullptr && readBool(*once, reader, std::string(path) + ".once", output.once) && good;
    good = trigger != nullptr && decodeTrigger(*trigger, reader,
                                               std::string(path) + ".trigger", output.trigger) && good;
    const auto* conditionArray = conditions == nullptr ? nullptr :
        array(*conditions, reader, std::string(path) + ".conditions");
    if (conditionArray == nullptr) good = false;
    else {
        output.conditions.reserve(conditionArray->size());
        for (std::size_t index = 0; index < conditionArray->size(); ++index) {
            WorldCondition item;
            const std::string itemPath = std::string(path) + ".conditions[" +
                                          std::to_string(index) + "]";
            const bool itemGood = decodeCondition((*conditionArray)[index], reader, itemPath, item);
            if (itemGood) output.conditions.push_back(std::move(item));
            good = itemGood && good;
        }
    }
    const auto* actionArray = actions == nullptr ? nullptr :
        array(*actions, reader, std::string(path) + ".actions");
    if (actionArray == nullptr) good = false;
    else {
        output.actions.reserve(actionArray->size());
        for (std::size_t index = 0; index < actionArray->size(); ++index) {
            WorldAction item;
            const std::string itemPath = std::string(path) + ".actions[" +
                                          std::to_string(index) + "]";
            const bool itemGood = decodeAction((*actionArray)[index], reader, itemPath, item);
            if (itemGood) output.actions.push_back(std::move(item));
            good = itemGood && good;
        }
    }
    return good;
}

bool decodePayload(const JsonValue& value, Reader& reader, std::string_view path,
                   gameplay::PickupPayload& output) {
    const auto* objectValue = object(value, reader, path);
    if (objectValue == nullptr) return false;
    allowed(*objectValue, reader, path, {"kind", "amount", "itemId", "quantity"});
    const auto* kind = required(*objectValue, value, reader, path, "kind");
    std::string text;
    bool good = kind != nullptr && readString(*kind, reader, std::string(path) + ".kind", text);
    if (good && text == "health") {
        const auto* amount = required(*objectValue, value, reader, path, "amount");
        int result{};
        good = amount != nullptr && readI32(*amount, reader,
                                            std::string(path) + ".amount", result) && good;
        if (good) output = gameplay::HealthPickup{result};
    } else if (good && text == "currency") {
        const auto* amount = required(*objectValue, value, reader, path, "amount");
        std::uint64_t result{};
        good = amount != nullptr && parseUnsigned(*amount, result) && good;
        if (!good && amount != nullptr) reader.error(*amount, std::string(path) + ".amount",
                                                      "out_of_range", "invalid unsigned amount");
        if (good) output = gameplay::CurrencyPickup{result};
    } else if (good && text == "item") {
        const auto* item = required(*objectValue, value, reader, path, "itemId");
        const auto* quantity = required(*objectValue, value, reader, path, "quantity");
        simulation::DefinitionId itemId;
        std::uint32_t count{};
        good = item != nullptr && readId(*item, reader, std::string(path) + ".itemId", itemId) && good;
        good = quantity != nullptr && readU32(*quantity, reader,
                                              std::string(path) + ".quantity", count) && good;
        if (good) output = gameplay::ItemPickup{std::move(itemId), count};
    } else if (good) {
        reader.error(*kind, std::string(path) + ".kind", "unknown_enum",
                     "unknown pickup payload kind");
        good = false;
    }
    return good;
}

JsonValue encodePayload(const gameplay::PickupPayload& payload) {
    JsonObject objectValue;
    if (const auto* value = std::get_if<gameplay::HealthPickup>(&payload)) {
        put(objectValue, "kind", stringValue("health"));
        put(objectValue, "amount", signedValue(value->amount));
    } else if (const auto* value = std::get_if<gameplay::CurrencyPickup>(&payload)) {
        put(objectValue, "kind", stringValue("currency"));
        put(objectValue, "amount", unsignedValue(value->amount));
    } else {
        const auto& item = std::get<gameplay::ItemPickup>(payload);
        put(objectValue, "kind", stringValue("item"));
        put(objectValue, "itemId", idValue(item.itemId.value()));
        put(objectValue, "quantity", unsignedValue(item.quantity));
    }
    return ::underworld::game::maps::objectValue(std::move(objectValue));
}

JsonValue encodeObject(const ObjectPlacement& value) {
    JsonObject objectValue;
    put(objectValue, "id", persistentValue(value.id));
    put(objectValue, "definitionId", idValue(value.definitionId.value()));
    put(objectValue, "position", pointValue(value.position));
    JsonArray contents;
    for (const auto& stack : value.initialContents) contents.push_back(encodeStack(stack));
    put(objectValue, "initialContents", arrayValue(std::move(contents)));
    return ::underworld::game::maps::objectValue(std::move(objectValue));
}

bool decodeObject(const JsonValue& value, Reader& reader, std::string_view path,
                  ObjectPlacement& output) {
    const auto* objectValue = object(value, reader, path);
    if (objectValue == nullptr) return false;
    allowed(*objectValue, reader, path, {"id", "definitionId", "position", "initialContents"});
    const auto* id = required(*objectValue, value, reader, path, "id");
    const auto* definition = required(*objectValue, value, reader, path, "definitionId");
    const auto* position = required(*objectValue, value, reader, path, "position");
    const auto* contents = required(*objectValue, value, reader, path, "initialContents");
    bool good = id != nullptr && readPersistentId(*id, reader, std::string(path) + ".id", output.id);
    good = definition != nullptr && readId(*definition, reader,
                                           std::string(path) + ".definitionId",
                                           output.definitionId) && good;
    good = position != nullptr && readPoint(*position, reader,
                                            std::string(path) + ".position",
                                            output.position) && good;
    const auto* values = contents == nullptr ? nullptr :
        array(*contents, reader, std::string(path) + ".initialContents");
    if (values == nullptr) good = false;
    else {
        for (std::size_t index = 0; index < values->size(); ++index) {
            gameplay::ItemStack stack;
            const bool stackGood = decodeStack((*values)[index], reader,
                std::string(path) + ".initialContents[" + std::to_string(index) + "]", stack);
            if (stackGood) output.initialContents.push_back(std::move(stack));
            good = stackGood && good;
        }
    }
    return good;
}

JsonValue encodeLink(const MapLink& value) {
    JsonObject objectValue;
    put(objectValue, "id", stringValue(value.id));
    put(objectValue, "trigger", areaValue(value.trigger));
    put(objectValue, "targetMapId", idValue(value.targetMapId.value()));
    put(objectValue, "targetSpawnId", idValue(value.targetSpawnId.value()));
    return ::underworld::game::maps::objectValue(std::move(objectValue));
}

bool decodeLink(const JsonValue& value, Reader& reader, std::string_view path, MapLink& output) {
    const auto* objectValue = object(value, reader, path);
    if (objectValue == nullptr) return false;
    allowed(*objectValue, reader, path, {"id", "trigger", "targetMapId", "targetSpawnId"});
    const auto* id = required(*objectValue, value, reader, path, "id");
    const auto* trigger = required(*objectValue, value, reader, path, "trigger");
    const auto* targetMap = required(*objectValue, value, reader, path, "targetMapId");
    const auto* targetSpawn = required(*objectValue, value, reader, path, "targetSpawnId");
    bool good = id != nullptr && readString(*id, reader, std::string(path) + ".id", output.id);
    good = trigger != nullptr && readArea(*trigger, reader,
                                          std::string(path) + ".trigger",
                                          output.trigger) && good;
    good = targetMap != nullptr && readMapId(*targetMap, reader,
                                             std::string(path) + ".targetMapId",
                                             output.targetMapId) && good;
    std::string spawn;
    good = targetSpawn != nullptr && readString(*targetSpawn, reader,
                                                std::string(path) + ".targetSpawnId",
                                                spawn) && good;
    if (targetSpawn != nullptr && !spawn.empty()) output.targetSpawnId = simulation::SpawnId{std::move(spawn)};
    return good;
}

JsonValue encodeOverride(const AuthoredPlacementOverride& value) {
    JsonObject objectValue;
    put(objectValue, "instanceId", persistentValue(value.instanceId));
    put(objectValue, "propertyId", stringValue(value.propertyId));
    JsonObject property;
    switch (value.value.kind) {
    case AuthoredPropertyValueKind::boolean:
        put(property, "kind", stringValue("boolean"));
        put(property, "value", boolValue(value.value.booleanValue));
        break;
    case AuthoredPropertyValueKind::integer:
        put(property, "kind", stringValue("integer"));
        put(property, "value", signedValue(value.value.integerValue));
        break;
    case AuthoredPropertyValueKind::enumeration:
        put(property, "kind", stringValue("enumeration"));
        put(property, "value", stringValue(value.value.textValue));
        break;
    case AuthoredPropertyValueKind::definitionReference:
        put(property, "kind", stringValue("definitionReference"));
        put(property, "value", idValue(value.value.definitionValue.value()));
        break;
    case AuthoredPropertyValueKind::instanceReference:
        put(property, "kind", stringValue("instanceReference"));
        put(property, "value", unsignedValue(value.value.instanceValue.value));
        break;
    }
    put(objectValue, "value", ::underworld::game::maps::objectValue(std::move(property)));
    return ::underworld::game::maps::objectValue(std::move(objectValue));
}

bool decodeOverride(const JsonValue& value, Reader& reader, std::string_view path,
                    AuthoredPlacementOverride& output) {
    const auto* objectValue = object(value, reader, path);
    if (objectValue == nullptr) return false;
    allowed(*objectValue, reader, path, {"instanceId", "propertyId", "value"});
    const auto* instance = required(*objectValue, value, reader, path, "instanceId");
    const auto* property = required(*objectValue, value, reader, path, "propertyId");
    const auto* propertyValue = required(*objectValue, value, reader, path, "value");
    bool good = instance != nullptr && readPersistentId(*instance, reader,
                                                        std::string(path) + ".instanceId",
                                                        output.instanceId);
    good = property != nullptr && readString(*property, reader,
                                             std::string(path) + ".propertyId",
                                             output.propertyId) && good;
    const auto* valueObject = propertyValue == nullptr ? nullptr :
        object(*propertyValue, reader, std::string(path) + ".value");
    if (valueObject == nullptr) return false;
    allowed(*valueObject, reader, std::string(path) + ".value", {"kind", "value"});
    const auto* kind = required(*valueObject, *propertyValue, reader,
                                std::string(path) + ".value", "kind");
    const auto* scalar = required(*valueObject, *propertyValue, reader,
                                  std::string(path) + ".value", "value");
    std::string kindText;
    good = kind != nullptr && readString(*kind, reader,
                                         std::string(path) + ".value.kind",
                                         kindText) && good;
    if (kindText == "boolean") {
        output.value.kind = AuthoredPropertyValueKind::boolean;
        good = scalar != nullptr && readBool(*scalar, reader,
                                             std::string(path) + ".value.value",
                                             output.value.booleanValue) && good;
    } else if (kindText == "integer") {
        output.value.kind = AuthoredPropertyValueKind::integer;
        good = scalar != nullptr && parseSigned(*scalar, output.value.integerValue) && good;
        if (!good && scalar != nullptr) reader.error(*scalar,
            std::string(path) + ".value.value", "out_of_range", "invalid signed integer");
    } else if (kindText == "enumeration") {
        output.value.kind = AuthoredPropertyValueKind::enumeration;
        good = scalar != nullptr && readString(*scalar, reader,
                                               std::string(path) + ".value.value",
                                               output.value.textValue) && good;
    } else if (kindText == "definitionReference") {
        output.value.kind = AuthoredPropertyValueKind::definitionReference;
        good = scalar != nullptr && readId(*scalar, reader,
                                           std::string(path) + ".value.value",
                                           output.value.definitionValue) && good;
    } else if (kindText == "instanceReference") {
        output.value.kind = AuthoredPropertyValueKind::instanceReference;
        good = scalar != nullptr && readPersistentId(*scalar, reader,
                                                     std::string(path) + ".value.value",
                                                     output.value.instanceValue) && good;
    } else if (good) {
        reader.error(*kind, std::string(path) + ".value.kind", "unknown_enum",
                     "unknown property value kind");
        good = false;
    }
    return good;
}

std::optional<std::pair<std::string, std::string>> validateAuthoredOverrides(
    const AuthoredMapSource& source) {
    std::unordered_set<std::uint64_t> placementIds;
    const auto collect = [&](const auto& placements) {
        for (const auto& placement : placements) placementIds.emplace(placement.id.value);
    };
    collect(source.geometry.enemies);
    collect(source.geometry.npcs);
    collect(source.geometry.objects);
    collect(source.geometry.pickups);

    std::unordered_set<std::string> overrideKeys;
    for (std::size_t index = 0; index < source.placementOverrides.size(); ++index) {
        const auto& authoredOverride = source.placementOverrides[index];
        const std::string path = "placementOverrides[" + std::to_string(index) + "]";
        if (!authoredOverride.instanceId || authoredOverride.propertyId.empty()) {
            return std::pair{path, "placement override requires a non-empty instance and property"};
        }
        if (!placementIds.contains(authoredOverride.instanceId.value)) {
            return std::pair{path + ".instanceId",
                             "placement override references an unknown instance"};
        }
        const std::string key = std::to_string(authoredOverride.instanceId.value) + "\n" +
                                authoredOverride.propertyId;
        if (!overrideKeys.emplace(key).second) {
            return std::pair{path, "duplicate placement override for the same property"};
        }
        switch (authoredOverride.value.kind) {
        case AuthoredPropertyValueKind::boolean:
        case AuthoredPropertyValueKind::integer:
            break;
        case AuthoredPropertyValueKind::enumeration:
            if (authoredOverride.value.textValue.empty()) {
                return std::pair{path + ".value.value", "enumeration override value is empty"};
            }
            break;
        case AuthoredPropertyValueKind::definitionReference:
            if (authoredOverride.value.definitionValue.empty()) {
                return std::pair{path + ".value.value",
                                 "definition reference override is empty"};
            }
            break;
        case AuthoredPropertyValueKind::instanceReference:
            if (!authoredOverride.value.instanceValue ||
                !placementIds.contains(authoredOverride.value.instanceValue.value)) {
                return std::pair{path + ".value.value",
                                 "instance reference override points to an unknown instance"};
            }
            break;
        }
    }
    return std::nullopt;
}

JsonValue encodeEncounter(const EncounterDefinition& value) {
    JsonObject objectValue;
    put(objectValue, "id", idValue(value.id.value()));
    JsonArray participants;
    for (const auto participant : value.participants) participants.push_back(unsignedValue(participant.value));
    put(objectValue, "participants", arrayValue(std::move(participants)));
    if (value.rewardGrantId) put(objectValue, "rewardGrantId", idValue(value.rewardGrantId->value()));
    return ::underworld::game::maps::objectValue(std::move(objectValue));
}

bool decodeEncounter(const JsonValue& value, Reader& reader, std::string_view path,
                     EncounterDefinition& output) {
    const auto* objectValue = object(value, reader, path);
    if (objectValue == nullptr) return false;
    allowed(*objectValue, reader, path, {"id", "participants", "rewardGrantId"});
    const auto* id = required(*objectValue, value, reader, path, "id");
    const auto* participants = required(*objectValue, value, reader, path, "participants");
    bool good = id != nullptr && readId(*id, reader, std::string(path) + ".id", output.id);
    const auto* values = participants == nullptr ? nullptr :
        array(*participants, reader, std::string(path) + ".participants");
    if (values == nullptr) good = false;
    else {
        for (std::size_t index = 0; index < values->size(); ++index) {
            simulation::PersistentInstanceId participant;
            const bool participantGood = readPersistentId((*values)[index], reader,
                std::string(path) + ".participants[" + std::to_string(index) + "]", participant);
            if (participantGood) output.participants.push_back(participant);
            good = participantGood && good;
        }
    }
    if (const auto* reward = field(*objectValue, "rewardGrantId")) {
        simulation::DefinitionId rewardId;
        good = readId(*reward, reader, std::string(path) + ".rewardGrantId", rewardId) && good;
        if (!rewardId.empty()) output.rewardGrantId = std::move(rewardId);
    }
    return good;
}

template<class Encoder, class Vector>
JsonValue encodeArray(const Vector& values, Encoder encoder) {
    JsonArray result;
    for (const auto& value : values) result.push_back(encoder(value));
    return arrayValue(std::move(result));
}

} // namespace

MapData mapDataFromAuthored(const AuthoredMapSource& source) {
    MapData result;
    result.id = source.geometry.id;
    result.width = source.geometry.width;
    result.height = source.geometry.height;
    result.tileSize = source.geometry.tileSize;
    result.tileReferences = source.geometry.tileReferences;
    result.layers = source.geometry.layers;
    result.collision = source.geometry.collision;
    result.playerSpawns = source.geometry.playerSpawns;
    result.enemies = source.geometry.enemies;
    result.npcs = source.geometry.npcs;
    result.objects = source.geometry.objects;
    result.pickups = source.geometry.pickups;
    result.links = source.geometry.links;
    result.regions = source.regions;
    result.worldRules = source.worldRules;
    result.encounters = source.encounters;
    return result;
}

AuthoredMapSource authoredMapFromMapData(const MapData& data) {
    AuthoredMapSource result;
    result.geometry.id = data.id;
    result.geometry.width = data.width;
    result.geometry.height = data.height;
    result.geometry.tileSize = data.tileSize;
    result.geometry.tileReferences = data.tileReferences;
    result.geometry.layers = data.layers;
    result.geometry.collision = data.collision;
    result.geometry.playerSpawns = data.playerSpawns;
    result.geometry.enemies = data.enemies;
    result.geometry.npcs = data.npcs;
    result.geometry.objects = data.objects;
    result.geometry.pickups = data.pickups;
    result.geometry.links = data.links;
    result.regions = data.regions;
    result.worldRules = data.worldRules;
    result.encounters = data.encounters;
    return result;
}

std::string encodeAuthoredMapJson(const AuthoredMapSource& source) {
    const auto& geometry = source.geometry;
    JsonObject root;
    put(root, "format", stringValue("dungeon-underworld-map-source"));
    put(root, "version", unsignedValue(3));
    put(root, "id", idValue(geometry.id.value()));
    put(root, "width", unsignedValue(geometry.width));
    put(root, "height", unsignedValue(geometry.height));
    put(root, "tileSize", unsignedValue(geometry.tileSize));
    JsonArray references;
    for (const auto& value : geometry.tileReferences) {
        JsonObject item;
        put(item, "tilesetId", idValue(value.tilesetId.value()));
        put(item, "sourceIndex", unsignedValue(value.sourceIndex));
        put(item, "flags", unsignedValue(static_cast<std::uint8_t>(value.flags)));
        references.push_back(objectValue(std::move(item)));
    }
    put(root, "tileReferences", arrayValue(std::move(references)));
    JsonArray layers;
    for (const auto& value : geometry.layers) {
        JsonObject item;
        put(item, "name", stringValue(value.name));
        put(item, "visible", boolValue(value.visible));
        JsonArray cells;
        for (const auto cell : value.cells) cells.push_back(cell ? unsignedValue(*cell) :
                                                            JsonValue{{}, nullptr});
        put(item, "cells", arrayValue(std::move(cells)));
        layers.push_back(objectValue(std::move(item)));
    }
    put(root, "layers", arrayValue(std::move(layers)));
    JsonArray collision;
    for (const auto value : geometry.collision) collision.push_back(unsignedValue(value));
    put(root, "collision", arrayValue(std::move(collision)));
    JsonArray spawns;
    for (const auto& value : geometry.playerSpawns) {
        JsonObject item;
        put(item, "id", stringValue(std::string(value.id.value())));
        put(item, "position", pointValue(value.position));
        put(item, "facing", stringValue(facingName(value.facing)));
        spawns.push_back(objectValue(std::move(item)));
    }
    put(root, "playerSpawns", arrayValue(std::move(spawns)));
    put(root, "enemies", encodeArray(geometry.enemies,
        [](const auto& value) { return encodePlacement(value); }));
    put(root, "npcs", encodeArray(geometry.npcs,
        [](const auto& value) { return encodePlacement(value); }));
    put(root, "objects", encodeArray(geometry.objects,
        [](const auto& value) { return encodeObject(value); }));
    JsonArray pickups;
    for (const auto& value : geometry.pickups) {
        JsonObject item;
        put(item, "id", persistentValue(value.id));
        put(item, "definitionId", idValue(value.definitionId.value()));
        put(item, "visualId", idValue(value.visualId.value()));
        put(item, "position", pointValue(value.position));
        put(item, "collectionBounds", areaValue(value.collectionBounds));
        put(item, "payload", encodePayload(value.payload));
        pickups.push_back(objectValue(std::move(item)));
    }
    put(root, "pickups", arrayValue(std::move(pickups)));
    put(root, "links", encodeArray(geometry.links,
        [](const auto& value) { return encodeLink(value); }));
    put(root, "regions", encodeArray(source.regions,
        [](const auto& value) { return encodeRegion(value); }));
    put(root, "worldRules", encodeArray(source.worldRules,
        [](const auto& value) { return encodeRule(value); }));
    put(root, "encounters", encodeArray(source.encounters,
        [](const auto& value) { return encodeEncounter(value); }));
    put(root, "placementOverrides", encodeArray(source.placementOverrides,
        [](const auto& value) { return encodeOverride(value); }));
    return engine::data::writeJson(objectValue(std::move(root)));
}

AuthoredMapDecodeResult decodeAuthoredMapJson(std::string_view json) {
    AuthoredMapDecodeResult result;
    const auto parsed = engine::data::parseJson(json);
    for (const auto& diagnostic : parsed.diagnostics) {
        result.diagnostics.push_back({AuthoredMapDiagnosticStage::decode, "json_parse",
            diagnostic.message, {}, diagnostic.location.line, diagnostic.location.column});
    }
    if (!parsed.value || !result.diagnostics.empty()) return result;
    Reader reader;
    const auto* root = object(*parsed.value, reader, "");
    if (root == nullptr) return result;
    allowed(*root, reader, "", {"format", "version", "id", "width", "height", "tileSize",
                                "tileReferences", "layers", "collision", "playerSpawns",
                                "enemies", "npcs", "objects", "pickups", "links", "regions",
                                "worldRules", "encounters", "placementOverrides"});
    const auto* format = required(*root, *parsed.value, reader, "", "format");
    const auto* version = required(*root, *parsed.value, reader, "", "version");
    const auto* id = required(*root, *parsed.value, reader, "", "id");
    const auto* width = required(*root, *parsed.value, reader, "", "width");
    const auto* height = required(*root, *parsed.value, reader, "", "height");
    const auto* tileSize = required(*root, *parsed.value, reader, "", "tileSize");
    const auto* references = required(*root, *parsed.value, reader, "", "tileReferences");
    const auto* layers = required(*root, *parsed.value, reader, "", "layers");
    const auto* collision = required(*root, *parsed.value, reader, "", "collision");
    const auto* spawns = required(*root, *parsed.value, reader, "", "playerSpawns");
    AuthoredMapSource source;
    bool good = true;
    std::string formatText;
    if (format == nullptr || !readString(*format, reader, "format", formatText) ||
        formatText != "dungeon-underworld-map-source") {
        if (format != nullptr) reader.error(*format, "format", "invalid_format",
                                             "invalid format identifier");
        good = false;
    }
    std::uint64_t schemaVersion{};
    if (version == nullptr || !parseUnsigned(*version, schemaVersion) ||
        (schemaVersion != 1 && schemaVersion != 2 && schemaVersion != 3)) {
        if (version != nullptr) reader.error(*version, "version", "unsupported_version",
                                              "unsupported map schema version");
        good = false;
    }
    if (schemaVersion == 1) {
        if (const auto* regionsValue = field(*root, "regions")) {
            if (const auto* regions = std::get_if<JsonArray>(&regionsValue->value)) {
                for (std::size_t index = 0; index < regions->size(); ++index) {
                    if (const auto* region = std::get_if<JsonObject>(&(*regions)[index].value)) {
                        if (const auto* environment = field(*region, "environmentEffectId")) {
                            reader.error(*environment, "regions[" + std::to_string(index) +
                                             "].environmentEffectId", "unsupported_version",
                                         "environment effect bindings require map schema version 2");
                            good = false;
                        }
                    }
                }
            }
        }
        if (const auto* rulesValue = field(*root, "worldRules")) {
            if (const auto* rules = std::get_if<JsonArray>(&rulesValue->value)) {
                for (std::size_t ruleIndex = 0; ruleIndex < rules->size(); ++ruleIndex) {
                    const auto* rule = std::get_if<JsonObject>(&(*rules)[ruleIndex].value);
                    if (rule == nullptr) continue;
                    const auto* actionsValue = field(*rule, "actions");
                    const auto* actions = actionsValue == nullptr ? nullptr :
                        std::get_if<JsonArray>(&actionsValue->value);
                    if (actions == nullptr) continue;
                    for (std::size_t actionIndex = 0; actionIndex < actions->size(); ++actionIndex) {
                        const auto* action = std::get_if<JsonObject>(&(*actions)[actionIndex].value);
                        if (action == nullptr) continue;
                        const auto* kind = field(*action, "kind");
                        const auto* text = kind == nullptr ? nullptr :
                            std::get_if<std::string>(&kind->value);
                        if (text != nullptr && *text == "playPresentationEffect") {
                            reader.error(*kind, "worldRules[" + std::to_string(ruleIndex) +
                                             "].actions[" + std::to_string(actionIndex) +
                                             "].kind", "unsupported_version",
                                         "presentation effect actions require map schema version 2");
                            good = false;
                        }
                    }
                }
            }
        }
    }
    if (schemaVersion >= 1 && schemaVersion < 3) {
        if (const auto* rulesValue = field(*root, "worldRules")) {
            if (const auto* rules = std::get_if<JsonArray>(&rulesValue->value)) {
                for (std::size_t ruleIndex = 0; ruleIndex < rules->size(); ++ruleIndex) {
                    const auto* rule = std::get_if<JsonObject>(&(*rules)[ruleIndex].value);
                    if (rule == nullptr) continue;
                    const auto rejectArrayKind = [&](std::string_view arrayName, std::string_view kindName,
                                                     std::string_view path) {
                        const auto* arrayValue = field(*rule, arrayName);
                        const auto* values = arrayValue == nullptr ? nullptr :
                            std::get_if<JsonArray>(&arrayValue->value);
                        if (values == nullptr) return;
                        for (std::size_t index = 0; index < values->size(); ++index) {
                            const auto* entry = std::get_if<JsonObject>(&(*values)[index].value);
                            if (entry == nullptr) continue;
                            const auto* kind = field(*entry, "kind");
                            const auto* text = kind == nullptr ? nullptr :
                                std::get_if<std::string>(&kind->value);
                            if (text != nullptr && *text == kindName) {
                                reader.error(*kind, std::string(path) + "[" + std::to_string(index) + "].kind",
                                              "unsupported_version", "object activation rules require map schema version 3");
                                good = false;
                            }
                        }
                    };
                    const auto triggerPath = "worldRules[" + std::to_string(ruleIndex) + "].trigger";
                    if (const auto* triggerValue = field(*rule, "trigger")) {
                        if (const auto* trigger = std::get_if<JsonObject>(&triggerValue->value)) {
                            if (const auto* kind = field(*trigger, "kind")) {
                                const auto* text = std::get_if<std::string>(&kind->value);
                                if (text && (*text == "objectActivated" || *text == "objectDeactivated")) {
                                    reader.error(*kind, triggerPath + ".kind", "unsupported_version",
                                                 "object activation rules require map schema version 3");
                                    good = false;
                                }
                            }
                        }
                    }
                    rejectArrayKind("conditions", "objectActive", "worldRules[" + std::to_string(ruleIndex) + "].conditions");
                    rejectArrayKind("conditions", "objectInactive", "worldRules[" + std::to_string(ruleIndex) + "].conditions");
                }
            }
        }
    }
    good = id != nullptr && readMapId(*id, reader, "id", source.geometry.id) && good;
    good = width != nullptr && readU32(*width, reader, "width", source.geometry.width,
                                       MapLimits::maximumDimension) && good;
    good = height != nullptr && readU32(*height, reader, "height", source.geometry.height,
                                        MapLimits::maximumDimension) && good;
    good = tileSize != nullptr && readU16(*tileSize, reader, "tileSize",
                                          source.geometry.tileSize) && good;

    const auto* referenceArray = references == nullptr ? nullptr :
        array(*references, reader, "tileReferences");
    if (referenceArray == nullptr) good = false;
    else {
        for (std::size_t index = 0; index < referenceArray->size(); ++index) {
            const auto& value = (*referenceArray)[index];
            const std::string path = "tileReferences[" + std::to_string(index) + "]";
            const auto* item = object(value, reader, path);
            if (item == nullptr) { good = false; continue; }
            allowed(*item, reader, path, {"tilesetId", "sourceIndex", "flags"});
            const auto* tileset = required(*item, value, reader, path, "tilesetId");
            const auto* sourceIndex = required(*item, value, reader, path, "sourceIndex");
            const auto* flags = required(*item, value, reader, path, "flags");
            MapTileReference decoded;
            bool itemGood = tileset != nullptr && readId(*tileset, reader, path + ".tilesetId",
                                                         decoded.tilesetId);
            itemGood = sourceIndex != nullptr && readU32(*sourceIndex, reader,
                path + ".sourceIndex", decoded.sourceIndex) && itemGood;
            std::uint32_t flagValue{};
            itemGood = flags != nullptr && readU32(*flags, reader, path + ".flags", flagValue, 1) &&
                       itemGood;
            decoded.flags = static_cast<world::TileFlags>(flagValue);
            if (itemGood) source.geometry.tileReferences.push_back(std::move(decoded));
            good = itemGood && good;
        }
    }
    const auto* layerArray = layers == nullptr ? nullptr : array(*layers, reader, "layers");
    if (layerArray == nullptr) good = false;
    else {
        for (std::size_t index = 0; index < layerArray->size(); ++index) {
            const auto& value = (*layerArray)[index];
            const std::string path = "layers[" + std::to_string(index) + "]";
            const auto* item = object(value, reader, path);
            if (item == nullptr) { good = false; continue; }
            allowed(*item, reader, path, {"name", "visible", "cells"});
            const auto* name = required(*item, value, reader, path, "name");
            const auto* cells = required(*item, value, reader, path, "cells");
            MapTileLayer layer;
            bool itemGood = name != nullptr && readString(*name, reader, path + ".name",
                                                          layer.name);
            if (const auto* visible = field(*item, "visible")) {
                itemGood = readBool(*visible, reader, path + ".visible", layer.visible) && itemGood;
            }
            const auto* cellArray = cells == nullptr ? nullptr : array(*cells, reader, path + ".cells");
            if (cellArray == nullptr) itemGood = false;
            else {
                for (std::size_t cellIndex = 0; cellIndex < cellArray->size(); ++cellIndex) {
                    const auto& cell = (*cellArray)[cellIndex];
                    if (std::holds_alternative<std::nullptr_t>(cell.value)) {
                        layer.cells.push_back(std::nullopt);
                    } else {
                        std::uint32_t cellValue{};
                        const bool cellGood = readU32(cell, reader,
                            path + ".cells[" + std::to_string(cellIndex) + "]", cellValue);
                        if (cellGood) layer.cells.push_back(cellValue);
                        itemGood = cellGood && itemGood;
                    }
                }
            }
            if (itemGood) source.geometry.layers.push_back(std::move(layer));
            good = itemGood && good;
        }
    }
    const auto* collisionArray = collision == nullptr ? nullptr :
        array(*collision, reader, "collision");
    if (collisionArray == nullptr) good = false;
    else {
        for (std::size_t index = 0; index < collisionArray->size(); ++index) {
            std::uint32_t value{};
            const bool itemGood = readU32((*collisionArray)[index], reader,
                "collision[" + std::to_string(index) + "]", value, 1);
            if (itemGood) source.geometry.collision.push_back(static_cast<std::uint8_t>(value));
            good = itemGood && good;
        }
    }
    const auto* spawnArray = spawns == nullptr ? nullptr : array(*spawns, reader, "playerSpawns");
    if (spawnArray == nullptr) good = false;
    else {
        for (std::size_t index = 0; index < spawnArray->size(); ++index) {
            const auto& value = (*spawnArray)[index];
            const std::string path = "playerSpawns[" + std::to_string(index) + "]";
            const auto* item = object(value, reader, path);
            if (item == nullptr) { good = false; continue; }
            allowed(*item, reader, path, {"id", "position", "facing"});
            const auto* idValueAt = required(*item, value, reader, path, "id");
            const auto* position = required(*item, value, reader, path, "position");
            const auto* facing = required(*item, value, reader, path, "facing");
            std::string spawnId;
            PlayerSpawn decoded;
            bool itemGood = idValueAt != nullptr && readString(*idValueAt, reader,
                path + ".id", spawnId);
            itemGood = position != nullptr && readPoint(*position, reader, path + ".position",
                                                        decoded.position) && itemGood;
            itemGood = facing != nullptr && readFacing(*facing, reader, path + ".facing",
                                                       decoded.facing) && itemGood;
            if (itemGood) { decoded.id = simulation::SpawnId{std::move(spawnId)}; source.geometry.playerSpawns.push_back(std::move(decoded)); }
            good = itemGood && good;
        }
    }

    const auto decodeOptionalArray = [&](std::string_view name, auto decoder, auto& output) {
        if (const auto* values = field(*root, name)) {
            const auto* valuesArray = array(*values, reader, name);
            if (valuesArray == nullptr) { good = false; return; }
            for (std::size_t index = 0; index < valuesArray->size(); ++index) {
                typename std::decay_t<decltype(output)>::value_type decoded;
                const bool itemGood = decoder((*valuesArray)[index], reader,
                    std::string(name) + "[" + std::to_string(index) + "]", decoded);
                if (itemGood) output.push_back(std::move(decoded));
                good = itemGood && good;
            }
        }
    };
    decodeOptionalArray("enemies",
        [](const JsonValue& v, Reader& r, std::string_view p, EnemyPlacement& o) {
            return decodePlacement(v, r, p, o);
        }, source.geometry.enemies);
    decodeOptionalArray("npcs",
        [](const JsonValue& v, Reader& r, std::string_view p, NpcPlacement& o) {
            return decodePlacement(v, r, p, o);
        }, source.geometry.npcs);
    decodeOptionalArray("objects",
        [](const JsonValue& v, Reader& r, std::string_view p, ObjectPlacement& o) {
            return decodeObject(v, r, p, o);
        }, source.geometry.objects);

    if (const auto* values = field(*root, "pickups")) {
        const auto* pickupArray = array(*values, reader, "pickups");
        if (pickupArray == nullptr) good = false;
        else for (std::size_t index = 0; index < pickupArray->size(); ++index) {
            const auto& value = (*pickupArray)[index];
            const std::string path = "pickups[" + std::to_string(index) + "]";
            const auto* item = object(value, reader, path);
            if (item == nullptr) { good = false; continue; }
            allowed(*item, reader, path, {"id", "definitionId", "visualId", "position",
                                           "collectionBounds", "payload"});
            const auto* id = required(*item, value, reader, path, "id");
            const auto* definition = required(*item, value, reader, path, "definitionId");
            const auto* visual = required(*item, value, reader, path, "visualId");
            const auto* position = required(*item, value, reader, path, "position");
            const auto* bounds = required(*item, value, reader, path, "collectionBounds");
            const auto* payload = required(*item, value, reader, path, "payload");
            PickupPlacement decoded;
            bool itemGood = id != nullptr && readPersistentId(*id, reader, path + ".id", decoded.id);
            itemGood = definition != nullptr && readId(*definition, reader, path + ".definitionId",
                                                       decoded.definitionId) && itemGood;
            itemGood = visual != nullptr && readId(*visual, reader, path + ".visualId",
                                                   decoded.visualId) && itemGood;
            itemGood = position != nullptr && readPoint(*position, reader, path + ".position",
                                                        decoded.position) && itemGood;
            itemGood = bounds != nullptr && readArea(*bounds, reader, path + ".collectionBounds",
                                                     decoded.collectionBounds) && itemGood;
            itemGood = payload != nullptr && decodePayload(*payload, reader, path + ".payload",
                                                           decoded.payload) && itemGood;
            if (itemGood) source.geometry.pickups.push_back(std::move(decoded));
            good = itemGood && good;
        }
    }
    decodeOptionalArray("links",
        [](const JsonValue& v, Reader& r, std::string_view p, MapLink& o) {
            return decodeLink(v, r, p, o);
        }, source.geometry.links);
    decodeOptionalArray("regions",
        [](const JsonValue& v, Reader& r, std::string_view p, MapRegionDefinition& o) {
            return decodeRegion(v, r, p, o);
        }, source.regions);
    decodeOptionalArray("worldRules",
        [](const JsonValue& v, Reader& r, std::string_view p, WorldRuleDefinition& o) {
            return decodeRule(v, r, p, o);
        }, source.worldRules);
    decodeOptionalArray("encounters",
        [](const JsonValue& v, Reader& r, std::string_view p, EncounterDefinition& o) {
            return decodeEncounter(v, r, p, o);
        }, source.encounters);
    decodeOptionalArray("placementOverrides",
        [](const JsonValue& v, Reader& r, std::string_view p, AuthoredPlacementOverride& o) {
            return decodeOverride(v, r, p, o);
        }, source.placementOverrides);

    if (reader.diagnostics.empty() && good) {
        if (const auto overrideError = validateAuthoredOverrides(source)) {
            reader.diagnostics.push_back({AuthoredMapDiagnosticStage::validation,
                "invalid_override", overrideError->second, overrideError->first, 1, 1});
        } else {
            const auto validation = validateMapData(mapDataFromAuthored(source));
            if (!validation) reader.diagnostics.push_back({AuthoredMapDiagnosticStage::validation,
                "invalid_map", validation.error, validation.path, 1, 1});
        }
    }
    result.diagnostics = std::move(reader.diagnostics);
    if (result.diagnostics.empty()) result.source = std::move(source);
    return result;
}

AuthoredMapDecodeResult readAuthoredMapFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {{}, {{AuthoredMapDiagnosticStage::io, "open_failed",
                              "could not open authored map", path.string(), 1, 1}}};
    std::ostringstream text;
    text << input.rdbuf();
    auto result = decodeAuthoredMapJson(text.str());
    for (auto& diagnostic : result.diagnostics) {
        diagnostic.path = path.generic_string() +
            (diagnostic.path.empty() ? "" : ":" + diagnostic.path);
    }
    return result;
}

bool writeAuthoredMapFile(const std::filesystem::path& path,
                          const AuthoredMapSource& source, std::string& error) {
    try {
        const auto text = encodeAuthoredMapJson(source);
        auto temporary = path;
        temporary += ".tmp";
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output) { error = "could not open temporary authored map"; return false; }
            output.write(text.data(), static_cast<std::streamsize>(text.size()));
            output.flush();
            if (!output) { error = "could not write temporary authored map"; return false; }
        }
        auto backup = path;
        backup += ".bak";
        std::error_code fsError;
        std::filesystem::remove(backup, fsError);
        fsError.clear();
        if (std::filesystem::exists(path, fsError)) {
            fsError.clear();
            std::filesystem::rename(path, backup, fsError);
            if (fsError) {
                std::filesystem::remove(temporary);
                error = "could not back up authored map";
                return false;
            }
        }
        fsError.clear();
        std::filesystem::rename(temporary, path, fsError);
        if (fsError) {
            std::error_code restoreError;
            if (std::filesystem::exists(backup, restoreError)) {
                std::filesystem::rename(backup, path, restoreError);
            }
            std::filesystem::remove(temporary);
            error = "could not replace authored map";
            return false;
        }
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

MapCompileResult compileAuthoredMap(const AuthoredMapSource& source,
                                    const game::GameContentRegistry& content) {
    if (const auto overrideError = validateAuthoredOverrides(source)) {
        return {{}, {{AuthoredMapDiagnosticStage::validation, "invalid_override",
                      overrideError->second, overrideError->first, 1, 1}}};
    }
    const auto compiled = mapDataFromAuthored(source);
    const auto catalogs = game::mapValidationCatalogs(content);
    const auto validation = validateMapData(compiled, &catalogs);
    if (!validation) {
        return {{}, {{AuthoredMapDiagnosticStage::validation, "invalid_map",
                      validation.error, validation.path, 1, 1}}};
    }
    return {compiled, {}};
}

} // namespace underworld::game::maps
