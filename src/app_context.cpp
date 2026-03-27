// ============================================================
//  app_context.cpp  —  Global variable definitions
// ============================================================
#include "app_context.h"

Database*          g_db       = nullptr;
SessionManager*   g_sessions  = nullptr;
std::mutex         g_mutex;
SmtpConfig         g_smtp;
LoginRateLimiter   g_limiter;
