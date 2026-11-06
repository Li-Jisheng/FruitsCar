// 确定性的水果布局生成（row-run 生成器），以及作为人类可读证据使用的
// ASCII 视图。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "fc/level.h"
#include "fc/state.h"

namespace fc {

// 显式种子的伪随机生成器（pcg32）。core 中唯一的随机源：确定性、调用顺序
// 固定，绝不使用 C 库的随机数生成器。
class Rng {
public:
    explicit Rng(std::uint64_t seed, std::uint64_t stream = 0xda3e39cb94b95bdbULL);
    std::uint32_t nextU32();
    // 在 [0, bound) 上均匀分布；bound 必须 > 0。
    int uniform(int bound);
    double nextDouble();

private:
    std::uint64_t state_;
    std::uint64_t inc_;
};

// `cols` 的所有合法游程切分，每段长度位于 [low, high] 内。
std::vector<std::vector<int>> segmentations(int cols, int low, int high);

bool generateFruits(const LevelDefinition& level, std::uint64_t seed,
                    std::vector<FruitCell>& cells, std::string& error);

// 校验 row-run 不变量；违反时返回 false 并填充 `error`。
bool validateRowRuns(const std::vector<FruitCell>& cells, int rows, int cols, int low, int high,
                     std::string& error);

std::uint64_t layoutHash(const std::vector<FruitCell>& cells);

std::string toAscii(const GameState& state);

}  // namespace fc
