// ============================================================
//  routes/auth_routes.cpp
//  Handlers: login, logout, me, change-password,
//            forgot-password, reset-password, refresh, profile
// ============================================================
#include "auth_routes.h"
#include "app_context.h"
#include "smtp.h"

static void doChangePassword(const httplib::Request& req, httplib::Response& res) {
    auto sess = getAuth(req);
    if (!sess) return jsonErr(res, 401, "Unauthorized");

    json body;
    try { body = json::parse(req.body); } catch (...) {
        return jsonErr(res, 400, "Invalid JSON"); }

    if (!body.contains("current_password") || !body.contains("new_password"))
        return jsonErr(res, 400, "current_password and new_password required");

    std::string curPw  = body["current_password"].get<std::string>();
    std::string newPw  = body["new_password"].get<std::string>();

    if (newPw.size() < 6)   return jsonErr(res, 400, "New password must be at least 6 characters");
    if (newPw.size() > 200) return jsonErr(res, 400, "New password too long");

    auto userOpt = g_db->getUserById(sess->userId);
    if (!userOpt) return jsonErr(res, 404, "User not found");

    if (sha256::hash(curPw) != userOpt->passwordHash) {
        LOG_WARN("Auth", "Password change rejected",
                 Logger::F("user_id", sess->userId),
                 Logger::F("reason", "wrong current password"));
        return jsonErr(res, 403, "Current password is incorrect");
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_db->updatePassword(sess->userId, sha256::hash(newPw)))
        return jsonErr(res, 500, "Failed to update password");

    LOG_INFO("Auth", "Password changed", Logger::F("user_id", sess->userId));
    jsonOk(res, {{"message", "Password changed successfully"}});
}

static void doLogin(const httplib::Request& req, httplib::Response& res) {
    json body;
    try { body = json::parse(req.body); } catch (...) {
        return jsonErr(res, 400, "Invalid JSON"); }

    if (!body.contains("username") || !body.contains("password"))
        return jsonErr(res, 400, "username and password required");

    std::string username = body["username"].get<std::string>();
    std::string password = body["password"].get<std::string>();

    if (username.empty() || username.size() > 64 || password.empty())
        return jsonErr(res, 400, "Invalid credentials");

    // Rate-limit check BEFORE touching the DB
    int lockSecs = g_limiter.check(username);
    if (lockSecs > 0) {
        int mins = (lockSecs + 59) / 60;
        LOG_WARN("Auth", "Login blocked - account locked",
                 Logger::F("user",           username),
                 Logger::F("retry_in_secs",  lockSecs));
        return jsonErr(res, 429,
            "Account temporarily locked after too many failed attempts. "
            "Try again in " + std::to_string(mins) + " minute(s).");
    }

    auto userOpt = g_db->getUserByUsername(username);
    if (!userOpt || sha256::hash(password) != userOpt->passwordHash) {
        bool locked = g_limiter.recordFailure(username);
        int  left   = 4 - g_limiter.failureCount(username);
        if (locked) {
            LOG_WARN("Auth", "Account locked after repeated failures",
                     Logger::F("user", username));
            return jsonErr(res, 429,
                "Account locked after 4 failed attempts. Try again in 5 minutes.");
        }
        LOG_WARN("Auth", "Login failed",
                 Logger::F("user",      username),
                 Logger::F("remaining", left < 0 ? 0 : left));
        return jsonErr(res, 401,
            "Invalid username or password. "
            + std::to_string(left < 0 ? 0 : left) + " attempt(s) remaining.");
    }

    g_limiter.reset(username);  // success — clear failure record

    std::string token = g_sessions->createSession(
        userOpt->id, userOpt->username, userOpt->fullName, userOpt->role);

    LOG_INFO("Auth", "Login success",
             Logger::F("user",    username),
             Logger::F("role",    userOpt->role),
             Logger::F("user_id", userOpt->id));
    jsonOk(res, {{"token", token}, {"user", userOpt->toJson()}});
}

static void doLogout(const httplib::Request& req, httplib::Response& res) {
    auto it = req.headers.find("Authorization");
    if (it != req.headers.end())
        g_sessions->removeSession(SessionManager::extractToken(it->second));
    jsonOk(res, {{"message", "Logged out"}});
}

static void doGetMe(const httplib::Request& req, httplib::Response& res) {
    auto sess = getAuth(req);
    if (!sess) return jsonErr(res, 401, "Unauthorized");
    auto userOpt = g_db->getUserById(sess->userId);
    if (!userOpt) return jsonErr(res, 404, "User not found");
    jsonOk(res, userOpt->toJson());
}

// ── Profile (self-edit) ───────────────────────────────────
static void doGetProfile(const httplib::Request& req, httplib::Response& res) {
    auto sess = getAuth(req);
    if (!sess) return jsonErr(res, 401, "Unauthorized");
    auto userOpt = g_db->getUserById(sess->userId);
    if (!userOpt) return jsonErr(res, 404, "User not found");
    jsonOk(res, userOpt->toJson());
}

static void doUpdateProfile(const httplib::Request& req, httplib::Response& res) {
    auto sess = getAuth(req);
    if (!sess) return jsonErr(res, 401, "Unauthorized");

    json body;
    try { body = json::parse(req.body); } catch (...) {
        return jsonErr(res, 400, "Invalid JSON"); }

    if (!body.contains("full_name") || body["full_name"].get<std::string>().empty())
        return jsonErr(res, 400, "full_name is required");
    if (!body.contains("email") || body["email"].get<std::string>().empty())
        return jsonErr(res, 400, "email is required");

    std::string fullName = body["full_name"].get<std::string>();
    std::string email    = body["email"].get<std::string>();
    if (fullName.size() > 200) return jsonErr(res, 400, "full_name too long");
    if (email.size()    > 200) return jsonErr(res, 400, "email too long");

    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_db->updateUserProfile(sess->userId, fullName, email))
        return jsonErr(res, 500, "Failed to update profile");
    LOG_INFO("Auth", "Profile updated", Logger::F("user_id", sess->userId));
    jsonOk(res, {{"message", "Profile updated successfully"}});
}

// ── Token refresh ─────────────────────────────────────────
static void doRefreshToken(const httplib::Request& req, httplib::Response& res) {
    auto it = req.headers.find("Authorization");
    if (it == req.headers.end()) return jsonErr(res, 401, "Unauthorized");
    std::string token = SessionManager::extractToken(it->second);
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_db->refreshSession(token))
        return jsonErr(res, 401, "Session expired or invalid");
    jsonOk(res, {{"message", "Session refreshed"}});
}

// ── Forgot / reset password ───────────────────────────────
static void doForgotPassword(const httplib::Request& req, httplib::Response& res) {
    json body;
    try { body = json::parse(req.body); } catch (...) {
        return jsonErr(res, 400, "Invalid JSON"); }

    if (!body.contains("email") || body["email"].get<std::string>().empty())
        return jsonErr(res, 400, "email is required");

    std::string email = body["email"].get<std::string>();
    if (email.size() > 200) return jsonErr(res, 400, "Invalid email");

    // Always return same response to prevent email enumeration
    const json okResp = {{"message",
        "If an account with that email exists, a reset link has been sent."}};

    std::optional<User> userOpt;
    std::string         token;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        userOpt = g_db->getUserByEmail(email);
        if (userOpt)
            token = g_db->createPasswordResetToken(userOpt->id);
    }

    if (userOpt && !token.empty()) {
        std::string resetLink = "http://localhost:8080/reset-password.html?token=" + token;
        std::string subject   = "Password Reset Request – Interview Scheduler";
        std::string htmlBody  =
            "<html><body style='font-family:sans-serif;max-width:480px;margin:40px auto'>"
            "<h2 style='color:#1e293b'>Password Reset Request</h2>"
            "<p>Hello " + userOpt->fullName + ",</p>"
            "<p>We received a request to reset your <strong>Interview Scheduler</strong> password.</p>"
            "<p>Click the button below — the link is valid for <strong>1 hour</strong>.</p>"
            "<p style='margin:28px 0'>"
            "  <a href='" + resetLink + "' style='background:#3b82f6;color:#fff;"
            "     padding:12px 28px;text-decoration:none;border-radius:6px;font-weight:bold'>"
            "    Reset Password</a></p>"
            "<p style='color:#64748b;font-size:0.9em'>"
            "  If you didn't request this, ignore this email.<br>"
            "  Or paste this link: <a href='" + resetLink + "'>" + resetLink + "</a></p>"
            "</body></html>";

        LOG_INFO("Auth", "Password reset token created",
                 Logger::F("user_id", userOpt->id),
                 Logger::F("user",    userOpt->username));
        if (g_smtp.enabled)
            sendEmailAsync(g_smtp, userOpt->email, userOpt->fullName, subject, htmlBody);
        else
            LOG_WARN("Auth", "SMTP disabled - reset token logged for dev",
                     Logger::F("user",  userOpt->username),
                     Logger::F("token", token));
    }

    jsonOk(res, okResp);
}

static void doResetPassword(const httplib::Request& req, httplib::Response& res) {
    json body;
    try { body = json::parse(req.body); } catch (...) {
        return jsonErr(res, 400, "Invalid JSON"); }

    if (!body.contains("token") || !body.contains("new_password"))
        return jsonErr(res, 400, "token and new_password are required");

    std::string token = body["token"].get<std::string>();
    std::string newPw = body["new_password"].get<std::string>();

    if (token.size() > 128) return jsonErr(res, 400, "Invalid token");
    if (newPw.size() < 6)   return jsonErr(res, 400, "Password must be at least 6 characters");
    if (newPw.size() > 200) return jsonErr(res, 400, "Password too long");

    std::lock_guard<std::mutex> lock(g_mutex);
    auto userIdOpt = g_db->validateResetToken(token);
    if (!userIdOpt) return jsonErr(res, 400, "Invalid or expired reset token");

    g_db->updatePassword(*userIdOpt, sha256::hash(newPw));
    g_db->consumeResetToken(token);
    LOG_INFO("Auth", "Password reset completed", Logger::F("user_id", *userIdOpt));
    jsonOk(res, {{"message",
        "Password reset successfully. You can now sign in with your new password."}});
}

void registerAuthRoutes(httplib::Server& svr) {
    svr.Post("/api/auth/login",            doLogin);
    svr.Post("/api/auth/logout",           doLogout);
    svr.Get ("/api/auth/me",               doGetMe);
    svr.Post("/api/auth/change-password",  doChangePassword);
    svr.Post("/api/auth/forgot-password",  doForgotPassword);
    svr.Post("/api/auth/reset-password",   doResetPassword);
    svr.Post("/api/auth/refresh",          doRefreshToken);
    svr.Get ("/api/auth/profile",          doGetProfile);
    svr.Put ("/api/auth/profile",          doUpdateProfile);
    svr.Get ("/api/version", [](const httplib::Request&, httplib::Response& res) {
        res.status = 200;
        res.set_content(
            json{{"version", APP_VERSION}}.dump(), "application/json");
    });
}
