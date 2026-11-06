// Game：core 中唯一可变的入口（reset / submit / tick / snapshot）。
// 其余一切都只是对 GameState 的纯函数。
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "fc/commands.h"
#include "fc/level.h"
#include "fc/state.h"
#include "fc/world.h"

namespace fc {

class Game {
public:
    Game() = default;

    void reset(const LevelDefinition& level, std::uint64_t seed);
    CommandResult submit(const Command& command);
    void tick();

    const GameState& state() const { return state_; }
    const LevelDefinition& level() const { return state_.level; }
    const World& world() const { return *world_; }

    std::string buildVersion() const { return buildVersion_; }
    void setBuildVersion(const std::string& version) { buildVersion_ = version; }

    std::vector<const Event*> eventsSince(std::uint64_t sinceSeq) const;

private:
    void moveCars();
    void collectCars();
    void refillAt(int slot);
    void routeCars();
    void applyEffects();
    void decaySettles();
    void updateStatus();
    CommandResult reject(const std::string& why);
    CommandResult submitItem(const Command& command);
    int firstEmptySlot() const;

    GameState state_;
    std::unique_ptr<World> world_ = makeWorld(WorldKind::Grid);
    std::string buildVersion_ = "phase1-dev";
};

}  // namespace fc
