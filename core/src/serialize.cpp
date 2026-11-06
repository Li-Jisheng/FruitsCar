#include "fc/serialize.h"

#include <cstddef>
#include <memory>

#include "fc/rules.h"
#include "fc/world.h"

namespace fc {
namespace {

std::string labelOf(const Car& car) {
    return std::to_string(car.load) + "/" + std::to_string(car.capacity);
}

std::string fixed3(double value) { return formatFixed3(value); }

// 仅用于渲染几何钩子的无状态 World。
const World& worldFor(const GameState& state) {
    static const std::unique_ptr<World> grid = makeGridWorld();
    static const std::unique_ptr<World> curve = makeCurveWorld();
    return state.worldKind == WorldKind::Curve ? *curve : *grid;
}

}  // namespace

std::string canonicalStateJson(const GameState& state) {
    const bool curve = state.worldKind == WorldKind::Curve;
    std::string out;
    out.reserve(4096);
    out += "{\"levelId\":" + jsonQuote(state.level.id);
    out += ",\"seed\":" + std::to_string(state.seed);
    out += ",\"rows\":" + std::to_string(state.level.rows);
    out += ",\"cols\":" + std::to_string(state.level.cols);
    out += ",\"spawnCount\":" + std::to_string(state.spawnCount);

    if (curve) {
        // 扁平水果列表：曲线水果不位于网格中（任务书 5.3）。
        out += ",\"world\":\"curves\"";
        out += ",\"roadLength\":" + formatFixed4(state.roadCurve.length());
        out += ",\"fruits\":[";
        bool firstFruit = true;
        for (const auto& slot : state.fruitSlots) {
            if (!slot.present) {
                continue;
            }
            if (!firstFruit) {
                out += ",";
            }
            firstFruit = false;
            out += "{\"id\":" + std::to_string(slot.fruitId) + ",\"fruit\":" + jsonQuote(slot.color) +
                   ",\"x\":" + formatFixed4(slot.world.x) + ",\"y\":" + formatFixed4(slot.world.y) +
                   ",\"size\":" + formatFixed4(slot.size) + ",\"settle\":" + std::to_string(slot.settleLeft) +
                   ",\"layer\":" + std::to_string(slot.layer) + "}";
        }
        out += "]";
        out += ",\"items\":[";
        for (std::size_t i = 0; i < state.items.size(); ++i) {
            if (i > 0) {
                out += ",";
            }
            out += "{\"id\":" + jsonQuote(state.items[i].id) + ",\"kind\":" +
                   jsonQuote(state.items[i].kindText) +
                   ",\"charges\":" + std::to_string(state.items[i].charges) + "}";
        }
        out += "]";
        out += ",\"effects\":[";
        for (std::size_t i = 0; i < state.effects.size(); ++i) {
            if (i > 0) {
                out += ",";
            }
            const EffectInstance& effect = state.effects[i];
            out += "{\"kind\":" + jsonQuote(toString(effect.kind)) +
                   ",\"start\":" + std::to_string(effect.startTick) +
                   ",\"expire\":" + std::to_string(effect.expireTick) +
                   ",\"target\":" + jsonQuote(effect.target) +
                   ",\"x\":" + formatFixed4(effect.center.x) +
                   ",\"y\":" + formatFixed4(effect.center.y) +
                   ",\"radius\":" + formatFixed4(effect.radius) + "}";
        }
        out += "]";
    } else {
        out += ",\"grid\":[";
        for (int r = 0; r < state.level.rows; ++r) {
            if (r > 0) {
                out += ",";
            }
            out += "[";
            for (int c = 0; c < state.level.cols; ++c) {
                if (c > 0) {
                    out += ",";
                }
                const FruitCell& cell = state.cells[static_cast<std::size_t>(r * state.level.cols + c)];
                if (!cell.present) {
                    out += "null";
                } else {
                    out += "{\"fruit\":" + jsonQuote(cell.color) + ",\"settle\":" + std::to_string(cell.settleLeft) +
                           ",\"ring\":" + std::to_string(cell.ring) + ",\"size\":" + fixed3(cell.size) + "}";
                }
            }
            out += "]";
        }
        out += "]";
    }

    out += ",\"cars\":[";
    for (std::size_t i = 0; i < state.cars.size(); ++i) {
        if (i > 0) {
            out += ",";
        }
        const Car& car = state.cars[i];
        out += "{\"id\":" + jsonQuote(car.id) + ",\"color\":" + jsonQuote(car.color) +
               ",\"load\":" + std::to_string(car.load) + ",\"capacity\":" + std::to_string(car.capacity) +
               ",\"s\":" + formatFixed3(car.s) + ",\"lane\":" + std::to_string(car.lane) +
               ",\"waiting\":" + (car.waiting ? "true" : "false") + "}";
    }
    out += "]";

    out += ",\"slots\":[";
    for (std::size_t i = 0; i < state.slots.size(); ++i) {
        if (i > 0) {
            out += ",";
        }
        out += state.slots[i].empty() ? "null" : jsonQuote(state.slots[i]);
    }
    out += "]";

    out += ",\"lanes\":[";
    for (std::size_t i = 0; i < state.lanes.size(); ++i) {
        if (i > 0) {
            out += ",";
        }
        out += "{\"color\":" + jsonQuote(state.lanes[i].color) + ",\"queue\":[";
        for (std::size_t q = 0; q < state.lanes[i].queue.size(); ++q) {
            if (q > 0) {
                out += ",";
            }
            out += jsonQuote(state.lanes[i].queue[q]);
        }
        out += "]}";
    }
    out += "]";

    out += ",\"counters\":{\"total\":" + std::to_string(state.fruitsTotal) +
           ",\"collected\":" + std::to_string(state.fruitsCollected) +
           ",\"remaining\":" + std::to_string(state.remainingFruits()) +
           ",\"exited\":" + std::to_string(state.exitedCount) +
           ",\"parked\":" + std::to_string(state.parkedCount) + "}";
    out += ",\"status\":" + jsonQuote(toString(state.status));
    out += ",\"requireSelect\":";
    out += state.requireSelect ? "true" : "false";
    out += ",\"lastSpawnTick\":" + std::to_string(state.lastSpawnTick);
    out += ",\"resultTick\":" + std::to_string(state.resultTick);
    out += "}";
    return out;
}

std::uint64_t stateHash(const GameState& state) { return fnv1a64(canonicalStateJson(state)); }

std::string snapshotJson(const GameState& state, const std::vector<const Event*>& events,
                         std::uint64_t snapshotSeq, const std::string& buildVersion,
                         const SnapshotContext& context) {
    const bool curve = state.worldKind == WorldKind::Curve;
    std::string out;
    out.reserve(16384);
    out += "{";
    out += "\"seq\":" + std::to_string(snapshotSeq);
    out += ",\"tick\":" + std::to_string(state.tick);
    out += ",\"elapsedMs\":" + std::to_string(tickToMs(state.tick));
    out += ",\"eventSeq\":" + std::to_string(state.eventSeq);
    out += ",\"levelId\":" + jsonQuote(state.level.id);
    out += ",\"levelEpoch\":" + std::to_string(context.levelEpoch);
    out += ",\"paused\":";
    out += context.paused ? "true" : "false";
    out += ",\"seed\":" + std::to_string(state.seed);
    out += ",\"buildVersion\":" + jsonQuote(buildVersion);
    out += ",\"status\":" + jsonQuote(toString(state.status));
    if (curve) {
        // 当 `world` 为 "curves" 时，客户端切换到扁平水果列表
        // （任务书附录 B）。
        out += ",\"world\":\"curves\"";
    }

    out += ",\"grid\":{\"rows\":" + std::to_string(state.level.rows) +
           ",\"cols\":" + std::to_string(state.level.cols) + ",\"cells\":[";
    for (int r = 0; r < state.level.rows; ++r) {
        if (r > 0) {
            out += ",";
        }
        out += "[";
        for (int c = 0; c < state.level.cols; ++c) {
            if (c > 0) {
                out += ",";
            }
            if (curve) {
                out += "null";
                continue;
            }
            const FruitCell& cell = state.cells[static_cast<std::size_t>(r * state.level.cols + c)];
            if (!cell.present) {
                out += "null";
            } else {
                out += "{\"fruit\":" + jsonQuote(cell.color) + ",\"settle\":" + std::to_string(cell.settleLeft) +
                       ",\"ring\":" + std::to_string(cell.ring) + ",\"size\":" + fixed3(cell.size) + "}";
            }
        }
        out += "]";
    }
    out += "]}";

    if (curve) {
        // 扁平水果列表（任务书附录 B）：id / 位置 / 尺寸 / 采集冷却。
        out += ",\"fruits\":[";
        bool firstFruit = true;
        for (const auto& slot : state.fruitSlots) {
            if (!slot.present) {
                continue;
            }
            if (!firstFruit) {
                out += ",";
            }
            firstFruit = false;
            out += "{\"id\":" + std::to_string(slot.fruitId) + ",\"fruit\":" + jsonQuote(slot.color) +
                   ",\"x\":" + formatFixed4(slot.world.x) + ",\"y\":" + formatFixed4(slot.world.y) +
                   ",\"size\":" + formatFixed4(slot.size) + ",\"settle\":" + std::to_string(slot.settleLeft) +
                   ",\"layer\":" + std::to_string(slot.layer) + ",\"roadS\":" + formatFixed4(slot.roadS) +
                   ",\"roadD\":" + formatFixed4(slot.roadD) + "}";
        }
        out += "]";
    }

    out += ",\"road\":{\"length\":";
    if (curve) {
        out += formatFixed4(state.roadCurve.length());
    } else {
        out += std::to_string(state.road.cells.size());
    }
    out += ",\"cells\":[";
    if (!curve) {
        for (std::size_t i = 0; i < state.road.cells.size(); ++i) {
            if (i > 0) {
                out += ",";
            }
            out += "[" + std::to_string(state.road.cells[i].r) + "," + std::to_string(state.road.cells[i].c) + "]";
        }
    }
    out += "],\"track\":[";
    if (!curve) {
        for (std::size_t i = 0; i < state.road.track.size(); ++i) {
            if (i > 0) {
                out += ",";
            }
            out += "[" + std::to_string(state.road.track[i].r) + "," + std::to_string(state.road.track[i].c) + "]";
        }
    }
    out += "]";
    const std::vector<LaneSide>& sides = state.level.road.collectSides;
    out += ",\"collectSide\":" + jsonQuote(curve && !sides.empty() ? toString(sides.front())
                                                                   : toString(state.level.road.collectSide));
    out += ",\"width\":" + std::to_string(state.level.road.width);
    out += ",\"laneOffset\":" + std::to_string(state.level.road.laneOffset);
    out += ",\"speed\":" + formatFixed3(state.level.road.speed);
    out += ",\"carLength\":" + formatFixed3(state.level.road.carLength);
    if (curve) {
        out += ",\"offset\":" + formatFixed4(state.level.road.offset);
        worldFor(state).appendRenderGeometry(state, out);
    }
    out += ",\"laneCells\":[";
    if (!curve) {
        for (std::size_t i = 0; i < state.road.laneCells.size(); ++i) {
            if (i > 0) {
                out += ",";
            }
            out += "[" + std::to_string(state.road.laneCells[i].r) + "," +
                   std::to_string(state.road.laneCells[i].c) + "]";
        }
    }
    out += "],\"laneCycle\":[";
    for (std::size_t i = 0; i < state.level.road.laneCycle.size(); ++i) {
        if (i > 0) {
            out += ",";
        }
        out += std::to_string(state.level.road.laneCycle[i]);
    }
    out += "],\"nextLane\":" + std::to_string(state.nextLane());
    out += ",\"lanes\":[";
    for (std::size_t l = 0; l < state.road.lanes.size(); ++l) {
        if (l > 0) {
            out += ",";
        }
        const LaneGeometry& lane = state.road.lanes[l];
        out += "{\"lane\":" + std::to_string(lane.lane) + ",\"collectSide\":" + jsonQuote(toString(lane.side));
        out += ",\"cells\":[";
        for (std::size_t i = 0; i < lane.cells.size(); ++i) {
            if (i > 0) {
                out += ",";
            }
            out += "[" + std::to_string(lane.cells[i].r) + "," + std::to_string(lane.cells[i].c) + "]";
        }
        out += "],\"contacts\":[";
        for (std::size_t i = 0; i < lane.contacts.size(); ++i) {
            if (i > 0) {
                out += ",";
            }
            out += "[";
            for (std::size_t k = 0; k < lane.contacts[i].size(); ++k) {
                if (k > 0) {
                    out += ",";
                }
                out += "[" + std::to_string(lane.contacts[i][k].r) + "," +
                       std::to_string(lane.contacts[i][k].c) + "]";
            }
            out += "]";
        }
        out += "]}";
    }
    out += "]}";

    out += ",\"vehicles\":[";
    for (std::size_t i = 0; i < state.cars.size(); ++i) {
        if (i > 0) {
            out += ",";
        }
        const Car& car = state.cars[i];
        const rules::Vec2d pos = rules::carLanePosition(state, car);
        const Vec2i cell = rules::carRoadCell(state, car);
        out += "{\"id\":" + jsonQuote(car.id) + ",\"color\":" + jsonQuote(car.color) +
               ",\"load\":" + std::to_string(car.load) + ",\"capacity\":" + std::to_string(car.capacity) +
               ",\"s\":" + formatFixed3(car.s) + ",\"lane\":" + std::to_string(car.lane) +
               ",\"pos\":[" + fixed3(pos.r) + "," + fixed3(pos.c) + "]" +
               ",\"heading\":" + std::to_string(rules::carHeading(state, car)) +
               ",\"cell\":[" + std::to_string(cell.r) + "," + std::to_string(cell.c) + "]" +
               ",\"state\":" + jsonQuote(car.waiting ? "WAITING" : "DRIVING") +
               ",\"label\":" + jsonQuote(labelOf(car));
        if (curve) {
            // x = 列轴，y = 行轴（任务书附录 B）。
            out += ",\"x\":" + formatFixed4(pos.c) + ",\"y\":" + formatFixed4(pos.r);
        }
        out += "}";
    }
    out += "]";

    out += ",\"parking\":{\"slots\":[";
    for (std::size_t i = 0; i < state.slots.size(); ++i) {
        if (i > 0) {
            out += ",";
        }
        if (state.slots[i].empty()) {
            out += "null";
            continue;
        }
        const auto it = state.vehicles.find(state.slots[i]);
        const std::string color = it != state.vehicles.end() ? it->second.color : std::string();
        const int capacity = it != state.vehicles.end() ? it->second.capacity : 0;
        const int load = it != state.vehicles.end() ? it->second.load : 0;
        out += "{\"index\":" + std::to_string(i) + ",\"vehicle\":" + jsonQuote(state.slots[i]) +
               ",\"color\":" + jsonQuote(color) + ",\"load\":" + std::to_string(load) +
               ",\"capacity\":" + std::to_string(capacity) +
               ",\"label\":" + jsonQuote(std::to_string(load) + "/" + std::to_string(capacity)) + "}";
    }
    out += "]}";

    out += ",\"lanes\":[";
    for (std::size_t i = 0; i < state.lanes.size(); ++i) {
        if (i > 0) {
            out += ",";
        }
        out += "{\"index\":" + std::to_string(i) + ",\"color\":" + jsonQuote(state.lanes[i].color) + ",\"queue\":[";
        for (std::size_t q = 0; q < state.lanes[i].queue.size(); ++q) {
            if (q > 0) {
                out += ",";
            }
            out += jsonQuote(state.lanes[i].queue[q]);
        }
        out += "],\"count\":" + std::to_string(state.lanes[i].queue.size()) + "}";
    }
    out += "]";

    out += ",\"vehicleInfo\":{";
    bool firstInfo = true;
    for (const auto& entry : state.vehicles) {
        if (!firstInfo) {
            out += ",";
        }
        firstInfo = false;
        out += jsonQuote(entry.first) + ":{\"color\":" + jsonQuote(entry.second.color) +
               ",\"load\":" + std::to_string(entry.second.load) +
               ",\"capacity\":" + std::to_string(entry.second.capacity) +
               ",\"label\":" + jsonQuote(std::to_string(entry.second.load) + "/" +
                                         std::to_string(entry.second.capacity)) +
               ",\"where\":" +
               jsonQuote(entry.second.where == Location::Lane
                             ? "LANE"
                             : (entry.second.where == Location::Slot
                                    ? "SLOT"
                                    : (entry.second.where == Location::Road ? "ROAD" : "EXITED"))) +
               "}";
    }
    out += "}";

    // 预留技能（任务书 5.8 / 8 T-33）：在每个曲线 snapshot 中可见。
    if (curve || !state.items.empty()) {
        out += ",\"items\":[";
        for (std::size_t i = 0; i < state.items.size(); ++i) {
            if (i > 0) {
                out += ",";
            }
            out += "{\"id\":" + jsonQuote(state.items[i].id) + ",\"kind\":" +
                   jsonQuote(state.items[i].kindText) +
                   ",\"charges\":" + std::to_string(state.items[i].charges) + "}";
        }
        out += "]";
        out += ",\"effects\":[";
        for (std::size_t i = 0; i < state.effects.size(); ++i) {
            if (i > 0) {
                out += ",";
            }
            const EffectInstance& effect = state.effects[i];
            out += "{\"kind\":" + jsonQuote(toString(effect.kind)) +
                   ",\"start\":" + std::to_string(effect.startTick) +
                   ",\"expire\":" + std::to_string(effect.expireTick) +
                   ",\"target\":" + jsonQuote(effect.target) + "}";
        }
        out += "]";
    }

    const int cooldownTicks = state.nextAllowedTick() - state.tick;
    const int cooldownMs = cooldownTicks > 0 ? tickToMs(cooldownTicks) : 0;
    const int laneCount = curve ? state.level.laneCount() : static_cast<int>(state.road.lanes.size());
    int layerCount = static_cast<int>(state.level.fruits.layers.size());
    if (curve) {
        layerCount = static_cast<int>(state.level.fruitLayers.size());
    }
    int itemCharges = 0;
    for (const auto& item : state.items) {
        itemCharges += item.charges > 0 ? item.charges : 0;
    }
    out += ",\"hud\":{\"levelName\":" + jsonQuote(state.level.name) +
           ",\"fruitsTotal\":" + std::to_string(state.fruitsTotal) +
           ",\"fruitsRemaining\":" + std::to_string(state.remainingFruits()) +
           ",\"fruitsCollected\":" + std::to_string(state.fruitsCollected) +
           ",\"spawnCooldownMs\":" + std::to_string(cooldownMs) +
           ",\"requireSelect\":" + (state.requireSelect ? "true" : "false") +
           ",\"result\":" +
           (state.status == GameStatus::Playing ? "null" : jsonQuote(toString(state.status))) +
           ",\"resultTick\":" +
           (state.resultTick >= 0 ? std::to_string(state.resultTick) : std::string("null")) +
           ",\"nextAllowedTick\":" + std::to_string(state.nextAllowedTick()) +
           ",\"exitedCount\":" + std::to_string(state.exitedCount) +
           ",\"parkedCount\":" + std::to_string(state.parkedCount) +
           ",\"nextLane\":" + std::to_string(state.nextLane()) +
           ",\"laneCount\":" + std::to_string(laneCount) +
           ",\"layerCount\":" + std::to_string(layerCount) +
           ",\"itemCharges\":" + std::to_string(itemCharges) +
           ",\"world\":" + jsonQuote(toString(state.worldKind)) + "}";

    out += ",\"events\":[";
    for (std::size_t i = 0; i < events.size(); ++i) {
        if (i > 0) {
            out += ",";
        }
        out += eventToJson(*events[i]);
    }
    out += "]}";
    return out;
}

std::string auditJson(const LevelDefinition& level) {
    const RoadMap map = buildRoadMap(level);
    const bool curve = level.usesCurves();
    GameState curveState;
    if (curve) {
        std::string ignored;
        buildCurveState(level, curveState, ignored);
    }
    std::string out;
    out += "{\"kind\":\"AUDIT\"";
    out += ",\"levelId\":" + jsonQuote(level.id);
    out += ",\"name\":" + jsonQuote(level.name);
    out += ",\"schemaVersion\":" + std::to_string(level.schemaVersion);
    out += ",\"mechanism\":" + jsonQuote(toString(level.mechanism));
    out += ",\"rows\":" + std::to_string(level.rows);
    out += ",\"cols\":" + std::to_string(level.cols);
    out += ",\"fruitsTotal\":" +
           std::to_string(curve ? static_cast<int>(curveState.fruitSlots.size()) : level.initialFruitCount());
    out += ",\"fruitsByLayer\":[";
    if (curve) {
        for (std::size_t k = 0; k < level.fruitLayers.size(); ++k) {
            if (k > 0) {
                out += ",";
            }
            int count = 0;
            for (const auto& slot : curveState.fruitSlots) {
                if (slot.layer == static_cast<int>(k) && slot.present) {
                    ++count;
                }
            }
            out += "{\"layer\":" + std::to_string(k) + ",\"id\":" + jsonQuote(level.fruitLayers[k].id) +
                   ",\"size\":" + fixed3(level.fruitLayers[k].size) +
                   ",\"color\":" + jsonQuote(level.fruitLayers[k].color()) +
                   ",\"cells\":" + std::to_string(count) +
                   ",\"spacing\":" + formatFixed4(level.fruitLayers[k].spacing) +
                   ",\"feedsFrom\":" + jsonQuote(level.fruitLayers[k].feedsFrom) + "}";
        }
    }
    for (std::size_t k = 0; !curve && k < level.fruits.layers.size(); ++k) {
        if (k > 0) {
            out += ",";
        }
        out += "{\"layer\":" + std::to_string(k) + ",\"size\":" + fixed3(level.fruits.layers[k].size) +
               ",\"color\":" + jsonQuote(level.fruits.layers[k].color) +
               ",\"cells\":" + std::to_string(level.fruits.layers[k].cells.size()) + "}";
    }
    out += "],\"refillDirection\":" + jsonQuote(toString(level.fruits.refill));
    out += ",\"roadLength\":" +
           std::to_string(curve ? static_cast<int>(curveState.roadCurve.sampleCount()) : level.road.path.size());
    if (curve) {
        out += ",\"arcLength\":" + formatFixed4(curveState.roadCurve.length());
        out += ",\"sampleStep\":" + formatFixed4(level.road.sampleStep);
    }
    out += ",\"track\":" + std::to_string(level.road.trackCells().size());
    out += ",\"width\":" + std::to_string(level.road.width);
    out += ",\"bendCollectsBoth\":";
    out += level.road.bendCollectsBoth ? "true" : "false";
    out += ",\"laneCycle\":[";
    for (std::size_t i = 0; i < level.road.laneCycle.size(); ++i) {
        if (i > 0) {
            out += ",";
        }
        out += std::to_string(level.road.laneCycle[i]);
    }
    out += "],\"island\":" + std::to_string(level.presentation.island.size());
    out += ",\"lanes\":[";
    for (std::size_t l = 0; l < map.lanes.size(); ++l) {
        const LaneGeometry& lane = map.lanes[l];
        if (l > 0) {
            out += ",";
        }
        std::vector<Vec2i> distinct;
        for (const auto& list : lane.contacts) {
            for (const Vec2i& cell : list) {
                bool known = false;
                for (const Vec2i& other : distinct) {
                    if (other == cell) {
                        known = true;
                        break;
                    }
                }
                if (!known) {
                    distinct.push_back(cell);
                }
            }
        }
        out += "{\"lane\":" + std::to_string(lane.lane) + ",\"collectSide\":" + jsonQuote(toString(lane.side)) +
               ",\"cells\":" + std::to_string(lane.cells.size()) +
               ",\"contacts\":" + std::to_string(distinct.size()) + ",\"contactList\":[";
        for (std::size_t k = 0; k < distinct.size(); ++k) {
            if (k > 0) {
                out += ",";
            }
            out += "[" + std::to_string(distinct[k].r) + "," + std::to_string(distinct[k].c) + "]";
        }
        out += "]}";
    }
    out += "]";
    out += ",\"capacities\":{\"total\":" + std::to_string(totalVehicleCapacity(level)) + "}";
    out += "}";
    return out;
}

std::string levelToJson(const LevelDefinition& level) {
    std::string out;
    out += "{\"schemaVersion\":" + std::to_string(level.schemaVersion);
    out += ",\"id\":" + jsonQuote(level.id);
    out += ",\"name\":" + jsonQuote(level.name);
    out += ",\"seed\":" + std::to_string(level.seed);
    out += ",\"grid\":{\"rows\":" + std::to_string(level.rows) + ",\"cols\":" + std::to_string(level.cols) + "}";
    out += ",\"palette\":[";
    for (std::size_t i = 0; i < level.palette.size(); ++i) {
        if (i > 0) {
            out += ",";
        }
        out += jsonQuote(level.palette[i]);
    }
    out += "],\"fruits\":{\"generator\":" + jsonQuote(level.fruits.explicitLayout ? "explicit" : level.fruits.generator) +
           ",\"runLength\":[" + std::to_string(level.fruits.runLow) + "," + std::to_string(level.fruits.runHigh) +
           "],\"colors\":[";
    for (std::size_t i = 0; i < level.fruits.colors.size(); ++i) {
        if (i > 0) {
            out += ",";
        }
        out += jsonQuote(level.fruits.colors[i]);
    }
    out += "],\"refillDirection\":" + jsonQuote(toString(level.fruits.refill)) + "}";
    out += ",\"road\":{\"path\":[";
    for (std::size_t i = 0; i < level.road.path.size(); ++i) {
        if (i > 0) {
            out += ",";
        }
        out += "[" + std::to_string(level.road.path[i].r) + "," + std::to_string(level.road.path[i].c) + "]";
    }
    out += "],\"width\":" + std::to_string(level.road.width);
    out += ",\"laneOffset\":" + std::to_string(level.road.laneOffset);
    out += ",\"collectSide\":" + jsonQuote(toString(level.road.collectSide));
    out += ",\"carLength\":" + formatFixed3(level.road.carLength);
    out += ",\"carGap\":" + formatFixed3(level.road.carGap);
    out += ",\"speed\":" + formatFixed3(level.road.speed) + "}";
    out += ",\"parking\":{\"slotCount\":" + std::to_string(level.parking.slotCount) + ",\"lanes\":[";
    for (std::size_t i = 0; i < level.parking.lanes.size(); ++i) {
        if (i > 0) {
            out += ",";
        }
        out += "{\"color\":" + jsonQuote(level.parking.lanes[i].color) + ",\"queue\":[";
        for (std::size_t q = 0; q < level.parking.lanes[i].queue.size(); ++q) {
            if (q > 0) {
                out += ",";
            }
            out += jsonQuote(level.parking.lanes[i].queue[q]);
        }
        out += "]}";
    }
    out += "]}";
    out += ",\"vehicles\":{";
    for (std::size_t i = 0; i < level.vehicles.size(); ++i) {
        if (i > 0) {
            out += ",";
        }
        out += jsonQuote(level.vehicles[i].first) + ":{\"color\":" + jsonQuote(level.vehicles[i].second.color) +
               ",\"capacity\":" + std::to_string(level.vehicles[i].second.capacity) + "}";
    }
    out += "}";
    out += ",\"timing\":{\"spawnIntervalMs\":" + std::to_string(level.timing.spawnIntervalMs) +
           ",\"settleTicks\":" + std::to_string(level.timing.settleTicks) + "}}";
    return out;
}

}  // namespace fc
