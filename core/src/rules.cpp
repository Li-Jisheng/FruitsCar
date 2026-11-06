#include "fc/rules.h"

#include <cmath>
#include <cstddef>

#include "fc/world.h"

namespace fc {
namespace rules {

int centerCellIndex(double s, double carLength, int roadLength) {
    const double centre = s + carLength / 2.0;
    if (centre < 0.0) {
        return -1;
    }
    const int index = static_cast<int>(std::floor(centre));
    if (index < 0 || index >= roadLength) {
        return -1;
    }
    return index;
}

Vec2i carRoadCell(const GameState& state, const Car& car) {
    if (state.worldKind == WorldKind::Curve) {
        // 曲线关卡的车返回其世界坐标落入的网格格，
        // 这样所有旧版消费方都能拿到合理的值。
        const fc::Vec2d point = state.roadCurve.positionAt(car.s + state.level.road.carLength / 2.0);
        return toVec2i(point);
    }
    const int length = static_cast<int>(state.road.cells.size());
    if (length <= 0) {
        return Vec2i{-1, -1};
    }
    const double centre = car.s + state.level.road.carLength / 2.0;
    int index = static_cast<int>(std::floor(centre));
    if (index < 0) {
        index = 0;
    }
    if (index >= length) {
        index = length - 1;
    }
    if (static_cast<std::size_t>(car.lane) < state.road.lanes.size()) {
        return state.road.lanes[static_cast<std::size_t>(car.lane)].cells[static_cast<std::size_t>(index)];
    }
    return state.road.laneCells[static_cast<std::size_t>(index)];
}

Vec2d carLanePosition(const GameState& state, const Car& car) {
    if (state.worldKind == WorldKind::Curve) {
        const double centre = car.s + state.level.road.carLength / 2.0;
        const fc::Vec2d point = state.roadCurve.positionAt(centre);
        const fc::Vec2d tangent = state.roadCurve.tangentAt(centre);
        const double laneOffset = laneCentreOffset(state, car.lane);
        const fc::Vec2d shifted = point + laneOffset * fc::Vec2d{tangent.y, -tangent.x};
        return Vec2d{shifted.y, shifted.x};  // 供 snapshot 使用的 (row, col)
    }
    const int length = static_cast<int>(state.road.cells.size());
    if (length <= 0) {
        return Vec2d{0.0, 0.0};
    }
    const std::vector<Vec2i>& cells = static_cast<std::size_t>(car.lane) < state.road.lanes.size()
                                          ? state.road.lanes[static_cast<std::size_t>(car.lane)].cells
                                          : state.road.laneCells;
    // 车头仍在起点线之后的车，会被放到向后外推的小数坐标上，
    // 以保证入口处始终可见。
    double s = car.s;
    if (s < 0.0) {
        s = 0.0;
    }
    if (s > static_cast<double>(length - 1)) {
        s = static_cast<double>(length - 1);
    }
    const int index = static_cast<int>(std::floor(s));
    const double fraction = s - static_cast<double>(index);
    const Vec2i a = cells[static_cast<std::size_t>(index)];
    const Vec2i b = cells[static_cast<std::size_t>(index + 1 < length ? index + 1 : index)];
    return Vec2d{static_cast<double>(a.r) + fraction * static_cast<double>(b.r - a.r),
                 static_cast<double>(a.c) + fraction * static_cast<double>(b.c - a.c)};
}

int carHeading(const GameState& state, const Car& car) {
    if (state.worldKind == WorldKind::Curve) {
        const double centre = car.s + state.level.road.carLength / 2.0;
        const fc::Vec2d tangent = state.roadCurve.tangentAt(centre);
        const double degrees = std::atan2(tangent.y, tangent.x) * 180.0 / 3.14159265358979323846;
        int rounded = static_cast<int>(std::lround(degrees));
        rounded %= 360;
        if (rounded < 0) {
            rounded += 360;
        }
        return rounded;
    }
    const int length = static_cast<int>(state.road.cells.size());
    if (length < 2) {
        return 0;
    }
    double s = car.s + state.level.road.carLength / 2.0;
    int index = static_cast<int>(std::floor(s));
    if (index < 0) {
        index = 0;
    }
    if (index >= length) {
        index = length - 1;
    }
    int next = index + 1 < length ? index + 1 : index;
    int previous = index > 0 ? index - 1 : index;
    Vec2i delta = state.road.cells[static_cast<std::size_t>(next)] -
                  state.road.cells[static_cast<std::size_t>(previous)];
    if (delta.r == 0 && delta.c == 0) {
        return 0;
    }
    if (delta.r != 0 && delta.c != 0) {
        // 拐角处的对角过渡：优先采用行驶轴向。
        if (delta.r > 0) {
            return 90;
        }
        if (delta.r < 0) {
            return 270;
        }
    }
    if (delta.c > 0) {
        return 0;
    }
    if (delta.c < 0) {
        return 180;
    }
    if (delta.r > 0) {
        return 90;
    }
    return 270;
}

CollectOutcome evaluateCollect(const GameState& state, const Car& car) {
    CollectOutcome outcome;
    const int length = static_cast<int>(state.road.cells.size());
    const int index = centerCellIndex(car.s, state.level.road.carLength, length);
    if (index < 0) {
        return outcome;
    }
    outcome.cellIndex = index;
    outcome.lane = car.lane;

    const LaneGeometry* lane = nullptr;
    if (static_cast<std::size_t>(car.lane) < state.road.lanes.size()) {
        lane = &state.road.lanes[static_cast<std::size_t>(car.lane)];
    }
    if (lane == nullptr) {
        return outcome;
    }
    outcome.roadCell = lane->cells[static_cast<std::size_t>(index)];
    const std::vector<Vec2i>& candidates = lane->contacts[static_cast<std::size_t>(index)];

    // 优先取真正可收集的第一个水果；主接触点优先，
    // 弯道补充方向其次（任务书 4.3.2）。
    std::string firstSkip;
    bool haveFirst = false;
    Vec2i firstCell{-1, -1};
    ColorId firstColor;
    for (const Vec2i& candidate : candidates) {
        const FruitCell* cell = state.fruitAt(candidate.r, candidate.c);
        if (cell == nullptr || !cell->present) {
            continue;
        }
        if (!haveFirst) {
            haveFirst = true;
            firstCell = candidate;
            firstColor = cell->color;
            if (car.load >= car.capacity) {
                firstSkip = skip::kFull;
            } else if (cell->color != car.color) {
                firstSkip = skip::kColor;
            } else if (cell->settleLeft > 0) {
                firstSkip = skip::kSettling;
            }
        }
        if (car.load >= car.capacity) {
            continue;
        }
        if (cell->color != car.color) {
            continue;
        }
        if (cell->settleLeft > 0) {
            continue;
        }
        outcome.collects = true;
        outcome.sawFruit = true;
        outcome.fruitCell = candidate;
        outcome.fruitColor = cell->color;
        return outcome;
    }
    if (haveFirst) {
        outcome.sawFruit = true;
        outcome.fruitCell = firstCell;
        outcome.fruitColor = firstColor;
        outcome.skipReason = firstSkip;
        return outcome;
    }

    if (candidates.empty()) {
        // phase-1 的宽路会说明车为何无法到达水果一侧。
        const Vec2i away = state.road.away[static_cast<std::size_t>(index)];
        const Vec2i potential = state.road.cells[static_cast<std::size_t>(index)] - away;
        const FruitCell* probe = state.fruitAt(potential.r, potential.c);
        if (probe != nullptr && probe->present) {
            outcome.sawFruit = true;
            outcome.skipReason = skip::kNotAdjacent;
            outcome.fruitCell = potential;
            outcome.fruitColor = probe->color;
        }
        return outcome;
    }
    outcome.skipReason = skip::kEmpty;
    outcome.fruitCell = lane->primary[static_cast<std::size_t>(index)];
    return outcome;
}

bool entranceBlocked(const GameState& state, int lane) {
    double minS = 1e9;
    bool found = false;
    for (const auto& car : state.cars) {
        if (lane >= 0 && car.lane != lane) {
            continue;
        }
        if (car.s < minS) {
            minS = car.s;
        }
        found = true;
    }
    if (!found) {
        return false;
    }
    return minS < state.level.road.carGap - kEps;
}

bool waitingAtEnd(const GameState& state) {
    const double length = state.roadLength();
    for (const auto& car : state.cars) {
        if (car.s >= length - kEps && car.load < car.capacity) {
            return true;
        }
    }
    return false;
}

bool jamConditions(const GameState& state) {
    return entranceBlocked(state, -1) && state.parkingFull() && waitingAtEnd(state);
}

GameStatus resolveStatus(const GameState& state) {
    if (state.remainingFruits() == 0) {
        return GameStatus::Win;
    }
    if (jamConditions(state)) {
        return GameStatus::FailTrafficJam;
    }
    return GameStatus::Playing;
}

}  // namespace rules
}  // namespace fc
