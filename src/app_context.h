// ============================================================
//  app_context.h
//  Shared globals (extern) and inline helper functions used
//  by every route module.  Include this in all route *.cpp
//  files.  Define the globals once in app_context.cpp.
// ============================================================
#pragma once

#include "httplib.h"
#include "json.hpp"
#include "database.h"
#include "session.h"
#include "sha256.h"
#include "smtp.h"

#include <iostream>
#include <string>
#include <regex>
#include <mutex>
#include <thread>
#include <optional>
#include "logger.h"

using json = nlohmann::json;

constexpr const char* APP_VERSION = "1.0.0";

// ── Shared globals (defined in app_context.cpp) ───────────
extern Database*          g_db;
extern SessionManager*    g_sessions;
extern std::mutex         g_mutex;
extern SmtpConfig         g_smtp;
extern LoginRateLimiter   g_limiter;

// ── JSON response helpers ─────────────────────────────────
inline void jsonOk(httplib::Response& res, const json& body) {
    res.status = 200;
    res.set_content(body.dump(), "application/json");
}
inline void jsonCreated(httplib::Response& res, const json& body) {
    res.status = 201;
    res.set_content(body.dump(), "application/json");
}
inline void jsonErr(httplib::Response& res, int code, const std::string& msg) {
    res.status = code;
    res.set_content(json{{"error", msg}}.dump(), "application/json");
}

// ── Auth helper ───────────────────────────────────────────
inline std::optional<Session> getAuth(const httplib::Request& req) {
    auto it = req.headers.find("Authorization");
    if (it == req.headers.end()) return {};
    return g_sessions->getSession(SessionManager::extractToken(it->second));
}

// ── Datetime helpers ──────────────────────────────────────
inline bool isValidDate(const std::string& s) {
    static const std::regex re(R"(\d{4}-\d{2}-\d{2})");
    return std::regex_match(s, re);
}
inline bool isValidDatetime(const std::string& s) {
    static const std::regex re(R"(\d{4}-\d{2}-\d{2}[T ]\d{2}:\d{2})");
    return std::regex_match(s, re);
}
inline std::string normDt(std::string s) {
    for (auto& c : s) if (c == 'T') c = ' ';
    return s;
}
