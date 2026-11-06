// FruitsCar phase 1 - core 领域类型。
// 纯值类型。不依赖引擎，无 IO，不读挂钟时间。
#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <utility>

namespace fc {

// 颜色与实体都是不透明的字符串 id，由关卡定义持有。
// core 从不解读颜色名称的含义，只按相等性比较 id。
using ColorId = std::string;
using EntityId = std::string;

// 位置比较的 epsilon，单位为网格格（任务书 3.7）。
constexpr double kEps = 1e-4;

// 逻辑 tick 频率在 phase 1 中固定不变。
constexpr int kTickRate = 60;

struct Vec2i {
    int r = 0;
    int c = 0;
};

inline bool operator==(const Vec2i& a, const Vec2i& b) { return a.r == b.r && a.c == b.c; }
inline bool operator!=(const Vec2i& a, const Vec2i& b) { return !(a == b); }
inline Vec2i operator+(const Vec2i& a, const Vec2i& b) { return Vec2i{a.r + b.r, a.c + b.c}; }
inline Vec2i operator-(const Vec2i& a, const Vec2i& b) { return Vec2i{a.r - b.r, a.c - b.c}; }
inline Vec2i operator*(int k, const Vec2i& a) { return Vec2i{k * a.r, k * a.c}; }

inline int manhattan(const Vec2i& a, const Vec2i& b) {
    const int dr = a.r - b.r;
    const int dc = a.c - b.c;
    return (dr < 0 ? -dr : dr) + (dc < 0 ? -dc : dc);
}

// 关卡坐标系中的自由二维点（任务书 6 T-01）。
//   x = 列轴（在屏幕上向右增长）
//   y = 行轴（在屏幕上向下增长，与 Vec2i::r 相同）
// 所有曲线几何都用这些单位表示；一个单位等于一个网格
// 格，因此 v1 与 v2 关卡共享相同的视觉尺度。
struct Vec2d {
    double x = 0.0;
    double y = 0.0;
};

inline bool operator==(const Vec2d& a, const Vec2d& b) { return a.x == b.x && a.y == b.y; }
inline bool operator!=(const Vec2d& a, const Vec2d& b) { return !(a == b); }
inline Vec2d operator+(const Vec2d& a, const Vec2d& b) { return Vec2d{a.x + b.x, a.y + b.y}; }
inline Vec2d operator-(const Vec2d& a, const Vec2d& b) { return Vec2d{a.x - b.x, a.y - b.y}; }
inline Vec2d operator*(double k, const Vec2d& a) { return Vec2d{k * a.x, k * a.y}; }
inline double dot(const Vec2d& a, const Vec2d& b) { return a.x * b.x + a.y * b.y; }
inline double cross(const Vec2d& a, const Vec2d& b) { return a.x * b.y - a.y * b.x; }
inline double lengthOf(const Vec2d& a) { return std::sqrt(a.x * a.x + a.y * a.y); }
inline Vec2d normalized(const Vec2d& a) {
    const double len = lengthOf(a);
    return len > 1e-12 ? Vec2d{a.x / len, a.y / len} : Vec2d{0.0, 0.0};
}
inline Vec2d vec2d(Vec2i cell) { return Vec2d{static_cast<double>(cell.c), static_cast<double>(cell.r)}; }
inline Vec2i toVec2i(const Vec2d& p) {
    return Vec2i{static_cast<int>(std::lround(p.y)), static_cast<int>(std::lround(p.x))};
}

// 对原始字节做 FNV-1a 64 位哈希；用于 stateHash / layoutHash。
std::uint64_t fnv1a64(const std::string& text);
std::uint64_t fnv1a64Update(std::uint64_t hash, const std::string& text);

// 规范化序列化器使用的固定三位小数格式（"%.3f"）。
std::string formatFixed3(double value);

// 二级曲线序列化器使用的固定四位小数格式（"%.4f"）
// （任务书附录 A.7）。
std::string formatFixed4(double value);

// 各序列化器共用的 JSON 字符串转义（无外部依赖）。
std::string jsonQuote(const std::string& text);

// 自 tick 零起的毫秒数，仅使用整数运算（任务书 3.7）。
inline int tickToMs(int tick) { return tick * 1000 / kTickRate; }

}  // namespace fc
