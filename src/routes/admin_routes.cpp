// ============================================================
//  routes/admin_routes.cpp
//  Handlers: utilization stats, skill distribution,
//            unutilized slots, calendar events
// ============================================================
#include "admin_routes.h"
#include "app_context.h"

static void doAdminUtilization(const httplib::Request& req, httplib::Response& res) {
    auto sess = getAuth(req);
    if (!sess || sess->role != "admin") return jsonErr(res, 403, "Forbidden");
    jsonOk(res, g_db->getUtilizationStats());
}

static void doAdminSkillDist(const httplib::Request& req, httplib::Response& res) {
    auto sess = getAuth(req);
    if (!sess || sess->role != "admin") return jsonErr(res, 403, "Forbidden");
    jsonOk(res, g_db->getSkillDistribution());
}

static void doAdminUnutilized(const httplib::Request& req, httplib::Response& res) {
    auto sess = getAuth(req);
    if (!sess || sess->role != "admin") return jsonErr(res, 403, "Forbidden");
    jsonOk(res, g_db->getUnutilizedSlots());
}

// ── CSV helpers ───────────────────────────────────────────
static std::string csvQuote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) { if (c == '"') out += '"'; out += c; }
    out += '"';
    return out;
}

static void doExportUtilizationCsv(const httplib::Request& req, httplib::Response& res) {
    auto sess = getAuth(req);
    if (!sess || sess->role != "admin") return jsonErr(res, 403, "Forbidden");

    json data = g_db->getUtilizationStats();
    std::string csv =
        "Interviewer,Total Slots,Blocked Slots,Available Slots,"
        "Confirmed,Completed,Declined,Utilization %\r\n";
    for (auto& r : data) {
        int total   = r["total_slots"].get<int>();
        int blocked = r["blocked_slots"].get<int>();
        int util    = total > 0 ? (blocked * 100 / total) : 0;
        csv += csvQuote(r["interviewer_name"].get<std::string>()) + ","
             + std::to_string(total) + ","
             + std::to_string(blocked) + ","
             + std::to_string(r["available_slots"].get<int>()) + ","
             + std::to_string(r["confirmed"].get<int>()) + ","
             + std::to_string(r["completed"].get<int>()) + ","
             + std::to_string(r["declined"].get<int>()) + ","
             + std::to_string(util) + "\r\n";
    }
    res.status = 200;
    res.set_header("Content-Disposition",
        "attachment; filename=\"utilization.csv\"");
    res.set_content(csv, "text/csv");
}

static void doExportInterviewsCsv(const httplib::Request& req, httplib::Response& res) {
    auto sess = getAuth(req);
    if (!sess || sess->role != "admin") return jsonErr(res, 403, "Forbidden");

    auto interviews = g_db->getInterviewsForUser(0, "admin");
    std::string csv =
        "ID,Candidate,Candidate Email,Interviewer,Recruiter,"
        "Date,Status,Decline Reason\r\n";
    for (auto& iv : interviews) {
        csv += std::to_string(iv["id"].get<int>()) + ","
             + csvQuote(iv["candidate_name"].get<std::string>()) + ","
             + csvQuote(iv["candidate_email"].get<std::string>()) + ","
             + csvQuote(iv["interviewer_name"].get<std::string>()) + ","
             + csvQuote(iv["recruiter_name"].get<std::string>()) + ","
             + csvQuote(iv["start_time"].get<std::string>()) + ","
             + csvQuote(iv["status"].get<std::string>()) + ","
             + csvQuote(iv["decline_reason"].get<std::string>()) + "\r\n";
    }
    res.status = 200;
    res.set_header("Content-Disposition",
        "attachment; filename=\"interviews.csv\"");
    res.set_content(csv, "text/csv");
}

static void doGetCalendar(const httplib::Request& req, httplib::Response& res) {
    auto sess = getAuth(req);
    if (!sess) return jsonErr(res, 401, "Unauthorized");

    json events = json::array();

    if (sess->role == "interviewer") {
        for (auto& s : g_db->getInterviewerSlots(sess->userId)) {
            std::string color = (s["status"] == "blocked") ? "#f59e0b" : "#10b981";
            std::string title = (s["status"] == "blocked")
                ? "Booked: " + s["candidate_name"].get<std::string>()
                : "Available";
            events.push_back({
                {"id",       "slot-" + std::to_string(s["id"].get<int>())},
                {"title",    title},
                {"start",    s["start_time"]}, {"end", s["end_time"]},
                {"color",    color},
                {"type",     "slot"},
                {"slotData", s}
            });
        }
    } else {
        for (auto& iv : g_db->getInterviewsForUser(sess->userId, sess->role)) {
            std::string st    = iv["status"].get<std::string>();
            std::string color = "#3b82f6";
            if (st == "completed") color = "#10b981";
            if (st == "declined")  color = "#ef4444";
            events.push_back({
                {"id",    "iv-" + std::to_string(iv["id"].get<int>())},
                {"title", iv["candidate_name"].get<std::string>() +
                          " @ " + iv["interviewer_name"].get<std::string>()},
                {"start", iv["start_time"]}, {"end", iv["end_time"]},
                {"color", color},
                {"type",  "interview"},
                {"data",  iv}
            });
        }
    }
    jsonOk(res, events);
}

void registerAdminRoutes(httplib::Server& svr) {
    svr.Get("/api/calendar",                    doGetCalendar);
    svr.Get("/api/admin/utilization",           doAdminUtilization);
    svr.Get("/api/admin/skill-distribution",    doAdminSkillDist);
    svr.Get("/api/admin/unutilized-slots",      doAdminUnutilized);
    svr.Get("/api/admin/export/utilization",    doExportUtilizationCsv);
    svr.Get("/api/admin/export/interviews",     doExportInterviewsCsv);
}
