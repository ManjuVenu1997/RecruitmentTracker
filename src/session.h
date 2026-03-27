#pragma once
#include <string>
#include <unordered_map>
#include <mutex>
#include <random>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <optional>
#include "database.h"

// ─────────────────────────────────────────────
//  Login rate-limiter  (per username, in-memory)
// ─────────────────────────────────────────────
class LoginRateLimiter {
    static constexpr int  MAX_ATTEMPTS  = 4;
    static constexpr int  LOCKOUT_SECS  = 300; // 5 minutes

    struct Entry {
        int  failures = 0;
        std::chrono::steady_clock::time_point lockedUntil{};
    };

    std::unordered_map<std::string, Entry> map_;
    std::mutex mutex_;

public:
    // Returns 0 if allowed, or seconds remaining until unlock.
    int check(const std::string& username) {
        std::lock_guard<std::mutex> lk(mutex_);
        auto it = map_.find(username);
        if (it == map_.end()) return 0;
        auto now = std::chrono::steady_clock::now();
        if (it->second.failures >= MAX_ATTEMPTS) {
            int secs = (int)std::chrono::duration_cast<std::chrono::seconds>(
                it->second.lockedUntil - now).count();
            if (secs > 0) return secs;
            // Lockout expired — reset
            map_.erase(it);
        }
        return 0;
    }

    // Call on FAILED login. Returns true if account just became locked.
    bool recordFailure(const std::string& username) {
        std::lock_guard<std::mutex> lk(mutex_);
        auto& e = map_[username];
        e.failures++;
        if (e.failures >= MAX_ATTEMPTS) {
            e.lockedUntil = std::chrono::steady_clock::now() +
                            std::chrono::seconds(LOCKOUT_SECS);
            return true;
        }
        return false;
    }

    // Call on SUCCESSFUL login — clear record.
    void reset(const std::string& username) {
        std::lock_guard<std::mutex> lk(mutex_);
        map_.erase(username);
    }

    int failureCount(const std::string& username) {
        std::lock_guard<std::mutex> lk(mutex_);
        auto it = map_.find(username);
        return it == map_.end() ? 0 : it->second.failures;
    }
};

struct Session {
    int         userId;
    std::string username;
    std::string fullName;
    std::string role;
};

class SessionManager {
    Database* db_;

    static std::string generateToken() {
        std::random_device rd;
        std::mt19937_64 gen(rd());
        std::uniform_int_distribution<uint64_t> dist;
        std::ostringstream oss;
        for (int i = 0; i < 4; ++i)
            oss << std::hex << std::setw(16) << std::setfill('0') << dist(gen);
        return oss.str();
    }

public:
    explicit SessionManager(Database* db) : db_(db) {}

    std::string createSession(int userId, const std::string& username,
                              const std::string& fullName, const std::string& role) {
        auto token = generateToken();
        db_->persistSession(token, userId, username, fullName, role);
        return token;
    }

    std::optional<Session> getSession(const std::string& token) {
        if (token.empty()) return std::nullopt;
        auto rec = db_->lookupSession(token);
        if (!rec) return std::nullopt;
        return Session{ std::get<0>(*rec), std::get<1>(*rec),
                        std::get<2>(*rec), std::get<3>(*rec) };
    }

    void removeSession(const std::string& token) {
        db_->deleteSession(token);
    }

    static std::string extractToken(const std::string& authHeader) {
        if (authHeader.size() > 7 && authHeader.substr(0, 7) == "Bearer ")
            return authHeader.substr(7);
        return "";
    }
};
