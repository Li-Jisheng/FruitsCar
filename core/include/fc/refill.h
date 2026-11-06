// 补充策略：基于直线（下/上/左/右）、基于环（ringIn/ringOut）、层树（layerIn）
// 与区域 BFS（regionIn）。core 只使用此处产出的有序格子序列，因此新增策略
// 可以保持数据驱动。
#pragma once

#include <functional>
#include <vector>

#include "fc/level.h"
#include "fc/types.h"

namespace fc {

// 判断某个格子当前是否持有水果的谓词。
using Occupancy = std::function<bool(Vec2i)>;

// 为被采集的格子补充水果时使用的有序格子序列。
// index 0 是被采集的格子，索引越大表示沿补充源方向离道路越远。
std::vector<Vec2i> refillLine(const LevelDefinition& level, Vec2i target);

// 所配置策略的完整补充链：被采集的格子在最前，最远的贡献水果格在最后。
// 当该策略无事可做时（拉动树 / 区域内没有水果）为空。
std::vector<Vec2i> refillPath(const LevelDefinition& level, Vec2i target, const Occupancy& present);

// layerIn 层级：每个网格格的父格（rows*cols，当该格属于第 0 层或不属于任何层
// 时为 {-1,-1}）。父指针始终朝第 0 层移动（任务书 4.4.1）。
std::vector<Vec2i> layerParents(const LevelDefinition& level);

// 每个网格格的子格，按补充顺序排列（角度、行、列升序）。
std::vector<std::vector<int>> layerChildren(const LevelDefinition& level, int rows, int cols);

// 所有网格格先按环（内环优先或外环优先）、再按角度排序。
// 该映射是一个全序，因此也是网格上的双射。
std::vector<Vec2i> ringOrderCells(int rows, int cols, bool innerFirst);

// 某个格在 ringOrderCells(rows, cols, true) 中的索引。
int ringIndexOf(int rows, int cols, Vec2i cell);

// 穿过 `target` 的径向辐条：index 0 是目标格，其后条目按每环一格向外
// （ringIn）或向内（ringOut）排列。
std::vector<Vec2i> ringSpoke(const LevelDefinition& level, Vec2i target, bool outward);

}  // namespace fc
