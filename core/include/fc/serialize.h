// 规范化序列化：固定字段顺序、"%.3f" 浮点格式、UTF-8 无 BOM。
// stateHash 在规范化状态上计算（不含易变字段）。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "fc/events.h"
#include "fc/level.h"
#include "fc/state.h"

namespace fc {

// 由宿主持有的字段，不属于 core 的玩法状态。
struct SnapshotContext {
    int levelEpoch = 0;
    bool paused = false;
};

std::string canonicalStateJson(const GameState& state);
std::uint64_t stateHash(const GameState& state);

std::string snapshotJson(const GameState& state, const std::vector<const Event*>& events,
                         std::uint64_t snapshotSeq, const std::string& buildVersion,
                         const SnapshotContext& context = SnapshotContext());

// 供加载期审计日志使用的关卡审计记录（任务书 9.1）。
std::string auditJson(const LevelDefinition& level);

std::string levelToJson(const LevelDefinition& level);

}  // namespace fc
