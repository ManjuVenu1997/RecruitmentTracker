// ============================================================
//  main.cpp  —  Entry point
//  Wires together the HTTP server, database, sessions,
//  and all route modules.
// ============================================================
#include "app_context.h"
#include <atomic>
#include <csignal>

#include "routes/auth_routes.h"
#include "routes/user_routes.h"
#include "routes/skill_routes.h"
#include "routes/slot_routes.h"
#include "routes/interview_routes.h"
#include "routes/notification_routes.h"
#include "routes/admin_routes.h"
#include "routes/smtp_routes.h"

// ── Graceful-shutdown signal handler ─────────────────────
static httplib::Server* g_svr_ptr = nullptr;
static void handleSignal(int) {
    if (g_svr_ptr) g_svr_ptr->stop();
}

int main() {
    Logger::initFromEnv();
    LOG_INFO("Startup", "Initialising server");

    try {
        Database       db("recruitment.db");
        SessionManager sessions(&db);

        db.initialize();
        g_db       = &db;
        g_sessions = &sessions;
        g_smtp     = SmtpConfig::loadFromFile("smtp_config.json");

        httplib::Server svr;

        // ── Graceful shutdown ─────────────────────────────
        g_svr_ptr = &svr;
        std::signal(SIGINT,  handleSignal);
        std::signal(SIGTERM, handleSignal);

        // ── Serve static frontend files ───────────────────
        if (!svr.set_mount_point("/", "./frontend")) {
            LOG_WARN("Startup", "./frontend not found - static files will not be served");
        }

        // ── Register all route groups ─────────────────────
        registerAuthRoutes        (svr);
        registerUserRoutes        (svr);
        registerSkillRoutes       (svr);
        registerSlotRoutes        (svr);
        registerInterviewRoutes   (svr);
        registerNotificationRoutes(svr);
        registerAdminRoutes       (svr);
        registerSmtpRoutes        (svr);

        // ── CORS pre-flight (OPTIONS) ─────────────────────
        svr.Options(".*", [](const httplib::Request&, httplib::Response& res) {
            res.set_header("Access-Control-Allow-Origin",  "*");
            res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
            res.set_header("Access-Control-Allow-Headers", "Authorization, Content-Type");
            res.status = 204;
        });

        // ── CORS headers on all responses ─────────────────
        svr.set_post_routing_handler([](const httplib::Request&, httplib::Response& res) {
            res.set_header("Access-Control-Allow-Origin",  "*");
            res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
            res.set_header("Access-Control-Allow-Headers", "Authorization, Content-Type");
        });

        // ── Request / access log ──────────────────────────
        svr.set_logger([](const httplib::Request& req, const httplib::Response& res) {
            Logger::Level lvl = res.status >= 500 ? Logger::Level::ERR
                              : res.status >= 400 ? Logger::Level::WARN
                              :                     Logger::Level::INFO;
            Logger::emit(lvl, "HTTP", "Request", {
                Logger::F("method", req.method),
                Logger::F("path",   req.path),
                Logger::F("status", res.status)
            });
        });

        // ── Reminder background thread ────────────────────
        // Wakes every 10 minutes, sends 24h and 1h reminder emails.
        std::atomic<bool> stopReminder{false};
        std::thread reminderThread([&stopReminder]() {
            while (!stopReminder.load()) {
                // Sleep 10 minutes in small increments so shutdown is responsive
                for (int i = 0; i < 600 && !stopReminder.load(); ++i)
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                if (stopReminder.load()) break;
                if (!g_smtp.enabled) continue;

                // Purge stale sessions while we're awake
                { std::lock_guard<std::mutex> lk(g_mutex); g_db->purgeExpiredSessions(); }

                std::vector<Database::ReminderItem> items;
                {
                    std::lock_guard<std::mutex> lk(g_mutex);
                    items = g_db->getPendingReminders();
                }
                for (auto& r : items) {
                    std::string hoursLabel = (r.reminderType == "1h") ? "1 hour" : "24 hours";
                    std::string subject    = "Interview Reminder (" + hoursLabel + ") - " +
                                            r.startTime.substr(0, 10);

                    // Email candidate
                    if (!r.candidateEmail.empty()) {
                        auto body = smtp_internal::buildReminderEmail(
                            r.candidateName, r.candidateName, r.interviewerName,
                            r.startTime, r.endTime, r.reminderType, false);
                        sendEmailAsync(g_smtp, r.candidateEmail, r.candidateName, subject, body);
                    }
                    // Email interviewer
                    if (!r.interviewerEmail.empty()) {
                        auto body = smtp_internal::buildReminderEmail(
                            r.interviewerName, r.candidateName, r.interviewerName,
                            r.startTime, r.endTime, r.reminderType, true);
                        sendEmailAsync(g_smtp, r.interviewerEmail, r.interviewerName, subject, body);
                    }
                    // In-app notification to interviewer
                    {
                        std::lock_guard<std::mutex> lk(g_mutex);
                        g_db->createNotification(r.interviewerUserId,
                            "Reminder: Interview with " + r.candidateName +
                            " in " + hoursLabel + " (" + r.startTime.substr(0,16) + ")",
                            "reminder", r.interviewId);
                        g_db->markReminderSent(r.interviewId, r.reminderType);
                    }
                    LOG_INFO("Reminder", "Sent reminder email",
                             Logger::F("type",         r.reminderType),
                             Logger::F("interview_id", r.interviewId));
                }
            }
        });
        reminderThread.detach();

        LOG_INFO("Startup", "Server listening",
                 Logger::F("address", "0.0.0.0"),
                 Logger::F("port",    8080));
        LOG_INFO("Startup", "Default accounts available",
                 Logger::F("note", "admin / recruiter1 / interviewer1 / interviewer2 (password: admin123)"));

        svr.listen("0.0.0.0", 8080);

    } catch (const std::exception& ex) {
        LOG_ERROR("Startup", "Fatal error", Logger::F("detail", ex.what()));
        return 1;
    }
    return 0;
}
