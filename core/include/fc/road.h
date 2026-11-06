// 完全由关卡定义推导出的道路几何。由校验器、规则层与客户端 snapshot 共享，
// 从而让道路形状成为数据。
#pragma once

#include <vector>

#include "fc/level.h"
#include "fc/types.h"

namespace fc {

// 指向行驶左侧（左舷）/ 行驶右侧（右舷）的单位向量。
// d = (dr, dc) -> left = (-dc, dr), right = (dc, -dr)  （任务书 4.3.1）。
Vec2i dirLeft(Vec2i dir);
Vec2i dirRight(Vec2i dir);
Vec2i sideOf(LaneSide side, Vec2i dir);

struct LaneGeometry {
    int lane = 0;
    LaneSide side = LaneSide::Left;
    // 本车道的车辆在每个路径索引处占据的格子。
    std::vector<Vec2i> cells;
    // 每个路径索引处本车道可达的接触格，先排主方向，
    // 再排弯道补充方向（任务书 4.3.2）。
    std::vector<std::vector<Vec2i>> contacts;
    // 每个索引的首个接触格，或 {-1,-1}。
    std::vector<Vec2i> primary;
};

struct RoadMap {
    // 每个道路里程步对应的 lane-0 格子（与 level.road.path 相同）。
    std::vector<Vec2i> cells;
    // 每个路面格（双向车道加转角填充）。
    std::vector<Vec2i> track;
    // 每个道路格上背离水果一侧的单位垂向量。
    std::vector<Vec2i> away;
    // 按配置的 laneOffset 车辆实际占据的格子（历史遗留）。
    std::vector<Vec2i> laneCells;
    // 历史遗留：每个道路索引的单一接触格（无接触时为 {-1,-1}）。
    std::vector<Vec2i> contactCells;
    // phase 2：每条车道一个条目（phase-1 关卡只有单个条目）。
    std::vector<LaneGeometry> lanes;
    bool valid = false;

    bool onTrack(Vec2i cell) const;
    bool inAnyContact(Vec2i cell) const;
};

RoadMap buildRoadMap(const LevelDefinition& level);

// 给定水果网格格接触任一 lane-0 道路格时为 true。
bool isRoadsideCell(const LevelDefinition& level, Vec2i cell);

// 每个水果网格格的标志位（rows*cols）：该格接触道路，或是任一车道的接触格时
// 为 1（phase-2 的宽道路可向外触及两格）。
std::vector<unsigned char> roadsideFlags(const LevelDefinition& level);

// 某一侧的方向单位向量，位于 (row, col) 空间。
Vec2i sideVector(Side side);

}  // namespace fc
