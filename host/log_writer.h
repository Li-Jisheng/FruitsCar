// host 的 NDJSON 证据写入器（E2 日志）。
#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "fc/events.h"

namespace fc {
namespace host {

class LogWriter {
public:
    bool open(const std::string& path, std::string& error);
    void close();
    bool isOpen() const { return file_.is_open(); }

    void setRunId(const std::string& runId) { runId_ = runId; }
    const std::string& runId() const { return runId_; }
    std::uint64_t seq() const { return seq_; }

    // 写入 {"ts":..,"run":..,"seq":..,"kind":..,<payload>}
    void writeRaw(const std::string& kind, const std::string& payloadJson);
    void writeSession(const std::string& levelId, std::uint64_t seed, int fruitsTotal,
                      const std::string& buildVersion);
    void writeCommand(int tick, const std::string& type, int index, bool accepted, const std::string& why,
                      int nextAllowedTick);
    void writeEvent(const Event& event);
    void writeSnapshot(int tick, int remaining, int roadCars, int occupiedSlots, bool requireSelect,
                       const std::string& status);
    void writeResult(int tick, const std::string& result, int collected, int exited);

private:
    std::ofstream file_;
    std::string runId_ = "host";
    std::uint64_t seq_ = 0;
};

}  // namespace host
}  // namespace fc
