// 用于驱动端到端演示以及 jam/win 场景的确定性命令策略。
// 它只读取公开的 Game API 并提交普通命令，
// 因此绝不会绕过规则。
#pragma once

#include "fc/game.h"

namespace fc {

class AutoplayDriver {
public:
    enum class Mode { Greedy, Spam };

    explicit AutoplayDriver(Mode mode = Mode::Greedy, int maxRoadCars = 2);

    // 当 driver 希望在当前 tick 行动时填充 `command`。
    bool decide(const Game& game, Command& command);

    Mode mode() const { return mode_; }

private:
    bool decideGreedy(const Game& game, Command& command);
    bool decideSpam(const Game& game, Command& command);

    Mode mode_;
    int maxRoadCars_;
};

// 用该 driver 运行游戏，直到达到终局状态或 maxTicks。
// 返回终局状态所在的 tick；运行超时时返回 -1。
int runAutoplay(Game& game, AutoplayDriver& driver, int maxTicks);

}  // namespace fc
