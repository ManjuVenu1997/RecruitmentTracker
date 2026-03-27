// ============================================================
//  routes/smtp_routes.cpp
//  Handlers: get config, save config, send test email
// ============================================================
#include "smtp_routes.h"
#include "app_context.h"

static void doGetSmtpConfig(const httplib::Request& req, httplib::Response& res) {
    auto sess = getAuth(req);
    if (!sess || sess->role != "admin") return jsonErr(res, 403, "Forbidden");
    json j = g_smtp.toJson();
    // Never expose the stored password to the browser
    j["password"] = g_smtp.password.empty()
        ? "" : "\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2";
    jsonOk(res, j);
}

static void doSaveSmtpConfig(const httplib::Request& req, httplib::Response& res) {
    auto sess = getAuth(req);
    if (!sess || sess->role != "admin") return jsonErr(res, 403, "Forbidden");

    json body;
    try { body = json::parse(req.body); } catch (...) {
        return jsonErr(res, 400, "Invalid JSON"); }

    SmtpConfig nc = SmtpConfig::fromJson(body);
    // Keep stored password when the UI sends back the masked placeholder
    std::string pw = nc.password;
    bool allBullets = !pw.empty() &&
        pw.find_first_not_of("\xe2\x80\xa2") == std::string::npos;
    if (allBullets) nc.password = g_smtp.password;

    if (nc.port < 1 || nc.port > 65535) return jsonErr(res, 400, "Invalid port");
    if (nc.host.empty())       return jsonErr(res, 400, "Host is required");
    if (nc.from_email.empty()) return jsonErr(res, 400, "from_email is required");

    g_smtp = nc;
    g_smtp.saveToFile("smtp_config.json");
    jsonOk(res, {{"message", "SMTP configuration saved"}});
}

static void doTestSmtpEmail(const httplib::Request& req, httplib::Response& res) {
    auto sess = getAuth(req);
    if (!sess || sess->role != "admin") return jsonErr(res, 403, "Forbidden");
    if (!g_smtp.enabled)
        return jsonErr(res, 400, "SMTP is not enabled. Save config with Enabled = ON first.");

    json body;
    try { body = json::parse(req.body); } catch (...) {
        return jsonErr(res, 400, "Invalid JSON"); }
    if (!body.contains("to_email") || body["to_email"].get<std::string>().empty())
        return jsonErr(res, 400, "to_email is required");

    std::string toEmail = body["to_email"].get<std::string>();
    auto userOpt = g_db->getUserById(sess->userId);
    std::string toName  = userOpt ? userOpt->fullName : "Admin";

    std::string subject = "SMTP Test - Interview Scheduler";
    std::string html =
        "<html><body style='font-family:Arial,sans-serif;padding:32px;background:#f0f4f8'>"
        "<div style='max-width:500px;margin:0 auto;background:#fff;border-radius:12px;"
        "padding:32px;box-shadow:0 2px 12px rgba(0,0,0,0.08)'>"
        "<h2 style='color:#4f46e5;margin-top:0'>&#x2705; SMTP Test Successful</h2>"
        "<p style='color:#374151'>If you received this email, your SMTP configuration "
        "is working correctly.</p>"
        "<p style='color:#9ca3af;font-size:13px'>Sent by Interview Scheduler</p>"
        "</div></body></html>";

    SmtpSender sender;
    bool ok = sender.sendEmail(g_smtp, toEmail, toName, subject, html);
    if (ok)
        jsonOk(res, {{"message", "Test email sent to " + toEmail}});
    else
        jsonErr(res, 502, "SMTP error: " + sender.lastError());
}

void registerSmtpRoutes(httplib::Server& svr) {
    svr.Get ("/api/admin/smtp-config", doGetSmtpConfig);
    svr.Post("/api/admin/smtp-config", doSaveSmtpConfig);
    svr.Post("/api/admin/smtp-test",   doTestSmtpEmail);
}
