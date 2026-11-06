#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

#include "fc/geom.h"
#include "fc/level.h"
#include "fc/refill.h"
#include "fc/road.h"
#include "fc/world.h"

namespace fc {
namespace {

constexpr int kMaxGridSide = 64;
constexpr int kRoadMargin = 2;
constexpr double kCurveBandEps = 1e-6;

LevelError makeError(const std::string& code, const std::string& path, const std::string& message) {
    LevelError error;
    error.code = code;
    error.path = path;
    error.message = message;
    return error;
}

bool contains(const std::vector<std::string>& list, const std::string& value) {
    for (const auto& item : list) {
        if (item == value) {
            return true;
        }
    }
    return false;
}

bool insideExtended(const LevelDefinition& level, Vec2i cell) {
    return cell.r >= -kRoadMargin && cell.r <= level.rows + kRoadMargin && cell.c >= -kRoadMargin &&
           cell.c <= level.cols + kRoadMargin;
}

bool insideGrid(const LevelDefinition& level, Vec2i cell) {
    return cell.r >= 0 && cell.r < level.rows && cell.c >= 0 && cell.c < level.cols;
}

bool cellInList(const std::vector<Vec2i>& list, Vec2i cell) {
    for (const Vec2i& other : list) {
        if (other == cell) {
            return true;
        }
    }
    return false;
}

// 关卡的静态水果布局（在任何收集发生之前）。
std::vector<unsigned char> fruitMask(const LevelDefinition& level) {
    std::vector<unsigned char> mask(static_cast<std::size_t>(level.rows) * static_cast<std::size_t>(level.cols),
                                    0);
    if (!level.fruits.layers.empty()) {
        for (const auto& layer : level.fruits.layers) {
            for (const Vec2i& cell : layer.cells) {
                if (insideGrid(level, cell)) {
                    mask[static_cast<std::size_t>(cell.r * level.cols + cell.c)] = 1;
                }
            }
        }
        return mask;
    }
    if (level.fruits.explicitLayout) {
        for (std::size_t i = 0; i < level.fruits.explicitCells.size(); ++i) {
            if (!level.fruits.explicitCells[i].empty()) {
                mask[i] = 1;
            }
        }
        return mask;
    }
    for (int r = 0; r < level.rows; ++r) {
        for (int c = 0; c < level.cols; ++c) {
            mask[static_cast<std::size_t>(r * level.cols + c)] = 1;
        }
    }
    return mask;
}

// E1015：每个水果单元格都必须可直接收集，或通过接触单元格的
// refill 拉取链收集（任务书 4.5）。
std::vector<unsigned char> reachableMask(const LevelDefinition& level, const RoadMap& map) {
    const std::size_t total = static_cast<std::size_t>(level.rows) * static_cast<std::size_t>(level.cols);
    std::vector<unsigned char> reachable(total, 0);
    std::vector<Vec2i> contacts;
    for (const LaneGeometry& lane : map.lanes) {
        for (const auto& list : lane.contacts) {
            for (const Vec2i& contact : list) {
                if (insideGrid(level, contact) && !cellInList(contacts, contact)) {
                    contacts.push_back(contact);
                }
            }
        }
    }
    auto mark = [&](Vec2i cell) {
        if (insideGrid(level, cell)) {
            reachable[static_cast<std::size_t>(cell.r * level.cols + cell.c)] = 1;
        }
    };
    if (level.fruits.refill == RefillDirection::LayerIn) {
        const std::vector<Vec2i> parents = layerParents(level);
        auto isContact = [&contacts](Vec2i cell) {
            for (const Vec2i& contact : contacts) {
                if (contact == cell) {
                    return true;
                }
            }
            return false;
        };
        for (int r = 0; r < level.rows; ++r) {
            for (int c = 0; c < level.cols; ++c) {
                Vec2i cursor{r, c};
                for (std::size_t guard = 0; guard <= level.fruits.layers.size() + 1; ++guard) {
                    if (!insideGrid(level, cursor)) {
                        break;
                    }
                    if (isContact(cursor)) {
                        mark(Vec2i{r, c});
                        break;
                    }
                    const Vec2i parent = parents[static_cast<std::size_t>(cursor.r * level.cols + cursor.c)];
                    if (parent.r < 0 || parent.c < 0) {
                        break;
                    }
                    cursor = parent;
                }
            }
        }
        return reachable;
    }

    for (const Vec2i& contact : contacts) {
        if (level.fruits.refill == RefillDirection::RegionIn) {
            // 接触单元格所在的整个非柏油连通区域均可达。
            std::vector<Vec2i> queue;
            queue.push_back(contact);
            mark(contact);
            const Vec2i offsets[4] = {Vec2i{-1, 0}, Vec2i{0, 1}, Vec2i{1, 0}, Vec2i{0, -1}};
            for (std::size_t head = 0; head < queue.size(); ++head) {
                const Vec2i cell = queue[head];
                for (const Vec2i& offset : offsets) {
                    const Vec2i next{cell.r + offset.r, cell.c + offset.c};
                    if (!insideGrid(level, next) || map.onTrack(next)) {
                        continue;
                    }
                    if (reachable[static_cast<std::size_t>(next.r * level.cols + next.c)] != 0) {
                        continue;
                    }
                    mark(next);
                    queue.push_back(next);
                }
            }
        } else {
            for (const Vec2i& cell : refillLine(level, contact)) {
                mark(cell);
            }
        }
    }
    return reachable;
}

int totalFruitCells(const LevelDefinition& level) {
    if (!level.fruits.layers.empty()) {
        return level.initialFruitCount();
    }
    return totalFruitSlots(level);
}

double orient(const Vec2d& a, const Vec2d& b, const Vec2d& c) {
    return cross(b - a, c - a);
}

bool onSegment(const Vec2d& a, const Vec2d& b, const Vec2d& p) {
    return std::fabs(orient(a, b, p)) <= 1e-9 &&
           p.x >= std::min(a.x, b.x) - 1e-9 && p.x <= std::max(a.x, b.x) + 1e-9 &&
           p.y >= std::min(a.y, b.y) - 1e-9 && p.y <= std::max(a.y, b.y) + 1e-9;
}

// 两条线段的真相交或共线重叠。相邻线段的共享端点
// 不视为相交。
bool segmentsIntersect(const Vec2d& a, const Vec2d& b, const Vec2d& c, const Vec2d& d) {
    const double o1 = orient(a, b, c);
    const double o2 = orient(a, b, d);
    const double o3 = orient(c, d, a);
    const double o4 = orient(c, d, b);
    if (((o1 > 1e-9 && o2 < -1e-9) || (o1 < -1e-9 && o2 > 1e-9)) &&
        ((o3 > 1e-9 && o4 < -1e-9) || (o3 < -1e-9 && o4 > 1e-9))) {
        return true;
    }
    if (onSegment(a, b, c) || onSegment(a, b, d) || onSegment(c, d, a) || onSegment(c, d, b)) {
        return true;
    }
    return false;
}

// E2013：道路中心线不得自相交（任务书 5.5）。
bool polylineSelfIntersects(const Polyline& line) {
    const std::size_t count = line.points.size();
    if (count < 4) {
        return false;
    }
    for (std::size_t i = 0; i + 1 < count; ++i) {
        for (std::size_t j = i + 2; j + 1 < count; ++j) {
            if (line.closed && i == 0 && j + 1 == count - 1) {
                continue;  // 闭合接缝不算交叉
            }
            if (segmentsIntersect(line.points[i], line.points[i + 1], line.points[j], line.points[j + 1])) {
                return true;
            }
        }
    }
    return false;
}

// 曲线关卡校验（任务书 5.3/5.4/5.5，附录 C）。
bool validateCurveLevel(const LevelDefinition& level, LevelError& error) {
    if (level.rows <= 0 || level.cols <= 0 || level.rows > kMaxGridSide || level.cols > kMaxGridSide) {
        error = makeError("E1001", "grid", "rows/cols must be in [1, 64]");
        return false;
    }
    if (level.palette.empty()) {
        error = makeError("E1002", "palette", "palette must contain at least one color id");
        return false;
    }
    if (level.parking.slotCount <= 0) {
        error = makeError("E1007", "parking.slotCount", "slotCount must be >= 1");
        return false;
    }
    for (std::size_t i = 0; i < level.parking.lanes.size(); ++i) {
        if (!contains(level.palette, level.parking.lanes[i].color)) {
            error = makeError("E1008", "parking.lanes[" + std::to_string(i) + "].color",
                              "lane color '" + level.parking.lanes[i].color + "' is not in palette");
            return false;
        }
    }
    for (std::size_t i = 0; i < level.vehicles.size(); ++i) {
        if (level.vehicles[i].second.capacity <= 0) {
            error = makeError("E1009", "vehicles." + level.vehicles[i].first + ".capacity",
                              "capacity must be >= 1");
            return false;
        }
    }
    std::vector<std::string> queued;
    for (std::size_t lane = 0; lane < level.parking.lanes.size(); ++lane) {
        for (std::size_t q = 0; q < level.parking.lanes[lane].queue.size(); ++q) {
            const std::string& id = level.parking.lanes[lane].queue[q];
            const std::string path =
                "parking.lanes[" + std::to_string(lane) + "].queue[" + std::to_string(q) + "]";
            if (level.findVehicle(id) == nullptr) {
                error = makeError("E1014", path, "unknown vehicle id '" + id + "'");
                return false;
            }
            if (std::find(queued.begin(), queued.end(), id) != queued.end()) {
                error = makeError("E1003", path, "duplicate vehicle id '" + id + "'");
                return false;
            }
            queued.push_back(id);
        }
    }

    // ---- 道路几何 -----------------------------------------------------
    if (level.road.controlPoints.size() < 2) {
        error = makeError("E2001", "road.controlPoints", "road needs at least two control points");
        return false;
    }
    if (!(level.road.sampleStep > 0.0) || level.road.sampleStep > 1.0) {
        error = makeError("E2002", "road.sampleStep", "sampleStep must be in (0, 1]");
        return false;
    }
    if (level.road.width < 1 || level.road.width > 2) {
        error = makeError("E1017", "road.width", "curve roads must have width 1 or 2");
        return false;
    }
    if (level.road.collectSides.size() != static_cast<std::size_t>(level.road.width)) {
        error = makeError("E1017", "road.collectSides",
                          "collectSides must have one entry per lane (width=" +
                              std::to_string(level.road.width) + ")");
        return false;
    }
    if (!(level.road.carLength > 0.0) || !(level.road.carGap >= 0.0) || !(level.road.speed > 0.0)) {
        error = makeError("E1012", "road", "carLength/speed must be > 0 and carGap >= 0");
        return false;
    }
    if (level.timing.spawnIntervalMs <= 0 || level.timing.settleTicks < 0) {
        error = makeError("E1012", "timing", "spawnIntervalMs must be > 0 and settleTicks >= 0");
        return false;
    }
    std::string geomError;
    const Polyline road = Polyline::fromControlPoints(level.road.controlPoints, level.road.closed,
                                                      level.road.sampleStep, geomError);
    if (!geomError.empty()) {
        error = makeError(geomError, "road.controlPoints", "cannot sample the road curve");
        return false;
    }
    if (polylineSelfIntersects(road)) {
        error = makeError("E2013", "road.controlPoints", "the road polyline intersects itself");
        return false;
    }

    // ---- 层链声明（E2004/E2005） -----------------------------
    for (std::size_t k = 0; k < level.fruitLayers.size(); ++k) {
        const CurveLayerDef& layer = level.fruitLayers[k];
        const std::string path = "fruitLayers[" + std::to_string(k) + "]";
        if (layer.controlPoints.size() < 2) {
            error = makeError("E2001", path + ".controlPoints", "layer needs two control points");
            return false;
        }
        if (!(layer.sampleStep > 0.0) || layer.sampleStep > 1.0) {
            error = makeError("E2002", path + ".sampleStep", "sampleStep must be in (0, 1]");
            return false;
        }
        if (!(layer.spacing > 0.0)) {
            error = makeError("E2003", path + ".spacing", "spacing must be > 0");
            return false;
        }
        if (layer.colors.empty()) {
            error = makeError("E1002", path + ".colors", "layer needs at least one color");
            return false;
        }
        for (std::size_t c = 0; c < layer.colors.size(); ++c) {
            if (!contains(level.palette, layer.colors[c])) {
                error = makeError("E1002", path + ".colors[" + std::to_string(c) + "]",
                                  "color '" + layer.colors[c] + "' is not in palette");
                return false;
            }
        }
        if (!layer.feedsFrom.empty()) {
            if (level.findFruitLayer(layer.feedsFrom) == nullptr) {
                error = makeError("E2004", path + ".refill.feedsFrom",
                                  "unknown layer '" + layer.feedsFrom + "'");
                return false;
            }
            // 遍历该链：环永远不会到达根层（E2005）。
            std::vector<std::string> visited;
            visited.push_back(layer.id);
            std::string cursor = layer.feedsFrom;
            for (std::size_t guard = 0; guard <= level.fruitLayers.size(); ++guard) {
                if (std::find(visited.begin(), visited.end(), cursor) != visited.end()) {
                    error = makeError("E2005", path + ".refill.feedsFrom", "layer refill chain has a cycle");
                    return false;
                }
                visited.push_back(cursor);
                const CurveLayerDef* parent = level.findFruitLayer(cursor);
                if (parent == nullptr) {
                    break;
                }
                cursor = parent->feedsFrom;
                if (cursor.empty()) {
                    break;
                }
            }
        }
    }

    // ---- 层采样与可达性（E2010/E2020） ---------------------
    // 若没有其他层把某层声明为自己的 refill 来源，则该层就是“可收集的”：
    // 它的水果正是汽车能直接触碰到的那些，因此必须
    // 位于接触带内。外层则通过该链到达。
    std::vector<bool> isFeedSource(level.fruitLayers.size(), false);
    for (const auto& layer : level.fruitLayers) {
        if (layer.feedsFrom.empty()) {
            continue;
        }
        for (std::size_t p = 0; p < level.fruitLayers.size(); ++p) {
            if (level.fruitLayers[p].id == layer.feedsFrom) {
                isFeedSource[p] = true;
                break;
            }
        }
    }
    int fruitTotal = 0;
    for (std::size_t k = 0; k < level.fruitLayers.size(); ++k) {
        const CurveLayerDef& layer = level.fruitLayers[k];
        const std::string path = "fruitLayers[" + std::to_string(k) + "]";
        const Polyline poly = Polyline::fromControlPoints(layer.controlPoints, layer.closed,
                                                          layer.sampleStep, geomError);
        if (!geomError.empty()) {
            error = makeError(geomError, path + ".controlPoints", "cannot sample the fruit layer");
            return false;
        }
        const std::vector<double> positions = fruitLayerArcPositions(poly, layer.spacing);
        if (positions.empty()) {
            error = makeError("E2010", path + ".spacing", "layer produces no sample");
            return false;
        }
        fruitTotal += static_cast<int>(positions.size());
        if (isFeedSource[k]) {
            continue;  // 只有可直接收集的层必须位于接触带内
        }
        const double band = level.road.contactReach + 0.5 * static_cast<double>(level.laneCount());
        const LaneSide side = level.road.collectSides.empty() ? LaneSide::Left
                                                             : level.road.collectSides.front();
        for (std::size_t i = 0; i < positions.size(); ++i) {
            const Vec2d point = poly.positionAt(positions[i]);
            const Projection projection = road.project(point);
            const bool onSide = side == LaneSide::Left ? projection.d >= -1e-9
                                                       : projection.d <= 1e-9;
            if (!onSide || std::fabs(projection.d) > band + kCurveBandEps) {
                error = makeError("E2020", path + ".controlPoints",
                                  "fruit at [" + formatFixed3(point.x) + "," + formatFixed3(point.y) +
                                      "] is outside the contact band (d=" +
                                      formatFixed3(projection.d) + ")");
                return false;
            }
        }
    }

    // ---- 构建真实状态：证明层链映射为双射 -----
    GameState probe;
    std::string buildError;
    if (!buildCurveState(level, probe, buildError)) {
        std::string code = buildError;
        std::string detail;
        const std::size_t bar = buildError.find('|');
        if (bar != std::string::npos) {
            code = buildError.substr(0, bar);
            detail = buildError.substr(bar + 1);
        }
        const std::string path = code == "E2004" ? std::string("fruitLayers.feedsFrom")
                                                 : std::string("fruitLayers");
        error = makeError(code.empty() ? "E2012" : code, path,
                          "cannot build the curve layer chain" +
                              (detail.empty() ? std::string() : (" (" + detail + ")")));
        return false;
    }
    fruitTotal = static_cast<int>(probe.fruitSlots.size());

    // ---- 预留技能（已解析，此处再做防御性检查） ----------
    for (std::size_t i = 0; i < level.items.size(); ++i) {
        const ItemDef& item = level.items[i];
        const std::string path = "items[" + std::to_string(i) + "]";
        EffectKind kind = EffectKind::RecolorCar;
        if (!parseEffectKind(item.kindText, kind)) {
            error = makeError("E2030", path + ".kind", "unknown item kind '" + item.kindText + "'");
            return false;
        }
        if (item.charges < 0) {
            error = makeError("E2031", path + ".charges", "charges must be >= 0");
            return false;
        }
    }

    // ---- 可解性 -------------------------------------------------------
    if (totalVehicleCapacity(level) < fruitTotal) {
        error = makeError("E1013", "vehicles",
                          "total vehicle capacity (" + std::to_string(totalVehicleCapacity(level)) +
                              ") is smaller than the fruit count (" + std::to_string(fruitTotal) + ")");
        return false;
    }

    error = LevelError();
    return true;
}

}  // namespace

int totalFruitSlots(const LevelDefinition& level) {
    if (level.fruits.explicitLayout) {
        int count = 0;
        for (const auto& cell : level.fruits.explicitCells) {
            if (!cell.empty()) {
                ++count;
            }
        }
        return count;
    }
    return level.rows * level.cols;
}

int totalVehicleCapacity(const LevelDefinition& level) {
    int total = 0;
    for (const auto& lane : level.parking.lanes) {
        for (const auto& id : lane.queue) {
            const VehicleDef* def = level.findVehicle(id);
            if (def != nullptr) {
                total += def->capacity;
            }
        }
    }
    return total;
}

bool validateLevel(const LevelDefinition& level, LevelError& error) {
    // 曲线关卡使用完全不同的几何；网格关卡沿用下方
    // 已冻结的 phase-1/2 校验路径（任务书 9 T-40）。
    if (level.usesCurves()) {
        return validateCurveLevel(level, error);
    }

    // E1001 网格边界。
    if (level.rows <= 0 || level.cols <= 0 || level.rows > kMaxGridSide || level.cols > kMaxGridSide) {
        error = makeError("E1001", "grid", "rows/cols must be in [1, 64]");
        return false;
    }

    // E1010 连续段长度范围。
    if (level.fruits.runLow < 1 || level.fruits.runLow > level.fruits.runHigh) {
        error = makeError("E1010", "fruits.runLength", "runLength must satisfy 1 <= low <= high");
        return false;
    }

    // E1002 水果颜色必须属于调色板。
    for (std::size_t i = 0; i < level.fruits.colors.size(); ++i) {
        if (!contains(level.palette, level.fruits.colors[i])) {
            error = makeError("E1002", "fruits.colors[" + std::to_string(i) + "]",
                              "fruit color '" + level.fruits.colors[i] + "' is not in palette");
            return false;
        }
    }
    for (std::size_t k = 0; k < level.fruits.layers.size(); ++k) {
        const LayerDef& layer = level.fruits.layers[k];
        if (!contains(level.palette, layer.color)) {
            error = makeError("E1002", "fruits.layers[" + std::to_string(k) + "].color",
                              "layer color '" + layer.color + "' is not in palette");
            return false;
        }
    }
    if (level.fruits.explicitLayout) {
        const std::size_t expected = static_cast<std::size_t>(level.rows) * static_cast<std::size_t>(level.cols);
        if (level.fruits.explicitCells.size() != expected) {
            error = makeError("E1012", "fruits.cells", "explicit layout size does not match grid");
            return false;
        }
        for (std::size_t i = 0; i < level.fruits.explicitCells.size(); ++i) {
            const std::string& color = level.fruits.explicitCells[i];
            if (!color.empty() && !contains(level.palette, color)) {
                const int r = static_cast<int>(i) / level.cols;
                const int c = static_cast<int>(i) % level.cols;
                error = makeError("E1002", "fruits.cells[" + std::to_string(r) + "][" + std::to_string(c) + "]",
                                  "fruit color '" + color + "' is not in palette");
                return false;
            }
        }
    } else if (level.fruits.colors.empty() && level.fruits.layers.empty()) {
        error = makeError("E1002", "fruits.colors", "at least one fruit color is required");
        return false;
    }

    // E1007 停车位。
    if (level.parking.slotCount <= 0) {
        error = makeError("E1007", "parking.slotCount", "slotCount must be >= 1");
        return false;
    }

    // E1008 车道颜色。
    for (std::size_t i = 0; i < level.parking.lanes.size(); ++i) {
        if (!contains(level.palette, level.parking.lanes[i].color)) {
            error = makeError("E1008", "parking.lanes[" + std::to_string(i) + "].color",
                              "lane color '" + level.parking.lanes[i].color + "' is not in palette");
            return false;
        }
    }

    // E1009 容量与 E1014 队列引用。
    for (std::size_t i = 0; i < level.vehicles.size(); ++i) {
        if (level.vehicles[i].second.capacity <= 0) {
            error = makeError("E1009", "vehicles." + level.vehicles[i].first + ".capacity",
                              "capacity must be >= 1");
            return false;
        }
    }
    std::vector<std::string> queued;
    for (std::size_t lane = 0; lane < level.parking.lanes.size(); ++lane) {
        for (std::size_t q = 0; q < level.parking.lanes[lane].queue.size(); ++q) {
            const std::string& id = level.parking.lanes[lane].queue[q];
            if (level.findVehicle(id) == nullptr) {
                error = makeError("E1014",
                                  "parking.lanes[" + std::to_string(lane) + "].queue[" + std::to_string(q) + "]",
                                  "unknown vehicle id '" + id + "'");
                return false;
            }
            if (std::find(queued.begin(), queued.end(), id) != queued.end()) {
                error = makeError("E1003",
                                  "parking.lanes[" + std::to_string(lane) + "].queue[" + std::to_string(q) + "]",
                                  "duplicate vehicle id '" + id + "'");
                return false;
            }
            queued.push_back(id);
        }
    }

    // 道路几何。
    if (level.road.path.empty()) {
        error = makeError("E1012", "road.path", "road.path must contain at least one cell");
        return false;
    }
    if (level.road.path.size() < 2) {
        error = makeError("E1012", "road.path", "road.path must contain at least two cells");
        return false;
    }
    if (level.road.width < 1 || level.road.width > 3) {
        error = makeError("E1012", "road.width", "road.width must be in [1, 3]");
        return false;
    }
    if (level.road.laneOffset < 0 || level.road.laneOffset >= level.road.width) {
        error = makeError("E1012", "road.laneOffset", "laneOffset must be in [0, width-1]");
        return false;
    }
    if (!(level.road.carLength > 0.0) || !(level.road.carGap >= 0.0) || !(level.road.speed > 0.0)) {
        error = makeError("E1012", "road", "carLength/speed must be > 0 and carGap >= 0");
        return false;
    }
    if (level.timing.spawnIntervalMs <= 0 || level.timing.settleTicks < 0) {
        error = makeError("E1012", "timing", "spawnIntervalMs must be > 0 and settleTicks >= 0");
        return false;
    }

    for (std::size_t i = 0; i < level.road.path.size(); ++i) {
        const Vec2i& cell = level.road.path[i];
        if (!insideExtended(level, cell)) {
            error = makeError("E1005", "road.path[" + std::to_string(i) + "]", "road cell outside the level bounds");
            return false;
        }
        if (i > 0) {
            const int step = manhattan(level.road.path[i - 1], cell);
            if (step != 1) {
                error = makeError("E1004", "road.path[" + std::to_string(i) + "]",
                                  "road path cells must be 4-adjacent");
                return false;
            }
        }
    }

    // E1017 车道声明（phase 2）。
    if (level.road.isV2()) {
        if (level.road.width < 1 || level.road.width > 2) {
            error = makeError("E1017", "road.width", "phase-2 roads must have width 1 or 2");
            return false;
        }
        if (level.road.collectSides.size() != static_cast<std::size_t>(level.road.width)) {
            error = makeError("E1017", "road.collectSides",
                              "collectSides must have one entry per lane (width=" +
                                  std::to_string(level.road.width) + ")");
            return false;
        }
        if (level.road.laneCycle.empty()) {
            error = makeError("E1017", "road.laneCycle", "laneCycle must contain at least one lane index");
            return false;
        }
        for (std::size_t i = 0; i < level.road.laneCycle.size(); ++i) {
            const int lane = level.road.laneCycle[i];
            if (lane < 0 || lane >= level.road.width) {
                error = makeError("E1017", "road.laneCycle[" + std::to_string(i) + "]",
                                  "laneCycle index out of range");
                return false;
            }
        }
    }

    // E1016 柏油路面一致性（phase 2）。
    if (level.road.isV2()) {
        const RoadMap map = buildRoadMap(level);
        for (std::size_t i = 0; i < level.road.path.size(); ++i) {
            if (!map.onTrack(level.road.path[i])) {
                error = makeError("E1016", "road.track[" + std::to_string(i) + "]",
                                  "road.track does not contain road.path");
                return false;
            }
        }
        for (std::size_t l = 0; l < map.lanes.size(); ++l) {
            for (std::size_t i = 0; i < map.lanes[l].cells.size(); ++i) {
                const Vec2i cell = map.lanes[l].cells[i];
                if (!insideExtended(level, cell) || !map.onTrack(cell)) {
                    error = makeError("E1016", "road.track", "road.track is missing lane " + std::to_string(l) +
                                                               " cell [" + std::to_string(cell.r) + "," +
                                                               std::to_string(cell.c) + "]");
                    return false;
                }
            }
        }
        // track 必须四连通。
        std::vector<unsigned char> seen(level.road.trackCells().size(), 0);
        std::vector<std::size_t> queue;
        queue.push_back(0);
        seen[0] = 1;
        for (std::size_t head = 0; head < queue.size(); ++head) {
            const Vec2i cell = level.road.trackCells()[queue[head]];
            for (std::size_t k = 0; k < level.road.trackCells().size(); ++k) {
                if (seen[k] != 0) {
                    continue;
                }
                if (manhattan(cell, level.road.trackCells()[k]) == 1) {
                    seen[k] = 1;
                    queue.push_back(k);
                }
            }
        }
        for (std::size_t k = 0; k < seen.size(); ++k) {
            if (seen[k] == 0) {
                const Vec2i cell = level.road.trackCells()[k];
                error = makeError("E1016", "road.track[" + std::to_string(k) + "]",
                                  "road.track is not 4-connected at [" + std::to_string(cell.r) + "," +
                                      std::to_string(cell.c) + "]");
                return false;
            }
        }
        // 拐角填充：与两条不同车道四邻接的单元格是弯道的内
        // 角，必须铺为柏油路面，否则接触带会出现缺口
        // （任务书 6.3，表 6-2）。
        if (map.lanes.size() >= 2) {
            for (int r = -kRoadMargin; r <= level.rows + kRoadMargin; ++r) {
                for (int c = -kRoadMargin; c <= level.cols + kRoadMargin; ++c) {
                    const Vec2i cell{r, c};
                    if (map.onTrack(cell)) {
                        continue;
                    }
                    Vec2i fromLane0{-1, -1};
                    Vec2i fromLane1{-1, -1};
                    for (const Vec2i& laneCell : map.lanes[0].cells) {
                        if (manhattan(laneCell, cell) == 1) {
                            fromLane0 = laneCell;
                            break;
                        }
                    }
                    for (const Vec2i& laneCell : map.lanes[1].cells) {
                        if (manhattan(laneCell, cell) == 1 && laneCell != fromLane0) {
                            fromLane1 = laneCell;
                            break;
                        }
                    }
                    if (fromLane0.r >= 0 && fromLane1.r >= 0 && fromLane0 != fromLane1) {
                        error = makeError("E1016", "road.track",
                                          "corner filler [" + std::to_string(cell.r) + "," +
                                              std::to_string(cell.c) + "] between lane 0 and lane 1 is missing");
                        return false;
                    }
                }
            }
        }
        // 柏油路面上不得有水果。
        const std::vector<unsigned char> mask = fruitMask(level);
        for (std::size_t k = 0; k < level.road.trackCells().size(); ++k) {
            const Vec2i cell = level.road.trackCells()[k];
            if (insideGrid(level, cell) && mask[static_cast<std::size_t>(cell.r * level.cols + cell.c)] != 0) {
                error = makeError("E1016", "road.track[" + std::to_string(k) + "]",
                                  "fruit cell [" + std::to_string(cell.r) + "," + std::to_string(cell.c) +
                                      "] lies on the road");
                return false;
            }
        }
        for (const Vec2i& island : level.presentation.island) {
            for (std::size_t k = 0; k < level.road.trackCells().size(); ++k) {
                if (level.road.trackCells()[k] == island) {
                    error = makeError("E1016", "presentation.island",
                                      "island cell overlaps the road at [" + std::to_string(island.r) + "," +
                                          std::to_string(island.c) + "]");
                    return false;
                }
            }
            if (insideGrid(level, island) && mask[static_cast<std::size_t>(island.r * level.cols + island.c)] != 0) {
                error = makeError("E1016", "presentation.island",
                                  "island cell overlaps a fruit at [" + std::to_string(island.r) + "," +
                                      std::to_string(island.c) + "]");
                return false;
            }
        }
    }

    // E1006 连通性：至少有一个水果单元格必须接触道路。
    bool connected = false;
    for (int r = 0; r < level.rows && !connected; ++r) {
        for (int c = 0; c < level.cols && !connected; ++c) {
            for (const Vec2i& roadCell : level.road.trackCells()) {
                if (manhattan(roadCell, Vec2i{r, c}) == 1) {
                    connected = true;
                    break;
                }
            }
        }
    }
    if (!connected) {
        error = makeError("E1006", "road.path", "road is not adjacent to any fruit cell");
        return false;
    }

    // E1018 层链（phase 2 的 `layers` 生成器）。
    if (!level.fruits.layers.empty()) {
        if (level.fruits.refill != RefillDirection::LayerIn) {
            error = makeError("E1018", "fruits.refillDirection",
                              "the layers generator requires refillDirection=layerIn");
            return false;
        }
        const std::vector<Vec2i> parents = layerParents(level);
        for (std::size_t k = 0; k < level.fruits.layers.size(); ++k) {
            const LayerDef& layer = level.fruits.layers[k];
            if (layer.cells.empty()) {
                error = makeError("E1018", "fruits.layers[" + std::to_string(k) + "].cells", "layer is empty");
                return false;
            }
            for (std::size_t i = 0; i < layer.cells.size(); ++i) {
                const Vec2i cell = layer.cells[i];
                if (!insideGrid(level, cell)) {
                    error = makeError("E1018",
                                      "fruits.layers[" + std::to_string(k) + "].cells[" + std::to_string(i) + "]",
                                      "layer cell outside the grid");
                    return false;
                }
                for (std::size_t inner = 0; inner < k; ++inner) {
                    if (cellInList(level.fruits.layers[inner].cells, cell)) {
                        error = makeError("E1018",
                                          "fruits.layers[" + std::to_string(k) + "].cells[" + std::to_string(i) + "]",
                                          "cell also belongs to layer " + std::to_string(inner));
                        return false;
                    }
                }
                if (k > 0) {
                    Vec2i cursor = cell;
                    bool reachedLayer0 = false;
                    for (std::size_t guard = 0; guard <= level.fruits.layers.size() + 1; ++guard) {
                        const Vec2i parent = parents[static_cast<std::size_t>(cursor.r * level.cols + cursor.c)];
                        if (parent.r < 0 || parent.c < 0) {
                            break;
                        }
                        if (level.layerOf(parent) == 0) {
                            reachedLayer0 = true;
                            break;
                        }
                        cursor = parent;
                    }
                    if (!reachedLayer0) {
                        error = makeError(
                            "E1018", "fruits.layers[" + std::to_string(k) + "].cells[" + std::to_string(i) + "]",
                            "layer parent chain does not reach layer 0");
                        return false;
                    }
                }
            }
        }
        // 此处不重新检查“第 0 层必须仅由接触单元格组成”：
        // 没有接触的第 0 层单元格恰好就是不可达水果，因此
        // E1015 会连同水果坐标一起报告（任务书 4.5）。
    }

    // E1015 可达性（仅 phase 2 关卡：phase-1 关卡早于该审计，
    // 并由其回归哈希锁定，见 decisions.md D-P2-17）。
    if (level.schemaVersion >= 2) {
        const RoadMap map = buildRoadMap(level);
        const std::vector<unsigned char> mask = fruitMask(level);
        const std::vector<unsigned char> reachable = reachableMask(level, map);
        for (std::size_t i = 0; i < mask.size(); ++i) {
            if (mask[i] != 0 && reachable[i] == 0) {
                const int r = static_cast<int>(i) / level.cols;
                const int c = static_cast<int>(i) % level.cols;
                error = makeError("E1015", "fruits.cells[" + std::to_string(r) + "][" + std::to_string(c) + "]",
                                  "fruit cell is unreachable: not a contact and not on any refill chain");
                return false;
            }
        }
    }

    // E1013 可解性：总容量必须覆盖所有水果。
    if (totalVehicleCapacity(level) < totalFruitCells(level)) {
        error = makeError("E1013", "vehicles",
                          "total vehicle capacity (" + std::to_string(totalVehicleCapacity(level)) +
                              ") is smaller than the fruit count (" +
                              std::to_string(totalFruitCells(level)) + ")");
        return false;
    }

    error = LevelError();
    return true;
}

}  // namespace fc
