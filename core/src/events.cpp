#include "fc/events.h"

#include <cstdio>

namespace fc {

const char* toString(EventType type) {
    switch (type) {
        case EventType::VehicleEntered: return "VEHICLE_ENTERED";
        case EventType::CarBlocked: return "CAR_BLOCKED";
        case EventType::VehicleExited: return "VEHICLE_EXITED";
        case EventType::VehicleParked: return "VEHICLE_PARKED";
        case EventType::RequireSelect: return "REQUIRE_SELECT";
        case EventType::FruitCollected: return "FRUIT_COLLECTED";
        case EventType::FruitRefilled: return "FRUIT_REFILLED";
        case EventType::CollectSkipped: return "COLLECT_SKIPPED";
        case EventType::CommandRejected: return "COMMAND_REJECTED";
        case EventType::Result: return "RESULT";
        case EventType::Session: return "SESSION";
        case EventType::ItemRejected: return "ITEM_REJECTED";
        case EventType::ItemActivated: return "ITEM_ACTIVATED";
        case EventType::EffectApplied: return "EFFECT_APPLIED";
        case EventType::EffectExpired: return "EFFECT_EXPIRED";
    }
    return "SESSION";
}

std::string eventToJson(const Event& event) {
    std::string out = "{";
    out += "\"seq\":" + std::to_string(event.seq);
    out += ",\"tick\":" + std::to_string(event.tick);
    out += ",\"type\":" + jsonQuote(toString(event.type));

    if (!event.car.empty()) {
        out += ",\"car\":" + jsonQuote(event.car);
    }
    if (!event.color.empty()) {
        out += ",\"fruit\":" + jsonQuote(event.color);
    }
    if (event.at.r >= 0 && event.at.c >= 0) {
        out += ",\"at\":[" + std::to_string(event.at.r) + "," + std::to_string(event.at.c) + "]";
    }
    if (event.from.r >= 0 && event.from.c >= 0) {
        out += ",\"from\":[" + std::to_string(event.from.r) + "," + std::to_string(event.from.c) + "]";
    }
    if (event.ring >= 0) {
        out += ",\"ring\":" + std::to_string(event.ring);
    }
    if (event.load >= 0) {
        out += ",\"load\":" + std::to_string(event.load);
    }
    if (event.capacity >= 0) {
        out += ",\"capacity\":" + std::to_string(event.capacity);
    }
    if (event.slot >= 0) {
        out += ",\"slot\":" + std::to_string(event.slot);
    }
    if (event.lane >= 0) {
        out += ",\"lane\":" + std::to_string(event.lane);
    }
    if (event.count >= 0) {
        out += ",\"count\":" + std::to_string(event.count);
    }
    if (event.nextAllowedTick >= 0) {
        out += ",\"nextAllowedTick\":" + std::to_string(event.nextAllowedTick);
    }
    if (event.type == EventType::CarBlocked || event.type == EventType::VehicleEntered) {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%.3f", event.value);
        out += ",\"s\":";
        out += buffer;
    }
    if (event.type == EventType::FruitRefilled) {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%.3f", event.value);
        out += ",\"sizeFrom\":";
        out += buffer;
        std::snprintf(buffer, sizeof(buffer), "%.3f", event.value2);
        out += ",\"sizeTo\":";
        out += buffer;
    }
    if (!event.reason.empty()) {
        out += ",\"reason\":" + jsonQuote(event.reason);
    }
    if (!event.commandType.empty()) {
        out += ",\"command\":" + jsonQuote(event.commandType);
    }
    if (!event.itemId.empty()) {
        out += ",\"item\":" + jsonQuote(event.itemId);
    }
    if (!event.levelId.empty()) {
        out += ",\"levelId\":" + jsonQuote(event.levelId);
    }
    if (event.type == EventType::Session) {
        out += ",\"seed\":" + std::to_string(event.seed);
    }
    if (event.hasWorld) {
        if (event.fruitId >= 0) {
            out += ",\"id\":" + std::to_string(event.fruitId);
        }
        if (event.fruitIdFrom >= 0) {
            out += ",\"fromId\":" + std::to_string(event.fruitIdFrom);
        }
        out += ",\"x\":" + formatFixed4(event.world.x);
        out += ",\"y\":" + formatFixed4(event.world.y);
        if (event.type == EventType::FruitRefilled) {
            out += ",\"fromX\":" + formatFixed4(event.worldFrom.x);
            out += ",\"fromY\":" + formatFixed4(event.worldFrom.y);
        }
    }
    out += "}";
    return out;
}

}  // namespace fc
