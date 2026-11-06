#include "fc/refill.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

namespace fc {
namespace {

struct RingInfo {
    int ring = 0;
    double angle = 0.0;
};

RingInfo ringInfoOf(int rows, int cols, Vec2i cell) {
    const double centreR = (static_cast<double>(rows) - 1.0) / 2.0;
    const double centreC = (static_cast<double>(cols) - 1.0) / 2.0;
    const double dr = static_cast<double>(cell.r) - centreR;
    const double dc = static_cast<double>(cell.c) - centreC;
    const double chebyshev = std::max(std::fabs(dr), std::fabs(dc));
    RingInfo info;
    info.ring = static_cast<int>(std::floor(chebyshev + 0.5));
    info.angle = std::atan2(dr, dc);
    return info;
}

bool lessByRingThenAngle(const std::pair<Vec2i, RingInfo>& a, const std::pair<Vec2i, RingInfo>& b,
                         bool innerFirst) {
    if (a.second.ring != b.second.ring) {
        return innerFirst ? (a.second.ring < b.second.ring) : (a.second.ring > b.second.ring);
    }
    if (a.second.angle != b.second.angle) {
        return a.second.angle < b.second.angle;
    }
    if (a.first.r != b.first.r) {
        return a.first.r < b.first.r;
    }
    return a.first.c < b.first.c;
}

double angleOf(const LevelDefinition& level, Vec2i cell) {
    const double centreR = (static_cast<double>(level.rows) - 1.0) / 2.0;
    const double centreC = (static_cast<double>(level.cols) - 1.0) / 2.0;
    return std::atan2(static_cast<double>(cell.r) - centreR, static_cast<double>(cell.c) - centreC);
}

double angleDelta(double a, double b) {
    constexpr double kTwoPi = 6.28318530717958647692;
    double delta = std::fabs(a - b);
    if (delta > 3.14159265358979323846) {
        delta = kTwoPi - delta;
    }
    return delta;
}

bool onTrack(const LevelDefinition& level, Vec2i cell) {
    const std::vector<Vec2i>& track = level.road.trackCells();
    for (const Vec2i& roadCell : track) {
        if (roadCell == cell) {
            return true;
        }
    }
    return false;
}

// 沿层树向下做深度优先搜索，寻找第一个水果。
bool findFirstFruitDfs(const std::vector<std::vector<int>>& children, int cols, const Occupancy& present,
                       int index, std::vector<Vec2i>& out) {
    const Vec2i cell{index / cols, index % cols};
    out.push_back(cell);
    if (present(cell)) {
        return true;
    }
    for (int child : children[static_cast<std::size_t>(index)]) {
        if (findFirstFruitDfs(children, cols, present, child, out)) {
            return true;
        }
    }
    out.pop_back();
    return false;
}

// 在非道路（non-tarmac）的格子上做广度优先搜索；填充包含 `target` 的
// 连通区域的父节点表，以及每个已到达格的距离。
std::vector<Vec2i> regionParents(const LevelDefinition& level, Vec2i target, std::vector<int>& dist) {
    const std::size_t total = static_cast<std::size_t>(level.rows) * static_cast<std::size_t>(level.cols);
    std::vector<Vec2i> parents(total, Vec2i{-1, -1});
    dist.assign(total, -1);
    if (target.r < 0 || target.r >= level.rows || target.c < 0 || target.c >= level.cols) {
        return parents;
    }
    std::vector<Vec2i> queue;
    queue.push_back(target);
    dist[static_cast<std::size_t>(target.r * level.cols + target.c)] = 0;
    const Vec2i offsets[4] = {Vec2i{-1, 0}, Vec2i{0, 1}, Vec2i{1, 0}, Vec2i{0, -1}};
    for (std::size_t head = 0; head < queue.size(); ++head) {
        const Vec2i cell = queue[head];
        const int base = dist[static_cast<std::size_t>(cell.r * level.cols + cell.c)];
        for (const Vec2i& offset : offsets) {
            const Vec2i next{cell.r + offset.r, cell.c + offset.c};
            if (next.r < 0 || next.r >= level.rows || next.c < 0 || next.c >= level.cols) {
                continue;
            }
            if (onTrack(level, next)) {
                continue;
            }
            const std::size_t index = static_cast<std::size_t>(next.r * level.cols + next.c);
            if (dist[index] < 0) {
                dist[index] = base + 1;
                parents[index] = cell;
                queue.push_back(next);
                continue;
            }
            // 同一距离也可由另一个父节点到达：保留 (row, col) 更小的那个父节点，
            // 以保证结果确定。
            if (dist[index] == base + 1) {
                const Vec2i current = parents[index];
                if (cell.r < current.r || (cell.r == current.r && cell.c < current.c)) {
                    parents[index] = cell;
                }
            }
        }
    }
    return parents;
}

}  // namespace

std::vector<Vec2i> refillLine(const LevelDefinition& level, Vec2i target) {
    std::vector<Vec2i> line;
    line.push_back(target);
    switch (level.fruits.refill) {
        case RefillDirection::Down:
            for (int r = target.r - 1; r >= 0; --r) {
                line.push_back(Vec2i{r, target.c});
            }
            break;
        case RefillDirection::Up:
            for (int r = target.r + 1; r < level.rows; ++r) {
                line.push_back(Vec2i{r, target.c});
            }
            break;
        case RefillDirection::Left:
            for (int c = target.c + 1; c < level.cols; ++c) {
                line.push_back(Vec2i{target.r, c});
            }
            break;
        case RefillDirection::Right:
            for (int c = target.c - 1; c >= 0; --c) {
                line.push_back(Vec2i{target.r, c});
            }
            break;
        case RefillDirection::RingIn:
            // 水果从外圈向中心补充。
            return ringSpoke(level, target, true);
        case RefillDirection::RingOut:
            return ringSpoke(level, target, false);
        case RefillDirection::LayerIn:
        case RefillDirection::RegionIn:
            // 由 refillPath 处理（需要实时的占用状态）。
            return line;
    }
    return line;
}

std::vector<Vec2i> layerParents(const LevelDefinition& level) {
    const std::size_t total = static_cast<std::size_t>(level.rows) * static_cast<std::size_t>(level.cols);
    std::vector<Vec2i> parents(total, Vec2i{-1, -1});
    if (level.fruits.layers.size() < 2) {
        return parents;
    }
    for (std::size_t k = 1; k < level.fruits.layers.size(); ++k) {
        const LayerDef& outer = level.fruits.layers[k];
        const LayerDef& inner = level.fruits.layers[k - 1];
        for (const Vec2i& cell : outer.cells) {
            if (cell.r < 0 || cell.r >= level.rows || cell.c < 0 || cell.c >= level.cols) {
                continue;
            }
            const double a = angleOf(level, cell);
            Vec2i best{-1, -1};
            double bestDelta = 1e9;
            for (const Vec2i& candidate : inner.cells) {
                const double delta = angleDelta(a, angleOf(level, candidate));
                if (delta < bestDelta - 1e-12 ||
                    (std::fabs(delta - bestDelta) <= 1e-12 &&
                     (candidate.r < best.r || (candidate.r == best.r && candidate.c < best.c)))) {
                    bestDelta = delta;
                    best = candidate;
                }
            }
            parents[static_cast<std::size_t>(cell.r * level.cols + cell.c)] = best;
        }
    }
    return parents;
}

std::vector<std::vector<int>> layerChildren(const LevelDefinition& level, int rows, int cols) {
    const std::size_t total = static_cast<std::size_t>(rows) * static_cast<std::size_t>(cols);
    std::vector<std::vector<int>> children(total);
    const std::vector<Vec2i> parents = layerParents(level);
    std::vector<int> order;
    order.reserve(total);
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            order.push_back(r * cols + c);
        }
    }
    // 同一父节点的子节点按 (angle, row, col) 排序。
    std::sort(order.begin(), order.end(), [&level, cols](int a, int b) {
        const Vec2i ca{a / cols, a % cols};
        const Vec2i cb{b / cols, b % cols};
        const double aa = angleOf(level, ca);
        const double ab = angleOf(level, cb);
        if (std::fabs(aa - ab) > 1e-12) {
            return aa < ab;
        }
        if (ca.r != cb.r) {
            return ca.r < cb.r;
        }
        return ca.c < cb.c;
    });
    for (int index : order) {
        const Vec2i parent = parents[static_cast<std::size_t>(index)];
        if (parent.r < 0 || parent.c < 0) {
            continue;
        }
        children[static_cast<std::size_t>(parent.r * cols + parent.c)].push_back(index);
    }
    return children;
}

std::vector<Vec2i> refillPath(const LevelDefinition& level, Vec2i target, const Occupancy& present) {
    if (level.fruits.refill == RefillDirection::LayerIn) {
        if (level.fruits.layers.empty()) {
            return std::vector<Vec2i>();
        }
        const std::vector<std::vector<int>> children = layerChildren(level, level.rows, level.cols);
        const int start = target.r * level.cols + target.c;
        std::vector<Vec2i> chain;
        if (!findFirstFruitDfs(children, level.cols, present, start, chain)) {
            return std::vector<Vec2i>();
        }
        return chain;
    }

    if (level.fruits.refill == RefillDirection::RegionIn) {
        std::vector<int> dist;
        const std::vector<Vec2i> parents = regionParents(level, target, dist);
        int bestIndex = -1;
        int bestDist = 0;
        for (int r = 0; r < level.rows; ++r) {
            for (int c = 0; c < level.cols; ++c) {
                const std::size_t index = static_cast<std::size_t>(r * level.cols + c);
                if (dist[index] <= 0 || !present(Vec2i{r, c})) {
                    continue;
                }
                if (bestIndex < 0) {
                    bestIndex = static_cast<int>(index);
                    bestDist = dist[index];
                    continue;
                }
                const int bestR = bestIndex / level.cols;
                const int bestC = bestIndex % level.cols;
                if (dist[index] < bestDist || (dist[index] == bestDist && (r < bestR || (r == bestR && c < bestC)))) {
                    bestIndex = static_cast<int>(index);
                    bestDist = dist[index];
                }
            }
        }
        if (bestIndex < 0) {
            return std::vector<Vec2i>();
        }
        std::vector<Vec2i> reversed;
        Vec2i cursor{bestIndex / level.cols, bestIndex % level.cols};
        for (;;) {
            reversed.push_back(cursor);
            const Vec2i parent = parents[static_cast<std::size_t>(cursor.r * level.cols + cursor.c)];
            if (parent.r < 0 || parent.c < 0 || parent == cursor) {
                break;
            }
            cursor = parent;
        }
        std::reverse(reversed.begin(), reversed.end());
        return reversed;
    }

    return refillLine(level, target);
}

std::vector<Vec2i> ringOrderCells(int rows, int cols, bool innerFirst) {
    std::vector<std::pair<Vec2i, RingInfo>> entries;
    entries.reserve(static_cast<std::size_t>(rows) * static_cast<std::size_t>(cols));
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            const Vec2i cell{r, c};
            entries.emplace_back(cell, ringInfoOf(rows, cols, cell));
        }
    }
    std::sort(entries.begin(), entries.end(),
              [innerFirst](const std::pair<Vec2i, RingInfo>& a, const std::pair<Vec2i, RingInfo>& b) {
                  return lessByRingThenAngle(a, b, innerFirst);
              });
    std::vector<Vec2i> out;
    out.reserve(entries.size());
    for (const auto& entry : entries) {
        out.push_back(entry.first);
    }
    return out;
}

int ringIndexOf(int rows, int cols, Vec2i cell) {
    const std::vector<Vec2i> order = ringOrderCells(rows, cols, true);
    for (std::size_t i = 0; i < order.size(); ++i) {
        if (order[i] == cell) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

std::vector<Vec2i> ringSpoke(const LevelDefinition& level, Vec2i target, bool outward) {
    std::vector<Vec2i> line;
    RingInfo targetInfo = ringInfoOf(level.rows, level.cols, target);
    line.push_back(target);

    struct Ring {
        int index = 0;
        std::vector<Vec2i> cells;
    };
    std::vector<Ring> rings;
    for (int r = 0; r < level.rows; ++r) {
        for (int c = 0; c < level.cols; ++c) {
            const Vec2i cell{r, c};
            const RingInfo info = ringInfoOf(level.rows, level.cols, cell);
            auto it = std::find_if(rings.begin(), rings.end(),
                                   [&info](const Ring& ring) { return ring.index == info.ring; });
            if (it == rings.end()) {
                Ring ring;
                ring.index = info.ring;
                ring.cells.push_back(cell);
                rings.push_back(ring);
            } else {
                it->cells.push_back(cell);
            }
        }
    }
    std::sort(rings.begin(), rings.end(), [](const Ring& a, const Ring& b) { return a.index < b.index; });

    // 按请求的方向逐圈推进，并在每个圈上取角度最接近的格子。
    for (;;) {
        int nextIndex = -1;
        if (outward) {
            for (const Ring& ring : rings) {
                if (ring.index > targetInfo.ring) {
                    nextIndex = ring.index;
                    break;
                }
            }
        } else {
            for (const Ring& ring : rings) {
                if (ring.index < targetInfo.ring && ring.index > nextIndex) {
                    nextIndex = ring.index;
                }
            }
        }
        if (nextIndex < 0) {
            break;
        }
        const Ring* chosen = nullptr;
        for (const Ring& ring : rings) {
            if (ring.index == nextIndex) {
                chosen = &ring;
                break;
            }
        }
        if (chosen == nullptr) {
            break;
        }
        Vec2i best = chosen->cells.front();
        double bestDelta = 1e9;
        for (const Vec2i& cell : chosen->cells) {
            double delta = std::fabs(ringInfoOf(level.rows, level.cols, cell).angle - targetInfo.angle);
            if (delta > 3.14159265358979323846) {
                delta = 6.28318530717958647692 - delta;
            }
            if (delta < bestDelta - 1e-12) {
                bestDelta = delta;
                best = cell;
            }
        }
        line.push_back(best);
        targetInfo.ring = nextIndex;
    }
    return line;
}

}  // namespace fc
