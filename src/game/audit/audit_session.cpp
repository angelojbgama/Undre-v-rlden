#include "game/audit/audit_session.h"

#include "game/audit/bmp_writer.h"

#include <chrono>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <type_traits>

namespace underworld::game::audit {
namespace {

void writeJsonString(std::ostream& out, std::string_view value) {
    out << '"' << escapeJsonString(value) << '"';
}

void writeValue(std::ostream& out, const AuditValue& value) {
    std::visit([&out](const auto& item) {
        using Value = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<Value, std::string>) {
            writeJsonString(out, item);
        } else if constexpr (std::is_same_v<Value, bool>) {
            out << (item ? "true" : "false");
        } else {
            out << item;
        }
    }, value);
}

void writeStringArray(std::ostream& out, const std::vector<std::string>& values) {
    out << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) { out << ','; }
        writeJsonString(out, values[index]);
    }
    out << ']';
}

} // namespace

std::string escapeJsonString(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (const unsigned char character : value) {
        switch (character) {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\b': result += "\\b"; break;
        case '\f': result += "\\f"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
            if (character < 0x20U) {
                std::ostringstream hex;
                hex << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                    << static_cast<unsigned int>(character);
                result += hex.str();
            } else {
                result += static_cast<char>(character);
            }
            break;
        }
    }
    return result;
}

std::string serializeAuditEvent(const AuditEvent& event) {
    std::ostringstream out;
    out << "{\"tick\":" << event.tick << ",\"type\":";
    writeJsonString(out, event.type);
    for (const auto& field : event.fields) {
        if (field.key == "tick" || field.key == "type") { continue; }
        out << ',';
        writeJsonString(out, field.key);
        out << ':';
        writeValue(out, field.value);
    }
    out << '}';
    return out.str();
}

std::filesystem::path makeAuditSessionDirectory(
    const std::filesystem::path& outputRoot, std::string_view sessionId) {
    if (sessionId.empty() || sessionId == "." || sessionId == "..") {
        throw std::invalid_argument("audit session id cannot be empty");
    }
    for (const unsigned char character : sessionId) {
        if (!std::isalnum(character) && character != '_' && character != '-' && character != '.') {
            throw std::invalid_argument("audit session id contains an unsafe character");
        }
    }
    return outputRoot / std::filesystem::path{std::string(sessionId)};
}

AuditSession::~AuditSession() { static_cast<void>(close("ABORTED")); }

bool AuditSession::open(const AuditSessionConfig& config, std::string& error) {
    static_cast<void>(close("ABORTED"));
    enabled_ = false;
    closed_ = false;
    eventCount_ = 0;
    stateCount_ = 0;
    eventTypes_.clear();
    hasState_ = false;
    checkpointInterval_ = config.stateCheckpointInterval == 0 ? 1 : config.stateCheckpointInterval;
    if (!config.enabled) { return true; }

    try {
        std::string sessionId = config.sessionId;
        if (sessionId.empty()) { sessionId = timestampNow(); }
        sessionDirectory_ = makeAuditSessionDirectory(config.outputRoot, sessionId);
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }

    std::error_code filesystemError;
    std::filesystem::create_directories(sessionDirectory_ / "screenshots", filesystemError);
    if (filesystemError) {
        error = "could not create audit session directory: " + filesystemError.message();
        return false;
    }
    eventsFile_.open(sessionDirectory_ / "events.jsonl", std::ios::out | std::ios::trunc);
    stateFile_.open(sessionDirectory_ / "state.jsonl", std::ios::out | std::ios::trunc);
    if (!eventsFile_ || !stateFile_) {
        error = "could not open audit event/state files";
        eventsFile_.close();
        stateFile_.close();
        return false;
    }
    if (!writeMetadata(config.metadata, error)) {
        eventsFile_.close();
        stateFile_.close();
        return false;
    }
    enabled_ = true;
    return true;
}

bool AuditSession::writeMetadata(const AuditSessionMetadata& metadata, std::string& error) {
    std::ofstream file(sessionDirectory_ / "session.json", std::ios::out | std::ios::trunc);
    if (!file) {
        error = "could not write audit metadata";
        return false;
    }
    file << "{\"schemaVersion\":1,\"buildVersion\":";
    writeJsonString(file, metadata.buildVersion);
    file << ",\"gitCommit\":";
    writeJsonString(file, metadata.gitCommit);
    file << ",\"platform\":";
    writeJsonString(file, metadata.platform);
    file << ",\"mode\":";
    writeJsonString(file, metadata.mode);
    file << ",\"scenario\":";
    writeJsonString(file, metadata.scenario);
    file << ",\"startTimestamp\":";
    writeJsonString(file, metadata.startTimestamp.empty() ? timestampNow() : metadata.startTimestamp);
    file << ",\"logicalFramebuffer\":{\"width\":" << metadata.logicalWidth
         << ",\"height\":" << metadata.logicalHeight << "}"
         << ",\"initialMap\":";
    writeJsonString(file, metadata.initialMap);
    file << ",\"initialSpawn\":";
    writeJsonString(file, metadata.initialSpawn);
    file << ",\"randomSeed\":";
    writeJsonString(file, metadata.randomSeed);
    file << ",\"assetRoot\":";
    writeJsonString(file, metadata.assetRoot);
    file << ",\"commandLine\":";
    writeStringArray(file, metadata.commandLine);
    file << "}\n";
    if (!file) {
        error = "could not flush audit metadata";
        return false;
    }
    return true;
}

bool AuditSession::recordEvent(const AuditEvent& event) {
    if (!enabled_ || closed_) { return false; }
    eventsFile_ << serializeAuditEvent(event) << '\n';
    if (!eventsFile_) { return false; }
    ++eventCount_;
    ++eventTypes_[event.type];
    return true;
}

bool AuditSession::recordState(const GameAuditSnapshot& snapshot, bool force) {
    if (!enabled_ || closed_) { return false; }
    if (!force && hasState_ && snapshot.tick >= lastStateTick_ &&
        snapshot.tick - lastStateTick_ < checkpointInterval_) {
        return false;
    }
    stateFile_ << serializeAuditSnapshot(snapshot) << '\n';
    if (!stateFile_) { return false; }
    lastStateTick_ = snapshot.tick;
    hasState_ = true;
    ++stateCount_;
    return true;
}

bool AuditSession::captureScreenshot(std::string_view name,
                                     core::PixelBufferView surface, std::string& error) {
    if (!enabled_ || closed_) { return false; }
    if (name.empty()) {
        error = "audit screenshot name cannot be empty";
        return false;
    }
    for (const unsigned char character : name) {
        if (!std::isalnum(character) && character != '_' && character != '-' && character != '.') {
            error = "audit screenshot name contains an unsafe character";
            return false;
        }
    }
    const auto path = sessionDirectory_ / "screenshots" /
        (std::string{name} + (name.ends_with(".bmp") ? "" : ".bmp"));
    if (!writeBmp32(path, surface, error)) { return false; }
    ++screenshotCount_;
    return true;
}

bool AuditSession::writeSummary(std::string_view result, std::string_view failure) {
    if (!enabled_) { return false; }
    std::ofstream file(sessionDirectory_ / "summary.txt", std::ios::out | std::ios::trunc);
    if (!file) { return false; }
    file << "Dungeon Underworld Audit Session\n\n"
         << "result: " << result << '\n'
         << "events: " << eventCount_ << '\n'
         << "state checkpoints: " << stateCount_ << '\n'
         << "screenshots: " << screenshotCount_ << '\n';
    if (!failure.empty()) { file << "failure: " << failure << '\n'; }
    if (!eventTypes_.empty()) {
        file << "\nevent types:\n";
        for (const auto& [type, count] : eventTypes_) {
            file << "- " << type << ": " << count << '\n';
        }
    }
    return static_cast<bool>(file);
}

bool AuditSession::close(std::string_view result, std::string_view failure) {
    if (closed_) { return !enabled_; }
    if (!enabled_) {
        closed_ = true;
        return true;
    }
    eventsFile_.flush();
    stateFile_.flush();
    const bool summary = writeSummary(result, failure);
    eventsFile_.close();
    stateFile_.close();
    closed_ = true;
    return summary && !eventsFile_.fail() && !stateFile_.fail();
}

std::string AuditSession::timestampNow() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &time);
#else
    gmtime_r(&time, &utc);
#endif
    std::ostringstream result;
    result << std::put_time(&utc, "%Y%m%d_%H%M%S");
    return result.str();
}

} // namespace underworld::game::audit
