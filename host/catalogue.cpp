#include "catalogue.h"

#include <fstream>
#include <sstream>
#include <utility>

#include "fc/fruit_gen.h"
#include "fc/json.h"
#include "fc/world.h"

namespace fc {
namespace {

LevelError makeError(const std::string& code, const std::string& path, const std::string& message) {
    LevelError error;
    error.code = code;
    error.path = path;
    error.message = message;
    return error;
}

std::string readTextFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::string joinPath(const std::string& root, const std::string& relative) {
    if (relative.empty()) {
        return root;
    }
    const char last = root.empty() ? '\0' : root[root.size() - 1];
    if (last == '/' || last == '\\') {
        return root + relative;
    }
    return root + "/" + relative;
}

bool readString(const json::Value& object, const char* key, std::string& out) {
    const json::Value* value = object.find(key);
    if (value == nullptr || !value->isString()) {
        return false;
    }
    out = value->asString();
    return true;
}

bool readInt(const json::Value& object, const char* key, int& out) {
    const json::Value* value = object.find(key);
    if (value == nullptr || !value->isNumber()) {
        return false;
    }
    out = static_cast<int>(value->asNumber());
    return true;
}

}  // namespace

int Catalogue::indexOf(const std::string& levelId) const {
    for (std::size_t i = 0; i < levels.size(); ++i) {
        if (levels[i].id == levelId) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

CatalogueResult loadCatalogue(const std::string& jsonText, const std::string& root) {
    CatalogueResult result;
    json::Value document;
    json::ParseError parseError;
    if (!json::parseWithBom(jsonText, document, parseError)) {
        result.error = makeError("E1019", "catalogue", "catalogue is not valid JSON: " + parseError.message);
        return result;
    }
    if (!document.isObject()) {
        result.error = makeError("E1019", "catalogue", "catalogue must be a JSON object");
        return result;
    }
    Catalogue catalogue;
    if (const json::Value* version = document.find("version")) {
        if (version->isNumber()) {
            catalogue.version = static_cast<int>(version->asNumber());
        }
    }
    const json::Value* levels = document.find("levels");
    if (levels == nullptr || !levels->isArray() || levels->items().empty()) {
        result.error = makeError("E1019", "levels", "catalogue must list at least one level");
        return result;
    }

    std::size_t index = 0;
    for (const auto& item : levels->items()) {
        const std::string path = "levels[" + std::to_string(index) + "]";
        if (!item.isObject()) {
            result.error = makeError("E1019", path, "entry must be an object");
            return result;
        }
        CatalogueEntry entry;
        if (!readString(item, "id", entry.id)) {
            result.error = makeError("E1019", path + ".id", "id is required");
            return result;
        }
        if (!readString(item, "file", entry.file)) {
            result.error = makeError("E1019", path + ".file", "file is required");
            return result;
        }
        readString(item, "name", entry.name);
        readString(item, "subtitle", entry.subtitle);
        readString(item, "tag", entry.tag);
        readString(item, "accent", entry.accent);
        readInt(item, "order", entry.order);
        readInt(item, "difficulty", entry.difficulty);
        if (entry.order <= 0) {
            result.error = makeError("E1019", path + ".order", "order must be a positive integer");
            return result;
        }
        if (entry.difficulty < 1 || entry.difficulty > 3) {
            result.error = makeError("E1019", path + ".difficulty", "difficulty must be in [1,3]");
            return result;
        }
        for (const auto& existing : catalogue.levels) {
            if (existing.id == entry.id) {
                result.error = makeError("E1019", path + ".id", "duplicate level id '" + entry.id + "'");
                return result;
            }
            if (existing.order == entry.order) {
                result.error = makeError("E1019", path + ".order", "duplicate order " + std::to_string(entry.order));
                return result;
            }
        }

        const std::string levelPath = joinPath(root, entry.file);
        const std::string levelText = readTextFile(levelPath);
        if (levelText.empty()) {
            result.error = makeError("E1019", path + ".file", "cannot read level file " + entry.file);
            return result;
        }
        const LevelParseResult parsed = parseLevel(levelText);
        if (!parsed.ok) {
            LevelError error = parsed.error;
            error.message = entry.file + ": " + error.message;
            result.error = error;
            return result;
        }
        if (parsed.level.id != entry.id) {
            result.error = makeError("E1019", path + ".id",
                                     "level file id '" + parsed.level.id + "' does not match catalogue id '" +
                                         entry.id + "'");
            return result;
        }
        entry.level = parsed.level;
        catalogue.levels.push_back(std::move(entry));
        ++index;
    }

    // 主界面按 `order` 展示卡片，因此目录始终保持有序。
    for (std::size_t i = 1; i < catalogue.levels.size(); ++i) {
        for (std::size_t k = i; k > 0 && catalogue.levels[k - 1].order > catalogue.levels[k].order; --k) {
            std::swap(catalogue.levels[k - 1], catalogue.levels[k]);
        }
    }
    result.ok = true;
    result.catalogue = std::move(catalogue);
    return result;
}

std::string ringPresentationJson(const LevelDefinition& level) {
    const PresentationDef::Ring& ring = level.presentation.ring;
    if (!ring.enabled()) {
        return std::string();
    }
    std::string out = "{\"centre\":[" + formatFixed3(ring.centre.x) + "," +
                      formatFixed3(ring.centre.y) + "]";
    out += ",\"sectors\":" + std::to_string(ring.sectors);
    out += ",\"startDeg\":" + formatFixed3(ring.startDeg);
    out += ",\"sweepDeg\":" + formatFixed3(ring.sweepDeg);
    out += ",\"innerRadius\":" + formatFixed3(ring.innerRadius);
    out += ",\"outerRadius\":" + formatFixed3(ring.outerRadius);
    out += ",\"gapDeg\":" + formatFixed3(ring.gapDeg);
    out += "}";
    return out;
}

std::string previewJson(const LevelDefinition& level) {
    if (level.usesCurves()) {
        // 曲线关卡：缩略图使用采样得到的中线以及扁平的水果列表，
        // 而不是网格布局。
        GameState state;
        std::string error;
        buildCurveState(level, state, error);
        std::string out;
        out += "{\"rows\":" + std::to_string(level.rows);
        out += ",\"cols\":" + std::to_string(level.cols);
        out += ",\"road\":[],\"polyline\":" + polylineJson(state.roadCurve);
        out += ",\"fruits\":[";
        bool first = true;
        for (const auto& slot : state.fruitSlots) {
            if (!slot.present) {
                continue;
            }
            if (!first) {
                out += ",";
            }
            first = false;
            out += "[" + formatFixed3(slot.world.y) + "," + formatFixed3(slot.world.x) + "," +
                   jsonQuote(slot.color) + "]";
        }
        out += "],\"island\":[]";
        const std::string ring = ringPresentationJson(level);
        if (!ring.empty()) {
            out += ",\"ring\":" + ring;
        }
        out += "}";
        return out;
    }
    std::vector<FruitCell> cells;
    std::string error;
    if (!generateFruits(level, level.seed, cells, error)) {
        cells.assign(static_cast<std::size_t>(level.rows) * static_cast<std::size_t>(level.cols), FruitCell());
    }
    std::string out;
    out += "{\"rows\":" + std::to_string(level.rows);
    out += ",\"cols\":" + std::to_string(level.cols);
    out += ",\"road\":[";
    const std::vector<Vec2i>& track = level.road.trackCells();
    for (std::size_t i = 0; i < track.size(); ++i) {
        if (i > 0) {
            out += ",";
        }
        out += "[" + std::to_string(track[i].r) + "," + std::to_string(track[i].c) + "]";
    }
    out += "],\"fruits\":[";
    bool first = true;
    for (int r = 0; r < level.rows; ++r) {
        for (int c = 0; c < level.cols; ++c) {
            const FruitCell& cell = cells[static_cast<std::size_t>(r * level.cols + c)];
            if (!cell.present) {
                continue;
            }
            if (!first) {
                out += ",";
            }
            first = false;
            out += "[" + std::to_string(r) + "," + std::to_string(c) + "," + jsonQuote(cell.color) + "]";
        }
    }
    out += "],\"island\":[";
    for (std::size_t i = 0; i < level.presentation.island.size(); ++i) {
        if (i > 0) {
            out += ",";
        }
        out += "[" + std::to_string(level.presentation.island[i].r) + "," +
               std::to_string(level.presentation.island[i].c) + "]";
    }
    out += "]}";
    return out;
}

std::string catalogueJson(const Catalogue& catalogue, int currentIndex) {
    std::string out;
    out += "{\"version\":" + std::to_string(catalogue.version);
    out += ",\"current\":";
    if (currentIndex >= 0 && currentIndex < static_cast<int>(catalogue.levels.size())) {
        out += "{\"id\":" + jsonQuote(catalogue.levels[static_cast<std::size_t>(currentIndex)].id) + "}";
    } else {
        out += "null";
    }
    out += ",\"levels\":[";
    for (std::size_t i = 0; i < catalogue.levels.size(); ++i) {
        const CatalogueEntry& entry = catalogue.levels[i];
        if (i > 0) {
            out += ",";
        }
        out += "{\"id\":" + jsonQuote(entry.id);
        out += ",\"order\":" + std::to_string(entry.order);
        out += ",\"name\":" + jsonQuote(entry.name);
        out += ",\"subtitle\":" + jsonQuote(entry.subtitle);
        out += ",\"tag\":" + jsonQuote(entry.tag);
        out += ",\"difficulty\":" + std::to_string(entry.difficulty);
        out += ",\"accent\":" + jsonQuote(entry.accent);
        out += ",\"file\":" + jsonQuote(entry.file);
        out += ",\"preview\":" + previewJson(entry.level);
        out += "}";
    }
    out += "]}";
    return out;
}

}  // namespace fc
