#include "fc/level.h"

#include <cmath>
#include <cstddef>

#include "fc/json.h"

namespace fc {
namespace {

constexpr int kMaxGridSide = 64;
constexpr int kRoadMargin = 2;

std::string indexPath(const std::string& base, std::size_t index) {
    return base + "[" + std::to_string(index) + "]";
}

LevelError makeError(const std::string& code, const std::string& path, const std::string& message) {
    LevelError error;
    error.code = code;
    error.path = path;
    error.message = message;
    return error;
}

LevelError syntaxError(const json::ParseError& parseError) {
    LevelError error = makeError("E1012", "$", parseError.message);
    error.line = parseError.line;
    error.column = parseError.column;
    return error;
}

bool readInt(const json::Value& value, int& out) {
    if (!value.isNumber()) {
        return false;
    }
    out = static_cast<int>(std::lround(value.asNumber()));
    return true;
}

bool readDouble(const json::Value& value, double& out) {
    if (!value.isNumber()) {
        return false;
    }
    out = value.asNumber();
    return true;
}

bool readStringArray(const json::Value& value, std::vector<std::string>& out, const std::string& path,
                     LevelError& error) {
    if (!value.isArray()) {
        error = makeError("E1012", path, "expected an array of strings");
        return false;
    }
    out.clear();
    std::size_t index = 0;
    for (const auto& item : value.items()) {
        if (!item.isString()) {
            error = makeError("E1012", indexPath(path, index), "expected a string");
            return false;
        }
        out.push_back(item.asString());
        ++index;
    }
    return true;
}

bool parsePath(const json::Value& value, std::vector<Vec2i>& out, LevelError& error) {
    if (!value.isArray()) {
        error = makeError("E1012", "road.path", "road.path must be an array of [row, col] pairs");
        return false;
    }
    out.clear();
    std::size_t index = 0;
    for (const auto& item : value.items()) {
        if (!item.isArray() || item.items().size() != 2 || !item.items()[0].isNumber() ||
            !item.items()[1].isNumber()) {
            error = makeError("E1012", indexPath("road.path", index), "expected [row, col]");
            return false;
        }
        int r = 0;
        int c = 0;
        readInt(item.items()[0], r);
        readInt(item.items()[1], c);
        out.push_back(Vec2i{r, c});
        ++index;
    }
    return true;
}

bool parseCellList(const json::Value& value, std::vector<Vec2i>& out, const std::string& path,
                   LevelError& error) {    if (!value.isArray()) {
        error = makeError("E1012", path, "expected an array of [row, col] pairs");
        return false;
    }
    out.clear();
    std::size_t index = 0;
    for (const auto& item : value.items()) {
        if (!item.isArray() || item.items().size() != 2 || !item.items()[0].isNumber() ||
            !item.items()[1].isNumber()) {
            error = makeError("E1012", indexPath(path, index), "expected [row, col]");
            return false;
        }
        int r = 0;
        int c = 0;
        readInt(item.items()[0], r);
        readInt(item.items()[1], c);
        out.push_back(Vec2i{r, c});
        ++index;
    }
    return true;
}

bool parseLayers(const json::Value& value, FruitDef& fruits, LevelError& error) {
    if (!value.isArray() || value.items().empty()) {
        error = makeError("E1018", "fruits.layers", "fruits.layers must be a non-empty array");
        return false;
    }
    fruits.layers.clear();
    std::size_t layerIndex = 0;
    for (const auto& item : value.items()) {
        const std::string path = indexPath("fruits.layers", layerIndex);
        if (!item.isObject()) {
            error = makeError("E1012", path, "layer must be an object");
            return false;
        }
        LayerDef layer;
        const json::Value* size = item.find("size");
        if (size != nullptr && !readDouble(*size, layer.size)) {
            error = makeError("E1012", path + ".size", "size must be a number");
            return false;
        }
        const json::Value* color = item.find("color");
        if (color == nullptr || !color->isString()) {
            error = makeError("E1012", path + ".color", "layer color must be a string");
            return false;
        }
        layer.color = color->asString();
        const json::Value* cells = item.find("cells");
        if (cells == nullptr) {
            error = makeError("E1012", path + ".cells", "layer cells are required");
            return false;
        }
        if (!parseCellList(*cells, layer.cells, path + ".cells", error)) {
            return false;
        }
        fruits.layers.push_back(std::move(layer));
        ++layerIndex;
    }
    return true;
}

bool parseExplicitCells(const json::Value& value, LevelDefinition& level, LevelError& error) {    if (!value.isArray() || value.items().size() != static_cast<std::size_t>(level.rows)) {
        error = makeError("E1012", "fruits.cells", "fruits.cells must be a rows x cols matrix");
        return false;
    }
    level.fruits.explicitCells.assign(static_cast<std::size_t>(level.rows) * static_cast<std::size_t>(level.cols),
                                      ColorId());
    for (int r = 0; r < level.rows; ++r) {
        const auto& row = value.items()[static_cast<std::size_t>(r)];
        if (!row.isArray() || row.items().size() != static_cast<std::size_t>(level.cols)) {
            error = makeError("E1012", indexPath("fruits.cells", static_cast<std::size_t>(r)),
                              "row must have exactly cols entries");
            return false;
        }
        for (int c = 0; c < level.cols; ++c) {
            const auto& cell = row.items()[static_cast<std::size_t>(c)];
            if (cell.isNull()) {
                level.fruits.explicitCells[static_cast<std::size_t>(r * level.cols + c)].clear();
            } else if (cell.isString()) {
                level.fruits.explicitCells[static_cast<std::size_t>(r * level.cols + c)] = cell.asString();
            } else {
                error = makeError("E1012", indexPath(indexPath("fruits.cells", static_cast<std::size_t>(r)),
                                                     static_cast<std::size_t>(c)),
                                  "cell must be a color string or null");
                return false;
            }
        }
    }
    level.fruits.explicitLayout = true;
    return true;
}

bool parseVehicles(const json::Value& value, LevelDefinition& level, LevelError& error) {
    if (!value.isObject()) {
        error = makeError("E1012", "vehicles", "vehicles must be an object keyed by vehicle id");
        return false;
    }
    std::vector<std::string> seen;
    for (const auto& member : value.members()) {
        for (const auto& existing : seen) {
            if (existing == member.first) {
                error = makeError("E1003", "vehicles." + member.first, "duplicate vehicle id '" + member.first + "'");
                return false;
            }
        }
        seen.push_back(member.first);
        if (!member.second.isObject()) {
            error = makeError("E1012", "vehicles." + member.first, "vehicle must be an object");
            return false;
        }
        VehicleDef def;
        const json::Value* color = member.second.find("color");
        if (color == nullptr || !color->isString()) {
            error = makeError("E1012", "vehicles." + member.first + ".color", "vehicle color must be a string");
            return false;
        }
        def.color = color->asString();
        const json::Value* capacity = member.second.find("capacity");
        if (capacity != nullptr && !readInt(*capacity, def.capacity)) {
            error = makeError("E1012", "vehicles." + member.first + ".capacity", "capacity must be a number");
            return false;
        }
        level.vehicles.emplace_back(member.first, def);
    }
    return true;
}

bool parseLanes(const json::Value& value, ParkingDef& parking, LevelError& error) {
    if (!value.isArray() || value.items().empty()) {
        error = makeError("E1012", "parking.lanes", "parking.lanes must be a non-empty array");
        return false;
    }
    parking.lanes.clear();
    std::size_t laneIndex = 0;
    for (const auto& item : value.items()) {
        const std::string path = indexPath("parking.lanes", laneIndex);
        if (!item.isObject()) {
            error = makeError("E1012", path, "lane must be an object");
            return false;
        }
        LaneDef lane;
        const json::Value* color = item.find("color");
        if (color == nullptr || !color->isString()) {
            error = makeError("E1012", path + ".color", "lane color must be a string");
            return false;
        }
        lane.color = color->asString();
        const json::Value* queue = item.find("queue");
        if (queue != nullptr) {
            if (!readStringArray(*queue, lane.queue, path + ".queue", error)) {
                return false;
            }
        }
        parking.lanes.push_back(std::move(lane));
        ++laneIndex;
    }
    return true;
}

// 关卡单元格坐标中的 [[x,y], ...]（曲线控制点，任务书 5.3）。
bool parsePointList(const json::Value& value, std::vector<Vec2d>& out, const std::string& path,
                    LevelError& error) {
    if (!value.isArray()) {
        error = makeError("E1012", path, "expected an array of [x, y] points");
        return false;
    }
    out.clear();
    std::size_t index = 0;
    for (const auto& item : value.items()) {
        if (!item.isArray() || item.items().size() != 2 || !item.items()[0].isNumber() ||
            !item.items()[1].isNumber()) {
            error = makeError("E1012", indexPath(path, index), "expected [x, y] numbers");
            return false;
        }
        out.push_back(Vec2d{item.items()[0].asNumber(), item.items()[1].asNumber()});
        ++index;
    }
    return true;
}

bool parseCurveLayers(const json::Value& value, LevelDefinition& level, LevelError& error) {
    if (!value.isArray() || value.items().empty()) {
        error = makeError("E2010", "fruitLayers", "fruitLayers must be a non-empty array");
        return false;
    }
    level.fruitLayers.clear();
    std::size_t layerIndex = 0;
    for (const auto& item : value.items()) {
        const std::string path = indexPath("fruitLayers", layerIndex);
        if (!item.isObject()) {
            error = makeError("E1012", path, "fruit layer must be an object");
            return false;
        }
        CurveLayerDef layer;
        const json::Value* id = item.find("id");
        if (id == nullptr || !id->isString() || id->asString().empty()) {
            error = makeError("E1012", path + ".id", "fruit layer id is required");
            return false;
        }
        layer.id = id->asString();
        for (const auto& existing : level.fruitLayers) {
            if (existing.id == layer.id) {
                error = makeError("E1003", path + ".id", "duplicate fruit layer id '" + layer.id + "'");
                return false;
            }
        }
        const json::Value* controlPoints = item.find("controlPoints");
        if (controlPoints == nullptr) {
            error = makeError("E2001", path + ".controlPoints", "controlPoints are required");
            return false;
        }
        if (!parsePointList(*controlPoints, layer.controlPoints, path + ".controlPoints", error)) {
            return false;
        }
        if (const json::Value* closed = item.find("closed")) {
            if (!closed->isBool()) {
                error = makeError("E1012", path + ".closed", "closed must be a boolean");
                return false;
            }
            layer.closed = closed->asBool();
        }
        if (const json::Value* spacing = item.find("spacing")) {
            if (!readDouble(*spacing, layer.spacing)) {
                error = makeError("E1012", path + ".spacing", "spacing must be a number");
                return false;
            }
        }
        if (const json::Value* size = item.find("size")) {
            if (!readDouble(*size, layer.size)) {
                error = makeError("E1012", path + ".size", "size must be a number");
                return false;
            }
        }
        if (const json::Value* step = item.find("sampleStep")) {
            if (!readDouble(*step, layer.sampleStep)) {
                error = makeError("E2002", path + ".sampleStep", "sampleStep must be a number");
                return false;
            }
        }
        if (const json::Value* colors = item.find("colors")) {
            if (!readStringArray(*colors, layer.colors, path + ".colors", error)) {
                return false;
            }
        }
        if (layer.colors.empty()) {
            layer.colors = level.palette;
        }
        if (const json::Value* refill = item.find("refill")) {
            if (!refill->isObject()) {
                error = makeError("E1012", path + ".refill", "refill must be an object");
                return false;
            }
            const json::Value* feedsFrom = refill->find("feedsFrom");
            if (feedsFrom != nullptr) {
                if (!feedsFrom->isString()) {
                    error = makeError("E2004", path + ".refill.feedsFrom", "feedsFrom must be a layer id");
                    return false;
                }
                layer.feedsFrom = feedsFrom->asString();
            }
            const json::Value* mode = refill->find("mode");
            if (mode != nullptr) {
                if (!mode->isString()) {
                    error = makeError("E1012", path + ".refill.mode", "mode must be a string");
                    return false;
                }
                if (mode->asString() != "layerIn" && mode->asString() != "none") {
                    error = makeError("E2004", path + ".refill.mode",
                                      "curve layers only support mode layerIn or none");
                    return false;
                }
            }
        }
        if (const json::Value* feedsFrom = item.find("feedsFrom")) {
            if (!feedsFrom->isString()) {
                error = makeError("E2004", path + ".feedsFrom", "feedsFrom must be a layer id");
                return false;
            }
            layer.feedsFrom = feedsFrom->asString();
        }
        if (const json::Value* jitter = item.find("jitter")) {
            if (!jitter->isObject()) {
                error = makeError("E1012", path + ".jitter", "jitter must be an object");
                return false;
            }
            if (const json::Value* seed = jitter->find("seed")) {
                double seedValue = 0.0;
                if (!readDouble(*seed, seedValue) || seedValue < 0.0) {
                    error = makeError("E1012", path + ".jitter.seed", "seed must be a non-negative number");
                    return false;
                }
                layer.jitterSeed = static_cast<std::uint64_t>(seedValue);
            }
            if (const json::Value* offset = jitter->find("offset")) {
                if (!readDouble(*offset, layer.jitterOffset)) {
                    error = makeError("E1012", path + ".jitter.offset", "offset must be a number");
                    return false;
                }
            }
            if (const json::Value* sizeJitter = jitter->find("sizeJitter")) {
                if (!readDouble(*sizeJitter, layer.jitterSize)) {
                    error = makeError("E1012", path + ".jitter.sizeJitter", "sizeJitter must be a number");
                    return false;
                }
            }
        }
        level.fruitLayers.push_back(std::move(layer));
        ++layerIndex;
    }
    return true;
}

// 预留技能：items[]（任务书 5.8 / 8 T-30）。
bool parseItems(const json::Value& value, LevelDefinition& level, LevelError& error) {
    if (!value.isArray()) {
        error = makeError("E1012", "items", "items must be an array");
        return false;
    }
    level.items.clear();
    std::size_t index = 0;
    for (const auto& item : value.items()) {
        const std::string path = indexPath("items", index);
        if (!item.isObject()) {
            error = makeError("E1012", path, "item must be an object");
            return false;
        }
        ItemDef def;
        const json::Value* id = item.find("id");
        if (id == nullptr || !id->isString() || id->asString().empty()) {
            error = makeError("E1012", path + ".id", "item id is required");
            return false;
        }
        def.id = id->asString();
        for (const auto& existing : level.items) {
            if (existing.id == def.id) {
                error = makeError("E1003", path + ".id", "duplicate item id '" + def.id + "'");
                return false;
            }
        }
        const json::Value* kind = item.find("kind");
        if (kind == nullptr || !kind->isString()) {
            error = makeError("E2030", path + ".kind", "item kind is required");
            return false;
        }
        def.kindText = kind->asString();
        if (!parseEffectKind(def.kindText, def.kind)) {
            error = makeError("E2030", path + ".kind", "unknown item kind '" + def.kindText + "'");
            return false;
        }
        if (const json::Value* charges = item.find("charges")) {
            if (!readInt(*charges, def.charges)) {
                error = makeError("E1012", path + ".charges", "charges must be a number");
                return false;
            }
        }
        if (def.charges < 0) {
            error = makeError("E2031", path + ".charges", "charges must be >= 0");
            return false;
        }
        if (const json::Value* params = item.find("params")) {
            if (!params->isObject()) {
                error = makeError("E1012", path + ".params", "params must be an object");
                return false;
            }
            if (const json::Value* radius = params->find("radius")) {
                if (!readDouble(*radius, def.params.radius)) {
                    error = makeError("E1012", path + ".params.radius", "radius must be a number");
                    return false;
                }
            }
            if (const json::Value* palette = params->find("palette")) {
                if (!readStringArray(*palette, def.params.palette, path + ".params.palette", error)) {
                    return false;
                }
            }
        }
        level.items.push_back(std::move(def));
        ++index;
    }
    return true;
}

}  // namespace

const char* toString(RefillDirection dir) {
    switch (dir) {
        case RefillDirection::Down: return "down";
        case RefillDirection::Up: return "up";
        case RefillDirection::Left: return "left";
        case RefillDirection::Right: return "right";
        case RefillDirection::RingIn: return "ringIn";
        case RefillDirection::RingOut: return "ringOut";
        case RefillDirection::LayerIn: return "layerIn";
        case RefillDirection::RegionIn: return "regionIn";
    }
    return "down";
}

const char* toString(Side side) {
    switch (side) {
        case Side::Up: return "up";
        case Side::Down: return "down";
        case Side::Left: return "left";
        case Side::Right: return "right";
    }
    return "up";
}

bool parseRefillDirection(const std::string& text, RefillDirection& out) {
    if (text == "down") { out = RefillDirection::Down; return true; }
    if (text == "up") { out = RefillDirection::Up; return true; }
    if (text == "left") { out = RefillDirection::Left; return true; }
    if (text == "right") { out = RefillDirection::Right; return true; }
    if (text == "ringIn") { out = RefillDirection::RingIn; return true; }
    if (text == "ringOut") { out = RefillDirection::RingOut; return true; }
    if (text == "layerIn") { out = RefillDirection::LayerIn; return true; }
    if (text == "regionIn") { out = RefillDirection::RegionIn; return true; }
    return false;
}

bool parseSide(const std::string& text, Side& out) {
    if (text == "up") { out = Side::Up; return true; }
    if (text == "down") { out = Side::Down; return true; }
    if (text == "left") { out = Side::Left; return true; }
    if (text == "right") { out = Side::Right; return true; }
    return false;
}

const char* toString(LaneSide side) {
    switch (side) {
        case LaneSide::Left: return "left";
        case LaneSide::Right: return "right";
        case LaneSide::None: return "none";
    }
    return "none";
}

bool parseLaneSide(const std::string& text, LaneSide& out) {
    if (text == "left") { out = LaneSide::Left; return true; }
    if (text == "right") { out = LaneSide::Right; return true; }
    if (text == "none") { out = LaneSide::None; return true; }
    return false;
}

const char* toString(Mechanism mechanism) { return mechanism == Mechanism::Curves ? "curves" : "grid"; }

bool parseMechanism(const std::string& text, Mechanism& out) {
    if (text == "grid") { out = Mechanism::Grid; return true; }
    if (text == "curves") { out = Mechanism::Curves; return true; }
    return false;
}

const VehicleDef* LevelDefinition::findVehicle(const EntityId& vehicleId) const {
    for (const auto& entry : vehicles) {
        if (entry.first == vehicleId) {
            return &entry.second;
        }
    }
    return nullptr;
}

bool LevelDefinition::isLaneRoad() const {
    if (road.path.size() < 2) {
        return true;
    }
    const Vec2i& first = road.path.front();
    const Vec2i& last = road.path.back();
    return (last.c - first.c) >= (last.r - first.r);
}

const std::vector<ColorId>& LevelDefinition::fruitColors() const { return fruits.colors; }

const CurveLayerDef* LevelDefinition::findFruitLayer(const std::string& layerId) const {
    for (const auto& layer : fruitLayers) {
        if (layer.id == layerId) {
            return &layer;
        }
    }
    return nullptr;
}

int LevelDefinition::layerOf(Vec2i cell) const {
    for (std::size_t k = 0; k < fruits.layers.size(); ++k) {
        for (const Vec2i& member : fruits.layers[k].cells) {
            if (member == cell) {
                return static_cast<int>(k);
            }
        }
    }
    return -1;
}

int LevelDefinition::initialFruitCount() const {
    if (!fruits.layers.empty()) {
        int count = 0;
        for (const auto& layer : fruits.layers) {
            count += static_cast<int>(layer.cells.size());
        }
        return count;
    }
    if (fruits.explicitLayout) {
        int count = 0;
        for (const auto& cell : fruits.explicitCells) {
            if (!cell.empty()) {
                ++count;
            }
        }
        return count;
    }
    return rows * cols;
}

LevelParseResult parseLevel(const std::string& jsonText) {
    LevelParseResult result;
    json::Value root;
    json::ParseError parseError;
    if (!json::parseWithBom(jsonText, root, parseError)) {
        result.ok = false;
        result.error = syntaxError(parseError);
        return result;
    }
    if (!root.isObject()) {
        result.error = makeError("E1012", "$", "level document must be a JSON object");
        return result;
    }

    LevelDefinition level;
    const json::Value* value = nullptr;

    if ((value = root.find("schemaVersion")) != nullptr) {
        if (!readInt(*value, level.schemaVersion)) {
            result.error = makeError("E1012", "schemaVersion", "must be a number");
            return result;
        }
    }
    if ((value = root.find("id")) != nullptr) {
        if (!value->isString()) {
            result.error = makeError("E1012", "id", "must be a string");
            return result;
        }
        level.id = value->asString();
    }
    if ((value = root.find("name")) != nullptr && value->isString()) {
        level.name = value->asString();
    }
    if ((value = root.find("seed")) != nullptr) {
        double seedValue = 0.0;
        if (!readDouble(*value, seedValue) || seedValue < 0.0) {
            result.error = makeError("E1012", "seed", "must be a non-negative number");
            return result;
        }
        level.seed = static_cast<std::uint64_t>(seedValue);
    }

    // 机制："grid"（v1 及基于网格的 v2 关卡的默认值）或
    // "curves"（自由形态的道路与水果层，任务书 5.3 / T-41）。
    bool mechanismExplicit = false;
    if ((value = root.find("mechanism")) != nullptr) {
        if (!value->isString() || !parseMechanism(value->asString(), level.mechanism)) {
            result.error = makeError("E1012", "mechanism", "mechanism must be \"grid\" or \"curves\"");
            return result;
        }
        mechanismExplicit = true;
    }

    if ((value = root.find("grid")) != nullptr) {
        if (!value->isObject()) {
            result.error = makeError("E1012", "grid", "must be an object");
            return result;
        }
        if (const json::Value* rows = value->find("rows")) {
            if (!readInt(*rows, level.rows)) {
                result.error = makeError("E1012", "grid.rows", "must be a number");
                return result;
            }
        }
        if (const json::Value* cols = value->find("cols")) {
            if (!readInt(*cols, level.cols)) {
                result.error = makeError("E1012", "grid.cols", "must be a number");
                return result;
            }
        }
    }

    if ((value = root.find("palette")) != nullptr) {
        if (!readStringArray(*value, level.palette, "palette", result.error)) {
            return result;
        }
    }
    if (level.palette.empty()) {
        result.error = makeError("E1012", "palette", "palette must contain at least one color id");
        return result;
    }

    if ((value = root.find("fruits")) != nullptr) {
        if (!value->isObject()) {
            result.error = makeError("E1012", "fruits", "must be an object");
            return result;
        }
        const json::Value& fruits = *value;
        if (const json::Value* generator = fruits.find("generator")) {
            if (!generator->isString()) {
                result.error = makeError("E1012", "fruits.generator", "must be a string");
                return result;
            }
            level.fruits.generator = generator->asString();
        }
        if (const json::Value* runLength = fruits.find("runLength")) {
            if (!runLength->isArray() || runLength->items().size() != 2) {
                result.error = makeError("E1010", "fruits.runLength", "runLength must be [low, high]");
                return result;
            }
            if (!readInt(runLength->items()[0], level.fruits.runLow) ||
                !readInt(runLength->items()[1], level.fruits.runHigh)) {
                result.error = makeError("E1010", "fruits.runLength", "runLength entries must be numbers");
                return result;
            }
        }
        if (const json::Value* colors = fruits.find("colors")) {
            if (!readStringArray(*colors, level.fruits.colors, "fruits.colors", result.error)) {
                return result;
            }
        }
        if (level.fruits.colors.empty()) {
            level.fruits.colors = level.palette;
        }
        if (const json::Value* refill = fruits.find("refillDirection")) {
            if (!refill->isString()) {
                result.error = makeError("E1011", "fruits.refillDirection", "must be a string");
                return result;
            }
            if (!parseRefillDirection(refill->asString(), level.fruits.refill)) {
                result.error = makeError("E1011", "fruits.refillDirection",
                                         "unknown refill direction '" + refill->asString() + "'");
                return result;
            }
        }
        if (const json::Value* cells = fruits.find("cells")) {
            if (!parseExplicitCells(*cells, level, result.error)) {
                return result;
            }
            level.fruits.generator = "explicit";
        }
        if (const json::Value* layers = fruits.find("layers")) {
            if (!parseLayers(*layers, level.fruits, result.error)) {
                return result;
            }
            level.fruits.generator = "layers";
            level.fruits.explicitLayout = false;
            level.fruits.explicitCells.clear();
        }
    }

    if ((value = root.find("road")) != nullptr) {
        if (!value->isObject()) {
            result.error = makeError("E1012", "road", "must be an object");
            return result;
        }
        const json::Value& road = *value;
        // 曲线道路：控制点 + 重采样步长（任务书 5.3）。
        if (const json::Value* controlPoints = road.find("controlPoints")) {
            if (!parsePointList(*controlPoints, level.road.controlPoints, "road.controlPoints",
                                result.error)) {
                return result;
            }
        }
        const bool curveRoad = level.mechanism == Mechanism::Curves || !level.road.controlPoints.empty();
        if (const json::Value* closed = road.find("closed")) {
            if (!closed->isBool()) {
                result.error = makeError("E1012", "road.closed", "closed must be a boolean");
                return result;
            }
            level.road.closed = closed->asBool();
        }
        if (const json::Value* step = road.find("sampleStep")) {
            if (!readDouble(*step, level.road.sampleStep)) {
                result.error = makeError("E2002", "road.sampleStep", "sampleStep must be a number");
                return result;
            }
        }
        if (const json::Value* slack = road.find("contactSlack")) {
            if (!readDouble(*slack, level.road.contactSlack)) {
                result.error = makeError("E1012", "road.contactSlack", "contactSlack must be a number");
                return result;
            }
        }
        if (const json::Value* reach = road.find("contactReach")) {
            if (!readDouble(*reach, level.road.contactReach)) {
                result.error = makeError("E1012", "road.contactReach", "contactReach must be a number");
                return result;
            }
        }
        if (const json::Value* offset = road.find("offset")) {
            if (!readDouble(*offset, level.road.offset)) {
                result.error = makeError("E1012", "road.offset", "offset must be a number");
                return result;
            }
        }
        const json::Value* path = road.find("path");
        if (path != nullptr) {
            if (!parsePath(*path, level.road.path, result.error)) {
                return result;
            }
        } else if (!curveRoad) {
            result.error = makeError("E1012", "road.path", "road.path is required");
            return result;
        }
        if (const json::Value* track = road.find("track")) {
            if (!parseCellList(*track, level.road.track, "road.track", result.error)) {
                return result;
            }
        }
        if (const json::Value* sides = road.find("collectSides")) {
            if (!sides->isArray() || sides->items().empty()) {
                result.error = makeError("E1017", "road.collectSides", "collectSides must be a non-empty array");
                return result;
            }
            std::size_t index = 0;
            for (const auto& item : sides->items()) {
                LaneSide side = LaneSide::Left;
                if (!item.isString() || !parseLaneSide(item.asString(), side)) {
                    result.error = makeError("E1017", indexPath("road.collectSides", index),
                                             "collectSides entries must be left/right/none");
                    return result;
                }
                level.road.collectSides.push_back(side);
                ++index;
            }
        }
        if (const json::Value* laneCycleValue = road.find("laneCycle")) {
            if (!laneCycleValue->isArray()) {
                result.error = makeError("E1017", "road.laneCycle", "laneCycle must be an array");
                return result;
            }
            for (const auto& item : laneCycleValue->items()) {
                int laneValue = 0;
                if (!readInt(item, laneValue)) {
                    result.error = makeError("E1017", "road.laneCycle", "laneCycle entries must be numbers");
                    return result;
                }
                level.road.laneCycle.push_back(laneValue);
            }
        }
        if (const json::Value* bend = road.find("bendCollectsBoth")) {
            if (!bend->isBool()) {
                result.error = makeError("E1012", "road.bendCollectsBoth", "must be a boolean");
                return result;
            }
            level.road.bendCollectsBoth = bend->asBool();
        }
        if (const json::Value* width = road.find("width")) {
            if (!readInt(*width, level.road.width)) {
                result.error = makeError("E1012", "road.width", "must be a number");
                return result;
            }
        }
        if (const json::Value* offset = road.find("laneOffset")) {
            if (!readInt(*offset, level.road.laneOffset)) {
                result.error = makeError("E1012", "road.laneOffset", "must be a number");
                return result;
            }
        }
        if (const json::Value* side = road.find("collectSide")) {
            if (curveRoad) {
                LaneSide laneSide = LaneSide::Left;
                if (!side->isString() || !parseLaneSide(side->asString(), laneSide)) {
                    result.error = makeError("E1017", "road.collectSide",
                                             "curve roads use collectSide left/right/none");
                    return result;
                }
                if (level.road.collectSides.empty()) {
                    level.road.collectSides.push_back(laneSide);
                } else {
                    level.road.collectSides[0] = laneSide;
                }
            } else if (!side->isString() || !parseSide(side->asString(), level.road.collectSide)) {
                result.error = makeError("E1012", "road.collectSide", "collectSide must be up/down/left/right");
                return result;
            }
        }
        if (const json::Value* carLength = road.find("carLength")) {
            if (!readDouble(*carLength, level.road.carLength)) {
                result.error = makeError("E1012", "road.carLength", "must be a number");
                return result;
            }
        }
        if (const json::Value* carGap = road.find("carGap")) {
            if (!readDouble(*carGap, level.road.carGap)) {
                result.error = makeError("E1012", "road.carGap", "must be a number");
                return result;
            }
        }
        if (const json::Value* speed = road.find("speed")) {
            if (!readDouble(*speed, level.road.speed)) {
                result.error = makeError("E1012", "road.speed", "must be a number");
                return result;
            }
        }
    } else {
        result.error = makeError("E1012", "road", "road is required");
        return result;
    }

    // 机制推断（任务书 9 T-41）：显式值优先；否则，
    // 带控制点的 schemaVersion 2 文档按曲线关卡处理。
    if (!mechanismExplicit) {
        level.mechanism = (level.schemaVersion >= 2 && !level.road.controlPoints.empty())
                              ? Mechanism::Curves
                              : Mechanism::Grid;
    }
    if (level.mechanism == Mechanism::Curves) {
        if (level.road.collectSides.empty()) {
            level.road.collectSides.push_back(LaneSide::Left);
        }
        if (level.road.laneCycle.empty()) {
            level.road.laneCycle.push_back(0);
        }
    }

    if ((value = root.find("fruitLayers")) != nullptr) {
        if (!parseCurveLayers(*value, level, result.error)) {
            return result;
        }
    }
    if ((value = root.find("items")) != nullptr) {
        if (!parseItems(*value, level, result.error)) {
            return result;
        }
    }
    if (level.mechanism == Mechanism::Curves && level.fruitLayers.empty()) {
        result.error = makeError("E2010", "fruitLayers", "curve levels require fruitLayers");
        return result;
    }

    if ((value = root.find("parking")) != nullptr) {
        if (!value->isObject()) {
            result.error = makeError("E1012", "parking", "must be an object");
            return result;
        }
        const json::Value& parking = *value;
        if (const json::Value* slots = parking.find("slotCount")) {
            if (!readInt(*slots, level.parking.slotCount)) {
                result.error = makeError("E1012", "parking.slotCount", "must be a number");
                return result;
            }
        }
        const json::Value* lanes = parking.find("lanes");
        if (lanes == nullptr) {
            result.error = makeError("E1012", "parking.lanes", "parking.lanes is required");
            return result;
        }
        if (!parseLanes(*lanes, level.parking, result.error)) {
            return result;
        }
    } else {
        result.error = makeError("E1012", "parking", "parking is required");
        return result;
    }

    if ((value = root.find("vehicles")) != nullptr) {
        if (!parseVehicles(*value, level, result.error)) {
            return result;
        }
    }

    if ((value = root.find("presentation")) != nullptr) {
        if (!value->isObject()) {
            result.error = makeError("E1012", "presentation", "must be an object");
            return result;
        }
        if (const json::Value* island = value->find("island")) {
            if (!parseCellList(*island, level.presentation.island, "presentation.island", result.error)) {
                return result;
            }
        }
        if (const json::Value* ring = value->find("ring")) {
            if (!ring->isObject()) {
                result.error = makeError("E1012", "presentation.ring", "must be an object");
                return result;
            }
            if (const json::Value* centre = ring->find("centre")) {
                if (!centre->isArray() || centre->items().size() != 2 ||
                    !centre->items()[0].isNumber() || !centre->items()[1].isNumber()) {
                    result.error = makeError("E1012", "presentation.ring.centre", "expected [x, y]");
                    return result;
                }
                level.presentation.ring.centre = Vec2d{centre->items()[0].asNumber(),
                                                       centre->items()[1].asNumber()};
            }
            const struct {
                const char* key;
                double* value;
            } doubles[] = {
                {"startDeg", &level.presentation.ring.startDeg},
                {"sweepDeg", &level.presentation.ring.sweepDeg},
                {"innerRadius", &level.presentation.ring.innerRadius},
                {"outerRadius", &level.presentation.ring.outerRadius},
                {"gapDeg", &level.presentation.ring.gapDeg},
            };
            for (const auto& field : doubles) {
                if (const json::Value* item = ring->find(field.key)) {
                    if (!readDouble(*item, *field.value)) {
                        result.error = makeError("E1012", "presentation.ring." + std::string(field.key),
                                                 "must be a number");
                        return result;
                    }
                }
            }
            if (const json::Value* sectors = ring->find("sectors")) {
                if (!readInt(*sectors, level.presentation.ring.sectors)) {
                    result.error = makeError("E1012", "presentation.ring.sectors", "must be a number");
                    return result;
                }
            }
            const PresentationDef::Ring& parsed = level.presentation.ring;
            if (parsed.sectors < 0 || parsed.sweepDeg < 0.0 || parsed.innerRadius < 0.0 ||
                parsed.outerRadius < parsed.innerRadius || parsed.gapDeg < 0.0) {
                result.error = makeError("E1012", "presentation.ring", "invalid ring geometry");
                return result;
            }
            if (parsed.enabled() && parsed.gapDeg >= parsed.sweepDeg / parsed.sectors) {
                result.error = makeError("E1012", "presentation.ring.gapDeg",
                                         "gapDeg must be smaller than one sector");
                return result;
            }
        }
    }

    if ((value = root.find("timing")) != nullptr) {
        if (!value->isObject()) {
            result.error = makeError("E1012", "timing", "must be an object");
            return result;
        }
        if (const json::Value* spawn = value->find("spawnIntervalMs")) {
            if (!readInt(*spawn, level.timing.spawnIntervalMs)) {
                result.error = makeError("E1012", "timing.spawnIntervalMs", "must be a number");
                return result;
            }
        }
        if (const json::Value* settle = value->find("settleTicks")) {
            if (!readInt(*settle, level.timing.settleTicks)) {
                result.error = makeError("E1012", "timing.settleTicks", "must be a number");
                return result;
            }
        }
    }

    LevelError error;
    if (!validateLevel(level, error)) {
        result.ok = false;
        result.error = error;
        result.level = level;
        return result;
    }
    result.ok = true;
    result.level = level;
    result.error = LevelError();
    return result;
}

}  // namespace fc
