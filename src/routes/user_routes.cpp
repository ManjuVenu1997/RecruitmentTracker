// ============================================================
//  routes/user_routes.cpp
//  Handlers: list users, create, update, delete, reset-password
// ============================================================
#include "user_routes.h"
#include "app_context.h"
#include <algorithm>

static void doUpdateUser(const httplib::Request& req, httplib::Response& res) {
    auto sess = getAuth(req);
    if (!sess || sess->role != "admin") return jsonErr(res, 403, "Forbidden");

    int uid = std::stoi(req.path_params.at("id"));

    json body;
    try { body = json::parse(req.body); } catch (...) {
        return jsonErr(res, 400, "Invalid JSON"); }

    for (auto& f : {"full_name","email","role"})
        if (!body.contains(f) || body[f].get<std::string>().empty())
            return jsonErr(res, 400, std::string(f) + " is required");

    std::string role = body["role"].get<std::string>();
    if (role != "recruiter" && role != "interviewer" && role != "admin")
        return jsonErr(res, 400, "Invalid role");

    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_db->updateUser(uid,
            body["full_name"].get<std::string>(),
            body["email"].get<std::string>(), role))
        return jsonErr(res, 404, "User not found");

    jsonOk(res, {{"message", "User updated"}});
}

static void doDeleteUser(const httplib::Request& req, httplib::Response& res) {
    auto sess = getAuth(req);
    if (!sess || sess->role != "admin") return jsonErr(res, 403, "Forbidden");

    int uid = std::stoi(req.path_params.at("id"));
    if (uid == sess->userId)
        return jsonErr(res, 400, "Cannot delete your own account");

    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_db->softDeleteUser(uid))
        return jsonErr(res, 404, "User not found");

    jsonOk(res, {{"message", "User deleted"}});
}

static void doAdminResetPassword(const httplib::Request& req, httplib::Response& res) {
    auto sess = getAuth(req);
    if (!sess || sess->role != "admin") return jsonErr(res, 403, "Forbidden");

    int uid = std::stoi(req.path_params.at("id"));
    json body;
    try { body = json::parse(req.body); } catch (...) {
        return jsonErr(res, 400, "Invalid JSON"); }

    if (!body.contains("new_password") || body["new_password"].get<std::string>().size() < 6)
        return jsonErr(res, 400, "new_password must be at least 6 characters");

    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_db->updatePassword(uid, sha256::hash(body["new_password"].get<std::string>())))
        return jsonErr(res, 404, "User not found");

    jsonOk(res, {{"message", "Password reset successfully"}});
}

static void doGetUsers(const httplib::Request& req, httplib::Response& res) {
    auto sess = getAuth(req);
    if (!sess) return jsonErr(res, 401, "Unauthorized");

    std::string roleFilter = req.get_param_value("role");
    json arr = json::array();

    // Interviewer-list for dropdowns (no pagination needed)
    if (roleFilter == "interviewer") {
        for (auto& u : g_db->getInterviewers()) {
            auto j = u.toJson();
            json skills = json::array();
            for (auto& s : g_db->getInterviewerSkills(u.id))
                skills.push_back(s.toJson());
            j["skills"] = skills;
            arr.push_back(j);
        }
        return jsonOk(res, arr);
    }

    if (sess->role != "admin") return jsonErr(res, 403, "Forbidden");

    // Paginated admin user management list
    std::string search = req.get_param_value("search");
    int page     = 1;
    int pageSize = 20;
    try {
        std::string p = req.get_param_value("page");
        if (!p.empty()) page = std::stoi(p);
        std::string ps = req.get_param_value("page_size");
        if (!ps.empty()) pageSize = std::stoi(ps);
    } catch (...) {}
    page     = std::max(1, page);
    pageSize = std::max(1, std::min(100, pageSize));

    auto result     = g_db->getUsersPaged(search, page, pageSize);
    int  totalPages = (result.total + pageSize - 1) / pageSize;
    if (totalPages < 1) totalPages = 1;

    json resp = {
        {"users",       json::array()},
        {"total",       result.total},
        {"page",        page},
        {"page_size",   pageSize},
        {"total_pages", totalPages}
    };
    for (auto& u : result.users)
        resp["users"].push_back(u.toJson());
    jsonOk(res, resp);
}

static void doCreateUser(const httplib::Request& req, httplib::Response& res) {
    auto sess = getAuth(req);
    if (!sess || sess->role != "admin") return jsonErr(res, 403, "Forbidden");

    json body;
    try { body = json::parse(req.body); } catch (...) {
        return jsonErr(res, 400, "Invalid JSON"); }

    for (auto& f : {"username","password","email","full_name","role"})
        if (!body.contains(f) || body[f].get<std::string>().empty())
            return jsonErr(res, 400, std::string(f) + " is required");

    std::string role = body["role"].get<std::string>();
    if (role != "recruiter" && role != "interviewer" && role != "admin")
        return jsonErr(res, 400, "role must be recruiter, interviewer, or admin");

    std::string pwHash = sha256::hash(body["password"].get<std::string>());

    std::lock_guard<std::mutex> lock(g_mutex);
    bool ok = g_db->createUser(
        body["username"].get<std::string>(), pwHash,
        body["email"].get<std::string>(),
        body["full_name"].get<std::string>(), role);

    if (!ok) return jsonErr(res, 409, "Username or email already exists");
    jsonCreated(res, {{"message", "User created"}});
}

void registerUserRoutes(httplib::Server& svr) {
    svr.Get   ("/api/users",                    doGetUsers);
    svr.Post  ("/api/users",                    doCreateUser);
    svr.Put   ("/api/users/:id",                doUpdateUser);
    svr.Delete("/api/users/:id",                doDeleteUser);
    svr.Post  ("/api/users/:id/reset-password", doAdminResetPassword);
}
