// 纯规则函数：采集几何、入场 / 停车 / 等待判定以及胜负裁决。
// 不做任何修改，无 IO。
#pragma once

#include <string>

#include "fc/state.h"

namespace fc {
namespace rules {

// 客户端用于车道偏移的分数网格位置 (row, col)。
struct Vec2d {
    double r = 0.0;
    double c = 0.0;
};

// 车辆在里程 `s` 处的中心格索引；当车辆尚未入场（s < 0）或已完全离开
// 可采集范围时返回 -1。
int centerCellIndex(double s, double carLength, int roadLength);

// 车辆当前在道路上占据的格子（被截断在道路范围内）。
Vec2i carRoadCell(const GameState& state, const Car& car);

// 车辆在其所属车道上的分数位置，在车道格中心之间插值
// （任务书 4.6：vehicles[].pos）。
Vec2d carLanePosition(const GameState& state, const Car& car);

// 朝向角，单位为度：0 = 右，90 = 下，180 = 左，270 = 上。
int carHeading(const GameState& state, const Car& car);

struct CollectOutcome {
    bool collects = false;
    bool sawFruit = false;
    std::string skipReason;
    int cellIndex = -1;
    int lane = 0;
    Vec2i roadCell{-1, -1};
    Vec2i fruitCell{-1, -1};
    ColorId fruitColor;
};

CollectOutcome evaluateCollect(const GameState& state, const Car& car);

// `lane` 的入口容不下一辆新车（最后一辆车与入口的距离小于
// carGap）。传 -1 表示检测所有车道（phase-1 语义）。
bool entranceBlocked(const GameState& state, int lane = -1);

// 有未完成的车辆正在道路末端等待空闲停车位。
bool waitingAtEnd(const GameState& state);

// 同一 tick 上 WIN 优先于 FAIL_TRAFFIC_JAM（任务书 D-09）。
GameStatus resolveStatus(const GameState& state);

// 三个拥堵条件同时成立时为 true。
bool jamConditions(const GameState& state);

}  // namespace rules
}  // namespace fc
