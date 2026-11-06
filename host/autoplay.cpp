#include "autoplay.h"

#include <algorithm>
#include <map>

#include "fc/rules.h"
#include "fc/world.h"

namespace fc {

AutoplayDriver::AutoplayDriver(Mode mode, int maxRoadCars) : mode_(mode), maxRoadCars_(maxRoadCars) {}

bool AutoplayDriver::decide(const Game& game, Command& command) {
    if (mode_ == Mode::Spam) {
        return decideSpam(game, command);
    }
    return decideGreedy(game, command);
}

bool AutoplayDriver::decideGreedy(const Game& game, Command& command) {
    const GameState& state = game.state();
    if (state.status != GameStatus::Playing) {
        return false;
    }
    // 遵守 0.5 s 节流，避免连续提交被拒绝的命令。
    if (state.tick - state.lastSpawnTick < state.level.spawnTicks()) {
        return false;
    }

    // 根据下一辆车实际驶入的 lane 的接触格上的水果为颜色打分
    // （任务书 8.3 M14-T6）。排在后面的 lane 权重更低，
    // 这样干净的下一 lane 就不会让循环死锁。
    const int laneCount = state.worldKind == WorldKind::Curve
                              ? state.level.laneCount()
                              : static_cast<int>(state.road.lanes.size());
    const int nextLane = state.nextLane();
    std::vector<std::map<ColorId, int>> laneMatches(static_cast<std::size_t>(laneCount));
    for (int l = 0; l < laneCount; ++l) {
        laneMatches[static_cast<std::size_t>(l)] = collectableColorCounts(state, l);
    }

    auto matchesOf = [&laneMatches, laneCount](int lane, const ColorId& color) {
        if (lane < 0 || lane >= laneCount) {
            return 0;
        }
        const auto& counts = laneMatches[static_cast<std::size_t>(lane)];
        const auto it = counts.find(color);
        return it == counts.end() ? 0 : it->second;
    };
    const int otherLane = laneCount > 1 ? (nextLane + 1) % laneCount : nextLane;

    auto gainOf = [&](const VehicleState& vehicle) {
        const int room = vehicle.capacity - vehicle.load;
        if (room <= 0) {
            return 0;
        }
        const int primary = std::min(matchesOf(nextLane, vehicle.color), room);
        const int secondary = otherLane == nextLane ? 0 : std::min(matchesOf(otherLane, vehicle.color), room);
        return primary * 4 + secondary;
    };

    if (state.requireSelect) {
        int bestSlot = -1;
        int bestGain = -1;
        int bestRoom = 1 << 30;
        for (std::size_t i = 0; i < state.slots.size(); ++i) {
            if (state.slots[i].empty()) {
                continue;
            }
            const auto it = state.vehicles.find(state.slots[i]);
            if (it == state.vehicles.end()) {
                continue;
            }
            const int gain = gainOf(it->second);
            const int room = it->second.capacity - it->second.load;
            if (gain > bestGain || (gain == bestGain && room < bestRoom)) {
                bestGain = gain;
                bestRoom = room;
                bestSlot = static_cast<int>(i);
            }
        }
        if (bestSlot < 0) {
            return false;
        }
        command.type = CommandType::SelectSlot;
        command.index = bestSlot;
        return true;
    }

    if (static_cast<int>(state.cars.size()) >= maxRoadCars_) {
        return false;
    }

    int bestLane = -1;
    int bestLaneGain = -1;
    for (std::size_t i = 0; i < state.lanes.size(); ++i) {
        if (state.lanes[i].queue.empty()) {
            continue;
        }
        const auto it = state.vehicles.find(state.lanes[i].queue.front());
        if (it == state.vehicles.end()) {
            continue;
        }
        const int gain = gainOf(it->second);
        if (gain > bestLaneGain) {
            bestLaneGain = gain;
            bestLane = static_cast<int>(i);
        }
    }

    int bestSlot = -1;
    int bestSlotGain = -1;
    for (std::size_t i = 0; i < state.slots.size(); ++i) {
        if (state.slots[i].empty()) {
            continue;
        }
        const auto it = state.vehicles.find(state.slots[i]);
        if (it == state.vehicles.end()) {
            continue;
        }
        const int gain = gainOf(it->second);
        if (gain > bestSlotGain) {
            bestSlotGain = gain;
            bestSlot = static_cast<int>(i);
        }
    }

    // 已停靠的车辆被重新派出时会腾出 slot，因此在同分时优先选择它们。
    if (bestSlot >= 0 && bestSlotGain >= 1 && bestSlotGain >= bestLaneGain) {
        command.type = CommandType::SelectSlot;
        command.index = bestSlot;
        return true;
    }
    if (bestLane >= 0 && bestLaneGain >= 1) {
        command.type = CommandType::SelectLane;
        command.index = bestLane;
        return true;
    }
    return false;
}

bool AutoplayDriver::decideSpam(const Game& game, Command& command) {
    const GameState& state = game.state();
    if (state.status != GameStatus::Playing) {
        return false;
    }
    if (state.requireSelect) {
        for (std::size_t i = 0; i < state.slots.size(); ++i) {
            if (!state.slots[i].empty()) {
                command.type = CommandType::SelectSlot;
                command.index = static_cast<int>(i);
                return true;
            }
        }
        return false;
    }
    if (state.lanes.empty()) {
        return false;
    }
    const int period = state.level.spawnTicks() > 0 ? state.level.spawnTicks() : 30;
    const int lane = (state.tick / period) % static_cast<int>(state.lanes.size());
    for (int offset = 0; offset < static_cast<int>(state.lanes.size()); ++offset) {
        const int index = (lane + offset) % static_cast<int>(state.lanes.size());
        if (!state.lanes[static_cast<std::size_t>(index)].queue.empty()) {
            command.type = CommandType::SelectLane;
            command.index = index;
            return true;
        }
    }
    return false;
}

int runAutoplay(Game& game, AutoplayDriver& driver, int maxTicks) {
    for (int i = 0; i < maxTicks; ++i) {
        if (game.state().status != GameStatus::Playing) {
            return game.state().resultTick;
        }
        Command command;
        if (driver.decide(game, command)) {
            game.submit(command);
        }
        game.tick();
    }
    return game.state().status == GameStatus::Playing ? -1 : game.state().resultTick;
}

}  // namespace fc
