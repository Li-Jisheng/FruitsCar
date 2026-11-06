// 预留的技能 / 道具接口（任务书 5.8、8 T-30..T-34）。
//
// 本阶段只解析道具、在 snapshot 中暴露它们、保存预留的效果容器，并以稳定的
// reason 拒绝 `use_item`。效果*实现*被刻意留空：待其落地时，只需填充
// `applyEffect` 的 switch 分支（参见 Docs/reports/decisions.md
// D-P3-15）。
#pragma once

#include <string>
#include <vector>

#include "fc/types.h"

namespace fc {

enum class EffectKind { RecolorCar, CollectArea };

const char* toString(EffectKind kind);
bool parseEffectKind(const std::string& text, EffectKind& out);

// 最小可用参数集（任务书 5.8 "只用最小字段"）。
struct EffectParams {
    double radius = 0.0;
    std::vector<ColorId> palette;
};

// 关卡数据：一个道具定义，与 `items[]` 中的书写方式一致。
struct ItemDef {
    EntityId id;
    std::string kindText;  // "recolor_car" | "collect_area"
    EffectKind kind = EffectKind::RecolorCar;
    int charges = 0;
    EffectParams params;
};

// 运行时数据：snapshot 中上报的内容（`items[]`）。
struct ItemState {
    EntityId id;
    std::string kindText;
    EffectKind kind = EffectKind::RecolorCar;
    int charges = 0;
};

// 预留的效果实例（本阶段为空，任务书 T-32）。
struct EffectInstance {
    EffectKind kind = EffectKind::RecolorCar;
    int startTick = 0;
    int expireTick = 0;
    EntityId target;
    Vec2d center;
    double radius = 0.0;
};

}  // namespace fc
