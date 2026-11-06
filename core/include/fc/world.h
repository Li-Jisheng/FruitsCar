// World 抽象（任务书 5.1、6 T-03/T-04）。
//
// 规则层不再谈论"网格相邻"或"曲线带"：它们向 World 询问接触判定、补充计划
// 以及某个槽位的水果摆放。共有两个实现：
//   * GridWorld  - phase-1/v2 网格关卡，行为由 M0-M19 回归套件与
//                  M_golden 哈希基线冻结；
//   * CurveWorld - schemaVersion 2 的 `mechanism: "curves"` 关卡。
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "fc/geom.h"
#include "fc/state.h"

namespace fc {

using FruitId = int;

// 补充链的一步：位于 `from` 的水果移动到 `to`
// （`to` 在该时刻保证为空）。
struct RefillMove {
    int from = -1;
    int to = -1;
};

// 槽位在两种坐标系中的位置（网格关卡只填充 `cell`）。
struct FruitPlacement {
    Vec2i cell{-1, -1};
    Vec2d world{0.0, 0.0};
    bool hasWorld = false;
};

// 向 World 询问车辆当前是否接触水果所得的结果。
struct TouchResult {
    bool collects = false;
    bool sawFruit = false;
    std::string skipReason;
    int slot = -1;      // 内部槽位索引（网格：row*cols+col）
    int fruitId = -1;   // 供动画 / 事件使用的稳定水果标识
    ColorId color;
    int layer = 0;
    int roadIndex = -1;  // 车辆的道路位置，用于跳过事件去重
    int lane = 0;
};

class World {
public:
    virtual ~World() = default;

    virtual WorldKind kind() const = 0;

    // 可行驶中心线的长度：道路格数（网格）或弧长（曲线）。
    virtual double roadLength(const GameState& state) const = 0;

    // "这辆车本 tick 是否接触水果？"——只判断接触条件。
    virtual TouchResult contact(const GameState& state, const Car& car) const = 0;

    // 针对某个特定水果的几何接触判定（任务书 5.1 的 `touches`）。
    virtual bool touches(const GameState& state, const Car& car, FruitId fruitId,
                         std::string& skipReason) const = 0;

    virtual Projection projectFruit(const GameState& state, FruitId fruitId) const = 0;
    virtual Vec2d fruitPosition(const GameState& state, FruitId fruitId) const = 0;
    virtual FruitPlacement placement(const GameState& state, int slot) const = 0;

    // 从 `slot` 采集到的水果的补充链，按应用顺序排列。
    virtual std::vector<RefillMove> planRefill(const GameState& state, int slot) const = 0;

    // 变更辅助函数，使表示细节保留在 World 内部。
    virtual void removeFruit(GameState& state, int slot) const = 0;
    virtual void applyRefillMove(GameState& state, const RefillMove& move) const = 0;

    // 追加到 snapshot 的渲染几何（曲线折线）。
    virtual void appendRenderGeometry(const GameState& state, std::string& outJson) const = 0;
};

std::unique_ptr<World> makeWorld(WorldKind kind);
std::unique_ptr<World> makeGridWorld();
std::unique_ptr<World> makeCurveWorld();
const char* toString(WorldKind kind);

// 曲线道路的横向几何（任务书 5.5）：车道 `lane` 的有符号中心偏移
// 以及接触带的半宽。
double laneCentreOffset(const GameState& state, int lane);
double contactBandWidth(const GameState& state);

// 构建曲线关卡的水果平铺列表、层链与缓存的投影。几何无法构建时返回 false
// 并用 "E2001"/"E2002"/"E2010"/"E2011"/"E2012"/"E2021" 填充 `err`。
bool buildCurveState(const LevelDefinition& level, GameState& state, std::string& err);

// 某条车道可触达水果的颜色直方图，供自动播放驱动使用。
// 网格世界精确复现 phase-1/2 的扫描；曲线世界使用每条车道的投影带。
std::map<ColorId, int> collectableColorCounts(const GameState& state, int lane);

}  // namespace fc
