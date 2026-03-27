// ============================================================
//  smtp.h  —  Minimal SMTP client (plain + AUTH LOGIN)
//             Uses WinSock2 (already linked via CMakeLists.txt)
//  Supports: plain SMTP, SMTP AUTH LOGIN (no TLS/STARTTLS).
//  For TLS-required servers (Gmail port 465/587) use a local
//  SMTP relay (e.g. MailHog, stunnel, send-email relay) that
//  handles TLS and forwards plain connections to this client.
// ============================================================
#pragma once
#include "json.hpp"
#include <string>
#include <sstream>
#include <fstream>
#include <cstring>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  using sock_t = SOCKET;
  #define INVALID_SOCK INVALID_SOCKET
  #define CLOSE_SOCK(s) closesocket(s)
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <netdb.h>
  #include <unistd.h>
  using sock_t = int;
  #define INVALID_SOCK (-1)
  #define CLOSE_SOCK(s) ::close(s)
#endif

using json = nlohmann::json;

// ─────────────────────────────────────────────
//  SmtpConfig — persisted to smtp_config.json
// ─────────────────────────────────────────────
struct SmtpConfig {
    bool        enabled    = false;
    std::string host       = "localhost";
    int         port       = 25;
    std::string username;               // leave empty for no-auth SMTP
    std::string password;               // stored in plain-text in config file
    std::string from_email = "noreply@example.com";
    std::string from_name  = "Interview Scheduler";

    json toJson() const {
        return {{"enabled",    enabled},
                {"host",       host},
                {"port",       port},
                {"username",   username},
                {"password",   password},
                {"from_email", from_email},
                {"from_name",  from_name}};
    }

    static SmtpConfig fromJson(const json& j) {
        SmtpConfig c;
        auto get_b = [&](const char* k, bool& v) { if (j.contains(k)) v = j[k].get<bool>(); };
        auto get_s = [&](const char* k, std::string& v) { if (j.contains(k)) v = j[k].get<std::string>(); };
        auto get_i = [&](const char* k, int& v) { if (j.contains(k)) v = j[k].get<int>(); };
        get_b("enabled",    c.enabled);
        get_s("host",       c.host);
        get_i("port",       c.port);
        get_s("username",   c.username);
        get_s("password",   c.password);
        get_s("from_email", c.from_email);
        get_s("from_name",  c.from_name);
        return c;
    }

    static SmtpConfig loadFromFile(const std::string& path) {
        try {
            std::ifstream f(path);
            if (!f.is_open()) return SmtpConfig{};
            json j; f >> j;
            return fromJson(j);
        } catch (...) { return SmtpConfig{}; }
    }

    void saveToFile(const std::string& path) const {
        std::ofstream f(path);
        if (f.is_open()) f << toJson().dump(2);
    }
};

// ─────────────────────────────────────────────
//  Internal helpers
// ─────────────────────────────────────────────
namespace smtp_internal {

static std::string base64(const std::string& in) {
    static const char t[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((in.size() + 2) / 3) * 4);
    int val = 0, valb = -6;
    for (unsigned char c : in) {
        val = (val << 8) + c; valb += 8;
        while (valb >= 0) { out.push_back(t[(val >> valb) & 0x3F]); valb -= 6; }
    }
    if (valb > -6) out.push_back(t[((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}

// Build the HTML email body for an interview booking confirmation
static std::string buildInterviewEmail(
        const std::string& candidateName,
        const std::string& startTime,
        const std::string& endTime,
        const std::string& interviewerName) {

    auto fmtDt = [](const std::string& iso) -> std::string {
        // ISO: "2025-08-15 14:00:00"  →  "August 15, 2025 at 14:00"
        if (iso.size() < 16) return iso;
        return iso.substr(0, 16);  // "2025-08-15 14:00" — good enough display
    };

    std::ostringstream o;
    o << "<!DOCTYPE html>"
         "<html><head><meta charset='UTF-8'></head>"
         "<body style='margin:0;padding:0;background:#f0f4f8;font-family:Arial,sans-serif'>"
         "<table width='100%' cellpadding='0' cellspacing='0'><tr><td align='center' style='padding:32px 16px'>"
         "<table width='600' cellpadding='0' cellspacing='0' style='background:#ffffff;border-radius:12px;"
         "overflow:hidden;box-shadow:0 4px 20px rgba(0,0,0,0.08)'>"
         // Header
         "<tr><td style='background:linear-gradient(135deg,#4f46e5 0%,#7c3aed 100%);"
         "padding:36px 40px;text-align:center'>"
         "<p style='margin:0;font-size:28px'>&#x1F4C5;</p>"
         "<h1 style='margin:10px 0 6px;color:#ffffff;font-size:24px;font-weight:700'>"
         "Interview Confirmed!</h1>"
         "<p style='margin:0;color:rgba(255,255,255,0.85);font-size:14px'>"
         "Your interview has been successfully scheduled.</p>"
         "</td></tr>"
         // Body
         "<tr><td style='padding:36px 40px'>"
         "<p style='color:#374151;font-size:16px;margin:0 0 16px'>Dear <strong>"
      << candidateName
      << "</strong>,</p>"
         "<p style='color:#6b7280;font-size:14px;margin:0 0 24px'>"
         "We are pleased to inform you that your interview has been scheduled. "
         "Please review the details below and make sure to be available at the specified time.</p>"
         // Details card
         "<table width='100%' cellpadding='0' cellspacing='0' "
         "style='background:#f8fafc;border-radius:10px;border:1px solid #e5e7eb;margin-bottom:24px'>"
         "<tr><td style='padding:20px 24px'>"
         "<table width='100%' cellpadding='0' cellspacing='0'>"
         "<tr>"
         "<td style='padding:8px 0;border-bottom:1px solid #e5e7eb'>"
         "<span style='color:#6b7280;font-size:13px;font-weight:600;text-transform:uppercase;"
         "letter-spacing:.05em'>Date &amp; Time</span><br>"
         "<span style='color:#111827;font-size:15px;font-weight:600;margin-top:4px;display:block'>"
      << fmtDt(startTime)
      << "</span></td></tr>"
         "<tr><td style='padding:8px 0;border-bottom:1px solid #e5e7eb'>"
         "<span style='color:#6b7280;font-size:13px;font-weight:600;text-transform:uppercase;"
         "letter-spacing:.05em'>End Time</span><br>"
         "<span style='color:#111827;font-size:15px;font-weight:600;margin-top:4px;display:block'>"
      << fmtDt(endTime)
      << "</span></td></tr>"
         "<tr><td style='padding:8px 0'>"
         "<span style='color:#6b7280;font-size:13px;font-weight:600;text-transform:uppercase;"
         "letter-spacing:.05em'>Interviewer</span><br>"
         "<span style='color:#111827;font-size:15px;font-weight:600;margin-top:4px;display:block'>"
      << interviewerName
      << "</span></td></tr>"
         "</table></td></tr></table>"
         // Note
         "<div style='background:#fffbeb;border:1px solid #fde68a;border-radius:8px;padding:14px 18px;margin-bottom:24px'>"
         "<p style='margin:0;color:#92400e;font-size:13px'>"
         "&#x26A0;&#xFE0F; <strong>Important:</strong> Please be on time and bring any required documents. "
         "If you need to reschedule, contact us as soon as possible.</p></div>"
         "<p style='color:#6b7280;font-size:14px;margin:0'>Best regards,<br>"
         "<strong style='color:#374151'>Interview Scheduling Team</strong></p>"
         "</td></tr>"
         // Footer
         "<tr><td style='background:#f8fafc;padding:18px 40px;text-align:center;"
         "border-top:1px solid #e5e7eb'>"
         "<p style='margin:0;color:#9ca3af;font-size:12px'>"
         "This is an automated message from Interview Scheduler. Please do not reply to this email.</p>"
         "</td></tr>"
         "</table></td></tr></table></body></html>";
    return o.str();
}

// Build reminder email (candidate + interviewer variant)
static std::string buildReminderEmail(
        const std::string& recipientName,
        const std::string& candidateName,
        const std::string& interviewerName,
        const std::string& startTime,
        const std::string& endTime,
        const std::string& reminderType,  // "24h" or "1h"
        bool isInterviewer) {

    auto fmtDt = [](const std::string& iso) {
        return iso.size() >= 16 ? iso.substr(0, 16) : iso;
    };
    std::string urgencyColor = (reminderType == "1h") ? "#dc2626" : "#f59e0b";
    std::string urgencyLabel = (reminderType == "1h") ? "⚠️ In 1 hour!" : "🔔 Tomorrow";
    std::string role         = isInterviewer ? "Interviewer" : "Candidate";

    std::ostringstream o;
    o << "<!DOCTYPE html><html><head><meta charset='UTF-8'></head>"
         "<body style='margin:0;padding:0;background:#f0f4f8;font-family:Arial,sans-serif'>"
         "<table width='100%' cellpadding='0' cellspacing='0'><tr><td align='center' style='padding:32px 16px'>"
         "<table width='600' cellpadding='0' cellspacing='0' style='background:#ffffff;border-radius:12px;"
         "overflow:hidden;box-shadow:0 4px 20px rgba(0,0,0,0.08)'>"
         "<tr><td style='background:" << urgencyColor << ";padding:28px 40px;text-align:center'>"
         "<p style='margin:0;font-size:26px'>🔔</p>"
         "<h1 style='margin:8px 0 4px;color:#ffffff;font-size:22px;font-weight:700'>Interview Reminder</h1>"
         "<span style='background:rgba(255,255,255,0.2);color:#fff;font-size:14px;padding:4px 14px;border-radius:20px;font-weight:600'>" << urgencyLabel << "</span>"
         "</td></tr>"
         "<tr><td style='padding:32px 40px'>"
         "<p style='color:#374151;font-size:16px;margin:0 0 16px'>Dear <strong>" << recipientName << "</strong>,</p>"
         "<p style='color:#6b7280;font-size:14px;margin:0 0 20px'>"
         "This is a reminder that an interview is coming up "
      << (reminderType == "1h" ? "<strong>in approximately 1 hour</strong>" : "<strong>in approximately 24 hours</strong>")
      << ". Please make sure you are prepared and available.</p>"
         "<table width='100%' cellpadding='0' cellspacing='0' "
         "style='background:#f8fafc;border-radius:10px;border:1px solid #e5e7eb;margin-bottom:24px'>"
         "<tr><td style='padding:20px 24px'>"
         "<table width='100%' cellpadding='0' cellspacing='0'>"
         "<tr><td style='padding:8px 0;border-bottom:1px solid #e5e7eb'>"
         "<span style='color:#6b7280;font-size:12px;font-weight:600;text-transform:uppercase'>Candidate</span><br>"
         "<span style='color:#111827;font-size:15px;font-weight:600'>" << candidateName << "</span></td></tr>"
         "<tr><td style='padding:8px 0;border-bottom:1px solid #e5e7eb'>"
         "<span style='color:#6b7280;font-size:12px;font-weight:600;text-transform:uppercase'>Interviewer</span><br>"
         "<span style='color:#111827;font-size:15px;font-weight:600'>" << interviewerName << "</span></td></tr>"
         "<tr><td style='padding:8px 0;border-bottom:1px solid #e5e7eb'>"
         "<span style='color:#6b7280;font-size:12px;font-weight:600;text-transform:uppercase'>Date &amp; Time</span><br>"
         "<span style='color:#111827;font-size:15px;font-weight:600'>" << fmtDt(startTime) << "</span></td></tr>"
         "<tr><td style='padding:8px 0'>"
         "<span style='color:#6b7280;font-size:12px;font-weight:600;text-transform:uppercase'>End Time</span><br>"
         "<span style='color:#111827;font-size:15px;font-weight:600'>" << fmtDt(endTime) << "</span></td></tr>"
         "</table></td></tr></table>"
         "<p style='color:#6b7280;font-size:14px;margin:0'>Best regards,<br>"
         "<strong style='color:#374151'>Interview Scheduling Team</strong></p>"
         "</td></tr>"
         "<tr><td style='background:#f8fafc;padding:16px 40px;text-align:center;border-top:1px solid #e5e7eb'>"
         "<p style='margin:0;color:#9ca3af;font-size:12px'>Automated reminder &mdash; Interview Scheduler</p>"
         "</td></tr>"
         "</table></td></tr></table></body></html>";
    return o.str();
}

} // namespace smtp_internal

// ─────────────────────────────────────────────
//  SmtpSender — call SmtpSender::send(...)
// ─────────────────────────────────────────────
class SmtpSender {
    sock_t sock_ = INVALID_SOCK;
    std::string err_;
#ifdef _WIN32
    bool wsaInitialized_ = false;
#endif

    bool init() {
#ifdef _WIN32
        WSADATA wsd{};
        if (WSAStartup(MAKEWORD(2, 2), &wsd) != 0) {
            err_ = "WSAStartup failed"; return false;
        }
        wsaInitialized_ = true;
#endif
        return true;
    }

    void cleanup() {
        if (sock_ != INVALID_SOCK) { CLOSE_SOCK(sock_); sock_ = INVALID_SOCK; }
#ifdef _WIN32
        if (wsaInitialized_) { WSACleanup(); wsaInitialized_ = false; }
#endif
    }

    bool tcpConnect(const std::string& host, int port) {
        if (!init()) return false;
        struct addrinfo hints{}, *res = nullptr;
        hints.ai_family   = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0) {
            err_ = "DNS lookup failed for " + host; return false;
        }
        sock_ = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (sock_ == INVALID_SOCK) {
            err_ = "Socket creation failed"; freeaddrinfo(res); return false;
        }
        // 10-second timeout
#ifdef _WIN32
        DWORD to = 10000;
        setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, (char*)&to, sizeof(to));
        setsockopt(sock_, SOL_SOCKET, SO_SNDTIMEO, (char*)&to, sizeof(to));
#else
        struct timeval to{ 10, 0 };
        setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof(to));
        setsockopt(sock_, SOL_SOCKET, SO_SNDTIMEO, &to, sizeof(to));
#endif
        if (::connect(sock_, res->ai_addr, (int)res->ai_addrlen) != 0) {
            err_ = "Connection refused: " + host + ":" + std::to_string(port);
            freeaddrinfo(res); return false;
        }
        freeaddrinfo(res);
        return true;
    }

    std::string readLine() {
        std::string line;
        char c = 0;
        while (recv(sock_, &c, 1, 0) > 0) {
            if (c == '\n') break;
            if (c != '\r') line += c;
        }
        return line;
    }

    // Reads full (possibly multi-line) SMTP response, returns numeric code
    int readResp(std::string* last = nullptr) {
        int code = 0;
        std::string line;
        do {
            line = readLine();
            if (line.size() >= 3) {
                try { code = std::stoi(line.substr(0, 3)); } catch (...) {}
            }
        } while (line.size() > 3 && line[3] == '-');
        if (last) *last = line;
        return code;
    }

    bool rawSend(const std::string& s) {
        int n = ::send(sock_, s.c_str(), (int)s.size(), 0);
        return n == (int)s.size();
    }

    bool cmd(const std::string& c, int expect, std::string* out = nullptr) {
        if (!rawSend(c + "\r\n")) { err_ = "Send failed for: " + c; return false; }
        std::string resp;
        int code = readResp(&resp);
        if (out) *out = resp;
        if (code != expect) {
            err_ = "Expected " + std::to_string(expect) +
                   ", got " + std::to_string(code) + " (" + resp + ")";
            return false;
        }
        return true;
    }

    // Dot-stuff a message body (RFC 5321 §4.5.2)
    static std::string dotStuff(const std::string& body) {
        std::ostringstream out;
        std::istringstream ss(body);
        std::string line;
        while (std::getline(ss, line)) {
            if (!line.empty() && line[0] == '.') out << '.';
            // strip trailing \r if present
            if (!line.empty() && line.back() == '\r') line.pop_back();
            out << line << "\r\n";
        }
        return out.str();
    }

public:
    ~SmtpSender() { cleanup(); }

    const std::string& lastError() const { return err_; }

    bool sendEmail(const SmtpConfig& cfg,
                   const std::string& toEmail,
                   const std::string& toName,
                   const std::string& subject,
                   const std::string& htmlBody) {
        if (!tcpConnect(cfg.host, cfg.port)) return false;

        // Greeting
        if (readResp() != 220) { err_ = "No SMTP greeting"; cleanup(); return false; }

        // EHLO
        std::string ehloResp;
        if (!cmd("EHLO interview-scheduler", 250, &ehloResp)) {
            // fallback to HELO
            if (!cmd("HELO interview-scheduler", 250)) { cleanup(); return false; }
        }

        // AUTH LOGIN (only if credentials provided)
        if (!cfg.username.empty()) {
            if (!cmd("AUTH LOGIN", 334)) { cleanup(); return false; }
            if (!cmd(smtp_internal::base64(cfg.username), 334)) { cleanup(); return false; }
            if (!cmd(smtp_internal::base64(cfg.password), 235)) { cleanup(); return false; }
        }

        // Envelope
        if (!cmd("MAIL FROM:<" + cfg.from_email + ">", 250)) { cleanup(); return false; }
        if (!cmd("RCPT TO:<"  + toEmail + ">",          250)) { cleanup(); return false; }
        if (!cmd("DATA", 354))                                { cleanup(); return false; }

        // Headers + body
        std::ostringstream msg;
        msg << "From: =?UTF-8?B?" << smtp_internal::base64(cfg.from_name)
            << "?= <" << cfg.from_email << ">\r\n";
        msg << "To: " << toName << " <" << toEmail << ">\r\n";
        msg << "Subject: =?UTF-8?B?" << smtp_internal::base64(subject) << "?=\r\n";
        msg << "MIME-Version: 1.0\r\n";
        msg << "Content-Type: text/html; charset=UTF-8\r\n";
        msg << "Content-Transfer-Encoding: 8bit\r\n";
        msg << "\r\n";
        msg << dotStuff(htmlBody);
        msg << "\r\n.\r\n";

        if (!rawSend(msg.str())) { err_ = "Failed sending message body"; cleanup(); return false; }
        int dcode = readResp();
        if (dcode != 250) { err_ = "DATA rejected (code " + std::to_string(dcode) + ")"; cleanup(); return false; }

        rawSend("QUIT\r\n");
        cleanup();
        return true;
    }
};

// ─────────────────────────────────────────────
//  Fire-and-forget: dispatches email in a
//  background thread (doesn't block HTTP resp)
// ─────────────────────────────────────────────
static void sendEmailAsync(SmtpConfig cfg,
                           std::string toEmail, std::string toName,
                           std::string subject, std::string htmlBody) {
    std::thread([cfg = std::move(cfg),
                 toEmail = std::move(toEmail),
                 toName  = std::move(toName),
                 subject = std::move(subject),
                 htmlBody = std::move(htmlBody)]() {
        SmtpSender sender;
        bool ok = sender.sendEmail(cfg, toEmail, toName, subject, htmlBody);
        if (!ok)
            std::cerr << "[SMTP] Email to <" << toEmail
                      << "> failed: " << sender.lastError() << "\n";
        else
            std::cerr << "[SMTP] Email sent to <" << toEmail << ">\n";
    }).detach();
}
