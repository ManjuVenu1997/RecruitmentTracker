# Interview Scheduling Web Application (C++)

A full-stack Interview Scheduling application built with C++17 (backend) and
HTML/CSS/JavaScript (frontend).

---

## Architecture

| Layer    | Technology |
|----------|-----------|
| Backend  | C++17 + [cpp-httplib](https://github.com/yhirose/cpp-httplib) (HTTP server) |
| Database | SQLite3 (embedded, no server required) |
| JSON     | [nlohmann/json](https://github.com/nlohmann/json) |
| Frontend | Bootstrap 5 + FullCalendar 6 + Chart.js (CDN) |

---

## Prerequisites

Install **one** of the following C++ toolchains:

**Option A – Visual Studio 2019/2022 (recommended on Windows)**
- Download from https://visualstudio.microsoft.com/
- When installing, select the **"Desktop development with C++"** workload

**Option B – MinGW-w64 via MSYS2**
```
winget install MSYS2.MSYS2
# Then inside MSYS2 terminal:
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja
```

**Option C – Any Linux/macOS** (GCC 9+ or Clang 10+):
```bash
sudo apt install build-essential cmake    # Ubuntu/Debian
brew install cmake                         # macOS
```

CMake 3.14+ must be on PATH.

---

## Quick Start

### 1. Download dependencies
```powershell
# Windows PowerShell
.\setup.ps1
```
```bash
# Linux/macOS
chmod +x setup.sh && ./setup.sh
```

### 2. Build

**Visual Studio (Windows):**
```powershell
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
cd Release
.\recruitment_tracker.exe
```

**MinGW / Linux / macOS:**
```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
./recruitment_tracker
```

### 3. Open the app
Navigate to **http://localhost:8080**

---

## Default Accounts

| Username      | Password   | Role        |
|---------------|-----------|-------------|
| admin         | admin123  | Admin       |
| recruiter1    | admin123  | Recruiter   |
| recruiter2    | admin123  | Recruiter   |
| interviewer1  | admin123  | Interviewer |
| interviewer2  | admin123  | Interviewer |
| interviewer3  | admin123  | Interviewer |

---

## Features

### Recruiter Dashboard
- Search available slots filtered by **skill** and **date**
- **Book interviews** with candidate name + email
- Calendar view (past 30 days → next 30 days)
- Real-time notifications for declined interviews

### Interviewer Dashboard
- Manage **skillsets** (add/remove skills)
- Create and delete **availability slots**
- Calendar shows all slots (green = free, orange = booked)
- **Decline** booked interviews with a mandatory reason
- Decline reason is sent to the recruiter automatically

### Admin Dashboard
- **Utilization table** – booked vs available slots per interviewer with % bar
- **Skill distribution doughnut chart** (Chart.js)
- **Unutilized slots report** – past slots that were never booked
- Read-only **calendar view** of all interviews
- **User management** – create new users of any role

---

## API Reference

### Authentication
| Method | Endpoint           | Description              |
|--------|--------------------|--------------------------|
| POST   | /api/auth/login    | Login → returns JWT token|
| POST   | /api/auth/logout   | Logout                   |
| GET    | /api/auth/me       | Get current user         |

### Skills
| Method | Endpoint                                    | Access     |
|--------|---------------------------------------------|------------|
| GET    | /api/skills                                 | All        |
| POST   | /api/skills                                 | Ivwr/Admin |
| GET    | /api/interviewers/:id/skills                | All        |
| POST   | /api/interviewers/:id/skills                | Ivwr/Admin |
| DELETE | /api/interviewers/:id/skills/:skill_id      | Ivwr/Admin |

### Slots
| Method | Endpoint                              | Access       |
|--------|---------------------------------------|--------------|
| GET    | /api/interviewers/:id/slots           | All          |
| POST   | /api/interviewers/:id/slots           | Ivwr/Admin   |
| DELETE | /api/slots/:id                        | Ivwr/Admin   |
| GET    | /api/available-slots?skill=&date=     | Recruiter    |

### Interviews
| Method | Endpoint                              | Access       |
|--------|---------------------------------------|--------------|
| POST   | /api/interviews                       | Recruiter    |
| GET    | /api/interviews                       | All (filtered)|
| PUT    | /api/interviews/:id/decline           | Interviewer  |
| PUT    | /api/interviews/:id/complete          | Ivwr/Admin   |

### Admin
| Method | Endpoint                              | Access |
|--------|---------------------------------------|--------|
| GET    | /api/admin/utilization                | Admin  |
| GET    | /api/admin/skill-distribution         | Admin  |
| GET    | /api/admin/unutilized-slots           | Admin  |
| GET    | /api/users                            | Admin  |
| POST   | /api/users                            | Admin  |

---

## Project Structure

```
RecruitmentTracker/
├── CMakeLists.txt          Build configuration
├── setup.ps1               Dependency downloader (PowerShell)
├── setup.bat               Dependency downloader (Batch)
├── src/
│   ├── main.cpp            HTTP server + all API route handlers
│   ├── database.h          SQLite3 database manager (all CRUD)
│   ├── session.h           In-memory session/token manager
│   └── sha256.h            SHA-256 password hashing
├── third_party/            (created by setup script)
│   ├── httplib.h           cpp-httplib – single header HTTP server
│   ├── json.hpp            nlohmann/json – single header JSON
│   ├── sqlite3.h           SQLite3 amalgamation header
│   └── sqlite3.c           SQLite3 amalgamation source
└── frontend/
    ├── index.html          Login page
    ├── recruiter.html      Recruiter dashboard
    ├── interviewer.html    Interviewer dashboard
    ├── admin.html          Admin dashboard
    ├── css/style.css       Global styles
    └── js/
        ├── common.js       Shared API client + utilities
        ├── recruiter.js    Recruiter page logic
        ├── interviewer.js  Interviewer page logic
        └── admin.js        Admin page logic
```

---

## Security Notes

- Passwords are hashed with **SHA-256** (stored, never logged)
- All API endpoints are **role-protected** via Bearer token auth
- SQL queries use **parameterized statements** (no SQL injection risk)
- Tokens expire after **24 hours**
- 30-day scheduling window enforced on both **client and server**
