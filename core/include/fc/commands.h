// 输入命令及其受理回执（任务书 3.5 / M4，由 5.8 扩展）。
#pragma once

#include <string>

#include "fc/types.h"

namespace fc {

enum class CommandType { SelectLane, SelectSlot, UseItem };

struct Command {
    CommandType type = CommandType::SelectLane;
    int index = -1;

    // ---- 预留技能命令（任务书 5.8 T-31） ----------------------
    EntityId itemId;
    EntityId targetCar;
    Vec2d center;
    double radius = 0.0;
};

const char* toString(CommandType type);

namespace reason {
constexpr const char* kAccept = "ACCEPT";
constexpr const char* kGameOver = "GAME_OVER";
constexpr const char* kBusy = "BUSY";
constexpr const char* kExpectSlotSelect = "EXPECT_SLOT_SELECT";
constexpr const char* kCooldown = "COOLDOWN";
constexpr const char* kRoadBlocked = "ROAD_BLOCKED";
constexpr const char* kLaneEmpty = "LANE_EMPTY";
constexpr const char* kSlotEmpty = "SLOT_EMPTY";
constexpr const char* kBadIndex = "BAD_INDEX";
// 预留技能（本阶段始终停在 kItemNotImplemented）。
constexpr const char* kItemNotFound = "ITEM_NOT_FOUND";
constexpr const char* kItemDepleted = "ITEM_DEPLETED";
constexpr const char* kItemTargetInvalid = "ITEM_TARGET_INVALID";
constexpr const char* kItemNotImplemented = "ITEM_NOT_IMPLEMENTED";
}  // namespace reason

struct CommandResult {
    bool accepted = false;
    std::string reason;
    int nextAllowedTick = 0;
};

}  // namespace fc
