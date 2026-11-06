// fc_host：固定步长 tick 驱动 + HTTP/JSON 协议 + NDJSON 证据日志
// + 确定性 replay + 可选的 autoplay driver + 关卡目录。
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "autoplay.h"
#include "catalogue.h"
#include "fc/fruit_gen.h"
#include "fc/game.h"
#include "fc/json.h"
#include "fc/serialize.h"
#include "http_server.h"
#include "log_writer.h"

namespace {

const char* kBuildVersion = "phase3-framework";

struct Options {
    std::string levelPath;
    std::string cataloguePath;
    std::string root = ".";
    std::string logPath;
    std::string replayPath;
    std::string saveReplayPath;
    std::string outPath;
    int seed = -1;
    int port = 8787;
    int maxTicks = 0;
    int maxRoadCars = 0;
    std::string autoplay;  // "", "greedy", "spam"
    bool exitOnResult = false;
    bool noServer = false;
    bool audit = false;
    // Golden hash 追踪（M_golden）：每 N 个 tick 记录一次规范的 stateHash。
    std::string hashTracePath;
    int hashEvery = 10;
    // 最终 snapshot 转储：供证据包使用的机器可读终局状态。
    std::string dumpPath;
    // 以暂停状态启动（证据采集：客户端选择之前不得运行任何 tick）。
    bool startPaused = false;
};

void printUsage() {
    std::cout << "fc_host --level levels/L_RING_OMEGA.json [--seed N] [--port 8787]\n"
                 "        [--catalogue levels/catalogue.json --root .]\n"
                 "        [--log host.ndjson] [--autoplay greedy|spam] [--exit-on-result]\n"
                 "        [--max-ticks N] [--save-replay cmds.jsonl] [--replay cmds.jsonl --out events.ndjson]\n"
                 "        [--audit] [--no-server] [--headless]\n"
                 "        [--hash-trace trace.log --hash-every 10]\n";
}

std::string readTextFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::string trim(const std::string& text) {
    std::size_t start = 0;
    std::size_t end = text.size();
    while (start < end && (text[start] == '\r' || text[start] == '\n' || text[start] == ' ' ||
                           text[start] == '\t')) {
        ++start;
    }
    while (end > start && (text[end - 1] == '\r' || text[end - 1] == '\n' || text[end - 1] == ' ' ||
                           text[end - 1] == '\t')) {
        --end;
    }
    return text.substr(start, end - start);
}

std::string nowStamp() {
    const std::time_t t = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &t);
#else
    localtime_r(&t, &local);
#endif
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%04d%02d%02d-%02d%02d", local.tm_year + 1900, local.tm_mon + 1,
                  local.tm_mday, local.tm_hour, local.tm_min);
    return std::string(buffer);
}

struct CommandRecord {
    int tick = 0;
    fc::Command command;
    bool accepted = false;
};

class Host {
public:
    Host(const Options& options, fc::Catalogue catalogue, int startIndex)
        : options_(options), game_(), catalogue_(std::move(catalogue)), current_(startIndex) {
        game_.setBuildVersion(kBuildVersion);
        paused_ = options_.startPaused;
        resetCurrent();
    }

    fc::Game& game() { return game_; }
    fc::host::LogWriter& log() { return log_; }
    const Options& options() const { return options_; }
    const fc::Catalogue& catalogue() const { return catalogue_; }
    int currentIndex() const { return current_; }
    const fc::CatalogueEntry& currentEntry() const { return catalogue_.levels[static_cast<std::size_t>(current_)]; }
    int levelEpoch() const { return levelEpoch_; }
    bool paused() const { return paused_; }
    void setPaused(bool paused) { paused_ = paused; }

    bool openLog(std::string& error) {
        if (options_.logPath.empty()) {
            return true;
        }
        if (!log_.open(options_.logPath, error)) {
            return false;
        }
        log_.setRunId("HOST-" + nowStamp());
        log_.writeSession(game_.state().level.id, game_.state().seed, game_.state().fruitsTotal, kBuildVersion);
        return true;
    }

    // Golden hash 追踪：每个采样 tick 一行。文件在运行开始
    // （首个 tick 之前）打开，因此追踪结果可以逐字节比对。
    bool openHashTrace(std::string& error) {
        if (options_.hashTracePath.empty()) {
            return true;
        }
        trace_.open(options_.hashTracePath, std::ios::binary | std::ios::trunc);
        if (!trace_.is_open()) {
            error = "cannot write hash trace file " + options_.hashTracePath;
            return false;
        }
        const int every = options_.hashEvery > 0 ? options_.hashEvery : 10;
        trace_ << "# level=" << game_.state().level.id << " seed=" << game_.state().seed
               << " every=" << every << " autoplay="
               << (options_.autoplay.empty() ? std::string("none") : options_.autoplay) << "\n";
        trace_.flush();
        return true;
    }

    void traceHash() {
        if (!trace_.is_open()) {
            return;
        }
        const int every = options_.hashEvery > 0 ? options_.hashEvery : 10;
        const fc::GameState& state = game_.state();
        if (state.tick % every != 0) {
            return;
        }
        trace_ << "tick=" << state.tick << " hash=" << fc::stateHash(state) << "\n";
        trace_.flush();
    }

    void closeHashTrace() {
        if (!trace_.is_open()) {
            return;
        }
        const fc::GameState& state = game_.state();
        trace_ << "final tick=" << state.tick << " status=" << fc::toString(state.status)
               << " resultTick=" << state.resultTick << " hash=" << fc::stateHash(state) << "\n";
        trace_.close();
    }

    // 切换到另一个目录条目；重置整个运行。
    bool select(const std::string& levelId, std::string& code, std::string& detail) {
        const int index = catalogue_.indexOf(levelId);
        if (index < 0) {
            code = "UNKNOWN_LEVEL";
            detail = "no catalogue entry with id '" + levelId + "'";
            return false;
        }
        const std::string previousId = game_.state().level.id;
        current_ = index;
        ++levelEpoch_;
        paused_ = false;
        resetCurrent();
        (void)previousId;
        log_.writeSession(game_.state().level.id, game_.state().seed, game_.state().fruitsTotal, kBuildVersion);
        return true;
    }

    fc::SnapshotContext context() const {
        fc::SnapshotContext ctx;
        ctx.levelEpoch = levelEpoch_;
        ctx.paused = paused_;
        return ctx;
    }

    void step() {
        if (game_.state().status == fc::GameStatus::Playing) {
            if (driver_) {
                fc::Command command;
                if (driver_->decide(game_, command)) {
                    const fc::CommandResult result = submit(command);
                    if (result.accepted && !options_.saveReplayPath.empty()) {
                        replay_.push_back(CommandRecord{game_.state().tick, command, true});
                    }
                }
            }
            const std::uint64_t before = game_.state().eventSeq;
            game_.tick();
            flushEvents(before);
            maybeSnapshot();
            checkResult();
        } else {
            game_.tick();
        }
        traceHash();
    }

    fc::CommandResult submit(const fc::Command& command) {
        const int tick = game_.state().tick;
        const fc::CommandResult result = game_.submit(command);
        log_.writeCommand(tick, fc::toString(command.type), command.index, result.accepted, result.reason,
                          result.nextAllowedTick);
        return result;
    }

    void flushEvents(std::uint64_t afterSeq) {
        for (const auto& event : game_.state().events) {
            if (event.seq <= afterSeq) {
                continue;
            }
            log_.writeEvent(event);
        }
    }

    void maybeSnapshot() {
        if (game_.state().tick % 60 != 0) {
            return;
        }
        const fc::GameState& state = game_.state();
        log_.writeSnapshot(state.tick, state.remainingFruits(), static_cast<int>(state.cars.size()),
                           state.occupiedSlots(), state.requireSelect, fc::toString(state.status));
    }

    void checkResult() {
        const fc::GameState& state = game_.state();
        if (state.status == fc::GameStatus::Playing || resultLogged_) {
            return;
        }
        resultLogged_ = true;
        log_.writeResult(state.tick, fc::toString(state.status), state.fruitsCollected, state.exitedCount);
        if (options_.exitOnResult) {
            finished_ = true;
        }
    }

    bool finished() const { return finished_; }

    bool writeReplay(std::string& error) {
        if (replay_.empty() || options_.saveReplayPath.empty()) {
            return true;
        }
        std::ofstream file(options_.saveReplayPath, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            error = "cannot write replay file";
            return false;
        }
        for (const auto& record : replay_) {
            file << "{\"tick\":" << record.tick << ",\"type\":\"" << fc::toString(record.command.type)
                 << "\",\"index\":" << record.command.index << "}\n";
        }
        return true;
    }

    std::uint64_t nextSnapshotSeq() { return ++snapshotSeq_; }

private:
    int maxRoadCars() const {
        if (options_.maxRoadCars > 0) {
            return options_.maxRoadCars;
        }
        const int lanes = game_.level().laneCount();
        // 任务书 6.11 建议每条 lane 为求解器配置两辆车；
        // 多出的一辆能让 0.5 s 的派车节流保持忙碌而不会堵死。
        return lanes * 2 + 1;
    }

    void resetCurrent() {
        const fc::CatalogueEntry& entry = currentEntry();
        game_.reset(entry.level, options_.seed >= 0 ? static_cast<std::uint64_t>(options_.seed) : entry.level.seed);
        resultLogged_ = false;
        replay_.clear();
        if (options_.autoplay == "greedy") {
            driver_.reset(new fc::AutoplayDriver(fc::AutoplayDriver::Mode::Greedy, maxRoadCars()));
        } else if (options_.autoplay == "spam") {
            driver_.reset(new fc::AutoplayDriver(fc::AutoplayDriver::Mode::Spam, 8));
        } else {
            driver_.reset();
        }
    }

    Options options_;
    fc::Game game_;
    fc::Catalogue catalogue_;
    int current_ = 0;
    int levelEpoch_ = 0;
    bool paused_ = false;
    std::unique_ptr<fc::AutoplayDriver> driver_;
    fc::host::LogWriter log_;
    std::ofstream trace_;
    std::vector<CommandRecord> replay_;
    std::uint64_t snapshotSeq_ = 0;
    bool resultLogged_ = false;
    bool finished_ = false;
};

bool decodeCommand(const fc::json::Value& body, fc::Command& command, std::string& error) {
    if (!body.isObject()) {
        error = "body must be a JSON object";
        return false;
    }
    const fc::json::Value* type = body.find("type");
    if (type == nullptr || !type->isString()) {
        error = "missing command type";
        return false;
    }
    if (type->asString() == "select_lane") {
        command.type = fc::CommandType::SelectLane;
        const fc::json::Value* lane = body.find("lane");
        if (lane == nullptr || !lane->isNumber()) {
            error = "select_lane requires a numeric lane";
            return false;
        }
        command.index = static_cast<int>(lane->asNumber());
        return true;
    }
    if (type->asString() == "select_slot") {
        command.type = fc::CommandType::SelectSlot;
        const fc::json::Value* slot = body.find("slot");
        if (slot == nullptr || !slot->isNumber()) {
            error = "select_slot requires a numeric slot";
            return false;
        }
        command.index = static_cast<int>(slot->asNumber());
        return true;
    }
    if (type->asString() == "use_item") {
        // 预留的技能命令（任务书 5.8）：本阶段先解析，
        // 由核心以 ITEM_NOT_IMPLEMENTED 拒绝。
        command.type = fc::CommandType::UseItem;
        const fc::json::Value* item = body.find("itemId");
        if (item != nullptr && item->isString()) {
            command.itemId = item->asString();
        }
        const fc::json::Value* target = body.find("targetCar");
        if (target != nullptr && target->isString()) {
            command.targetCar = target->asString();
        }
        const fc::json::Value* center = body.find("center");
        if (center != nullptr && center->isArray() && center->items().size() == 2 &&
            center->items()[0].isNumber() && center->items()[1].isNumber()) {
            command.center = fc::Vec2d{center->items()[0].asNumber(), center->items()[1].asNumber()};
        }
        const fc::json::Value* radius = body.find("radius");
        if (radius != nullptr && radius->isNumber()) {
            command.radius = radius->asNumber();
        }
        return true;
    }
    error = "unknown command type '" + type->asString() + "'";
    return false;
}

struct ReplayCommand {
    int tick = 0;
    fc::Command command;
};

int runReplay(const Options& options, const fc::LevelDefinition& level) {
    std::vector<ReplayCommand> commands;
    std::ifstream file(options.replayPath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "cannot open replay file " << options.replayPath << "\n";
        return 2;
    }
    std::string line;
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty()) {
            continue;
        }
        fc::json::Value value;
        fc::json::ParseError error;
        if (!fc::json::parse(line, value, error)) {
            std::cerr << "bad replay line: " << error.message << "\n";
            return 2;
        }
        ReplayCommand record;
        const fc::json::Value* tick = value.find("tick");
        if (tick == nullptr || !tick->isNumber()) {
            std::cerr << "replay line without tick\n";
            return 2;
        }
        record.tick = static_cast<int>(tick->asNumber());
        const fc::json::Value* type = value.find("type");
        if (type != nullptr && type->isString()) {
            if (type->asString() == "select_slot") {
                record.command.type = fc::CommandType::SelectSlot;
            } else {
                record.command.type = fc::CommandType::SelectLane;
            }
        }
        const fc::json::Value* index = value.find("index");
        if (index != nullptr && index->isNumber()) {
            record.command.index = static_cast<int>(index->asNumber());
        }
        commands.push_back(record);
    }

    fc::Game game;
    game.setBuildVersion(kBuildVersion);
    game.reset(level, options.seed >= 0 ? static_cast<std::uint64_t>(options.seed) : level.seed);

    fc::host::LogWriter log;
    if (!options.outPath.empty()) {
        std::string error;
        if (!log.open(options.outPath, error)) {
            std::cerr << error << "\n";
            return 2;
        }
        log.setRunId("REPLAY-" + nowStamp());
        log.writeSession(level.id, game.state().seed, game.state().fruitsTotal, kBuildVersion);
    }

    std::size_t cursor = 0;
    const int limit = options.maxTicks > 0 ? options.maxTicks : 100000;
    std::uint64_t flushed = 0;
    for (int tick = 0; tick < limit; ++tick) {
        while (cursor < commands.size() && commands[cursor].tick <= game.state().tick) {
            const fc::CommandResult result = game.submit(commands[cursor].command);
            log.writeCommand(game.state().tick, fc::toString(commands[cursor].command.type),
                             commands[cursor].command.index, result.accepted, result.reason,
                             result.nextAllowedTick);
            ++cursor;
        }
        game.tick();
        for (const auto& event : game.state().events) {
            if (event.seq > flushed) {
                log.writeEvent(event);
                flushed = event.seq;
            }
        }
        if (game.state().status != fc::GameStatus::Playing) {
            log.writeResult(game.state().tick, fc::toString(game.state().status), game.state().fruitsCollected,
                            game.state().exitedCount);
            break;
        }
    }
    log.close();
    std::cout << "REPLAY result=" << fc::toString(game.state().status)
              << " tick=" << game.state().resultTick << " hash=" << fc::stateHash(game.state()) << "\n";
    return 0;
}

int runServer(const Options& options, fc::Catalogue catalogue) {
    Host host(options, std::move(catalogue), 0);
    std::string error;
    if (!host.openLog(error)) {
        std::cerr << error << "\n";
        return 2;
    }
    if (!host.openHashTrace(error)) {
        std::cerr << error << "\n";
        return 2;
    }

    fc::host::HttpServer server;
    if (!options.noServer && !server.start("127.0.0.1", options.port, error)) {
        std::cerr << error << "\n";
        return 2;
    }

    server.setHandler([&host](const fc::host::HttpRequest& request) {
        fc::host::HttpResponse response;
        if (request.method == "GET" && request.path == "/health") {
            response.body = "{\"ok\":true,\"version\":\"" + std::string(kBuildVersion) + "\",\"tick\":" +
                            std::to_string(host.game().state().tick) + "}";
            return response;
        }
        if (request.method == "GET" && request.path == "/level") {
            response.body = fc::levelToJson(host.game().level());
            return response;
        }
        if (request.method == "GET" && request.path == "/levels") {
            response.body = fc::catalogueJson(host.catalogue(), host.currentIndex());
            return response;
        }
        if (request.method == "POST" && request.path == "/level/select") {
            fc::json::Value body;
            fc::json::ParseError parseError;
            if (!fc::json::parse(request.body, body, parseError)) {
                response.status = 400;
                response.body = "{\"ok\":false,\"error\":\"BAD_JSON\"}";
                return response;
            }
            const fc::json::Value* id = body.find("id");
            if (id == nullptr || !id->isString()) {
                response.status = 400;
                response.body = "{\"ok\":false,\"error\":\"BAD_JSON\"}";
                return response;
            }
            std::string code;
            std::string detail;
            if (!host.select(id->asString(), code, detail)) {
                response.status = 404;
                response.body = "{\"ok\":false,\"error\":\"UNKNOWN_LEVEL\",\"detail\":" +
                                fc::jsonQuote(detail) + "}";
                return response;
            }
            response.body = "{\"ok\":true,\"id\":" + fc::jsonQuote(host.currentEntry().id) +
                            ",\"levelEpoch\":" + std::to_string(host.levelEpoch()) +
                            ",\"seed\":" + std::to_string(host.game().state().seed) + "}";
            return response;
        }
        if (request.method == "POST" && request.path == "/pause") {
            fc::json::Value body;
            fc::json::ParseError parseError;
            if (!fc::json::parse(request.body, body, parseError) || !body.isObject()) {
                response.status = 400;
                response.body = "{\"ok\":false,\"error\":\"BAD_JSON\"}";
                return response;
            }
            const fc::json::Value* paused = body.find("paused");
            if (paused == nullptr || !paused->isBool()) {
                response.status = 400;
                response.body = "{\"ok\":false,\"error\":\"BAD_JSON\"}";
                return response;
            }
            host.setPaused(paused->asBool());
            response.body = std::string("{\"ok\":true,\"paused\":") + (host.paused() ? "true" : "false") + "}";
            return response;
        }
        if (request.method == "GET" && request.path == "/state") {
            std::uint64_t since = 0;
            const std::size_t at = request.query.find("since=");
            if (at != std::string::npos) {
                since = static_cast<std::uint64_t>(std::strtoull(request.query.c_str() + at + 6, nullptr, 10));
            }
            const auto events = host.game().eventsSince(since);
            response.body = fc::snapshotJson(host.game().state(), events, host.nextSnapshotSeq(), kBuildVersion,
                                             host.context());
            return response;
        }
        if (request.method == "POST" && request.path == "/command") {
            fc::json::Value body;
            fc::json::ParseError parseError;
            if (!fc::json::parse(request.body, body, parseError)) {
                response.status = 400;
                response.body = "{\"accepted\":false,\"reason\":\"BAD_JSON\"}";
                return response;
            }
            if (host.paused()) {
                response.body = "{\"accepted\":false,\"reason\":\"PAUSED\"}";
                return response;
            }
            fc::Command command;
            std::string decodeError;
            if (!decodeCommand(body, command, decodeError)) {
                response.status = 400;
                response.body = "{\"accepted\":false,\"reason\":\"BAD_COMMAND\",\"detail\":" +
                                fc::jsonQuote(decodeError) + "}";
                return response;
            }
            const fc::CommandResult result = host.submit(command);
            response.body = std::string("{\"accepted\":") + (result.accepted ? "true" : "false") +
                            ",\"reason\":" + fc::jsonQuote(result.reason) +
                            ",\"nextAllowedTick\":" + std::to_string(result.nextAllowedTick) + "}";
            return response;
        }
        if (request.method == "POST" && request.path == "/client_report") {
            // 客户端一致性报告（节点计数与 snapshot 字段的对比）
            // 会以 kind=CLIENT 行写入同一个 E2 流。
            std::string payload = request.body.empty() ? std::string("{}") : request.body;
            host.log().writeRaw("CLIENT", "\"payload\":" + payload);
            response.body = "{\"ok\":true}";
            return response;
        }
        if (request.method == "POST" && request.path == "/reset") {
            fc::json::Value body;
            fc::json::ParseError parseError;
            std::uint64_t seed = host.game().state().seed;
            if (!request.body.empty() && fc::json::parse(request.body, body, parseError)) {
                const fc::json::Value* value = body.find("seed");
                if (value != nullptr && value->isNumber()) {
                    seed = static_cast<std::uint64_t>(value->asNumber());
                }
            }
            host.game().reset(host.game().level(), seed);
            host.setPaused(false);
            response.body = std::string("{\"ok\":true,\"seed\":") + std::to_string(seed) +
                            ",\"levelEpoch\":" + std::to_string(host.levelEpoch()) + "}";
            return response;
        }
        response.status = 404;
        response.body = "{\"ok\":false,\"error\":\"not found\"}";
        return response;
    });

    const auto tickDuration = std::chrono::microseconds(1000000 / fc::kTickRate);
    auto nextTick = std::chrono::steady_clock::now();
    long long tickCount = 0;
    const long long tickLimit = options.maxTicks > 0 ? options.maxTicks : 1000000;

    while (!host.finished()) {
        if (options.noServer) {
            if (!host.paused()) {
                host.step();
            }
            ++tickCount;
            if (tickCount >= tickLimit) {
                break;
            }
            continue;
        }
        server.poll(1);
        if (host.paused()) {
            // 暂停的 host 不得推进 tick（任务书 TC-M18-04）。
            nextTick = std::chrono::steady_clock::now();
            if (tickCount >= tickLimit) {
                break;
            }
            continue;
        }
        const auto now = std::chrono::steady_clock::now();
        int steps = 0;
        while (now >= nextTick && steps < 8) {
            host.step();
            ++tickCount;
            nextTick += tickDuration;
            ++steps;
            if (host.finished()) {
                break;
            }
        }
        if (steps == 8) {
            nextTick = std::chrono::steady_clock::now();
        }
        if (tickCount >= tickLimit) {
            break;
        }
    }

    std::string replayError;
    host.writeReplay(replayError);
    server.stop();
    host.closeHashTrace();
    if (!options.dumpPath.empty()) {
        const fc::GameState& state = host.game().state();
        std::ofstream dump(options.dumpPath, std::ios::binary | std::ios::trunc);
        if (dump.is_open()) {
            dump << fc::snapshotJson(state, host.game().eventsSince(0), 1, kBuildVersion, host.context()) << "\n";
            dump.close();
        }
    }
    host.log().close();
    std::cout << "HOST done tick=" << host.game().state().tick << " status="
              << fc::toString(host.game().state().status)
              << " hash=" << fc::stateHash(host.game().state()) << "\n";
    return 0;
}

// 单个关卡或整个目录：构建 host 运行时使用的内存内目录。
bool buildCatalogue(const Options& options, fc::Catalogue& out, int& status) {
    status = 0;
    if (!options.cataloguePath.empty()) {
        const std::string text = readTextFile(options.cataloguePath);
        if (text.empty()) {
            std::cerr << "cannot read catalogue " << options.cataloguePath << "\n";
            status = 2;
            return false;
        }
        const fc::CatalogueResult result = fc::loadCatalogue(text, options.root);
        if (!result.ok) {
            std::cerr << "LEVEL_INVALID " << result.error.code << " " << result.error.path << " "
                      << result.error.message << "\n";
            status = 3;
            return false;
        }
        out = result.catalogue;
        return true;
    }

    const std::string text = readTextFile(options.levelPath);
    if (text.empty()) {
        std::cerr << "cannot read level " << options.levelPath << "\n";
        status = 2;
        return false;
    }
    const fc::LevelParseResult parsed = fc::parseLevel(text);
    if (!parsed.ok) {
        std::cerr << "LEVEL_INVALID " << parsed.error.code << " " << parsed.error.path << " "
                  << parsed.error.message << "\n";
        status = 3;
        return false;
    }
    fc::CatalogueEntry entry;
    entry.id = parsed.level.id;
    entry.name = parsed.level.name.empty() ? parsed.level.id : parsed.level.name;
    entry.file = options.levelPath;
    entry.order = 1;
    entry.level = parsed.level;
    out.levels.push_back(entry);
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto nextValue = [&](const std::string& name) {
            if (i + 1 >= argc) {
                std::cerr << "missing value for " << name << "\n";
                std::exit(2);
            }
            return std::string(argv[++i]);
        };
        if (arg == "--level") {
            options.levelPath = nextValue(arg);
        } else if (arg == "--catalogue") {
            options.cataloguePath = nextValue(arg);
        } else if (arg == "--root") {
            options.root = nextValue(arg);
        } else if (arg == "--seed") {
            options.seed = std::atoi(nextValue(arg).c_str());
        } else if (arg == "--port") {
            options.port = std::atoi(nextValue(arg).c_str());
        } else if (arg == "--log") {
            options.logPath = nextValue(arg);
        } else if (arg == "--replay") {
            options.replayPath = nextValue(arg);
        } else if (arg == "--save-replay") {
            options.saveReplayPath = nextValue(arg);
        } else if (arg == "--out") {
            options.outPath = nextValue(arg);
        } else if (arg == "--max-road-cars") {
            options.maxRoadCars = std::atoi(nextValue(arg).c_str());
        } else if (arg == "--max-ticks") {
            options.maxTicks = std::atoi(nextValue(arg).c_str());
        } else if (arg == "--autoplay") {
            options.autoplay = nextValue(arg);
        } else if (arg == "--exit-on-result") {
            options.exitOnResult = true;
        } else if (arg == "--audit") {
            options.audit = true;
        } else if (arg == "--no-server") {
            options.noServer = true;
        } else if (arg == "--headless") {
            options.noServer = true;
        } else if (arg == "--hash-trace") {
            options.hashTracePath = nextValue(arg);
        } else if (arg == "--hash-every") {
            options.hashEvery = std::atoi(nextValue(arg).c_str());
        } else if (arg == "--dump") {
            options.dumpPath = nextValue(arg);
        } else if (arg == "--paused") {
            options.startPaused = true;
        } else if (arg == "--help" || arg == "-h") {
            printUsage();
            return 0;
        } else {
            std::cerr << "unknown argument: " << arg << "\n";
            printUsage();
            return 2;
        }
    }

    if (options.levelPath.empty() && options.cataloguePath.empty()) {
        printUsage();
        return 2;
    }

    fc::Catalogue catalogue;
    int status = 0;
    if (!buildCatalogue(options, catalogue, status)) {
        return status;
    }

    if (options.audit) {
        std::cout << "{\"kind\":\"CATALOGUE\",\"levels\":" << catalogue.levels.size()
                  << ",\"version\":" << catalogue.version << "}\n";
        for (const auto& entry : catalogue.levels) {
            std::cout << fc::auditJson(entry.level) << "\n";
            std::cout << "{\"kind\":\"PREVIEW\"," << "\"levelId\":" << fc::jsonQuote(entry.level.id)
                      << ",\"preview\":" << fc::previewJson(entry.level) << "}\n";
        }
        return 0;
    }

    if (!options.replayPath.empty()) {
        return runReplay(options, catalogue.levels.front().level);
    }
    return runServer(options, std::move(catalogue));
}

