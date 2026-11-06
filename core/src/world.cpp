#include "fc/world.h"

#include "fc/rules.h"

namespace fc {

const char* toString(WorldKind kind) { return kind == WorldKind::Curve ? "curves" : "grid"; }

std::unique_ptr<World> makeWorld(WorldKind kind) {
    if (kind == WorldKind::Curve) {
        return makeCurveWorld();
    }
    return makeGridWorld();
}

std::map<ColorId, int> collectableColorCounts(const GameState& state, int lane) {
    std::map<ColorId, int> counts;
    if (state.worldKind == WorldKind::Curve) {
        for (const auto& slot : state.fruitSlots) {
            if (!slot.present || slot.settleLeft > 0 || !slot.roadside) {
                continue;
            }
            ++counts[slot.color];
        }
        return counts;
    }
    if (lane < 0 || lane >= static_cast<int>(state.road.lanes.size())) {
        return counts;
    }
    const LaneGeometry& geometry = state.road.lanes[static_cast<std::size_t>(lane)];
    std::vector<Vec2i> seen;
    for (const auto& list : geometry.contacts) {
        for (const Vec2i& cell : list) {
            bool known = false;
            for (const Vec2i& other : seen) {
                if (other == cell) {
                    known = true;
                    break;
                }
            }
            if (known) {
                continue;
            }
            seen.push_back(cell);
            const FruitCell* fruit = state.fruitAt(cell.r, cell.c);
            if (fruit == nullptr || !fruit->present || fruit->settleLeft > 0) {
                continue;
            }
            ++counts[fruit->color];
        }
    }
    return counts;
}

}  // namespace fc
