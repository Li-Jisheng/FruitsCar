// 完整玩法状态。值类型：可拷贝、可按 hash 比较、可序列化。
#pragma once

#include <map>
#include <string>
#include <vector>

#include "fc/effects.h"
#include "fc/events.h"
#include "fc/geom.h"
#include "fc/level.h"
#include "fc/road.h"
#include "fc/types.h"

namespace fc {

// 由哪个 World 实现持有水果几何（任务书 5.1）。
enum class WorldKind { Grid, Curve };

struct FruitCell {
    bool present = false;
    ColorId color;
    int settleLeft = 0;      // 剩余采集冷却的 tick 数
    int settleSetTick = -1;  // 该冷却被设定时的 tick
    int ring = 0;            // 当前所在格所属的层索引
    double size = 1.0;       // 该层的渲染缩放（1.00 / 1.12 / 1.24）
    int fruitId = -1;        // 稳定标识（曲线世界；网格关卡上为 -1）
};

// 曲线关卡中的一个水果槽位（任务书 5.3 的 snapshot `fruits[]`）。
// 槽位是水果层上的一个固定点；补充链触发时水果在槽位之间移动，
// 并随身携带各自的 `fruitId`。
struct FruitSlot {
    bool present = false;
    bool roadside = false;  // 位于其自身一侧的采集带内（静态）
    ColorId color;
    int settleLeft = 0;
    int settleSetTick = -1;
    int layer = 0;
    double size = 1.0;
    int fruitId = -1;
    Vec2d world{0.0, 0.0};
    double layerS = 0.0;  // 在其所属层曲线上的弧长位置
    double layerU = 0.0;  // 在其所属层上归一化到 [0,1) 的弧长位置
    double roadS = 0.0;   // 投影到道路中心线上的位置（缓存）
    double roadD = 0.0;   // 有符号横向距离（缓存）
    double roadDistance = 0.0;
    int child = -1;   // 向本槽位补充的外层槽位（-1 = 无）
    int parent = -1;  // 本槽位向其补充的内层槽位（-1 = 无）
};

struct Car {
    EntityId id;
    ColorId color;
    int load = 0;
    int capacity = 0;
    double s = 0.0;      // 沿道路的里程，范围 [-carLength, roadLength]
    bool waiting = false;  // 停在道路末端等待空闲车位
    int lane = 0;          // 沿道路的车道索引（phase 2）

    // 用于跳过事件去重的派生记账字段。不参与序列化，也不属于
    // stateHash：它只在车辆停留于同一格且判定结果不变时抑制重复的证据行。
    int evalCell = -1;
    std::string evalSkip;
};

enum class Location { Lane, Slot, Road, Exited };

struct VehicleState {
    EntityId id;
    ColorId color;
    int capacity = 0;
    int load = 0;
    Location where = Location::Lane;
    int index = -1;  // 车道索引或车位索引
};

struct LaneState {
    ColorId color;
    std::vector<EntityId> queue;
};

enum class GameStatus { Playing, Win, FailTrafficJam };

const char* toString(GameStatus status);

struct GameState {
    LevelDefinition level;
    std::uint64_t seed = 0;
    RoadMap road;  // 由关卡派生，随值状态一起保存，使副本自包含
    std::vector<unsigned char> roadside;  // rows*cols，该格接触道路时为 1

    std::vector<FruitCell> cells;  // rows*cols，行优先
    std::vector<int> cellRing;     // 每个格的静态层索引（phase 2）
    std::vector<double> cellSize;  // 每个格的静态渲染缩放（phase 2）
    std::vector<Car> cars;         // 最前车在前（s 降序）
    std::vector<LaneState> lanes;
    std::vector<EntityId> slots;   // 空字符串 == 空闲车位
    std::map<EntityId, VehicleState> vehicles;

    // ---- 曲线世界（任务书 5.3 / 6 T-08） -----------------------------
    WorldKind worldKind = WorldKind::Grid;
    Polyline roadCurve;              // 曲线道路的稠密中心线
    std::vector<FruitSlot> fruitSlots;  // 曲线关卡的水果平铺列表
    int nextFruitId = 1;
    std::vector<ItemState> items;      // 预留技能（snapshot `items[]`）
    std::vector<EffectInstance> effects;  // 预留（本阶段始终为空）

    int fruitsTotal = 0;
    int fruitsCollected = 0;
    int exitedCount = 0;
    int parkedCount = 0;

    int lastSpawnTick = -1000000;
    bool commandUsedThisTick = false;
    bool requireSelect = false;
    int spawnCount = 0;  // 迄今已受理的刷车次数（phase 2 车道轮转）

    int tick = 0;
    GameStatus status = GameStatus::Playing;
    int resultTick = -1;
    std::string resultReason;

    std::uint64_t eventSeq = 0;
    std::vector<Event> events;

    // ---- 辅助函数 ---------------------------------------------------------
    int cellIndex(int r, int c) const { return r * level.cols + c; }
    bool inGrid(int r, int c) const { return r >= 0 && r < level.rows && c >= 0 && c < level.cols; }
    FruitCell* fruitAt(int r, int c) {
        if (!inGrid(r, c)) {
            return nullptr;
        }
        return &cells[static_cast<std::size_t>(cellIndex(r, c))];
    }
    const FruitCell* fruitAt(int r, int c) const {
        if (!inGrid(r, c)) {
            return nullptr;
        }
        return &cells[static_cast<std::size_t>(cellIndex(r, c))];
    }
    int remainingFruits() const { return fruitsTotal - fruitsCollected; }
    // 可行驶中心线长度：网格关卡使用道路格数，曲线关卡使用
    // 采样后的弧长（任务书 5.2）。
    double roadLength() const {
        return worldKind == WorldKind::Curve ? roadCurve.length()
                                             : static_cast<double>(road.cells.size());
    }
    const FruitSlot* fruitSlotOfId(int fruitId) const;
    FruitSlot* fruitSlot(int index) {
        return index >= 0 && index < static_cast<int>(fruitSlots.size())
                   ? &fruitSlots[static_cast<std::size_t>(index)]
                   : nullptr;
    }
    const FruitSlot* fruitSlot(int index) const {
        return index >= 0 && index < static_cast<int>(fruitSlots.size())
                   ? &fruitSlots[static_cast<std::size_t>(index)]
                   : nullptr;
    }
    int occupiedSlots() const;
    bool parkingFull() const;
    const Car* findCar(const EntityId& id) const;
    VehicleState* findVehicle(const EntityId& id);
    int nextAllowedTick() const { return lastSpawnTick + level.spawnTicks(); }
    // 第 `spawnIndex` 次刷车所使用的道路车道（laneCycle，任务书 6.5）。
    int laneForSpawn(int spawnIndex) const;
    int nextLane() const { return laneForSpawn(spawnCount); }
    Event& pushEvent(EventType type);
};

}  // namespace fc
