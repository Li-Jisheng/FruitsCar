// 有序、自描述的玩法事件。每个事件都携带全局单调递增的 seq，以便两个进程
// 精确比对事件流。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "fc/types.h"

namespace fc {

enum class EventType {
    VehicleEntered,
    CarBlocked,
    VehicleExited,
    VehicleParked,
    RequireSelect,
    FruitCollected,
    FruitRefilled,
    CollectSkipped,
    CommandRejected,
    Result,
    Session,
    // 预留技能（任务书 5.8 / 附录 C）。
    ItemRejected,
    ItemActivated,
    EffectApplied,
    EffectExpired
};

const char* toString(EventType type);

struct Event {
    std::uint64_t seq = 0;
    int tick = 0;
    EventType type = EventType::Session;

    EntityId car;
    ColorId color;
    Vec2i at{-1, -1};
    Vec2i from{-1, -1};
    int load = -1;
    int capacity = -1;
    int slot = -1;
    int lane = -1;
    int count = -1;
    int ring = -1;
    int nextAllowedTick = -1;
    double value = 0.0;
    double value2 = 0.0;
    std::string reason;   // 拒绝原因 / NO_COLLECT_* 错误码 / 结果名
    std::string levelId;
    std::string commandType;
    std::string itemId;   // 预留技能 id（道具事件）
    std::uint64_t seed = 0;

    // ---- 曲线关卡（任务书 5.7 A-6/A-7、6 T-09） ----------------------
    // 网格关卡从不设置 `hasWorld`，因此其事件 JSON 与 phase-1/2 的证据保持
    // 逐字节一致。
    int fruitId = -1;         // 稳定的水果标识（被采集 / 被补充）
    int fruitIdFrom = -1;     // 同一 id，位于补充链的源端
    bool hasWorld = false;
    Vec2d world{0.0, 0.0};
    Vec2d worldFrom{0.0, 0.0};
};

std::string eventToJson(const Event& event);

// 以 CollectSkipped 事件形式发出的跳过原因。
namespace skip {
constexpr const char* kFull = "NO_COLLECT_FULL";
constexpr const char* kColor = "NO_COLLECT_COLOR";
constexpr const char* kSettling = "NO_COLLECT_SETTLING";
constexpr const char* kEmpty = "NO_COLLECT_EMPTY";
constexpr const char* kNotAdjacent = "NO_COLLECT_NOT_ADJACENT";
// 曲线世界：水果位于采集侧的相对另一侧。
constexpr const char* kSide = "NO_COLLECT_SIDE";
}  // namespace skip

}  // namespace fc
