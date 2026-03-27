// ============================================================
//  routes/slot_routes.cpp
//  Handlers: get/create/delete interviewer slots, available slots
// ============================================================
#include "slot_routes.h"
#include "app_context.h"

static void doGetInterviewerSlots(const httplib::Request& req, httplib::Response& res) {
    auto sess = getAuth(req);
    if (!sess) return jsonErr(res, 401, "Unauthorized");

    int iid = std::stoi(req.path_params.at("id"));
    json arr = json::array();
    for (auto& s : g_db->getInterviewerSlots(iid)) arr.push_back(s);
    jsonOk(res, arr);
}

static void doCreateSlot(const httplib::Request& req, httplib::Response& res) {
    auto sess = getAuth(req);
    if (!sess || (sess->role != "interviewer" && sess->role != "admin"))
        return jsonErr(res, 403, "Forbidden");

    int iid = std::stoi(req.path_params.at("id"));
    if (sess->role == "interviewer" && sess->userId != iid)
        return jsonErr(res, 403, "You can only manage your own slots");

    json body;
    try { body = json::parse(req.body); } catch (...) {
        return jsonErr(res, 400, "Invalid JSON"); }

    if (!body.contains("start_time") || !body.contains("end_time"))
        return jsonErr(res, 400, "start_time and end_time required");

    std::string start = normDt(body["start_time"].get<std::string>());
    std::string end   = normDt(body["end_time"].get<std::string>());

    if (!isValidDatetime(start) || !isValidDatetime(end))
        return jsonErr(res, 400, "Invalid datetime format. Use YYYY-MM-DDTHH:MM");
    if (start >= end)
        return jsonErr(res, 400, "start_time must be before end_time");

    std::lock_guard<std::mutex> lock(g_mutex);
    int id = g_db->createSlot(iid, start, end);
    if (id < 0) return jsonErr(res, 500, "Failed to create slot");
    auto slotOpt = g_db->getSlotById(id);
    jsonCreated(res, slotOpt ? *slotOpt : json{{"id", id}});
}

static void doDeleteSlot(const httplib::Request& req, httplib::Response& res) {
    auto sess = getAuth(req);
    if (!sess || (sess->role != "interviewer" && sess->role != "admin"))
        return jsonErr(res, 403, "Forbidden");

    int slotId  = std::stoi(req.path_params.at("id"));
    int ownerId = sess->userId;
    if (sess->role == "admin") {
        auto slotOpt = g_db->getSlotById(slotId);
        if (!slotOpt) return jsonErr(res, 404, "Slot not found");
        ownerId = (*slotOpt)["interviewer_id"].get<int>();
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_db->deleteSlot(slotId, ownerId))
        return jsonErr(res, 400, "Cannot delete slot (not found, not yours, or already booked)");
    jsonOk(res, {{"message", "Slot deleted"}});
}

static void doGetAvailableSlots(const httplib::Request& req, httplib::Response& res) {
    auto sess = getAuth(req);
    if (!sess) return jsonErr(res, 401, "Unauthorized");
    if (sess->role != "recruiter" && sess->role != "admin")
        return jsonErr(res, 403, "Forbidden");

    std::string skill = req.get_param_value("skill");
    std::string date  = req.get_param_value("date");

    if (!date.empty() && !isValidDate(date))
        return jsonErr(res, 400, "Invalid date format. Use YYYY-MM-DD");

    json arr = json::array();
    for (auto& s : g_db->getAvailableSlots(skill, date)) arr.push_back(s);
    jsonOk(res, arr);
}

void registerSlotRoutes(httplib::Server& svr) {
    svr.Get   ("/api/interviewers/:id/slots", doGetInterviewerSlots);
    svr.Post  ("/api/interviewers/:id/slots", doCreateSlot);
    svr.Delete("/api/slots/:id",              doDeleteSlot);
    svr.Get   ("/api/available-slots",        doGetAvailableSlots);
}
