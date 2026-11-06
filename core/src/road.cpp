#include "fc/road.h"

#include <cstddef>

namespace fc {
namespace {

int dot(Vec2i a, Vec2i b) { return a.r * b.r + a.c * b.c; }

Vec2i perpendicularA(Vec2i dir) { return Vec2i{dir.c, -dir.r}; }
Vec2i perpendicularB(Vec2i dir) { return Vec2i{-dir.c, dir.r}; }

bool insideGrid(const LevelDefinition& level, Vec2i cell) {
    return cell.r >= 0 && cell.r < level.rows && cell.c >= 0 && cell.c < level.cols;
}

bool containsCell(const std::vector<Vec2i>& list, Vec2i cell) {
    for (const Vec2i& other : list) {
        if (other == cell) {
            return true;
        }
    }
    return false;
}

bool isRoadCell(const LevelDefinition& level, Vec2i cell) {
    const std::vector<Vec2i>& track = level.road.trackCells();
    for (const Vec2i& roadCell : track) {
        if (roadCell == cell) {
            return true;
        }
    }
    return false;
}

// 路径索引 `i` 处的行驶方向；首格借用其出向方向，
// 末格借用其入向方向（任务书 4.3.2）。
Vec2i pathDirAt(const std::vector<Vec2i>& path, std::size_t i, bool outgoing) {
    if (path.size() < 2) {
        return Vec2i{0, 1};
    }
    if (outgoing) {
        if (i + 1 < path.size()) {
            return path[i + 1] - path[i];
        }
        return path[i] - path[i - 1];
    }
    if (i > 0) {
        return path[i] - path[i - 1];
    }
    return path[i + 1] - path[i];
}

}  // namespace

Vec2i dirLeft(Vec2i dir) { return Vec2i{-dir.c, dir.r}; }
Vec2i dirRight(Vec2i dir) { return Vec2i{dir.c, -dir.r}; }

Vec2i sideOf(LaneSide side, Vec2i dir) {
    switch (side) {
        case LaneSide::Left: return dirLeft(dir);
        case LaneSide::Right: return dirRight(dir);
        case LaneSide::None: return Vec2i{0, 0};
    }
    return Vec2i{0, 0};
}

bool RoadMap::onTrack(Vec2i cell) const {
    return containsCell(track, cell);
}

bool RoadMap::inAnyContact(Vec2i cell) const {
    for (const LaneGeometry& lane : lanes) {
        for (const Vec2i& contact : lane.primary) {
            if (contact == cell) {
                return true;
            }
        }
        for (const auto& list : lane.contacts) {
            for (const Vec2i& contact : list) {
                if (contact == cell) {
                    return true;
                }
            }
        }
    }
    return false;
}

Vec2i sideVector(Side side) {
    switch (side) {
        case Side::Up: return Vec2i{-1, 0};
        case Side::Down: return Vec2i{1, 0};
        case Side::Left: return Vec2i{0, -1};
        case Side::Right: return Vec2i{0, 1};
    }
    return Vec2i{-1, 0};
}

RoadMap buildRoadMap(const LevelDefinition& level) {
    RoadMap map;
    const std::vector<Vec2i>& path = level.road.path;
    const std::size_t count = path.size();
    if (count == 0) {
        return map;
    }
    map.cells = path;
    map.track = level.road.trackCells();
    map.away.assign(count, Vec2i{0, 0});
    map.laneCells.assign(count, Vec2i{0, 0});
    map.contactCells.assign(count, Vec2i{-1, -1});

    if (level.road.isV2()) {
        // ---- phase 2：为每个声明的车道各建一份几何 -----------------------
        const int laneCount = level.road.width < 1 ? 1 : level.road.width;
        map.lanes.resize(static_cast<std::size_t>(laneCount));
        for (int l = 0; l < laneCount; ++l) {
            LaneGeometry& lane = map.lanes[static_cast<std::size_t>(l)];
            lane.lane = l;
            lane.side = static_cast<std::size_t>(l) < level.road.collectSides.size()
                            ? level.road.collectSides[static_cast<std::size_t>(l)]
                            : LaneSide::None;
            lane.cells.assign(count, Vec2i{-1, -1});
            lane.contacts.resize(count);
            lane.primary.assign(count, Vec2i{-1, -1});
        }
        for (std::size_t i = 0; i < count; ++i) {
            const Vec2i dOut = pathDirAt(path, i, true);
            const Vec2i dIn = pathDirAt(path, i, false);
            std::vector<Vec2i> dirs;
            dirs.push_back(dOut);
            if (level.road.bendCollectsBoth && dIn != dOut) {
                dirs.push_back(dIn);
            }
            for (int l = 0; l < laneCount; ++l) {
                LaneGeometry& lane = map.lanes[static_cast<std::size_t>(l)];
                if (lane.side == LaneSide::None) {
                    lane.cells[i] = path[i];
                    continue;
                }
                const int dist = l + 1;
                const Vec2i sideCell = sideOf(lane.side, dOut);
                // 车道 L 的车位于本车道一侧、距路径 L 个格子的位置；
                // 当 L = 0 时，该位置就是路径格本身。
                lane.cells[i] = Vec2i{path[i].r + l * sideCell.r, path[i].c + l * sideCell.c};
                lane.contacts[i].clear();
                for (const Vec2i& d : dirs) {
                    const Vec2i sideDir = sideOf(lane.side, d);
                    const Vec2i t{path[i].r + sideDir.r * dist, path[i].c + sideDir.c * dist};
                    if (!insideGrid(level, t) || map.onTrack(t) || containsCell(lane.contacts[i], t)) {
                        continue;
                    }
                    lane.contacts[i].push_back(t);
                }
                if (!lane.contacts[i].empty()) {
                    lane.primary[i] = lane.contacts[i].front();
                }
            }
        }
        for (std::size_t i = 0; i < count; ++i) {
            if (!map.lanes.empty()) {
                map.laneCells[i] = map.lanes[0].cells[i];
                map.contactCells[i] = map.lanes[0].primary[i];
            }
        }
        if (!map.lanes.empty()) {
            // `away` 仍然由车道指向其水果一侧，
            // 这正是旧版 snapshot 消费方所期望的。
            for (std::size_t i = 0; i < count; ++i) {
                const Vec2i dOut = pathDirAt(path, i, true);
                const Vec2i side = sideOf(map.lanes[0].side, dOut);
                map.away[i] = Vec2i{-side.r, -side.c};
            }
        }
        map.valid = true;
        return map;
    }

    // ---- phase 1：绝对 collectSide + laneOffset ------------------------
    const Vec2i fruitSide = sideVector(level.road.collectSide);
    LaneGeometry legacy;
    legacy.lane = 0;
    legacy.side = LaneSide::None;
    legacy.cells.assign(count, Vec2i{-1, -1});
    legacy.contacts.resize(count);
    legacy.primary.assign(count, Vec2i{-1, -1});

    for (std::size_t i = 0; i < count; ++i) {
        Vec2i dir{0, 1};
        if (i + 1 < count) {
            dir = path[i + 1] - path[i];
        } else if (i > 0) {
            dir = path[i] - path[i - 1];
        }
        Vec2i away = perpendicularA(dir);
        const Vec2i alt = perpendicularB(dir);

        // 选取背离水果一侧的那条垂线。
        const int scoreA = -dot(away, fruitSide);
        const int scoreB = -dot(alt, fruitSide);
        if (scoreB > scoreA) {
            away = alt;
        }
        map.away[i] = away;
        map.laneCells[i] = path[i] + level.road.laneOffset * away;
        legacy.cells[i] = map.laneCells[i];

        // 只有当车所在格紧邻水果一侧的水果格时，车才会接触到水果
        // （宽路在 offset > 0 时会失去接触）。
        const Vec2i lane = map.laneCells[i];
        const Vec2i contact = lane - away;
        if (insideGrid(level, contact) && !isRoadCell(level, contact)) {
            map.contactCells[i] = contact;
            legacy.contacts[i].push_back(contact);
            legacy.primary[i] = contact;
        }
    }
    map.lanes.push_back(std::move(legacy));
    map.valid = true;
    return map;
}

bool isRoadsideCell(const LevelDefinition& level, Vec2i cell) {
    const std::vector<Vec2i>& track = level.road.trackCells();
    for (const Vec2i& roadCell : track) {
        if (manhattan(roadCell, cell) == 1) {
            return true;
        }
    }
    return false;
}

std::vector<unsigned char> roadsideFlags(const LevelDefinition& level) {
    std::vector<unsigned char> flags(static_cast<std::size_t>(level.rows) * static_cast<std::size_t>(level.cols), 0);
    for (int r = 0; r < level.rows; ++r) {
        for (int c = 0; c < level.cols; ++c) {
            if (isRoadsideCell(level, Vec2i{r, c})) {
                flags[static_cast<std::size_t>(r * level.cols + c)] = 1;
            }
        }
    }
    if (!level.road.isV2()) {
        return flags;
    }
    const RoadMap map = buildRoadMap(level);
    for (const LaneGeometry& lane : map.lanes) {
        for (const auto& list : lane.contacts) {
            for (const Vec2i& contact : list) {
                if (contact.r >= 0 && contact.r < level.rows && contact.c >= 0 && contact.c < level.cols) {
                    flags[static_cast<std::size_t>(contact.r * level.cols + contact.c)] = 1;
                }
            }
        }
    }
    return flags;
}

}  // namespace fc
