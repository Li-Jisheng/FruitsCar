#include "fc/geom.h"

#include <algorithm>
#include <cmath>

namespace fc {
namespace {

// Catmull-Rom 节点间距辅助函数：t_i = |P_i - P_{i-1}|^0.5（向心参数化，
// alpha = 0.5）。零距离会导致除零，因此对增量做了下限截断。
double knotDelta(const Vec2d& a, const Vec2d& b) {
    const double d = std::sqrt(lengthOf(b - a));
    return d < 1e-4 ? 1e-4 : d;
}

// 向心 Catmull-Rom 样条的 Barry-Goldman 形式：对任意非退化的节点序列
// 都有良好定义，这一点与矩阵形式不同。
Vec2d catmullRom(const Vec2d& p0, const Vec2d& p1, const Vec2d& p2, const Vec2d& p3, double u) {
    const double t0 = 0.0;
    const double t1 = t0 + knotDelta(p0, p1);
    const double t2 = t1 + knotDelta(p1, p2);
    const double t3 = t2 + knotDelta(p2, p3);
    const double t = t1 + (t2 - t1) * u;

    const Vec2d a1 = (1.0 / (t1 - t0)) * ((t1 - t) * p0 + (t - t0) * p1);
    const Vec2d a2 = (1.0 / (t2 - t1)) * ((t2 - t) * p1 + (t - t1) * p2);
    const Vec2d a3 = (1.0 / (t3 - t2)) * ((t3 - t) * p2 + (t - t2) * p3);
    const Vec2d b1 = (1.0 / (t2 - t0)) * ((t2 - t) * a1 + (t - t0) * a2);
    const Vec2d b2 = (1.0 / (t3 - t1)) * ((t3 - t) * a2 + (t - t1) * a3);
    return (1.0 / (t2 - t1)) * ((t2 - t) * b1 + (t - t1) * b2);
}

}  // namespace

double normalizeArc(double s, double length, bool closed) {
    if (length <= 0.0) {
        return 0.0;
    }
    if (closed) {
        double wrapped = std::fmod(s, length);
        if (wrapped < 0.0) {
            wrapped += length;
        }
        if (wrapped >= length - 1e-12) {
            wrapped = 0.0;
        }
        return wrapped;
    }
    if (s < 0.0) {
        return 0.0;
    }
    if (s > length) {
        return length;
    }
    return s;
}

Polyline Polyline::fromPoints(std::vector<Vec2d> points, bool closed) {
    Polyline line;
    line.closed = closed;
    line.points = std::move(points);
    line.arc.assign(line.points.size(), 0.0);
    for (std::size_t i = 1; i < line.points.size(); ++i) {
        line.arc[i] = line.arc[i - 1] + lengthOf(line.points[i] - line.points[i - 1]);
    }
    line.valid = line.points.size() >= 2 && line.length() > 0.0;
    return line;
}

Polyline Polyline::fromControlPoints(const std::vector<Vec2d>& controlPoints, bool closed, double step,
                                     std::string& err) {
    err.clear();
    Polyline out;
    if (controlPoints.size() < 2) {
        err = "E2001";
        return out;
    }
    if (!(step > 0.0) || step > 1.0) {
        err = "E2002";
        return out;
    }

    const std::size_t n = controlPoints.size();
    const std::size_t segments = closed ? n : n - 1;

    // 1. 密集参数采样，相邻采样点间距不超过 step/4。
    std::vector<Vec2d> dense;
    dense.reserve(n * 32);
    for (std::size_t i = 0; i < segments; ++i) {
        const Vec2d& p1 = controlPoints[i];
        const Vec2d& p2 = controlPoints[(i + 1) % n];
        const Vec2d p0 = closed ? controlPoints[(i + n - 1) % n] : (2.0 * p1 - p2);
        const Vec2d p3 = closed ? controlPoints[(i + 2) % n] : (2.0 * p2 - p1);
        const double chord = lengthOf(p2 - p1);
        if (chord < 1e-9) {
            continue;
        }
        int sub = static_cast<int>(std::ceil(chord / (step * 0.25)));
        if (sub < 4) {
            sub = 4;
        }
        if (sub > 512) {
            sub = 512;
        }
        for (int k = 0; k < sub; ++k) {
            dense.push_back(catmullRom(p0, p1, p2, p3, static_cast<double>(k) / static_cast<double>(sub)));
        }
    }
    if (dense.empty()) {
        err = "E2001";
        return out;
    }
    // 精确闭合密集点链（闭合曲线：回到第一个控制点）。
    dense.push_back(closed ? controlPoints.front() : controlPoints.back());

    // 2. 以 `step` 为间距做等弧长重采样。
    out.closed = closed;
    out.points.push_back(dense.front());
    out.arc.push_back(0.0);
    double accumulated = 0.0;
    double target = step;
    for (std::size_t i = 1; i < dense.size(); ++i) {
        const Vec2d delta = dense[i] - dense[i - 1];
        const double segLen = lengthOf(delta);
        if (segLen <= 1e-12) {
            continue;
        }
        while (target <= accumulated + segLen - 1e-12) {
            const double t = (target - accumulated) / segLen;
            out.points.push_back(dense[i - 1] + t * delta);
            out.arc.push_back(target);
            target += step;
            if (out.points.size() > static_cast<std::size_t>(kMaxSamples) + 1) {
                err = "E2021";
                out.points.clear();
                out.arc.clear();
                out.valid = false;
                return out;
            }
        }
        accumulated += segLen;
    }
    // 末尾总是落在最后一个密集点上（闭合曲线：与起点重合）。
    if (out.points.empty() || lengthOf(out.points.back() - dense.back()) > 1e-12) {
        out.points.push_back(dense.back());
        out.arc.push_back(accumulated);
    }
    if (out.points.size() > static_cast<std::size_t>(kMaxSamples)) {
        err = "E2021";
        out.points.clear();
        out.arc.clear();
        out.valid = false;
        return out;
    }
    out.valid = true;
    return out;
}

Vec2d Polyline::positionAt(double s) const {
    if (points.empty()) {
        return Vec2d{0.0, 0.0};
    }
    if (points.size() == 1) {
        return points.front();
    }
    const double total = length();
    if (total <= 0.0) {
        return points.front();
    }
    s = normalizeArc(s, total, closed);
    const auto it = std::upper_bound(arc.begin(), arc.end(), s);
    std::size_t index = it == arc.begin() ? 0 : static_cast<std::size_t>(it - arc.begin() - 1);
    if (index + 1 >= points.size()) {
        index = points.size() - 2;
    }
    const double a = arc[index];
    const double b = arc[index + 1];
    const double t = (b - a) > 1e-12 ? (s - a) / (b - a) : 0.0;
    return points[index] + t * (points[index + 1] - points[index]);
}

Vec2d Polyline::tangentAt(double s) const {
    const double total = length();
    if (total <= 1e-9 || points.size() < 2) {
        return Vec2d{1.0, 0.0};
    }
    double h = std::min(0.05, total * 0.01);
    if (h < 1e-4) {
        h = 1e-4;
    }
    const Vec2d ahead = positionAt(s + h);
    const Vec2d behind = positionAt(s - h);
    const Vec2d direction = normalized(ahead - behind);
    if (lengthOf(direction) < 1e-9) {
        return Vec2d{1.0, 0.0};
    }
    return direction;
}

Projection Polyline::project(const Vec2d& p) const {
    Projection best;
    best.distance = 1e18;
    if (points.size() < 2) {
        return best;
    }
    for (std::size_t i = 0; i + 1 < points.size(); ++i) {
        const Vec2d a = points[i];
        const Vec2d ab = points[i + 1] - a;
        const double len2 = dot(ab, ab);
        double t = 0.0;
        if (len2 > 1e-18) {
            t = dot(p - a, ab) / len2;
            if (t < 0.0) {
                t = 0.0;
            }
            if (t > 1.0) {
                t = 1.0;
            }
        }
        const Vec2d q = a + t * ab;
        const Vec2d v = p - q;
        const double dist = lengthOf(v);
        if (dist < best.distance - 1e-12) {
            const double segLen = std::sqrt(len2);
            const Vec2d tangent = segLen > 1e-12 ? (1.0 / segLen) * ab : Vec2d{1.0, 0.0};
            best.distance = dist;
            best.s = arc[i] + t * segLen;
            best.segment = static_cast<int>(i);
            // 带符号的横向距离：d = dot(v, (ty, -tx))；在关卡坐标系中
            // （x 向右、y 向下）d 为正表示驾驶员的左侧。
            best.d = v.x * tangent.y - v.y * tangent.x;
        }
    }
    return best;
}

std::vector<double> sampleArcPositions(const Polyline& line, double spacing) {
    std::vector<double> out;
    const double total = line.length();
    if (!(spacing > 0.0) || total <= 0.0 || line.points.size() < 2) {
        return out;
    }
    const int maxCount = static_cast<int>(total / spacing) + 2;
    for (int k = 0; k < maxCount; ++k) {
        const double s = static_cast<double>(k) * spacing;
        if (s >= total - 1e-9) {
            break;
        }
        out.push_back(s);
    }
    return out;
}

std::vector<double> fruitLayerArcPositions(const Polyline& line, double spacing) {
    std::vector<double> out;
    const double total = line.length();
    if (!(spacing > 0.0) || total <= 0.0 || line.points.size() < 2) {
        return out;
    }
    static const std::vector<double> kNone;
    if (!line.closed) {
        const double phase = spacing * 0.5;
        const int maxCount = static_cast<int>(total / spacing) + 2;
        for (int k = 0; k < maxCount; ++k) {
            const double s = phase + static_cast<double>(k) * spacing;
            if (s >= total - 1e-9) {
                break;
            }
            out.push_back(s);
        }
        return out;
    }
    return sampleArcPositions(line, spacing);
}

std::string polylineJson(const Polyline& line) {
    std::string out = "[";
    for (std::size_t i = 0; i < line.points.size(); ++i) {
        if (i > 0) {
            out += ",";
        }
        out += "[" + formatFixed4(line.points[i].x) + "," + formatFixed4(line.points[i].y) + "]";
    }
    out += "]";
    return out;
}

}  // namespace fc
