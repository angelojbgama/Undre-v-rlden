#pragma once

#include "game/audit/audit_snapshot.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace underworld::game::audit {

using AuditValue = std::variant<std::string, std::int64_t, std::uint64_t, bool>;

struct AuditField final {
    std::string key;
    AuditValue value;
};

struct AuditEvent final {
    simulation::Tick tick{};
    std::string type;
    std::vector<AuditField> fields;
};

[[nodiscard]] std::string escapeJsonString(std::string_view value);
[[nodiscard]] std::string serializeAuditEvent(const AuditEvent& event);

struct AuditSessionMetadata final {
    std::string buildVersion;
    std::string gitCommit;
    std::string platform;
    std::string mode;
    std::string scenario;
    std::string startTimestamp;
    int logicalWidth{272};
    int logicalHeight{224};
    std::string initialMap;
    std::string initialSpawn;
    std::string assetRoot;
    std::string randomSeed;
    std::vector<std::string> commandLine;
};

struct AuditSessionConfig final {
    std::filesystem::path outputRoot{"audit"};
    std::string sessionId;
    AuditSessionMetadata metadata;
    std::uint64_t stateCheckpointInterval{60};
    bool enabled{true};
};

[[nodiscard]] std::filesystem::path makeAuditSessionDirectory(
    const std::filesystem::path& outputRoot, std::string_view sessionId);

class AuditSession final {
public:
    AuditSession() = default;
    AuditSession(const AuditSession&) = delete;
    AuditSession& operator=(const AuditSession&) = delete;
    ~AuditSession();

    [[nodiscard]] bool open(const AuditSessionConfig& config, std::string& error);
    [[nodiscard]] bool enabled() const noexcept { return enabled_; }
    [[nodiscard]] bool recordEvent(const AuditEvent& event);
    [[nodiscard]] bool recordState(const GameAuditSnapshot& snapshot, bool force = false);
    [[nodiscard]] bool writeSummary(std::string_view result, std::string_view failure = {});
    [[nodiscard]] bool close(std::string_view result = "PASS");
    [[nodiscard]] const std::filesystem::path& sessionDirectory() const noexcept {
        return sessionDirectory_;
    }
    [[nodiscard]] std::size_t eventCount() const noexcept { return eventCount_; }
    [[nodiscard]] std::size_t stateCount() const noexcept { return stateCount_; }

private:
    bool writeMetadata(const AuditSessionMetadata& metadata, std::string& error);
    static std::string timestampNow();

    bool enabled_{};
    bool closed_{};
    std::uint64_t checkpointInterval_{60};
    std::filesystem::path sessionDirectory_;
    std::ofstream eventsFile_;
    std::ofstream stateFile_;
    std::map<std::string, std::size_t> eventTypes_;
    std::size_t eventCount_{};
    std::size_t stateCount_{};
    std::size_t screenshotCount_{};
    simulation::Tick lastStateTick_{};
    bool hasState_{};
};

} // namespace underworld::game::audit
