// ============================================================
//  routes/skill_routes.cpp
//  Handlers: list skills, create skill, interviewer skill mgmt
// ============================================================
#include "skill_routes.h"
#include "app_context.h"

static void doGetSkills(const httplib::Request& req, httplib::Response& res) {
    auto sess = getAuth(req);
    if (!sess) return jsonErr(res, 401, "Unauthorized");
    json arr = json::array();
    for (auto& s : g_db->getAllSkills()) arr.push_back(s.toJson());
    jsonOk(res, arr);
}

static void doCreateSkill(const httplib::Request& req, httplib::Response& res) {
    auto sess = getAuth(req);
    if (!sess || (sess->role != "admin" && sess->role != "interviewer"))
        return jsonErr(res, 403, "Forbidden");

    json body;
    try { body = json::parse(req.body); } catch (...) {
        return jsonErr(res, 400, "Invalid JSON"); }

    if (!body.contains("name") || body["name"].get<std::string>().empty())
        return jsonErr(res, 400, "name is required");

    std::string name = body["name"].get<std::string>();
    if (name.size() > 100) return jsonErr(res, 400, "Skill name too long");

    std::lock_guard<std::mutex> lock(g_mutex);
    int id = g_db->createOrGetSkill(name);
    if (id < 0) return jsonErr(res, 500, "Failed to create skill");
    jsonCreated(res, {{"id", id}, {"name", name}});
}

static void doGetInterviewerSkills(const httplib::Request& req, httplib::Response& res) {
    auto sess = getAuth(req);
    if (!sess) return jsonErr(res, 401, "Unauthorized");

    int iid = std::stoi(req.path_params.at("id"));
    json arr = json::array();
    for (auto& s : g_db->getInterviewerSkills(iid)) arr.push_back(s.toJson());
    jsonOk(res, arr);
}

static void doAddInterviewerSkill(const httplib::Request& req, httplib::Response& res) {
    auto sess = getAuth(req);
    if (!sess) return jsonErr(res, 401, "Unauthorized");

    int iid = std::stoi(req.path_params.at("id"));
    if (sess->role == "interviewer" && sess->userId != iid)
        return jsonErr(res, 403, "Forbidden");
    if (sess->role == "recruiter") return jsonErr(res, 403, "Forbidden");

    json body;
    try { body = json::parse(req.body); } catch (...) {
        return jsonErr(res, 400, "Invalid JSON"); }

    if (!body.contains("skill_id")) return jsonErr(res, 400, "skill_id required");
    int sid = body["skill_id"].get<int>();

    std::lock_guard<std::mutex> lock(g_mutex);
    g_db->addInterviewerSkill(iid, sid);
    jsonCreated(res, {{"message", "Skill added"}});
}

static void doRemoveInterviewerSkill(const httplib::Request& req, httplib::Response& res) {
    auto sess = getAuth(req);
    if (!sess) return jsonErr(res, 401, "Unauthorized");

    int iid = std::stoi(req.path_params.at("id"));
    int sid = std::stoi(req.path_params.at("skill_id"));
    if (sess->role == "interviewer" && sess->userId != iid)
        return jsonErr(res, 403, "Forbidden");
    if (sess->role == "recruiter") return jsonErr(res, 403, "Forbidden");

    std::lock_guard<std::mutex> lock(g_mutex);
    g_db->removeInterviewerSkill(iid, sid);
    jsonOk(res, {{"message", "Skill removed"}});
}

void registerSkillRoutes(httplib::Server& svr) {
    svr.Get   ("/api/skills",                             doGetSkills);
    svr.Post  ("/api/skills",                             doCreateSkill);
    svr.Get   ("/api/interviewers/:id/skills",            doGetInterviewerSkills);
    svr.Post  ("/api/interviewers/:id/skills",            doAddInterviewerSkill);
    svr.Delete("/api/interviewers/:id/skills/:skill_id",  doRemoveInterviewerSkill);
}
