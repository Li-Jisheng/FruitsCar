// GridWorld - 用 World 接口封装 phase-1/v2 的网格规则。
// 行为已冻结：这里返回的每个数值都来自重构前代码所用的同一批函数
// （rules::evaluateCollect、refillPath），因此 M0-M19 回归套件与 M_golden
// 哈希基线保持逐字节一致。
#include <cstddef>
#include <string>
#include <vector>

#include "fc/refill.h"
#include "fc/rules.h"
#include "fc/world.h"

namespace fc {
namespace {

class GridWorld final : public World {
public:
    WorldKind kind() const override { return WorldKind::Grid; }

    double roadLength(const GameState& state) const override {
        return static_cast<double>(state.road.cells.size());
    }

    TouchResult contact(const GameState& state, const Car& car) const override {
        const rules::CollectOutcome outcome = rules::evaluateCollect(state, car);
        TouchResult result;
        result.sawFruit = outcome.sawFruit;
        result.collects = outcome.collects;
        result.skipReason = outcome.skipReason;
        result.color = outcome.fruitColor;
        result.roadIndex = outcome.cellIndex;
        result.lane = outcome.lane;
        if (outcome.fruitCell.r >= 0 && outcome.fruitCell.c >= 0) {
            result.slot = outcome.fruitCell.r * state.level.cols + outcome.fruitCell.c;
            const FruitCell* cell = state.fruitAt(outcome.fruitCell.r, outcome.fruitCell.c);
            if (cell != nullptr) {
                result.fruitId = cell->fruitId;
                result.layer = cell->ring;
            }
        }
        return result;
    }

    bool touches(const GameState& state, const Car& car, FruitId fruitId,
                 std::string& skipReason) const override {
        const int length = static_cast<int>(state.road.cells.size());
        const int index = rules::centerCellIndex(car.s, state.level.road.carLength, length);
        if (index < 0) {
            skipReason = skip::kEmpty;
            return false;
        }
        if (car.lane < 0 || car.lane >= static_cast<int>(state.road.lanes.size())) {
            skipReason = skip::kNotAdjacent;
            return false;
        }
        const LaneGeometry& geometry = state.road.lanes[static_cast<std::size_t>(car.lane)];
        const std::vector<Vec2i>& candidates = geometry.contacts[static_cast<std::size_t>(index)];
        for (const Vec2i& cell : candidates) {
            if (state.cellIndex(cell.r, cell.c) == fruitId) {
                return true;
            }
        }
        skipReason = skip::kNotAdjacent;
        return false;
    }

    Projection projectFruit(const GameState& state, FruitId fruitId) const override {
        Projection projection;
        const FruitPlacement where = placement(state, fruitId);
        if (where.cell.r < 0) {
            return projection;
        }
        // 网格关卡不基于弧长坐标；弧坐标取该水果所在道路格的索引
        // （snapshot 对它们仍然使用 grid.cells）。
        projection.s = static_cast<double>(where.cell.r);
        projection.d = static_cast<double>(where.cell.c);
        projection.distance = 0.0;
        projection.segment = where.cell.r;
        return projection;
    }

    Vec2d fruitPosition(const GameState& state, FruitId fruitId) const override {
        const FruitPlacement where = placement(state, fruitId);
        return where.world;
    }

    FruitPlacement placement(const GameState& state, int slot) const override {
        FruitPlacement where;
        if (slot < 0 || slot >= static_cast<int>(state.cells.size())) {
            return where;
        }
        const FruitCell& cell = state.cells[static_cast<std::size_t>(slot)];
        if (!cell.present) {
            return where;
        }
        const int cols = state.level.cols > 0 ? state.level.cols : 1;
        where.cell = Vec2i{slot / cols, slot % cols};
        where.world = Vec2d{static_cast<double>(where.cell.c), static_cast<double>(where.cell.r)};
        where.hasWorld = false;
        return where;
    }

    std::vector<RefillMove> planRefill(const GameState& state, int slot) const override {
        std::vector<RefillMove> moves;
        if (slot < 0) {
            return moves;
        }
        const int cols = state.level.cols > 0 ? state.level.cols : 1;
        const Vec2i target{slot / cols, slot % cols};
        const std::vector<Vec2i> chain = refillPath(state.level, target, [&state](Vec2i cell) {
            const FruitCell* fruit = state.fruitAt(cell.r, cell.c);
            return fruit != nullptr && fruit->present;
        });
        if (chain.size() <= 1) {
            return moves;
        }
        std::vector<int> sources;
        for (std::size_t i = 1; i < chain.size(); ++i) {
            const FruitCell* cell = state.fruitAt(chain[i].r, chain[i].c);
            if (cell != nullptr && cell->present) {
                sources.push_back(chain[i].r * cols + chain[i].c);
            }
        }
        for (std::size_t k = 0; k < sources.size(); ++k) {
            const Vec2i destination = chain[k];
            moves.push_back(RefillMove{sources[k], destination.r * cols + destination.c});
        }
        return moves;
    }

    void removeFruit(GameState& state, int slot) const override {
        if (slot < 0 || slot >= static_cast<int>(state.cells.size())) {
            return;
        }
        FruitCell& cell = state.cells[static_cast<std::size_t>(slot)];
        cell.present = false;
        cell.color.clear();
        cell.settleLeft = 0;
        cell.settleSetTick = -1;
        cell.fruitId = -1;
    }

    void applyRefillMove(GameState& state, const RefillMove& move) const override {
        if (move.from < 0 || move.to < 0) {
            return;
        }
        if (move.from >= static_cast<int>(state.cells.size()) ||
            move.to >= static_cast<int>(state.cells.size())) {
            return;
        }
        FruitCell fruit = state.cells[static_cast<std::size_t>(move.from)];
        const std::size_t destination = static_cast<std::size_t>(move.to);
        const std::size_t index = destination;
        const int ring = index < state.cellRing.size() ? state.cellRing[index] : 0;
        const double size = index < state.cellSize.size() ? state.cellSize[index] : 1.0;
        const bool roadside = index < state.roadside.size() && state.roadside[index] != 0;
        if (roadside) {
            fruit.settleLeft = state.level.timing.settleTicks;
            fruit.settleSetTick = state.tick;
        }
        fruit.ring = ring;
        fruit.size = size;
        state.cells[destination] = fruit;

        FruitCell& source = state.cells[static_cast<std::size_t>(move.from)];
        source.present = false;
        source.color.clear();
        source.settleLeft = 0;
        source.settleSetTick = -1;
        source.fruitId = -1;
    }

    void appendRenderGeometry(const GameState&, std::string&) const override {}
};

}  // namespace

std::unique_ptr<World> makeGridWorld() { return std::unique_ptr<World>(new GridWorld()); }

}  // namespace fc
