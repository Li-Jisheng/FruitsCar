// 曲线几何内核（任务书 6 T-02、附录 A）：向心 Catmull-Rom 控制点 ->
// 确定性稠密折线 -> 弧长、位置 / 切线查询与有符号投影。纯数学计算，无 IO。
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "fc/types.h"

namespace fc {

// 重采样后顶点数的上限（任务书附录 A.6 / E2021）。
constexpr int kMaxSamples = 4096;
// 允许的最大采样步长，单位为格（任务书 4.1 / A-12）。
constexpr double kMaxSampleStep = 0.25;

// 将点投影到折线上的结果（任务书 5.1 的 `Projection`）。
struct Projection {
    double s = 0.0;         // 最近点的弧长
    double d = 0.0;         // 有符号横向距离：> 0 = 驾驶员左侧
    double distance = 0.0;  // 到折线的绝对距离
    int segment = -1;       // 最近段的索引（-1 = 空折线）
};

// 稠密、按弧长参数化的折线。闭合折线会在末尾重复其首个顶点，因此
// points.back() == points.front() 严格成立，且 length() == arc.back()。
struct Polyline {
    std::vector<Vec2d> points;
    std::vector<double> arc;  // arc[i] = points[i] 处的累计长度，arc[0] = 0
    bool closed = false;
    bool valid = false;

    double length() const { return arc.empty() ? 0.0 : arc.back(); }
    std::size_t sampleCount() const { return points.size(); }

    Vec2d positionAt(double s) const;
    Vec2d tangentAt(double s) const;
    Projection project(const Vec2d& p) const;

    // Catmull-Rom（向心）插值 + 定步长重采样。
    // 失败时 `err` 会收到 "E2001"/"E2002"/"E2021"。
    static Polyline fromControlPoints(const std::vector<Vec2d>& controlPoints, bool closed, double step,
                                      std::string& err);
    // 由已存在的顶点列表（已完成重采样）构建弧长表。
    static Polyline fromPoints(std::vector<Vec2d> points, bool closed);
};

// 将弧长坐标环绕 / 截断到折线域内。
double normalizeArc(double s, double length, bool closed);

// 每隔 `spacing` 格取一个弧长位置：0、spacing、2*spacing ... < length。
// 只输出完整区间，因此相邻位置之差始终恰好为 `spacing`（任务书 A-03）。
std::vector<double> sampleArcPositions(const Polyline& line, double spacing);

// 某一层的水果槽位位置。开放层按 spacing/2 + k*spacing 采样，这样没有槽位
// 恰好落在道路起点上（车辆在 s = 0 处入场，并在首次接触检测之前就已前进，
// 这会使 s = 0 处的槽位无法被触达）。闭合层保持 phase-0 的采样方式。
std::vector<double> fruitLayerArcPositions(const Polyline& line, double spacing);

// "[[x,y],[x,y],...]"，保留四位小数（snapshot / 确定性证据）。
std::string polylineJson(const Polyline& line);

}  // namespace fc
