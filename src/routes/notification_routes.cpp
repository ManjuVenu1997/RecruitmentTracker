// ============================================================
//  routes/notification_routes.cpp
//  Handlers: get notifications, mark read, mark all read
// ============================================================
#include "notification_routes.h"
#include "app_context.h"

static void doGetNotifications(const httplib::Request& req, httplib::Response& res) {
    auto sess = getAuth(req);
    if (!sess) return jsonErr(res, 401, "Unauthorized");
    json arr = json::array();
    for (auto& n : g_db->getNotificationsForUser(sess->userId)) arr.push_back(n);
    jsonOk(res, {{"notifications",  arr},
                 {"unread_count",   g_db->getUnreadCount(sess->userId)}});
}

static void doMarkNotificationRead(const httplib::Request& req, httplib::Response& res) {
    auto sess = getAuth(req);
    if (!sess) return jsonErr(res, 401, "Unauthorized");
    int nid = std::stoi(req.path_params.at("id"));
    g_db->markNotificationRead(nid, sess->userId);
    jsonOk(res, {{"message", "Marked as read"}});
}

static void doMarkAllRead(const httplib::Request& req, httplib::Response& res) {
    auto sess = getAuth(req);
    if (!sess) return jsonErr(res, 401, "Unauthorized");
    g_db->markAllNotificationsRead(sess->userId);
    jsonOk(res, {{"message", "All marked as read"}});
}

void registerNotificationRoutes(httplib::Server& svr) {
    svr.Get("/api/notifications",          doGetNotifications);
    svr.Put("/api/notifications/:id/read", doMarkNotificationRead);
    svr.Put("/api/notifications/read-all", doMarkAllRead);
}
