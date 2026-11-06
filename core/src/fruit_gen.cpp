#include "fc/fruit_gen.h"

#include <cstddef>

namespace fc {
namespace {

void splitsRec(int remaining, int low, int high, std::vector<int>& current,
               std::vector<std::vector<int>>& out) {
    if (remaining == 0) {
        out.push_back(current);
        return;
    }
    for (int len = low; len <= high; ++len) {
        if (len > remaining) {
            break;
        }
        const int rest = remaining - len;
        if (rest != 0 && rest < low) {
            continue;
        }
        current.push_back(len);
        splitsRec(rest, low, high, current, out);
        current.pop_back();
    }
}

}  // namespace

Rng::Rng(std::uint64_t seed, std::uint64_t stream) : state_(0), inc_((stream << 1u) | 1u) {
    state_ = 0;
    nextU32();
    state_ += seed;
    nextU32();
}

std::uint32_t Rng::nextU32() {
    const std::uint64_t old = state_;
    state_ = old * 6364136223846793005ULL + inc_;
    const std::uint32_t xorshifted = static_cast<std::uint32_t>(((old >> 18u) ^ old) >> 27u);
    const std::uint32_t rot = static_cast<std::uint32_t>(old >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
}

int Rng::uniform(int bound) {
    if (bound <= 1) {
        return 0;
    }
    // 采用拒绝采样（rejection sampling）保证分布无偏。
    const std::uint32_t threshold = static_cast<std::uint32_t>((0u - static_cast<std::uint32_t>(bound)) % static_cast<std::uint32_t>(bound));
    for (;;) {
        const std::uint32_t value = nextU32();
        if (value >= threshold) {
            return static_cast<int>(value % static_cast<std::uint32_t>(bound));
        }
    }
}

double Rng::nextDouble() { return static_cast<double>(nextU32()) / 4294967296.0; }

std::vector<std::vector<int>> segmentations(int cols, int low, int high) {
    std::vector<std::vector<int>> out;
    if (cols <= 0 || low < 1 || low > high) {
        return out;
    }
    std::vector<int> current;
    splitsRec(cols, low, high, current, out);
    return out;
}

bool generateFruits(const LevelDefinition& level, std::uint64_t seed,
                    std::vector<FruitCell>& cells, std::string& error) {
    const std::size_t total = static_cast<std::size_t>(level.rows) * static_cast<std::size_t>(level.cols);
    cells.assign(total, FruitCell());
    if (!level.fruits.layers.empty()) {
        for (std::size_t k = 0; k < level.fruits.layers.size(); ++k) {
            const LayerDef& layer = level.fruits.layers[k];
            for (const Vec2i& cell : layer.cells) {
                if (cell.r < 0 || cell.r >= level.rows || cell.c < 0 || cell.c >= level.cols) {
                    error = "layer cell outside the grid";
                    return false;
                }
                FruitCell& target = cells[static_cast<std::size_t>(cell.r * level.cols + cell.c)];
                target.present = true;
                target.color = layer.color;
                target.ring = static_cast<int>(k);
                target.size = layer.size;
            }
        }
        return true;
    }
    if (level.fruits.explicitLayout) {
        if (level.fruits.explicitCells.size() != total) {
            error = "explicit layout size mismatch";
            return false;
        }
        for (std::size_t i = 0; i < total; ++i) {
            if (!level.fruits.explicitCells[i].empty()) {
                cells[i].present = true;
                cells[i].color = level.fruits.explicitCells[i];
            }
        }
        return true;
    }

    const std::vector<ColorId>& colors = level.fruits.colors;
    if (colors.empty()) {
        error = "no fruit colors configured";
        return false;
    }
    const std::vector<std::vector<int>> splits =
        segmentations(level.cols, level.fruits.runLow, level.fruits.runHigh);
    if (splits.empty()) {
        error = "no valid run segmentation for cols=" + std::to_string(level.cols);
        return false;
    }

    Rng rng(seed);
    for (int r = 0; r < level.rows; ++r) {
        const std::vector<int>& split = splits[static_cast<std::size_t>(rng.uniform(static_cast<int>(splits.size())))];
        int col = 0;
        ColorId previous;
        for (int runLength : split) {
            ColorId chosen;
            if (colors.size() == 1) {
                chosen = colors.front();
            } else {
                // 在与上一个游程颜色不同的颜色中均匀挑选。
                int candidates = static_cast<int>(colors.size());
                if (!previous.empty()) {
                    --candidates;
                }
                int pick = rng.uniform(candidates);
                for (const auto& color : colors) {
                    if (!previous.empty() && color == previous) {
                        continue;
                    }
                    if (pick == 0) {
                        chosen = color;
                        break;
                    }
                    --pick;
                }
            }
            for (int k = 0; k < runLength; ++k) {
                const int index = r * level.cols + col + k;
                cells[static_cast<std::size_t>(index)].present = true;
                cells[static_cast<std::size_t>(index)].color = chosen;
            }
            previous = chosen;
            col += runLength;
        }
    }
    return true;
}

bool validateRowRuns(const std::vector<FruitCell>& cells, int rows, int cols, int low, int high,
                     std::string& error) {
    if (static_cast<int>(cells.size()) != rows * cols) {
        error = "cell count mismatch";
        return false;
    }
    for (int r = 0; r < rows; ++r) {
        int runLength = 0;
        int runs = 0;
        ColorId previous;
        for (int c = 0; c < cols; ++c) {
            const FruitCell& cell = cells[static_cast<std::size_t>(r * cols + c)];
            if (!cell.present) {
                error = "empty cell at row " + std::to_string(r) + " col " + std::to_string(c);
                return false;
            }
            if (cell.color != previous) {
                if (runLength > 0) {
                    if (runLength < low || runLength > high) {
                        error = "run length " + std::to_string(runLength) + " at row " + std::to_string(r) +
                                " outside [" + std::to_string(low) + "," + std::to_string(high) + "]";
                        return false;
                    }
                    ++runs;
                }
                previous = cell.color;
                runLength = 1;
            } else {
                ++runLength;
            }
        }
        if (runLength > 0) {
            if (runLength < low || runLength > high) {
                error = "run length " + std::to_string(runLength) + " at row " + std::to_string(r) +
                        " outside [" + std::to_string(low) + "," + std::to_string(high) + "]";
                return false;
            }
            ++runs;
        }
        if (runs < 1) {
            error = "row " + std::to_string(r) + " has no fruit";
            return false;
        }
    }
    return true;
}

std::uint64_t layoutHash(const std::vector<FruitCell>& cells) {
    std::string canonical;
    canonical.reserve(cells.size() * 6);
    for (const auto& cell : cells) {
        if (cell.present) {
            canonical += cell.color;
        }
        canonical.push_back('|');
    }
    return fnv1a64(canonical);
}

std::string toAscii(const GameState& state) {
    const LevelDefinition& level = state.level;
    int minR = 0;
    int maxR = level.rows - 1;
    int minC = 0;
    int maxC = level.cols - 1;
    for (const Vec2i& cell : level.road.path) {
        if (cell.r < minR) minR = cell.r;
        if (cell.r > maxR) maxR = cell.r;
        if (cell.c < minC) minC = cell.c;
        if (cell.c > maxC) maxC = cell.c;
    }
    const int height = maxR - minR + 1;
    const int width = maxC - minC + 1;
    std::vector<std::string> canvas(static_cast<std::size_t>(height),
                                    std::string(static_cast<std::size_t>(width), ' '));

    for (int r = minR; r <= maxR; ++r) {
        for (int c = minC; c <= maxC; ++c) {
            if (r >= 0 && r < level.rows && c >= 0 && c < level.cols) {
                canvas[static_cast<std::size_t>(r - minR)][static_cast<std::size_t>(c - minC)] = '.';
            }
        }
    }
    for (const Vec2i& cell : level.road.path) {
        if (cell.r < minR || cell.r > maxR || cell.c < minC || cell.c > maxC) {
            continue;
        }
        canvas[static_cast<std::size_t>(cell.r - minR)][static_cast<std::size_t>(cell.c - minC)] = '#';
    }
    for (int r = 0; r < level.rows; ++r) {
        for (int c = 0; c < level.cols; ++c) {
            const FruitCell& cell = state.cells[static_cast<std::size_t>(r * level.cols + c)];
            if (!cell.present || cell.color.empty()) {
                continue;
            }
            char glyph = cell.color[0];
            if (glyph >= 'a' && glyph <= 'z') {
                glyph = static_cast<char>(glyph - 'a' + 'A');
            }
            char& target = canvas[static_cast<std::size_t>(r - minR)][static_cast<std::size_t>(c - minC)];
            target = cell.settleLeft > 0 ? static_cast<char>(glyph + 32) : glyph;
        }
    }

    std::string out;
    out += "level=" + level.id + " seed=" + std::to_string(state.seed) + " tick=" + std::to_string(state.tick) +
           " rows=" + std::to_string(level.rows) + " cols=" + std::to_string(level.cols) + "\n";
    out += "     ";
    for (int c = minC; c <= maxC; ++c) {
        const int value = ((c % 10) + 10) % 10;
        out += static_cast<char>('0' + value);
    }
    out += "\n";
    for (int r = minR; r <= maxR; ++r) {
        const int label = r;
        out += "r" + std::to_string(label);
        if (label < 10) {
            out += ' ';
        }
        out += "  ";
        out += canvas[static_cast<std::size_t>(r - minR)];
        out += "\n";
    }
    out += "legend: '#'=road  A-Z=fruit  a-z=fruit settling  '.'=empty grid  ' '=outside\n";
    return out;
}

}  // namespace fc
