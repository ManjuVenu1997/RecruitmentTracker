// ============================================================
//  logger.h  —  Structured NDJSON logging
//
//  Each log line is a self-contained JSON object (one per line),
//  suitable for ingestion by Splunk / ELK / Datadog / Loki.
//
//  Example output:
//    {"ts":"2026-03-27T10:30:01.042Z","level":"INFO","component":"HTTP","msg":"Request","method":"GET","path":"/api/users","status":200}
//    {"ts":"2026-03-27T10:30:02.117Z","level":"WARN","component":"Auth","msg":"Login failed","user":"bob","remaining":2}
//
//  Usage:
//    LOG_INFO ("Auth", "Login success",
//              Logger::F("user", username), Logger::F("role", role))
//    LOG_WARN ("Auth", "Login failed",
//              Logger::F("user", username), Logger::F("remaining", 2))
//    LOG_ERROR("DB",   "Query failed",  Logger::F("detail", ex.what()))
//
//  Runtime level control (set before first log call):
//    Logger::setLevel(Logger::Level::DEBUG);
//  or via environment variable in main():
//    LOG_LEVEL=DEBUG ./recruitment_tracker
//
//  All output goes to stderr so it can be redirected independently:
//    ./recruitment_tracker 2>app.log
// ============================================================
#pragma once

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <mutex>
#include <string>

namespace Logger {

// ── Severity levels ───────────────────────────────────────
enum class Level : int { DEBUG = 0, INFO = 1, WARN = 2, ERR = 3 };

inline const char* levelStr(Level l) noexcept {
    switch (l) {
        case Level::DEBUG: return "DEBUG";
        case Level::INFO:  return "INFO";
        case Level::WARN:  return "WARN";
        case Level::ERR:   return "ERROR";
    }
    return "INFO";
}

// Global minimum level (default INFO).
inline Level& minLevel() noexcept {
    static Level lvl = Level::INFO;
    return lvl;
}
inline void setLevel(Level l) noexcept { minLevel() = l; }

// Read level from the LOG_LEVEL environment variable.
// Call once from main() before the server starts.
inline void initFromEnv() {
    const char* ev = std::getenv("LOG_LEVEL");
    if (!ev) return;
    std::string s(ev);
    if      (s == "DEBUG") setLevel(Level::DEBUG);
    else if (s == "INFO")  setLevel(Level::INFO);
    else if (s == "WARN")  setLevel(Level::WARN);
    else if (s == "ERROR") setLevel(Level::ERR);
}

// ── ISO-8601 timestamp with milliseconds ─────────────────
inline std::string nowIso() {
    using namespace std::chrono;
    auto now  = system_clock::now();
    auto tt   = system_clock::to_time_t(now);
    int  ms   = static_cast<int>(
                    duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &tt);
#else
    gmtime_r(&tt, &tm);
#endif
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
        tm.tm_hour, tm.tm_min, tm.tm_sec, ms);
    return buf;
}

// ── JSON string escaping ──────────────────────────────────
inline std::string jsonEsc(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 2);
    o += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"':  o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n";  break;
            case '\r': o += "\\r";  break;
            case '\t': o += "\\t";  break;
            default:
                if (c < 0x20) {
                    char b[8];
                    std::snprintf(b, sizeof(b), "\\u%04x", (unsigned)c);
                    o += b;
                } else {
                    o += static_cast<char>(c);
                }
        }
    }
    o += '"';
    return o;
}

// ── Typed key-value field ─────────────────────────────────
struct Field {
    std::string key;
    std::string val;
    bool        raw;  // true → emit val unquoted (numbers / booleans)
};

// Factory helpers — overloaded on value type:
inline Field F(std::string k, std::string  v) { return {std::move(k), std::move(v), false}; }
inline Field F(std::string k, const char*  v) { return {std::move(k), v,            false}; }
inline Field F(std::string k, int          v) { return {std::move(k), std::to_string(v), true}; }
inline Field F(std::string k, long         v) { return {std::move(k), std::to_string(v), true}; }
inline Field F(std::string k, long long    v) { return {std::move(k), std::to_string(v), true}; }
inline Field F(std::string k, bool         v) { return {std::move(k), v ? "true" : "false", true}; }

// ── Core emit function ────────────────────────────────────
inline std::mutex& logMtx() { static std::mutex m; return m; }

inline void emit(Level level,
                const std::string& component,
                const std::string& msg,
                std::initializer_list<Field> fields = {})
{
    if (level < minLevel()) return;

    std::string line;
    line.reserve(128 + msg.size());
    line += "{\"ts\":";
    line += jsonEsc(nowIso());
    line += ",\"level\":";
    line += jsonEsc(levelStr(level));
    line += ",\"component\":";
    line += jsonEsc(component);
    line += ",\"msg\":";
    line += jsonEsc(msg);

    for (const auto& f : fields) {
        line += ',';
        line += jsonEsc(f.key);
        line += ':';
        line += f.raw ? f.val : jsonEsc(f.val);
    }
    line += "}\n";

    std::lock_guard<std::mutex> lk(logMtx());
    std::cerr << line;
}

} // namespace Logger

// ── Convenience macros ────────────────────────────────────
// Allow zero or more Logger::F(...) fields as trailing arguments.
#define LOG_DEBUG(comp, msg, ...) Logger::emit(Logger::Level::DEBUG, comp, msg, {__VA_ARGS__})
#define LOG_INFO(comp, msg, ...)  Logger::emit(Logger::Level::INFO,  comp, msg, {__VA_ARGS__})
#define LOG_WARN(comp, msg, ...)  Logger::emit(Logger::Level::WARN,  comp, msg, {__VA_ARGS__})
#define LOG_ERROR(comp, msg, ...) Logger::emit(Logger::Level::ERR,   comp, msg, {__VA_ARGS__})
