# Interview Scheduler — Technical Documentation

A self-contained, full-stack **Interview Scheduling Web Application** built with C++17 (backend) and HTML/CSS/JavaScript (frontend). Runs as a single binary with no external runtime dependencies.

---

## Table of Contents

1. [Overview](#1-overview)
2. [Technology Stack](#2-technology-stack)
3. [Architecture](#3-architecture)
4. [Project Structure](#4-project-structure)
5. [Database Schema](#5-database-schema)
6. [User Roles & Permissions](#6-user-roles--permissions)
7. [API Reference](#7-api-reference)
8. [Authentication & Session Flow](#8-authentication--session-flow)
9. [Frontend Pages](#9-frontend-pages)
10. [Key Design Decisions](#10-key-design-decisions)
11. [Prerequisites & Build](#11-prerequisites--build)
12. [Running the Application](#12-running-the-application)
13. [Default Accounts](#13-default-accounts)
14. [Security](#14-security)
15. [Testing](#15-testing)
16. [Deployment / Sharing](#16-deployment--sharing)

---

## 1. Overview

The Interview Scheduler allows three types of users to collaborate on scheduling technical interviews:

- **Recruiters** search for available interviewer slots, book them for candidates, and track interview outcomes.
- **Interviewers** manage their availability, accept or decline bookings, complete interviews, and submit feedback.
- **Admins** manage users, monitor utilization, view reports, and configure email notifications.

Key capabilities:
- Skill-based slot search with date filtering
- Automatic email notifications (booking confirmation, 24h/1h reminders)
- In-app real-time notifications (polled every 30 seconds)
- Calendar views with FullCalendar (month / week / list)
- Admin analytics: utilization rates, skill distribution, unutilized slots
- CSV export for reports
- Password reset via email token

---

## 2. Technology Stack

### Backend

| Component | Technology | Version | Purpose |
|---|---|---|---|
| Language | C++17 | — | Application logic |
| HTTP Server | [cpp-httplib](https://github.com/yhirose/cpp-httplib) | v0.15.3 | Single-header embedded HTTP/REST server |
| Database | [SQLite3](https://sqlite.org) | 3.45.1 | Embedded relational database (no server needed) |
| JSON | [nlohmann/json](https://github.com/nlohmann/json) | v3.11.3 | JSON parsing and serialization |
| Hashing | SHA-256 (custom) | — | Password hashing (self-contained, no OpenSSL) |
| Email | Custom SMTP client | — | Plain TCP SMTP with AUTH LOGIN and Base64 |
| Build System | CMake | 3.14+ | Cross-platform build configuration |
| Test Framework | GoogleTest | v1.14.0 | Unit testing (fetched via CMake FetchContent) |
| Concurrency | std::thread / std::mutex | C++17 | Background reminder thread, write serialization |

### Frontend

| Component | Technology | Version | How loaded |
|---|---|---|---|
| UI Framework | [Bootstrap 5](https://getbootstrap.com) | 5.3 | CDN |
| Icons | [Bootstrap Icons](https://icons.getbootstrap.com) | 1.11 | CDN |
| Calendar | [FullCalendar](https://fullcalendar.io) | 6.x | CDN |
| Charts | [Chart.js](https://chartjs.org) | 4.x | CDN |
| Language | Vanilla JavaScript (ES2020) | — | Local files |
| CSS | Custom + Bootstrap | — | Local files |

### Third-party (vendored)

All dependencies in `third_party/` are single-file headers or amalgamation sources — no package manager needed.

| File | Library | Notes |
|---|---|---|
| `httplib.h` | cpp-httplib | Single-header HTTP server |
| `json.hpp` | nlohmann/json | Single-header JSON |
| `sqlite3.h` / `sqlite3.c` | SQLite3 | Amalgamation, compiled as static lib |

---

## 3. Architecture

```
┌──────────────────────────────────────────────────────┐
│                    BROWSER                           │
│  index.html · admin.html · recruiter.html           │
│  interviewer.html · reset-password.html             │
│  Bootstrap 5 + FullCalendar 6 + Chart.js (CDN)     │
│  common.js · admin.js · recruiter.js · interviewer.js│
└─────────────────────┬────────────────────────────────┘
                      │  HTTP/REST  —  Authorization: Bearer <token>
                      │  CORS headers on every response
┌─────────────────────▼────────────────────────────────┐
│         cpp-httplib HTTP Server  :0.0.0.0:8080       │
│  ┌──────────────────────────────────────────────┐    │
│  │              Route Modules                   │    │
│  │  auth · user · skill · slot · interview     │    │
│  │  notification · admin · smtp                │    │
│  └──────────────────┬───────────────────────────┘    │
│                     │                                │
│  ┌──────────────────▼───────────────────────────┐    │
│  │  Database Layer  (database.h / SQLite3)      │    │
│  │  All SQL via parameterized statements        │    │
│  └──────────────────────────────────────────────┘    │
│                                                      │
│  ┌──────────────────────────────────────────────┐    │
│  │  Background Thread (wakes every 10 min)      │    │
│  │  • Purge expired sessions                    │    │
│  │  • Send 24h / 1h reminder emails             │    │
│  └──────────────────────────────────────────────┘    │
└──────────────────────────────────────────────────────┘
          │                              │
          ▼                              ▼
   recruitment.db                 SMTP Server
   (SQLite WAL mode)         (optional, plain TCP)
```

**Key architectural properties:**
- Single-process, single-binary — no microservices, no Docker required
- All shared mutable state protected by one `std::mutex` (`g_mutex`)
- Sessions persisted in SQLite — survive server restarts
- Static frontend files served directly from the `./frontend` directory
- All SQL uses parameterized binding — no string concatenation

---

## 4. Project Structure

```
RecruitmentTracker/
├── CMakeLists.txt              Build system (C++17, CMake 3.14+)
├── setup.ps1                   Download third_party deps (PowerShell)
├── setup.bat                   Download third_party deps (Batch)
├── setup.sh                    Download third_party deps (Linux/macOS)
├── start.ps1                   Launch server with colored log tailing
├── src/
│   ├── main.cpp                Entry point: wires server + routes + background thread
│   ├── app_context.h/cpp       Global singletons (db, sessions, mutex, smtp, limiter)
│   │                           Inline helpers: jsonOk, jsonErr, getAuth, isValidDate
│   ├── database.h              Complete SQLite3 data access layer (all SQL lives here)
│   ├── session.h               Session token manager + LoginRateLimiter
│   ├── logger.h                Structured NDJSON logging to stderr (thread-safe)
│   ├── sha256.h                Self-contained SHA-256 implementation
│   ├── smtp.h                  SMTP client + HTML email builders + sendEmailAsync
│   └── routes/
│       ├── auth_routes.cpp/h       Login, logout, profile, password reset
│       ├── user_routes.cpp/h       User CRUD (admin), user listing
│       ├── skill_routes.cpp/h      Skills + interviewer↔skill mapping
│       ├── slot_routes.cpp/h       Availability slot management
│       ├── interview_routes.cpp/h  Book, decline, complete, feedback
│       ├── notification_routes.cpp/h  In-app notifications
│       ├── admin_routes.cpp/h      Calendar, utilization, CSV exports
│       └── smtp_routes.cpp/h       SMTP config management
├── frontend/
│   ├── index.html              Login page + forgot password
│   ├── admin.html              Admin dashboard (stats, calendar, users, SMTP)
│   ├── recruiter.html          Recruiter dashboard (slot search, booking, calendar)
│   ├── interviewer.html        Interviewer dashboard (slots, skills, feedback)
│   ├── reset-password.html     Token-based password reset
│   ├── css/style.css           Custom styles (Bootstrap extends)
│   └── js/
│       ├── common.js           API wrapper, auth guard, notifications, utilities
│       ├── admin.js            Admin page logic
│       ├── recruiter.js        Recruiter page logic
│       └── interviewer.js      Interviewer page logic
├── third_party/                (auto-downloaded by setup scripts)
│   ├── httplib.h               cpp-httplib single-header HTTP server
│   ├── json.hpp                nlohmann/json single-header JSON
│   ├── sqlite3.h               SQLite3 amalgamation header
│   └── sqlite3.c               SQLite3 amalgamation source
├── tests/
│   ├── test_sha256.cpp         SHA-256 NIST vector tests
│   ├── test_session.cpp        Session manager + rate limiter tests
│   └── test_database.cpp       Full DB CRUD tests (in-memory SQLite)
└── InterviewScheduler_Portable/
    ├── recruitment_tracker.exe  Pre-built Windows binary
    ├── libgcc_s_seh-1.dll       MinGW runtime
    ├── libstdc++-6.dll
    ├── libwinpthread-1.dll
    ├── START.bat                Double-click launcher
    └── frontend/               Bundled frontend files
```

---

## 5. Database Schema

The application uses **SQLite3** with 11 tables. Schema is created on first run via `db.initialize()` and versioned migrations in `runMigrations()`.

### `users`
| Column | Type | Notes |
|---|---|---|
| `id` | INTEGER PK | Auto-increment |
| `username` | TEXT UNIQUE | Login name |
| `password_hash` | TEXT | SHA-256 hex (64 chars) |
| `email` | TEXT UNIQUE | For password reset + notifications |
| `full_name` | TEXT | Display name |
| `role` | TEXT | `recruiter` / `interviewer` / `admin` |
| `created_at` | TEXT | ISO datetime (migration v1) |
| `updated_at` | TEXT | ISO datetime (migration v1) |
| `deleted_at` | TEXT | NULL = active; set = soft-deleted (migration v1) |

### `skills`
| Column | Type | Notes |
|---|---|---|
| `id` | INTEGER PK | — |
| `name` | TEXT UNIQUE | Case-insensitive (`COLLATE NOCASE`) |

### `interviewer_skills`
| Column | Type | Notes |
|---|---|---|
| `interviewer_id` | INTEGER FK → users | — |
| `skill_id` | INTEGER FK → skills | — |
| PK | composite `(interviewer_id, skill_id)` | — |

### `availability_slots`
| Column | Type | Notes |
|---|---|---|
| `id` | INTEGER PK | — |
| `interviewer_id` | INTEGER FK → users | — |
| `start_time` | TEXT | ISO datetime |
| `end_time` | TEXT | ISO datetime; must be > start_time |
| `status` | TEXT | `available` / `blocked` |
| `created_at` | TEXT | — |

### `interviews`
| Column | Type | Notes |
|---|---|---|
| `id` | INTEGER PK | — |
| `slot_id` | INTEGER FK → availability_slots | — |
| `recruiter_id` | INTEGER FK → users | — |
| `candidate_name` | TEXT | Max 200 chars |
| `candidate_email` | TEXT | Max 200 chars |
| `status` | TEXT | `confirmed` / `declined` / `completed` |
| `decline_reason` | TEXT | Required when declining |
| `booked_at` | TEXT | — |

### `interview_feedback`
| Column | Type | Notes |
|---|---|---|
| `id` | INTEGER PK | — |
| `interview_id` | INTEGER UNIQUE FK | One feedback per interview |
| `rating` | INTEGER | 1–5 (CHECK constraint) |
| `notes` | TEXT | Max 2000 chars |
| `submitted_at` | TEXT | — |

### `notifications`
| Column | Type | Notes |
|---|---|---|
| `id` | INTEGER PK | — |
| `user_id` | INTEGER FK → users | Recipient |
| `message` | TEXT | — |
| `type` | TEXT | `booking` / `decline` / `feedback` / `reminder` |
| `reference_id` | INTEGER | Interview ID |
| `is_read` | INTEGER | 0 = unread, 1 = read |
| `created_at` | TEXT | — |

### `sessions`
| Column | Type | Notes |
|---|---|---|
| `token` | TEXT PK | 256-bit random hex |
| `user_id` | INTEGER FK → users | — |
| `username` | TEXT | Denormalised for fast lookup |
| `full_name` | TEXT | — |
| `role` | TEXT | — |
| `expires_at` | TEXT | 24h from creation; checked on every request |

### `password_reset_tokens` *(migration v2)*
| Column | Type | Notes |
|---|---|---|
| `token` | TEXT PK | Secure random |
| `user_id` | INTEGER FK → users | — |
| `expires_at` | TEXT | 1-hour validity |

### `reminder_log`
| Column | Type | Notes |
|---|---|---|
| `interview_id` | INTEGER (composite PK) | — |
| `reminder_type` | TEXT (composite PK) | `24h` or `1h` |

> Prevents duplicate reminder emails even if the background thread fires multiple times.

### `schema_version`
Tracks migration level (currently `2`). `initialize()` is idempotent.

---

## 6. User Roles & Permissions

| Feature | Recruiter | Interviewer | Admin |
|---|:---:|:---:|:---:|
| Login / update own profile / change password | ✓ | ✓ | ✓ |
| View all skills | ✓ | ✓ | ✓ |
| Create new skill | ✗ | ✓ | ✓ |
| View interviewer skills | ✓ | ✓ | ✓ |
| Add / remove own skills | ✗ | ✓ | ✓ |
| Create / delete own slots | ✗ | ✓ | ✓ |
| Search available slots | ✓ | ✗ | ✓ |
| Book interview for candidate | ✓ | ✗ | ✗ |
| View own interviews | ✓ | ✓ | ✓ (all) |
| Decline interview (own) | ✗ | ✓ | ✗ |
| Mark interview complete | ✗ | ✓ | ✓ |
| Submit feedback | ✗ | ✓ | ✗ |
| View feedback | ✓ (own) | ✓ (own) | ✓ (all) |
| Notifications | ✓ | ✓ | ✗ |
| Calendar | ✓ (own) | ✓ (own slots) | ✓ (all) |
| User management | ✗ | ✗ | ✓ |
| Utilization / reports | ✗ | ✗ | ✓ |
| SMTP configuration | ✗ | ✗ | ✓ |

---

## 7. API Reference

All endpoints are under `/api/`. All authenticated endpoints require:
```
Authorization: Bearer <token>
```

### Authentication
| Method | Endpoint | Auth | Description |
|---|---|---|---|
| POST | `/api/auth/login` | No | Credential check → returns token + user object |
| POST | `/api/auth/logout` | No | Invalidates session token |
| GET | `/api/auth/me` | Yes | Returns current user |
| GET | `/api/auth/profile` | Yes | Same as `/me` |
| PUT | `/api/auth/profile` | Yes | Update `full_name` + `email` |
| POST | `/api/auth/change-password` | Yes | Verify old password, set new |
| POST | `/api/auth/forgot-password` | No | Send password reset email |
| POST | `/api/auth/reset-password` | No | Consume token, set new password |
| POST | `/api/auth/refresh` | Yes | Extend session expiry by 24h |
| GET | `/api/version` | No | Returns `{"version":"1.0.0"}` |

### Users (Admin)
| Method | Endpoint | Access | Description |
|---|---|---|---|
| GET | `/api/users` | Admin / Any `?role=interviewer` | Paginated user list or interviewer dropdown |
| POST | `/api/users` | Admin | Create user |
| PUT | `/api/users/:id` | Admin | Update name, email, role |
| DELETE | `/api/users/:id` | Admin | Soft-delete (cannot self-delete) |
| POST | `/api/users/:id/reset-password` | Admin | Force set password |

### Skills
| Method | Endpoint | Access | Description |
|---|---|---|---|
| GET | `/api/skills` | Any | List all skills |
| POST | `/api/skills` | Interviewer / Admin | Create or return existing skill (upsert) |
| GET | `/api/interviewers/:id/skills` | Any | Skills for one interviewer |
| POST | `/api/interviewers/:id/skills` | Own / Admin | Assign skill |
| DELETE | `/api/interviewers/:id/skills/:skill_id` | Own / Admin | Remove skill |

### Availability Slots
| Method | Endpoint | Access | Description |
|---|---|---|---|
| GET | `/api/interviewers/:id/slots` | Any | All slots with linked interview info |
| POST | `/api/interviewers/:id/slots` | Own / Admin | Create slot (validates format, start < end) |
| DELETE | `/api/slots/:id` | Own / Admin | Delete only `available` slots |
| GET | `/api/available-slots` | Recruiter / Admin | Free slots (now → +30 days); `?skill=&date=` |

### Interviews
| Method | Endpoint | Access | Description |
|---|---|---|---|
| POST | `/api/interviews` | Recruiter | Book: lock slot, notify interviewer, email candidate |
| GET | `/api/interviews` | Any | Role-filtered list (±30 day window) |
| PUT | `/api/interviews/:id/decline` | Interviewer (own) | Decline with reason; releases slot |
| PUT | `/api/interviews/:id/complete` | Interviewer (own) / Admin | Mark completed |
| POST | `/api/interviews/:id/feedback` | Interviewer (own) | Submit rating (1–5) + notes |
| GET | `/api/interviews/:id/feedback` | Any (own) | Get feedback for interview |
| GET | `/api/feedback` | Recruiter (own) / Admin | List all feedback |

### Notifications
| Method | Endpoint | Access | Description |
|---|---|---|---|
| GET | `/api/notifications` | Any | Last 50 + unread count |
| PUT | `/api/notifications/:id/read` | Any (own) | Mark one read |
| PUT | `/api/notifications/read-all` | Any | Mark all read |

### Admin
| Method | Endpoint | Access | Description |
|---|---|---|---|
| GET | `/api/calendar` | Any | Calendar events (role-filtered) |
| GET | `/api/admin/utilization` | Admin | Per-interviewer slot stats |
| GET | `/api/admin/skill-distribution` | Admin | Skill → confirmed+completed count |
| GET | `/api/admin/unutilized-slots` | Admin | Past available-but-never-booked slots |
| GET | `/api/admin/export/utilization` | Admin | CSV download |
| GET | `/api/admin/export/interviews` | Admin | CSV download |

### SMTP
| Method | Endpoint | Access | Description |
|---|---|---|---|
| GET | `/api/admin/smtp-config` | Admin | Get config (password masked) |
| POST | `/api/admin/smtp-config` | Admin | Save to `smtp_config.json` |
| POST | `/api/admin/smtp-test` | Admin | Send a live test email |

---

## 8. Authentication & Session Flow

```
Login:
  Browser                   Server                      DB
    │ POST /api/auth/login    │                           │
    │ {username, password}    │                           │
    │ ──────────────────────► │ LoginRateLimiter.check()  │
    │                         │ getUserByUsername()──────►│
    │                         │ sha256(pwd) == hash ?     │
    │                         │ createSession() ─────────►│ INSERT sessions
    │ ◄────────────────────── │                           │
    │ {token, user}           │                           │
    │ localStorage.setItem()  │                           │
    │                         │                           │
Subsequent requests:
    │ Any API call            │                           │
    │ Authorization: Bearer X │                           │
    │ ──────────────────────► │ extractToken()            │
    │                         │ lookupSession(X) ────────►│ SELECT sessions
    │                         │ check expires_at          │
    │                         │ → std::optional<Session>  │
```

**Token details:**
- 256-bit random hex (`std::random_device` + `std::mt19937_64`)
- Stored in `sessions` table — survives server restart
- 24-hour expiry; refreshable via `POST /api/auth/refresh`
- Brute-force guard: **4 failed logins → 5-minute lockout** (in-memory per username)
- Expired sessions purged every 10 minutes by background thread

---

## 9. Frontend Pages

### `index.html` — Login
- Bootstrap 5 login card
- Forgot-password modal → `POST /api/auth/forgot-password`
- Role-based redirect on success (admin / recruiter / interviewer dashboard)
- Demo account quick-fill buttons

### `admin.html` — Admin Dashboard
| Tab | Content |
|---|---|
| Dashboard | KPI cards · Utilization table · Skill doughnut chart (Chart.js) · Unutilized slots |
| Calendar | FullCalendar 6 read-only view of all interviews (blue=confirmed, green=completed, red=declined) |
| Users | Paginated + searchable table · Create / Edit / Delete modals · Per-user password reset |
| Email Config | SMTP form · Test email button |

### `recruiter.html` — Recruiter Dashboard
- Skill + date filter → slot cards → **Book** modal (candidate name + email)
- Stats KPI bar (total / confirmed / declined)
- FullCalendar showing own interviews
- Feedback received panel
- Notification bell (polled every 30s)

### `interviewer.html` — Interviewer Dashboard
- My Skills badges + Add Skill modal (checkbox + create new)
- Add Slot form (datetime-local pickers with start < end validation)
- My Slots list with Decline / Complete+Feedback per booked slot
- Star rating (1–5) feedback modal
- FullCalendar (green = available, orange = booked)
- Notification bell

### `reset-password.html`
- Reads `?token=` from URL → `POST /api/auth/reset-password`

### `js/common.js` — Shared Utilities
| Utility | Purpose |
|---|---|
| `API.get/post/put/delete` | Attaches Bearer token; auto-redirects to login on 401 |
| `requireAuth(roles)` | Guards page load; redirects wrong roles |
| `initNavbar()` | Populates username, logout, change-password buttons |
| `initNotifications(userId)` | Notification panel + 30s polling |
| `showToast(msg, type)` | Bottom-right Bootstrap alert |
| `escHtml(s)` | XSS-safe HTML escaping for all dynamic content |
| `fmtDt / fmtDate` | Locale-aware date/time formatters |
| `statusBadge(status)` | Coloured `<span class="badge">` |

---

## 10. Key Design Decisions

| Decision | Rationale |
|---|---|
| Single binary | Simplicity — no container, no service manager needed |
| SQLite embedded | Zero-config database; sufficient for team-scale usage |
| All SQL in `database.h` | Clean separation; routes never see SQL strings |
| Single `g_mutex` for writes | Simple serialization; acceptable for moderate load |
| Sessions in DB (not memory) | Survive server restarts without re-login |
| SHA-256 (no OpenSSL) | Removes a heavy dependency; avoids OpenSSL version issues |
| Fire-and-forget email threads | HTTP responses not blocked by SMTP latency |
| Idempotent `reminder_log` | Prevents duplicate emails if background thread overlaps |
| Soft delete for users | Preserves referential integrity in interview history |
| Parameterized SQL everywhere | Prevents SQL injection (OWASP A03) |
| `escHtml()` on all output | Prevents XSS (OWASP A07) |

---

## 11. Prerequisites & Build

### Toolchain (choose one)

**Option A — MinGW-w64 via MSYS2 (Windows)**
```powershell
winget install MSYS2.MSYS2
# In MSYS2 terminal:
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja
```

**Option B — Visual Studio 2019/2022 (Windows)**
- Install from https://visualstudio.microsoft.com/
- Select "Desktop development with C++" workload

**Option C — GCC / Clang (Linux / macOS)**
```bash
sudo apt install build-essential cmake   # Ubuntu/Debian
brew install cmake                        # macOS
```

CMake 3.14+ must be on `PATH`.

### Download dependencies
```powershell
# Windows
.\setup.ps1      # or setup.bat
```
```bash
# Linux/macOS
chmod +x setup.sh && ./setup.sh
```
Downloads `httplib.h`, `json.hpp`, and the SQLite3 amalgamation into `third_party/`.

### Build

**MinGW / Linux / macOS:**
```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

**Visual Studio:**
```powershell
mkdir build; cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
```

**With tests:**
```bash
cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON
cmake --build .
ctest --output-on-failure
```

---

## 12. Running the Application

```powershell
# Windows — with colored log output
.\start.ps1

# Or start manually
.\build\recruitment_tracker.exe

# Linux/macOS
./build/recruitment_tracker
```

Open **http://localhost:8080** in a browser.

**Log level** (default: INFO):
```powershell
$env:LOG_LEVEL = "DEBUG"
.\start.ps1
```
Logs are NDJSON (one JSON object per line), written to stderr / `app.log`.

---

## 13. Default Accounts

| Username | Password | Role |
|---|---|---|
| `admin` | `admin123` | Admin |
| `recruiter1` | `admin123` | Recruiter |
| `recruiter2` | `admin123` | Recruiter |
| `interviewer1` | `admin123` | Interviewer |
| `interviewer2` | `admin123` | Interviewer |
| `interviewer3` | `admin123` | Interviewer |

> **Change these passwords before any production or shared use.**

---

## 14. Security

| Concern | Mitigation |
|---|---|
| Password storage | SHA-256 hash (never stored plaintext, never logged) |
| SQL Injection | All queries use `sqlite3_bind_*` parameterized statements |
| XSS | All dynamic HTML rendered via `escHtml()` |
| Brute-force login | 4 failures → 5-minute lockout per username (`LoginRateLimiter`) |
| Session hijacking | 256-bit random tokens; 24h expiry; stored in DB |
| CSRF | Bearer token in `Authorization` header (not cookies) |
| Role escalation | Role checked server-side on every endpoint; client role is display-only |
| Input length | All user inputs have server-side max-length validation |
| SMTP credentials | Password masked in API response (`•••`); stored in `smtp_config.json` |

---

## 15. Testing

Unit tests use **GoogleTest** (auto-downloaded by CMake):

```bash
cmake .. -DBUILD_TESTS=ON
cmake --build .
ctest --output-on-failure
```

| Test File | Coverage |
|---|---|
| `test_sha256.cpp` | NIST FIPS vectors, known password hashes, determinism |
| `test_session.cpp` | Rate limiter lockout/reset, token create/lookup/remove, edge cases |
| `test_database.cpp` | Full CRUD for users, slots, interviews, notifications, feedback, password reset; in-memory SQLite per test |

---

## 16. Deployment / Sharing

### Option A — Portable ZIP (Windows, no install)
```
InterviewScheduler_Portable/
  recruitment_tracker.exe
  libgcc_s_seh-1.dll
  libstdc++-6.dll
  libwinpthread-1.dll
  frontend/
  START.bat              ← double-click to run
```
Share the ZIP. Unzip → double-click `START.bat` → open `http://localhost:8080`.

### Option B — Source via Git (all platforms)
```bash
git clone <repo>
cd RecruitmentTracker
./setup.sh          # or setup.ps1 on Windows
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
./recruitment_tracker
```

### Option C — Shared Server (team access)
The server binds to `0.0.0.0:8080` — accessible from any machine on the network.
```powershell
# Open firewall on the host machine
netsh advfirewall firewall add rule name="Interview Scheduler" `
  dir=in action=allow protocol=TCP localport=8080
```
Team members open `http://<server-ip>:8080`.

### SMTP Email Setup (optional)
Configure in the Admin dashboard → **Email Config** tab, or pre-populate `smtp_config.json`:
```json
{
  "enabled": true,
  "host": "smtp.example.com",
  "port": 587,
  "username": "you@example.com",
  "password": "yourpassword",
  "from_email": "noreply@example.com",
  "from_name": "Interview Scheduler"
}
```
