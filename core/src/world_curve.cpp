// CurveWorld - `mechanism: "curves"` 关卡的弧长（arc-length）+ 横向带（lateral-band）模型
// （任务书 5.2/5.4/5.5）。道路与每个水果层在加载时都由同一套确定性几何内核
// 采样一次，因此逻辑侧的接触判定与渲染所用的折线共享同一份几何
// （任务书 4.1）。
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

#include "fc/fruit_gen.h"
#include "fc/world.h"

namespace fc {
namespace {

constexpr double kSideEps = 1e-9;

LaneSide laneSideOf(const GameState& state, int lane) {
    const std::vector<LaneSide>& sides = state.level.road.collectSides;
    if (lane >= 0 && lane < static_cast<int>(sides.size())) {
        return sides[static_cast<std::size_t>(lane)];
    }
    return LaneSide::Left;
}

int laneCountOf(const GameState& state) {
    const int width = state.level.road.width;
    return width < 1 ? 1 : width;
}

double laneCentreOf(const GameState& state, int lane) {
    const int width = laneCountOf(state);
    const double centre = static_cast<double>(width - 1) * 0.5;
    return static_cast<double>(lane) - centre + state.level.road.offset;
}

double contactBandOf(const GameState& state) {
    const double width = static_cast<double>(laneCountOf(state));
    return state.level.road.contactReach + 0.5 * width;
}

bool sideMatches(LaneSide side, double d) {
    if (side == LaneSide::Left) {
        return d >= -kSideEps;
    }
    if (side == LaneSide::Right) {
        return d <= kSideEps;
    }
    return false;
}

int slotIndexById(const GameState& state, int fruitId) {
    for (std::size_t i = 0; i < state.fruitSlots.size(); ++i) {
        if (state.fruitSlots[i].present && state.fruitSlots[i].fruitId == fruitId) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

class CurveWorld final : public World {
public:
    WorldKind kind() const override { return WorldKind::Curve; }

    double roadLength(const GameState& state) const override { return state.roadCurve.length(); }

    TouchResult contact(const GameState& state, const Car& car) const override {
        TouchResult result;
        result.lane = car.lane;
        const double length = state.roadCurve.length();
        if (length <= 0.0) {
            return result;
        }
        const double carLength = state.level.road.carLength;
        const double slack = state.level.road.contactSlack;
        const double rear = car.s;
        const double front = car.s + carLength + slack;
        result.roadIndex = static_cast<int>(std::floor(car.s + carLength / 2.0));

        const LaneSide side = laneSideOf(state, car.lane);
        const double laneCentre = laneCentreOf(state, car.lane);
        const double band = contactBandOf(state);

        bool haveCandidate = false;
        int firstSlot = -1;
        ColorId firstColor;
        std::string firstSkip;
        bool sawProbe = false;
        int probeSlot = -1;
        ColorId probeColor;
        std::string probeSkip;

        for (std::size_t i = 0; i < state.fruitSlots.size(); ++i) {
            const FruitSlot& slot = state.fruitSlots[i];
            if (!slot.present) {
                continue;
            }
            // 纵向窗口（longitudinal window）：s_car <= s_f < s_car + carLength + slack。
            if (!(slot.roadS + 1e-9 >= rear && slot.roadS < front)) {
                continue;
            }
            const bool onSide = sideMatches(side, slot.roadD);
            const bool inBand = std::fabs(slot.roadD - laneCentre) <= band + 1e-6;
            if (!onSide || !inBand) {
                if (!sawProbe) {
                    sawProbe = true;
                    probeSlot = static_cast<int>(i);
                    probeColor = slot.color;
                    probeSkip = onSide ? skip::kNotAdjacent : skip::kSide;
                }
                continue;
            }
            if (!haveCandidate) {
                haveCandidate = true;
                firstSlot = static_cast<int>(i);
                firstColor = slot.color;
                if (car.load >= car.capacity) {
                    firstSkip = skip::kFull;
                } else if (slot.color != car.color) {
                    firstSkip = skip::kColor;
                } else if (slot.settleLeft > 0) {
                    firstSkip = skip::kSettling;
                }
            }
            if (car.load >= car.capacity || slot.color != car.color || slot.settleLeft > 0) {
                continue;
            }
            result.collects = true;
            result.sawFruit = true;
            result.slot = static_cast<int>(i);
            result.fruitId = slot.fruitId;
            result.color = slot.color;
            result.layer = slot.layer;
            return result;
        }

        if (haveCandidate) {
            result.sawFruit = true;
            result.slot = firstSlot;
            result.color = firstColor;
            result.skipReason = firstSkip;
            return result;
        }
        if (sawProbe) {
            result.sawFruit = true;
            result.slot = probeSlot;
            result.color = probeColor;
            result.skipReason = probeSkip;
            return result;
        }
        result.skipReason = skip::kEmpty;
        return result;
    }

    bool touches(const GameState& state, const Car& car, FruitId fruitId,
                 std::string& skipReason) const override {
        const int slotIndex = slotIndexById(state, fruitId);
        if (slotIndex < 0) {
            skipReason = skip::kEmpty;
            return false;
        }
        const FruitSlot& slot = state.fruitSlots[static_cast<std::size_t>(slotIndex)];
        const double rear = car.s;
        const double front = car.s + state.level.road.carLength + state.level.road.contactSlack;
        if (!(slot.roadS + 1e-9 >= rear && slot.roadS < front)) {
            skipReason = skip::kEmpty;
            return false;
        }
        const LaneSide side = laneSideOf(state, car.lane);
        if (!sideMatches(side, slot.roadD)) {
            skipReason = skip::kSide;
            return false;
        }
        const double band = contactBandOf(state);
        if (std::fabs(slot.roadD - laneCentreOf(state, car.lane)) > band + 1e-6) {
            skipReason = skip::kNotAdjacent;
            return false;
        }
        return true;
    }

    Projection projectFruit(const GameState& state, FruitId fruitId) const override {
        Projection projection;
        const int slotIndex = slotIndexById(state, fruitId);
        if (slotIndex < 0) {
            return projection;
        }
        const FruitSlot& slot = state.fruitSlots[static_cast<std::size_t>(slotIndex)];
        projection.s = slot.roadS;
        projection.d = slot.roadD;
        projection.distance = slot.roadDistance;
        return projection;
    }

    Vec2d fruitPosition(const GameState& state, FruitId fruitId) const override {
        const int slotIndex = slotIndexById(state, fruitId);
        if (slotIndex < 0) {
            return Vec2d{0.0, 0.0};
        }
        return state.fruitSlots[static_cast<std::size_t>(slotIndex)].world;
    }

    FruitPlacement placement(const GameState& state, int slot) const override {
        FruitPlacement where;
        if (slot < 0 || slot >= static_cast<int>(state.fruitSlots.size())) {
            return where;
        }
        const FruitSlot& entry = state.fruitSlots[static_cast<std::size_t>(slot)];
        where.world = entry.world;
        where.hasWorld = true;
        where.cell = toVec2i(entry.world);
        return where;
    }

    std::vector<RefillMove> planRefill(const GameState& state, int slot) const override {
        std::vector<RefillMove> moves;
        if (slot < 0 || slot >= static_cast<int>(state.fruitSlots.size())) {
            return moves;
        }
        // 单个水果的补充链是一条路径：它自己，然后是为它供果的外层槽位，
        // 如此向外延伸（任务书 5.4 步骤 4）。
        std::vector<int> chain;
        chain.push_back(slot);
        int cursor = slot;
        for (;;) {
            const int feeder = state.fruitSlots[static_cast<std::size_t>(cursor)].child;
            if (feeder < 0 || chain.size() > state.fruitSlots.size()) {
                break;
            }
            chain.push_back(feeder);
            cursor = feeder;
        }
        if (chain.size() <= 1) {
            return moves;
        }
        std::vector<int> sources;
        for (std::size_t i = 1; i < chain.size(); ++i) {
            if (state.fruitSlots[static_cast<std::size_t>(chain[i])].present) {
                sources.push_back(chain[i]);
            }
        }
        for (std::size_t k = 0; k < sources.size(); ++k) {
            moves.push_back(RefillMove{sources[k], chain[k]});
        }
        return moves;
    }

    void removeFruit(GameState& state, int slot) const override {
        if (slot < 0 || slot >= static_cast<int>(state.fruitSlots.size())) {
            return;
        }
        FruitSlot& entry = state.fruitSlots[static_cast<std::size_t>(slot)];
        entry.present = false;
        entry.color.clear();
        entry.settleLeft = 0;
        entry.settleSetTick = -1;
        entry.fruitId = -1;
    }

    void applyRefillMove(GameState& state, const RefillMove& move) const override {
        if (move.from < 0 || move.to < 0) {
            return;
        }
        if (move.from >= static_cast<int>(state.fruitSlots.size()) ||
            move.to >= static_cast<int>(state.fruitSlots.size())) {
            return;
        }
        const FruitSlot source = state.fruitSlots[static_cast<std::size_t>(move.from)];
        FruitSlot target = state.fruitSlots[static_cast<std::size_t>(move.to)];
        // 目标槽位的静态几何保持不变，只有水果本身移动。
        target.present = true;
        target.color = source.color;
        target.fruitId = source.fruitId;
        if (target.roadside) {
            target.settleLeft = state.level.timing.settleTicks;
            target.settleSetTick = state.tick;
        } else {
            target.settleLeft = source.settleLeft;
            target.settleSetTick = source.settleSetTick;
        }
        state.fruitSlots[static_cast<std::size_t>(move.to)] = target;

        FruitSlot& emptied = state.fruitSlots[static_cast<std::size_t>(move.from)];
        emptied.present = false;
        emptied.color.clear();
        emptied.settleLeft = 0;
        emptied.settleSetTick = -1;
        emptied.fruitId = -1;
    }

    // 道路折线由 snapshot 序列化器从 state.roadCurve 输出，
    // 因此这里不需要额外处理。
    void appendRenderGeometry(const GameState& state, std::string& outJson) const override {
        outJson += ",\"polyline\":" + polylineJson(state.roadCurve);
        outJson += ",\"sampleCount\":" + std::to_string(state.roadCurve.sampleCount());
        outJson += ",\"sampleStep\":" + formatFixed4(state.level.road.sampleStep);
    }
};

}  // namespace

double laneCentreOffset(const GameState& state, int lane) { return laneCentreOf(state, lane); }

double contactBandWidth(const GameState& state) { return contactBandOf(state); }

std::unique_ptr<World> makeCurveWorld() { return std::unique_ptr<World>(new CurveWorld()); }

bool buildCurveState(const LevelDefinition& level, GameState& state, std::string& err) {
    err.clear();
    state.worldKind = WorldKind::Curve;
    state.roadCurve = Polyline::fromControlPoints(level.road.controlPoints, level.road.closed,
                                                  level.road.sampleStep, err);
    if (!err.empty()) {
        return false;
    }
    if (level.fruitLayers.empty()) {
        err = "E2010";
        return false;
    }

    state.fruitSlots.clear();
    struct Range {
        std::size_t begin = 0;
        std::size_t end = 0;
    };
    std::vector<Range> ranges(level.fruitLayers.size());
    const LaneSide collectSide = level.road.collectSides.empty() ? LaneSide::Left
                                                                  : level.road.collectSides.front();
    const double band = level.road.contactReach + 0.5 * static_cast<double>(level.laneCount());

    int nextId = 1;
    for (std::size_t k = 0; k < level.fruitLayers.size(); ++k) {
        const CurveLayerDef& layer = level.fruitLayers[k];
        const Polyline poly = Polyline::fromControlPoints(layer.controlPoints, layer.closed, layer.sampleStep, err);
        if (!err.empty()) {
            return false;
        }
        const std::vector<double> positions = fruitLayerArcPositions(poly, layer.spacing);
        if (positions.empty()) {
            err = "E2010";
            return false;
        }
        const double layerLength = poly.length();
        ranges[k].begin = state.fruitSlots.size();
        Rng jitterRng(layer.jitterSeed != 0 ? layer.jitterSeed : level.seed + k + 1, 0x4552ULL + k);
        for (std::size_t i = 0; i < positions.size(); ++i) {
            const double arc = positions[i];
            FruitSlot slot;
            slot.present = true;
            slot.layer = static_cast<int>(k);
            slot.color = layer.colors.empty() ? ColorId() : layer.colors[i % layer.colors.size()];
            slot.size = layer.size;
            slot.fruitId = nextId++;
            slot.layerS = arc;
            slot.layerU = layerLength > 0.0 ? arc / layerLength : 0.0;
            Vec2d point = poly.positionAt(arc);
            if (layer.jitterOffset > 0.0) {
                const double offset = (jitterRng.nextDouble() * 2.0 - 1.0) * layer.jitterOffset;
                const Vec2d tangent = poly.tangentAt(arc);
                point = point + offset * Vec2d{tangent.y, -tangent.x};
            }
            if (layer.jitterSize > 0.0) {
                slot.size = layer.size * (1.0 + (jitterRng.nextDouble() * 2.0 - 1.0) * layer.jitterSize);
            }
            slot.world = point;
            const Projection projection = state.roadCurve.project(point);
            slot.roadS = projection.s;
            slot.roadD = projection.d;
            slot.roadDistance = projection.distance;
            slot.roadside = sideMatches(collectSide, projection.d) &&
                            std::fabs(projection.d) <= band + 1e-6;
            state.fruitSlots.push_back(slot);
        }
        ranges[k].end = state.fruitSlots.size();
    }

    // 层链（任务书 5.4）：对于声明了 `feedsFrom` 的层，*源*（外层）层的每个
    // 槽位都映射到本层（内层）中离它最近的槽位。
    // 内层槽位通过 `child` 记录外层供果者，
    // 因此一次链式遍历即可串起整条线。
    // 两个外层槽位不得共享同一个内层槽位（E2011：该映射必须是单射，
    // 否则关卡无法被清空）。
    for (std::size_t k = 0; k < level.fruitLayers.size(); ++k) {
        const CurveLayerDef& layer = level.fruitLayers[k];
        if (layer.feedsFrom.empty()) {
            continue;
        }
        int parentLayer = -1;
        for (std::size_t p = 0; p < level.fruitLayers.size(); ++p) {
            if (level.fruitLayers[p].id == layer.feedsFrom) {
                parentLayer = static_cast<int>(p);
                break;
            }
        }
        if (parentLayer < 0 || parentLayer == static_cast<int>(k)) {
            err = "E2004";
            return false;
        }
        const std::size_t sourceBegin = ranges[static_cast<std::size_t>(parentLayer)].begin;
        const std::size_t sourceEnd = ranges[static_cast<std::size_t>(parentLayer)].end;
        if (sourceBegin == sourceEnd) {
            err = "E2012";
            return false;
        }
        const bool circular = layer.closed && level.fruitLayers[static_cast<std::size_t>(parentLayer)].closed;
        // 单射最近匹配（任务书 5.4 步骤 2-3）：每个外层槽位取最近的
        // *未被占用*的内层槽位；距离相同时按 (s, index) 决胜，
        // 也就是目标区间的遍历顺序。
        std::vector<unsigned char> claimed(ranges[k].end - ranges[k].begin, 0);
        for (std::size_t m = sourceBegin; m < sourceEnd; ++m) {
            const double u = state.fruitSlots[m].layerU;
            int best = -1;
            double bestDistance = 1e18;
            for (std::size_t j = ranges[k].begin; j < ranges[k].end; ++j) {
                if (claimed[j - ranges[k].begin] != 0) {
                    continue;
                }
                double delta = std::fabs(u - state.fruitSlots[j].layerU);
                if (circular) {
                    delta = std::min(delta, 1.0 - delta);
                }
                if (delta < bestDistance - 1e-12) {
                    bestDistance = delta;
                    best = static_cast<int>(j);
                }
            }
            if (best < 0) {
                // 外层槽位比内层槽位多：该关卡永远无法被清空
                // （任务书 5.4 步骤 3 / E2011）。
                err = "E2011|layer=" + layer.id + " outerU=" +
                      formatFixed3(state.fruitSlots[m].layerU) + " slot=" + std::to_string(m) +
                      " reason=no-unclaimed-inner (outer=" + std::to_string(sourceEnd - sourceBegin) +
                      ", inner=" + std::to_string(ranges[k].end - ranges[k].begin) + ")";
                return false;
            }
            claimed[static_cast<std::size_t>(best) - ranges[k].begin] = 1;
            state.fruitSlots[static_cast<std::size_t>(best)].child = static_cast<int>(m);
            state.fruitSlots[m].parent = best;
        }
    }

    state.nextFruitId = nextId;
    state.fruitsTotal = static_cast<int>(state.fruitSlots.size());
    state.fruitsCollected = 0;
    return true;
}

}  // namespace fc
