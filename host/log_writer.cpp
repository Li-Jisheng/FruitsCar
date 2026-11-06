#include "log_writer.h"

#include <chrono>
#include <cstdio>
#include <ctime>

#include "fc/types.h"

namespace fc {
namespace host {
namespace {

std::string timestamp() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    const std::time_t seconds = system_clock::to_time_t(now);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &seconds);
#else
    localtime_r(&seconds, &local);
#endif
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02d.%03d+08:00", local.tm_year + 1900,
                  local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec,
                  static_cast<int>(ms.count()));
    return std::string(buffer);
}

}  // namespace

bool LogWriter::open(const std::string& path, std::string& error) {
    file_.open(path, std::ios::binary | std::ios::trunc);
    if (!file_.is_open()) {
        error = "cannot open log file " + path;
        return false;
    }
    return true;
}

void LogWriter::close() {
    if (file_.is_open()) {
        file_.flush();
        file_.close();
    }
}

void LogWriter::writeRaw(const std::string& kind, const std::string& payloadJson) {
    if (!file_.is_open()) {
        return;
    }
    file_ << "{\"ts\":\"" << timestamp() << "\",\"run\":" << jsonQuote(runId_) << ",\"seq\":" << (++seq_)
          << ",\"kind\":\"" << kind << "\"" << (payloadJson.empty() ? "" : "," + payloadJson) << "}\n";
    file_.flush();
}

void LogWriter::writeSession(const std::string& levelId, std::uint64_t seed, int fruitsTotal,
                             const std::string& buildVersion) {
    writeRaw("SESSION", "\"levelId\":" + jsonQuote(levelId) + ",\"seed\":" + std::to_string(seed) +
                            ",\"fruitsTotal\":" + std::to_string(fruitsTotal) +
                            ",\"buildVersion\":" + jsonQuote(buildVersion));
}

void LogWriter::writeCommand(int tick, const std::string& type, int index, bool accepted, const std::string& why,
                             int nextAllowedTick) {
    writeRaw("COMMAND", "\"tick\":" + std::to_string(tick) + ",\"type\":" + jsonQuote(type) +
                            ",\"index\":" + std::to_string(index) + ",\"accepted\":" +
                            (accepted ? "true" : "false") + ",\"reason\":" + jsonQuote(why) +
                            ",\"nextAllowedTick\":" + std::to_string(nextAllowedTick));
}

void LogWriter::writeEvent(const Event& event) {
    writeRaw("EVENT", "\"tick\":" + std::to_string(event.tick) + ",\"event\":" + eventToJson(event));
}

void LogWriter::writeSnapshot(int tick, int remaining, int roadCars, int occupiedSlots, bool requireSelect,
                              const std::string& status) {
    writeRaw("SNAPSHOT", "\"tick\":" + std::to_string(tick) + ",\"fruitsRemaining\":" + std::to_string(remaining) +
                             ",\"roadCars\":" + std::to_string(roadCars) +
                             ",\"occupiedSlots\":" + std::to_string(occupiedSlots) +
                             ",\"requireSelect\":" + (requireSelect ? "true" : "false") +
                             ",\"status\":" + jsonQuote(status));
}

void LogWriter::writeResult(int tick, const std::string& result, int collected, int exited) {
    writeRaw("RESULT", "\"tick\":" + std::to_string(tick) + ",\"result\":" + jsonQuote(result) +
                           ",\"fruitsCollected\":" + std::to_string(collected) +
                           ",\"exitedCount\":" + std::to_string(exited));
}

}  // namespace host
}  // namespace fc
