// ============================================================
//  routes/interview_routes.cpp
//  Handlers: book, list, decline, complete interviews
// ============================================================
#include "interview_routes.h"
#include "app_context.h"

static void doBookInterview(const httplib::Request& req, httplib::Response& res) {
    auto sess = getAuth(req);
    if (!sess || sess->role != "recruiter")
        return jsonErr(res, 403, "Only recruiters can book interviews");

    json body;
    try { body = json::parse(req.body); } catch (...) {
        return jsonErr(res, 400, "Invalid JSON"); }

    for (auto& f : {"slot_id","candidate_name","candidate_email"})
        if (!body.contains(f)) return jsonErr(res, 400, std::string(f) + " required");

    int slotId            = body["slot_id"].get<int>();
    std::string candName  = body["candidate_name"].get<std::string>();
    std::string candEmail = body["candidate_email"].get<std::string>();

    if (candName.empty()  || candName.size()  > 200) return jsonErr(res, 400, "Invalid candidate_name");
    if (candEmail.empty() || candEmail.size() > 200) return jsonErr(res, 400, "Invalid candidate_email");

    std::lock_guard<std::mutex> lock(g_mutex);

    auto slotOpt = g_db->getSlotById(slotId);
    if (!slotOpt) return jsonErr(res, 404, "Slot not found");
    if ((*slotOpt)["status"].get<std::string>() != "available")
        return jsonErr(res, 409, "Slot is no longer available");

    int interviewId = g_db->createInterview(slotId, sess->userId, candName, candEmail);
    if (interviewId < 0) return jsonErr(res, 500, "Failed to book interview");

    g_db->updateSlotStatus(slotId, "blocked");

    int interviewerId   = (*slotOpt)["interviewer_id"].get<int>();
    std::string start_t = (*slotOpt)["start_time"].get<std::string>();
    std::string end_t   = (*slotOpt)["end_time"].get<std::string>();

    g_db->createNotification(interviewerId,
        "New interview scheduled: " + candName + " on " + start_t,
        "booking", interviewId);

    LOG_INFO("Interview", "Interview booked",
             Logger::F("interview_id",  interviewId),
             Logger::F("slot_id",       slotId),
             Logger::F("recruiter_id",  sess->userId),
             Logger::F("interviewer_id",interviewerId),
             Logger::F("candidate",     candName),
             Logger::F("start_time",    start_t));

    auto ivOpt = g_db->getInterviewById(interviewId);

    // Send confirmation email to candidate asynchronously
    if (g_smtp.enabled && !candEmail.empty()) {
        std::string ivrName = ivOpt
            ? (*ivOpt)["interviewer_name"].get<std::string>() : "Interviewer";
        std::string emailBody =
            smtp_internal::buildInterviewEmail(candName, start_t, end_t, ivrName);
        std::string subject = "Interview Scheduled \xe2\x80\x93 " + start_t.substr(0, 10);
        sendEmailAsync(g_smtp, candEmail, candName, subject, emailBody);
    }

    jsonCreated(res, ivOpt ? *ivOpt : json{{"id", interviewId}});
}

static void doGetInterviews(const httplib::Request& req, httplib::Response& res) {
    auto sess = getAuth(req);
    if (!sess) return jsonErr(res, 401, "Unauthorized");
    json arr = json::array();
    for (auto& iv : g_db->getInterviewsForUser(sess->userId, sess->role))
        arr.push_back(iv);
    jsonOk(res, arr);
}

static void doDeclineInterview(const httplib::Request& req, httplib::Response& res) {
    auto sess = getAuth(req);
    if (!sess || sess->role != "interviewer")
        return jsonErr(res, 403, "Only interviewers can decline");

    int ivId = std::stoi(req.path_params.at("id"));

    json body;
    try { body = json::parse(req.body); } catch (...) {
        return jsonErr(res, 400, "Invalid JSON"); }

    std::string reason = body.value("reason", "");
    if (reason.empty())      return jsonErr(res, 400, "Decline reason is required");
    if (reason.size() > 500) return jsonErr(res, 400, "Reason too long");

    std::lock_guard<std::mutex> lock(g_mutex);

    auto ivOpt = g_db->getInterviewById(ivId);
    if (!ivOpt) return jsonErr(res, 404, "Interview not found");

    if ((*ivOpt)["interviewer_id"].get<int>() != sess->userId)
        return jsonErr(res, 403, "Not your interview");
    if ((*ivOpt)["status"].get<std::string>() != "confirmed")
        return jsonErr(res, 400, "Only confirmed interviews can be declined");

    g_db->updateInterviewStatus(ivId, "declined", reason);
    g_db->updateSlotStatus((*ivOpt)["slot_id"].get<int>(), "available");

    int recruiterId      = (*ivOpt)["recruiter_id"].get<int>();
    std::string candName = (*ivOpt)["candidate_name"].get<std::string>();
    std::string start_t  = (*ivOpt)["start_time"].get<std::string>();
    g_db->createNotification(recruiterId,
        "Interview declined by " + sess->fullName + " for " + candName +
        " (" + start_t + "). Reason: " + reason,
        "decline", ivId);

    LOG_WARN("Interview", "Interview declined",
             Logger::F("interview_id",  ivId),
             Logger::F("interviewer_id",sess->userId),
             Logger::F("recruiter_id",  recruiterId),
             Logger::F("candidate",     candName),
             Logger::F("reason",        reason));
    jsonOk(res, {{"message", "Interview declined, slot released"}});
}

static void doCompleteInterview(const httplib::Request& req, httplib::Response& res) {
    auto sess = getAuth(req);
    if (!sess || (sess->role != "admin" && sess->role != "interviewer"))
        return jsonErr(res, 403, "Forbidden");

    int ivId = std::stoi(req.path_params.at("id"));

    std::lock_guard<std::mutex> lock(g_mutex);
    auto ivOpt = g_db->getInterviewById(ivId);
    if (!ivOpt) return jsonErr(res, 404, "Interview not found");

    if (sess->role == "interviewer" &&
        (*ivOpt)["interviewer_id"].get<int>() != sess->userId)
        return jsonErr(res, 403, "Not your interview");

    g_db->updateInterviewStatus(ivId, "completed");
    LOG_INFO("Interview", "Interview completed",
             Logger::F("interview_id", ivId),
             Logger::F("marked_by",   sess->userId));
    jsonOk(res, {{"message", "Interview marked as completed"}});
}

// ─────────────────────────────────────────────
//  FEEDBACK handlers
// ─────────────────────────────────────────────
static void doSubmitFeedback(const httplib::Request& req, httplib::Response& res) {
    auto sess = getAuth(req);
    if (!sess || sess->role != "interviewer")
        return jsonErr(res, 403, "Only interviewers can submit feedback");

    int ivId = std::stoi(req.path_params.at("id"));

    std::lock_guard<std::mutex> lock(g_mutex);
    auto ivOpt = g_db->getInterviewById(ivId);
    if (!ivOpt) return jsonErr(res, 404, "Interview not found");
    if ((*ivOpt)["interviewer_id"].get<int>() != sess->userId)
        return jsonErr(res, 403, "Not your interview");
    if ((*ivOpt)["status"].get<std::string>() != "completed")
        return jsonErr(res, 400, "Feedback can only be submitted for completed interviews");

    json body;
    try { body = json::parse(req.body); } catch (...) {
        return jsonErr(res, 400, "Invalid JSON"); }

    if (!body.contains("rating"))
        return jsonErr(res, 400, "rating is required (1-5)");
    int rating = body["rating"].get<int>();
    if (rating < 1 || rating > 5)
        return jsonErr(res, 400, "rating must be between 1 and 5");

    std::string notes = body.value("notes", "");
    if (notes.size() > 2000)
        return jsonErr(res, 400, "notes too long (max 2000 chars)");

    if (!g_db->submitFeedback(ivId, rating, notes))
        return jsonErr(res, 500, "Failed to save feedback");

    // Notify recruiter that feedback is available
    int recruiterId = (*ivOpt)["recruiter_id"].get<int>();
    std::string candName = (*ivOpt)["candidate_name"].get<std::string>();
    g_db->createNotification(recruiterId,
        sess->fullName + " submitted feedback for " + candName +
        " (Rating: " + std::to_string(rating) + "/5)",
        "feedback", ivId);

    jsonOk(res, {{"message", "Feedback submitted"}});
}

static void doGetFeedback(const httplib::Request& req, httplib::Response& res) {
    auto sess = getAuth(req);
    if (!sess) return jsonErr(res, 401, "Unauthorized");

    int ivId = std::stoi(req.path_params.at("id"));

    // Interviewers can only see their own; recruiters/admin see any
    if (sess->role == "interviewer") {
        auto ivOpt = g_db->getInterviewById(ivId);
        if (!ivOpt || (*ivOpt)["interviewer_id"].get<int>() != sess->userId)
            return jsonErr(res, 403, "Forbidden");
    }

    auto fb = g_db->getFeedback(ivId);
    if (!fb) return jsonErr(res, 404, "No feedback submitted yet");
    jsonOk(res, *fb);
}

static void doListFeedback(const httplib::Request& req, httplib::Response& res) {
    auto sess = getAuth(req);
    if (!sess) return jsonErr(res, 401, "Unauthorized");
    if (sess->role == "interviewer")
        return jsonErr(res, 403, "Forbidden");

    json arr = json::array();
    if (sess->role == "recruiter") {
        for (auto& f : g_db->getFeedbackForRecruiter(sess->userId)) arr.push_back(f);
    } else { // admin
        for (auto& f : g_db->getAllFeedback()) arr.push_back(f);
    }
    jsonOk(res, arr);
}

void registerInterviewRoutes(httplib::Server& svr) {
    svr.Post("/api/interviews",                  doBookInterview);
    svr.Get ("/api/interviews",                  doGetInterviews);
    svr.Put ("/api/interviews/:id/decline",      doDeclineInterview);
    svr.Put ("/api/interviews/:id/complete",     doCompleteInterview);
    svr.Post("/api/interviews/:id/feedback",     doSubmitFeedback);
    svr.Get ("/api/interviews/:id/feedback",     doGetFeedback);
    svr.Get ("/api/feedback",                    doListFeedback);
}
