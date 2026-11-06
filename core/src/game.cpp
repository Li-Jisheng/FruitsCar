#include "fc/game.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numeric>
#include <utility>

#include "fc/fruit_gen.h"
#include "fc/refill.h"
#include "fc/rules.h"

namespace fc {

const char* toString(CommandType type) {
    switch (type) {
        case CommandType::SelectLane: return "select_lane";
        case CommandType::SelectSlot: return "select_slot";
        case CommandType::UseItem: return "use_item";
    }
    return "select_lane";
}

int GameState::laneForSpawn(int spawnIndex) const {
    const std::vector<int>& cycle = level.road.laneCycle;
    if (cycle.empty()) {
        return 0;
    }
    const int size = static_cast<int>(cycle.size());
    const int index = ((spawnIndex % size) + size) % size;
    return cycle[static_cast<std::size_t>(index)];
}

void Game::reset(const LevelDefinition& level, std::uint64_t seed) {
    state_ = GameState();
    state_.level = level;
    state_.seed = seed;
    world_ = makeWorld(level.usesCurves() ? WorldKind::Curve : WorldKind::Grid);
    state_.road = buildRoadMap(level);
    state_.roadside = roadsideFlags(level);

    // 预留技能：加载时解析，并在每个 snapshot 中暴露。
    for (const auto& item : level.items) {
        ItemState itemState;
        itemState.id = item.id;
        itemState.kindText = item.kindText;
        itemState.kind = item.kind;
        itemState.charges = item.charges;
        state_.items.push_back(itemState);
    }

    if (level.usesCurves()) {
        std::string error;
        if (!buildCurveState(level, state_, error)) {
            state_.fruitSlots.clear();
            state_.fruitsTotal = 0;
        }
    } else {
        std::string error;
        if (!generateFruits(level, seed, state_.cells, error)) {
            state_.cells.assign(
                static_cast<std::size_t>(level.rows) * static_cast<std::size_t>(level.cols), FruitCell());
        }
        // 每个单元格的静态渲染数据：它属于哪一层，以及位于该处的
        // 水果必须按多大尺寸绘制（任务书 4.6 grid.cells[].ring/size）。
        const std::size_t total =
            static_cast<std::size_t>(level.rows) * static_cast<std::size_t>(level.cols);
        state_.cellRing.assign(total, 0);
        state_.cellSize.assign(total, 1.0);
        for (std::size_t k = 0; k < level.fruits.layers.size(); ++k) {
            for (const Vec2i& cell : level.fruits.layers[k].cells) {
                if (!state_.inGrid(cell.r, cell.c)) {
                    continue;
                }
                const std::size_t index = static_cast<std::size_t>(cell.r * level.cols + cell.c);
                state_.cellRing[index] = static_cast<int>(k);
                state_.cellSize[index] = level.fruits.layers[k].size;
            }
        }
        state_.fruitsTotal = 0;
        int nextFruitId = 1;
        for (std::size_t i = 0; i < state_.cells.size(); ++i) {
            if (state_.cells[i].present) {
                ++state_.fruitsTotal;
                state_.cells[i].ring = state_.cellRing[i];
                state_.cells[i].size = state_.cellSize[i];
                // 用于动画/事件的稳定标识（任务书 6 T-11）。
                state_.cells[i].fruitId = nextFruitId++;
            }
        }
        state_.nextFruitId = nextFruitId;
        state_.fruitsCollected = 0;
    }

    state_.lanes.clear();
    for (const auto& lane : level.parking.lanes) {
        LaneState laneState;
        laneState.color = lane.color;
        laneState.queue = lane.queue;
        state_.lanes.push_back(std::move(laneState));
    }
    state_.slots.assign(static_cast<std::size_t>(level.parking.slotCount), EntityId());

    for (std::size_t laneIndex = 0; laneIndex < state_.lanes.size(); ++laneIndex) {
        const ColorId laneColor = level.parking.lanes[laneIndex].color;
        for (const auto& id : state_.lanes[laneIndex].queue) {
            const VehicleDef* def = level.findVehicle(id);
            VehicleState vehicle;
            vehicle.id = id;
            vehicle.color = def != nullptr ? def->color : laneColor;
            vehicle.capacity = def != nullptr ? def->capacity : 1;
            vehicle.load = 0;
            vehicle.where = Location::Lane;
            vehicle.index = static_cast<int>(laneIndex);
            state_.vehicles[id] = vehicle;
        }
    }

    state_.tick = 0;
    state_.lastSpawnTick = -level.spawnTicks();
    state_.commandUsedThisTick = false;
    state_.requireSelect = false;
    state_.spawnCount = 0;
    state_.status = GameStatus::Playing;
    state_.resultTick = -1;
    state_.resultReason.clear();
    state_.eventSeq = 0;
    state_.events.clear();

    Event& session = state_.pushEvent(EventType::Session);
    session.levelId = level.id;
    session.seed = seed;
    session.count = state_.fruitsTotal;
}

std::vector<const Event*> Game::eventsSince(std::uint64_t sinceSeq) const {
    std::vector<const Event*> out;
    for (const auto& event : state_.events) {
        if (event.seq > sinceSeq) {
            out.push_back(&event);
        }
    }
    return out;
}

CommandResult Game::reject(const std::string& why) {
    CommandResult result;
    result.accepted = false;
    result.reason = why;
    result.nextAllowedTick = state_.nextAllowedTick();
    Event& event = state_.pushEvent(EventType::CommandRejected);
    event.reason = why;
    event.nextAllowedTick = result.nextAllowedTick;
    return result;
}

int Game::firstEmptySlot() const {
    for (std::size_t i = 0; i < state_.slots.size(); ++i) {
        if (state_.slots[i].empty()) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// 预留技能命令（任务书 5.8 T-31）：无论走哪条路径，最终都会返回稳定的拒绝，
// 不得改变任何状态，且恰好记录一个 ITEM_REJECTED 事件。
CommandResult Game::submitItem(const Command& command) {
    CommandResult result;
    result.nextAllowedTick = state_.nextAllowedTick();

    const char* verdict = nullptr;
    if (state_.status != GameStatus::Playing) {
        verdict = reason::kGameOver;
    } else if (state_.commandUsedThisTick) {
        verdict = reason::kBusy;
    } else {
        const ItemState* item = nullptr;
        for (const auto& candidate : state_.items) {
            if (candidate.id == command.itemId) {
                item = &candidate;
                break;
            }
        }
        if (item == nullptr) {
            verdict = reason::kItemNotFound;
        } else if (item->charges <= 0) {
            verdict = reason::kItemDepleted;
        } else if (!command.targetCar.empty() && state_.findVehicle(command.targetCar) == nullptr) {
            verdict = reason::kItemTargetInvalid;
        } else {
            // 效果本身在当前阶段尚未实现。
            verdict = reason::kItemNotImplemented;
        }
    }

    result.accepted = false;
    result.reason = verdict;
    Event& event = state_.pushEvent(EventType::ItemRejected);
    event.reason = verdict;
    event.commandType = toString(CommandType::UseItem);
    event.itemId = command.itemId;
    event.car = command.targetCar;
    event.value = command.center.x;
    event.value2 = command.center.y;
    event.count = 1;
    return result;
}

CommandResult Game::submit(const Command& command) {
    if (command.type == CommandType::UseItem) {
        return submitItem(command);
    }

    CommandResult result;
    result.nextAllowedTick = state_.nextAllowedTick();

    const int roadLane = state_.laneForSpawn(state_.spawnCount);

    const char* verdict = nullptr;
    if (state_.status != GameStatus::Playing) {
        verdict = reason::kGameOver;
    } else if (state_.commandUsedThisTick) {
        verdict = reason::kBusy;
    } else if (state_.requireSelect && command.type == CommandType::SelectLane) {
        verdict = reason::kExpectSlotSelect;
    } else if (state_.tick - state_.lastSpawnTick < state_.level.spawnTicks()) {
        verdict = reason::kCooldown;
    } else if (rules::entranceBlocked(state_, roadLane)) {
        verdict = reason::kRoadBlocked;
    }

    std::string vehicleId;
    if (verdict == nullptr) {
        if (command.type == CommandType::SelectLane) {
            if (command.index < 0 || command.index >= static_cast<int>(state_.lanes.size())) {
                verdict = reason::kBadIndex;
            } else if (state_.lanes[static_cast<std::size_t>(command.index)].queue.empty()) {
                verdict = reason::kLaneEmpty;
            } else {
                vehicleId = state_.lanes[static_cast<std::size_t>(command.index)].queue.front();
            }
        } else {
            if (command.index < 0 || command.index >= static_cast<int>(state_.slots.size())) {
                verdict = reason::kBadIndex;
            } else if (state_.slots[static_cast<std::size_t>(command.index)].empty()) {
                verdict = reason::kSlotEmpty;
            } else {
                vehicleId = state_.slots[static_cast<std::size_t>(command.index)];
            }
        }
    }

    if (verdict != nullptr) {
        return reject(verdict);
    }

    // 已接受：将车辆从其来源中取出，并放到道路上。
    VehicleState* vehicle = state_.findVehicle(vehicleId);
    if (vehicle == nullptr) {
        return reject(reason::kBadIndex);
    }
    const int laneIndex = command.type == CommandType::SelectLane ? command.index : -1;
    if (command.type == CommandType::SelectLane) {
        state_.lanes[static_cast<std::size_t>(command.index)].queue.erase(
            state_.lanes[static_cast<std::size_t>(command.index)].queue.begin());
    } else {
        state_.slots[static_cast<std::size_t>(command.index)].clear();
    }

    Car car;
    car.id = vehicleId;
    car.color = vehicle->color;
    car.load = vehicle->load;
    car.capacity = vehicle->capacity;
    car.s = 0.0;
    car.lane = roadLane;
    // 在最小间距允许的范围内尽量靠前进入：车道空闲时为 0，
    // 当该车道入口繁忙时，则退到起点线后方。
    double minS = 1e9;
    bool found = false;
    for (const auto& other : state_.cars) {
        if (other.lane != roadLane) {
            continue;
        }
        if (other.s < minS) {
            minS = other.s;
        }
        found = true;
    }
    if (found) {
        const double clearance = state_.level.road.carLength + state_.level.road.carGap;
        const double pushed = minS - clearance;
        car.s = pushed < 0.0 ? pushed : 0.0;
    }
    car.waiting = false;
    state_.cars.push_back(car);

    vehicle->where = Location::Road;
    vehicle->index = -1;

    state_.lastSpawnTick = state_.tick;
    state_.commandUsedThisTick = true;
    ++state_.spawnCount;

    Event& event = state_.pushEvent(EventType::VehicleEntered);
    event.car = vehicleId;
    event.color = car.color;
    event.load = car.load;
    event.capacity = car.capacity;
    event.lane = roadLane;
    event.slot = command.type == CommandType::SelectSlot ? command.index : -1;
    event.count = laneIndex;  // 停车车道 / 来源索引
    event.value = car.s;

    result.accepted = true;
    result.reason = reason::kAccept;
    result.nextAllowedTick = state_.nextAllowedTick();
    return result;
}

void Game::tick() {
    if (state_.status != GameStatus::Playing) {
        ++state_.tick;
        return;
    }
    ++state_.tick;
    state_.commandUsedThisTick = false;

    moveCars();
    collectCars();
    routeCars();
    applyEffects();
    decaySettles();
    updateStatus();
}

void Game::moveCars() {
    const double step = state_.level.road.speed / static_cast<double>(kTickRate);
    const double laneLength = state_.roadLength();
    const double clearance = state_.level.road.carLength + state_.level.road.carGap;

    // 汽车只跟随 *本车道内* 的前车（任务书 G5）。
    std::vector<std::size_t> order(state_.cars.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [this](std::size_t a, std::size_t b) {
        const Car& ca = state_.cars[a];
        const Car& cb = state_.cars[b];
        if (ca.lane != cb.lane) {
            return ca.lane < cb.lane;
        }
        return ca.s > cb.s;
    });

    for (std::size_t k = 0; k < order.size(); ++k) {
        Car& car = state_.cars[order[k]];
        double limit = laneLength;
        if (k > 0) {
            const Car& ahead = state_.cars[order[k - 1]];
            if (ahead.lane == car.lane) {
                limit = ahead.s - clearance;
            }
        }
        if (limit > laneLength) {
            limit = laneLength;
        }
        const double desired = std::min(car.s + step, laneLength);
        if (desired > limit + kEps) {
            const double clamped = limit > car.s ? limit : car.s;
            if (desired > car.s + kEps && clamped < desired - kEps) {
                Event& blocked = state_.pushEvent(EventType::CarBlocked);
                blocked.car = car.id;
                blocked.lane = car.lane;
                blocked.value = clamped;
            }
            car.s = clamped;
        } else {
            car.s = desired;
        }
    }
}

void Game::collectCars() {
    const bool curve = state_.worldKind == WorldKind::Curve;
    for (auto& car : state_.cars) {
        const TouchResult outcome = world_->contact(state_, car);
        const int roadIndex = outcome.roadIndex;
        if (!outcome.sawFruit) {
            car.evalCell = roadIndex;
            car.evalSkip.clear();
            continue;
        }
        if (!outcome.collects) {
            // 便于取证的去重：每个（位置，原因）组合只记录一行。
            if (roadIndex != car.evalCell || car.evalSkip != outcome.skipReason) {
                const FruitPlacement where = world_->placement(state_, outcome.slot);
                Event& skipped = state_.pushEvent(EventType::CollectSkipped);
                skipped.car = car.id;
                skipped.color = outcome.color;
                if (!curve) {
                    skipped.at = where.cell;
                }
                skipped.lane = car.lane;
                skipped.reason = outcome.skipReason;
                skipped.load = car.load;
                skipped.capacity = car.capacity;
                if (curve) {
                    skipped.hasWorld = true;
                    skipped.world = where.world;
                    skipped.fruitId = outcome.fruitId;
                    skipped.ring = outcome.layer;
                }
            }
            car.evalCell = roadIndex;
            car.evalSkip = outcome.skipReason;
            continue;
        }
        car.evalCell = roadIndex;
        car.evalSkip.clear();

        const FruitPlacement where = world_->placement(state_, outcome.slot);
        if (where.cell.r < 0 && !where.hasWorld) {
            continue;
        }
        world_->removeFruit(state_, outcome.slot);

        ++car.load;
        ++state_.fruitsCollected;
        if (VehicleState* vehicle = state_.findVehicle(car.id)) {
            vehicle->load = car.load;
        }

        Event& collected = state_.pushEvent(EventType::FruitCollected);
        collected.car = car.id;
        collected.color = outcome.color;
        if (!curve) {
            collected.at = where.cell;
        }
        collected.lane = car.lane;
        collected.load = car.load;
        collected.capacity = car.capacity;
        if (curve) {
            collected.hasWorld = true;
            collected.world = where.world;
            collected.fruitId = outcome.fruitId;
            collected.ring = outcome.layer;
        }

        refillAt(outcome.slot);
    }
}

void Game::refillAt(int slot) {
    const std::vector<RefillMove> moves = world_->planRefill(state_, slot);
    if (moves.empty()) {
        return;
    }
    const bool curve = state_.worldKind == WorldKind::Curve;
    for (const auto& move : moves) {
        // 在移动前记录来源的 snapshot，以便事件可以为其制作动画。
        ColorId color;
        double sizeFrom = 1.0;
        int fruitId = -1;
        if (curve) {
            const FruitSlot* source = state_.fruitSlot(move.from);
            if (source == nullptr) {
                continue;
            }
            color = source->color;
            sizeFrom = source->size;
            fruitId = source->fruitId;
        } else {
            if (move.from < 0 || move.from >= static_cast<int>(state_.cells.size())) {
                continue;
            }
            const FruitCell& source = state_.cells[static_cast<std::size_t>(move.from)];
            color = source.color;
            sizeFrom = source.size;
            fruitId = source.fruitId;
        }
        const FruitPlacement fromWhere = world_->placement(state_, move.from);
        world_->applyRefillMove(state_, move);
        const FruitPlacement toWhere = world_->placement(state_, move.to);

        int settleLeft = 0;
        double sizeTo = 1.0;
        int layer = 0;
        if (curve) {
            const FruitSlot* destination = state_.fruitSlot(move.to);
            if (destination == nullptr) {
                continue;
            }
            settleLeft = destination->settleLeft;
            sizeTo = destination->size;
            layer = destination->layer;
        } else {
            const FruitCell& destination = state_.cells[static_cast<std::size_t>(move.to)];
            settleLeft = destination.settleLeft;
            sizeTo = destination.size;
            layer = destination.ring;
        }

        Event& refilled = state_.pushEvent(EventType::FruitRefilled);
        refilled.color = color;
        if (!curve) {
            refilled.from = fromWhere.cell;
            refilled.at = toWhere.cell;
        }
        refilled.count = settleLeft;
        refilled.value = sizeFrom;
        refilled.value2 = sizeTo;
        refilled.ring = layer;
        if (curve) {
            refilled.hasWorld = true;
            refilled.world = toWhere.world;
            refilled.worldFrom = fromWhere.world;
            refilled.fruitId = fruitId;
            refilled.fruitIdFrom = fruitId;
        }
    }
}

void Game::applyEffects() {
    // 预留效果阶段（任务书 5.8 T-32）：当前阶段不会创建任何效果，
    // 因此该循环只负责让未来阶段可能推入的效果过期。
    if (state_.effects.empty()) {
        return;
    }
    std::vector<EffectInstance> alive;
    alive.reserve(state_.effects.size());
    for (const auto& effect : state_.effects) {
        if (state_.tick < effect.expireTick) {
            alive.push_back(effect);
            continue;
        }
        Event& expired = state_.pushEvent(EventType::EffectExpired);
        expired.reason = toString(effect.kind);
        expired.car = effect.target;
        expired.hasWorld = true;
        expired.world = effect.center;
        expired.ring = static_cast<int>(effect.kind);
    }
    state_.effects = std::move(alive);
}

void Game::routeCars() {
    const double laneLength = state_.roadLength();
    std::vector<Car> remaining;
    remaining.reserve(state_.cars.size());

    for (auto& car : state_.cars) {
        if (car.s < laneLength - kEps) {
            remaining.push_back(car);
            continue;
        }
        car.s = laneLength;

        VehicleState* vehicle = state_.findVehicle(car.id);
        if (car.load >= car.capacity) {
            ++state_.exitedCount;
            if (vehicle != nullptr) {
                vehicle->where = Location::Exited;
                vehicle->index = -1;
                vehicle->load = car.load;
            }
            Event& exited = state_.pushEvent(EventType::VehicleExited);
            exited.car = car.id;
            exited.lane = car.lane;
            exited.load = car.load;
            exited.capacity = car.capacity;
            exited.count = state_.exitedCount;
            continue;
        }

        const int slot = firstEmptySlot();
        if (slot >= 0) {
            state_.slots[static_cast<std::size_t>(slot)] = car.id;
            ++state_.parkedCount;
            if (vehicle != nullptr) {
                vehicle->where = Location::Slot;
                vehicle->index = slot;
                vehicle->load = car.load;
            }
            Event& parked = state_.pushEvent(EventType::VehicleParked);
            parked.car = car.id;
            parked.slot = slot;
            parked.lane = car.lane;
            parked.load = car.load;
            parked.capacity = car.capacity;
            if (state_.requireSelect) {
                state_.requireSelect = false;
            }
            continue;
        }

        car.waiting = true;
        if (!state_.requireSelect) {
            state_.requireSelect = true;
            Event& waiting = state_.pushEvent(EventType::RequireSelect);
            waiting.car = car.id;
            waiting.slot = -1;
            waiting.lane = car.lane;
        }
        remaining.push_back(car);
    }
    state_.cars = std::move(remaining);
}

void Game::decaySettles() {
    for (auto& cell : state_.cells) {
        if (cell.present && cell.settleLeft > 0 && cell.settleSetTick != state_.tick) {
            --cell.settleLeft;
        }
    }
    for (auto& slot : state_.fruitSlots) {
        if (slot.present && slot.settleLeft > 0 && slot.settleSetTick != state_.tick) {
            --slot.settleLeft;
        }
    }
}

void Game::updateStatus() {
    const GameStatus status = rules::resolveStatus(state_);
    if (status == GameStatus::Playing) {
        return;
    }
    state_.status = status;
    state_.resultTick = state_.tick;
    state_.resultReason = toString(status);
    Event& event = state_.pushEvent(EventType::Result);
    event.reason = state_.resultReason;
    event.count = state_.exitedCount;
    event.load = state_.fruitsCollected;
    event.value = static_cast<double>(state_.tick);
}

}  // namespace fc
