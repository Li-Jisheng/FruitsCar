// 关卡定义（仅数据）与 JSON 加载器入口。
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "fc/effects.h"
#include "fc/types.h"

namespace fc {

enum class RefillDirection { Down, Up, Left, Right, RingIn, RingOut, LayerIn, RegionIn };
enum class Side { Up, Down, Left, Right };
// 车道的采集侧，相对于行驶方向（phase 2）。
enum class LaneSide { Left, Right, None };
// 关卡机制：网格（v1/v2 网格关卡）或自由曲线（v3）。
enum class Mechanism { Grid, Curves };

const char* toString(RefillDirection dir);
const char* toString(Side side);
const char* toString(LaneSide side);
const char* toString(Mechanism mechanism);
bool parseRefillDirection(const std::string& text, RefillDirection& out);
bool parseSide(const std::string& text, Side& out);
bool parseLaneSide(const std::string& text, LaneSide& out);
bool parseMechanism(const std::string& text, Mechanism& out);

struct RoadDef {
    std::vector<Vec2i> path;
    // 被路面覆盖的每个格子（双向车道 + 转角填充）。为空表示
    // "与 path 相同"（phase 1 默认）。
    std::vector<Vec2i> track;
    int width = 1;
    int laneOffset = 0;              // phase 1 单车道偏移
    Side collectSide = Side::Up;     // phase 1 绝对采集侧
    std::vector<LaneSide> collectSides;  // phase 2：每条车道一个条目
    std::vector<int> laneCycle;      // phase 2：第 n 次刷车使用的车道
    bool bendCollectsBoth = true;
    double carLength = 1.0;
    double carGap = 0.5;
    double speed = 2.0;

    // ---- schemaVersion 2 `mechanism: "curves"` 道路（任务书 5.3） --------
    std::vector<Vec2d> controlPoints;  // [x=col, y=row]，单位为格
    bool closed = false;
    double sampleStep = 0.25;          // 重采样步长，单位为格（<= 1）
    double contactSlack = 0.5;         // 接触判定允许的纵向松弛
    double contactReach = 0.5;         // 超出车道中心线的半宽
    double offset = 0.0;               // 车道的横向偏移（格）

    bool isV2() const { return !collectSides.empty(); }
    bool usesControlPoints() const { return !controlPoints.empty(); }
    const std::vector<Vec2i>& trackCells() const { return track.empty() ? path : track; }
};

// 曲线关卡的一层水果：每隔 `spacing` 格采样一次的（闭合或开放）曲线，
// 拥有自己的显示尺寸、颜色循环和补充父层。
struct CurveLayerDef {
    std::string id;
    std::vector<Vec2d> controlPoints;
    bool closed = true;
    double spacing = 1.0;
    double size = 1.0;
    std::vector<ColorId> colors;  // 在各采样点之间循环使用
    std::string feedsFrom;        // 父层（内层）id，"" = 无
    double jitterOffset = 0.0;    // 确定性的横向抖动幅度
    double jitterSize = 0.0;      // 确定性的尺寸抖动幅度
    std::uint64_t jitterSeed = 0;
    double sampleStep = 0.25;
    ColorId color() const { return colors.empty() ? ColorId() : colors.front(); }
};

struct LaneDef {
    ColorId color;
    std::vector<EntityId> queue;
};

struct ParkingDef {
    int slotCount = 5;
    std::vector<LaneDef> lanes;
};

// `layers` 生成器中的一个同心水果环（phase 2）。
struct LayerDef {
    double size = 1.0;
    ColorId color;
    std::vector<Vec2i> cells;
};

struct FruitDef {
    std::string generator = "rowRuns";
    int runLow = 3;
    int runHigh = 5;
    std::vector<ColorId> colors;
    RefillDirection refill = RefillDirection::Down;
    bool explicitLayout = false;
    // explicitLayout 时大小为 rows*cols；空字符串表示"无水果"。
    std::vector<ColorId> explicitCells;
    // `layers` 生成器：由内向外排序。
    std::vector<LayerDef> layers;
};

struct TimingDef {
    int spawnIntervalMs = 500;
    int settleTicks = 30;
};

struct PresentationDef {
    std::vector<Vec2i> island;

    // "扇环"水果区域的装饰性半环（仅视图数据：
    // 采集与补充从不读取它）。`sectors == 0` 时禁用。
    struct Ring {
        Vec2d centre{0.0, 0.0};
        int sectors = 0;
        double startDeg = 0.0;
        double sweepDeg = 0.0;
        double innerRadius = 0.0;
        double outerRadius = 0.0;
        double gapDeg = 0.0;
        bool enabled() const {
            return sectors > 0 && sweepDeg > 0.0 && outerRadius > innerRadius;
        }
    } ring;
};

struct VehicleDef {
    ColorId color;
    int capacity = 4;
};

struct LevelDefinition {
    int schemaVersion = 1;
    std::string id = "L01_default";
    std::string name;
    std::uint64_t seed = 0;

    int rows = 5;
    int cols = 10;

    std::vector<ColorId> palette;
    FruitDef fruits;
    RoadDef road;
    ParkingDef parking;
    TimingDef timing;
    PresentationDef presentation;
    std::vector<std::pair<EntityId, VehicleDef>> vehicles;

    // ---- schemaVersion 2 曲线关卡（任务书 5.3） ----------------------
    Mechanism mechanism = Mechanism::Grid;
    std::vector<CurveLayerDef> fruitLayers;  // 内层（靠近道路）在前
    std::vector<ItemDef> items;              // 预留技能（任务书 5.8）

    // 派生辅助函数 -------------------------------------------------------
    int spawnTicks() const { return timing.spawnIntervalMs * kTickRate / 1000; }
    int fruitTotal() const { return rows * cols; }
    int laneCount() const { return road.width < 1 ? 1 : road.width; }
    const VehicleDef* findVehicle(const EntityId& vehicleId) const;
    bool isLaneRoad() const;  // 道路沿行方向（水平）延伸时为 true
    const std::vector<ColorId>& fruitColors() const;
    bool usesLayers() const { return !fruits.layers.empty(); }
    bool usesCurves() const { return mechanism == Mechanism::Curves; }
    const CurveLayerDef* findFruitLayer(const std::string& layerId) const;
    // 水果格所属的层索引（该格不属于任何层时为 -1）。
    int layerOf(Vec2i cell) const;
    // 初始布局中的水果格数量（layers/explicit/rows*cols）。
    int initialFruitCount() const;
};

struct LevelError {
    std::string code;   // E1001 .. E1019
    std::string path;   // 类似 JSON pointer 的位置，例如 "road.path[3]"
    int line = 0;
    int column = 0;
    std::string message;

    bool ok() const { return code.empty(); }
};

struct LevelParseResult {
    bool ok = false;
    LevelDefinition level;
    LevelError error;
};

// 解析并校验一份关卡文档。从不抛异常；错误通过返回值上报
// （core 没有异常边界）。
LevelParseResult parseLevel(const std::string& jsonText);

// 仅做校验；假定定义由解析器或测试构建。
bool validateLevel(const LevelDefinition& level, LevelError& error);

// 所有车道队列的水果容量总和（可解性检查 E1013）。
int totalVehicleCapacity(const LevelDefinition& level);
int totalFruitSlots(const LevelDefinition& level);

}  // namespace fc
