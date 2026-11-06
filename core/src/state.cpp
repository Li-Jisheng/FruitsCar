#include "fc/state.h"

namespace fc {

const char* toString(GameStatus status) {
    switch (status) {
        case GameStatus::Playing: return "PLAYING";
        case GameStatus::Win: return "WIN";
        case GameStatus::FailTrafficJam: return "FAIL_TRAFFIC_JAM";
    }
    return "PLAYING";
}

int GameState::occupiedSlots() const {
    int count = 0;
    for (const auto& slot : slots) {
        if (!slot.empty()) {
            ++count;
        }
    }
    return count;
}

bool GameState::parkingFull() const { return occupiedSlots() >= level.parking.slotCount; }

const Car* GameState::findCar(const EntityId& id) const {
    for (const auto& car : cars) {
        if (car.id == id) {
            return &car;
        }
    }
    return nullptr;
}

VehicleState* GameState::findVehicle(const EntityId& id) {
    auto it = vehicles.find(id);
    if (it == vehicles.end()) {
        return nullptr;
    }
    return &it->second;
}

const FruitSlot* GameState::fruitSlotOfId(int fruitId) const {
    if (fruitId < 0) {
        return nullptr;
    }
    for (const auto& slot : fruitSlots) {
        if (slot.present && slot.fruitId == fruitId) {
            return &slot;
        }
    }
    return nullptr;
}

Event& GameState::pushEvent(EventType type) {
    Event event;
    event.seq = ++eventSeq;
    event.tick = tick;
    event.type = type;
    events.push_back(event);
    return events.back();
}

}  // namespace fc
