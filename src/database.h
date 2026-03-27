#pragma once
#include <sqlite3.h>
#include <string>
#include <vector>
#include <optional>
#include <stdexcept>
#include <algorithm>
#include <random>
#include <cstdio>
#include <cctype>
#include "json.hpp"

using json = nlohmann::json;

// ─────────────────────────────────────────────
//  Domain structs
// ─────────────────────────────────────────────
struct User {
    int         id = 0;
    std::string username;
    std::string passwordHash;
    std::string email;
    std::string fullName;
    std::string role;   // recruiter | interviewer | admin

    json toJson() const {
        return {{"id",id},{"username",username},{"email",email},
                {"full_name",fullName},{"role",role}};
    }
};

struct Skill {
    int         id = 0;
    std::string name;
    json toJson() const { return {{"id",id},{"name",name}}; }
};

// ─────────────────────────────────────────────
//  Database class
// ─────────────────────────────────────────────
class Database {
    sqlite3* db_ = nullptr;

    // RAII statement wrapper
    struct Stmt {
        sqlite3_stmt* s = nullptr;
        ~Stmt() { if (s) sqlite3_finalize(s); }
        sqlite3_stmt*  get()  { return s; }
        sqlite3_stmt** ptr()  { return &s; }
    };

    void exec(const std::string& sql) {
        char* err = nullptr;
        if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
            std::string msg = err ? err : "unknown";
            sqlite3_free(err);
            throw std::runtime_error("SQL error: " + msg);
        }
    }

    static std::string col(sqlite3_stmt* s, int i) {
        const unsigned char* t = sqlite3_column_text(s, i);
        return t ? reinterpret_cast<const char*>(t) : "";
    }

public:
    explicit Database(const std::string& path) {
        if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK)
            throw std::runtime_error(std::string("Cannot open DB: ") + sqlite3_errmsg(db_));
        exec("PRAGMA foreign_keys = ON;");
        exec("PRAGMA journal_mode = WAL;");
    }
    ~Database() { if (db_) sqlite3_close(db_); }

    // ──────────────────────────────────────────
    //  Schema creation + seed data
    // ──────────────────────────────────────────
    void initialize() {
        exec(R"(
            CREATE TABLE IF NOT EXISTS users (
                id            INTEGER PRIMARY KEY AUTOINCREMENT,
                username      TEXT UNIQUE NOT NULL,
                password_hash TEXT NOT NULL,
                email         TEXT UNIQUE NOT NULL,
                full_name     TEXT NOT NULL,
                role          TEXT NOT NULL CHECK(role IN ('recruiter','interviewer','admin'))
            );
        )");
        exec(R"(
            CREATE TABLE IF NOT EXISTS skills (
                id   INTEGER PRIMARY KEY AUTOINCREMENT,
                name TEXT UNIQUE NOT NULL COLLATE NOCASE
            );
        )");
        exec(R"(
            CREATE TABLE IF NOT EXISTS interviewer_skills (
                interviewer_id INTEGER NOT NULL,
                skill_id       INTEGER NOT NULL,
                PRIMARY KEY (interviewer_id, skill_id),
                FOREIGN KEY (interviewer_id) REFERENCES users(id) ON DELETE CASCADE,
                FOREIGN KEY (skill_id)       REFERENCES skills(id) ON DELETE CASCADE
            );
        )");
        exec(R"(
            CREATE TABLE IF NOT EXISTS availability_slots (
                id             INTEGER PRIMARY KEY AUTOINCREMENT,
                interviewer_id INTEGER NOT NULL,
                start_time     TEXT NOT NULL,
                end_time       TEXT NOT NULL,
                status         TEXT NOT NULL DEFAULT 'available'
                               CHECK(status IN ('available','blocked')),
                created_at     TEXT DEFAULT (datetime('now')),
                FOREIGN KEY (interviewer_id) REFERENCES users(id) ON DELETE CASCADE
            );
        )");
        exec(R"(
            CREATE TABLE IF NOT EXISTS interviews (
                id              INTEGER PRIMARY KEY AUTOINCREMENT,
                slot_id         INTEGER NOT NULL,
                recruiter_id    INTEGER NOT NULL,
                candidate_name  TEXT NOT NULL,
                candidate_email TEXT NOT NULL,
                status          TEXT NOT NULL DEFAULT 'confirmed'
                                CHECK(status IN ('confirmed','declined','completed')),
                decline_reason  TEXT DEFAULT '',
                booked_at       TEXT DEFAULT (datetime('now')),
                FOREIGN KEY (slot_id)      REFERENCES availability_slots(id),
                FOREIGN KEY (recruiter_id) REFERENCES users(id)
            );
        )");
        exec(R"(
            CREATE TABLE IF NOT EXISTS notifications (
                id           INTEGER PRIMARY KEY AUTOINCREMENT,
                user_id      INTEGER NOT NULL,
                message      TEXT NOT NULL,
                type         TEXT NOT NULL,
                reference_id INTEGER DEFAULT 0,
                is_read      INTEGER DEFAULT 0,
                created_at   TEXT DEFAULT (datetime('now')),
                FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
            );
        )");
        exec(R"(
            CREATE TABLE IF NOT EXISTS interview_feedback (
                id           INTEGER PRIMARY KEY AUTOINCREMENT,
                interview_id INTEGER NOT NULL UNIQUE,
                rating       INTEGER NOT NULL CHECK(rating BETWEEN 1 AND 5),
                notes        TEXT NOT NULL DEFAULT '',
                submitted_at TEXT DEFAULT (datetime('now')),
                FOREIGN KEY (interview_id) REFERENCES interviews(id) ON DELETE CASCADE
            );
        )");
        exec(R"(
            CREATE TABLE IF NOT EXISTS reminder_log (
                interview_id INTEGER NOT NULL,
                reminder_type TEXT NOT NULL,
                PRIMARY KEY (interview_id, reminder_type)
            );
        )");
        exec(R"(
            CREATE TABLE IF NOT EXISTS sessions (
                token      TEXT PRIMARY KEY,
                user_id    INTEGER NOT NULL,
                username   TEXT NOT NULL,
                full_name  TEXT NOT NULL,
                role       TEXT NOT NULL,
                expires_at TEXT NOT NULL,
                FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
            );
        )");

        runMigrations();
        seedDefaults();
    }

private:
    // ──────────────────────────────────────────
    //  Schema migrations (run once on startup)
    // ──────────────────────────────────────────
    void runMigrations() {
        exec("CREATE TABLE IF NOT EXISTS schema_version (version INTEGER NOT NULL)");

        int version = 0;
        {
            Stmt st;
            if (sqlite3_prepare_v2(db_,
                    "SELECT COALESCE(MAX(version),0) FROM schema_version",
                    -1, st.ptr(), nullptr) == SQLITE_OK &&
                sqlite3_step(st.get()) == SQLITE_ROW)
                version = sqlite3_column_int(st.get(), 0);
        }

        if (version < 1) {
            // Add audit / soft-delete columns to users table.
            // We use try/catch because ALTER TABLE fails on duplicate columns
            // when built against SQLite < 3.37 (which lacks IF NOT EXISTS).
            try { exec("ALTER TABLE users ADD COLUMN created_at TEXT DEFAULT (datetime('now'))"); } catch (...) {}
            try { exec("ALTER TABLE users ADD COLUMN updated_at TEXT DEFAULT (datetime('now'))"); } catch (...) {}
            try { exec("ALTER TABLE users ADD COLUMN deleted_at TEXT DEFAULT NULL"); } catch (...) {}
            exec("INSERT INTO schema_version (version) VALUES (1)");
        }

        if (version < 2) {
            exec(R"(
                CREATE TABLE IF NOT EXISTS password_reset_tokens (
                    token      TEXT PRIMARY KEY,
                    user_id    INTEGER NOT NULL,
                    expires_at TEXT NOT NULL,
                    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
                )
            )");
            exec("INSERT INTO schema_version (version) VALUES (2)");
        }
    }

    void seedDefaults() {
        Stmt st;
        sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM users WHERE role='admin'", -1, st.ptr(), nullptr);
        if (sqlite3_step(st.get()) == SQLITE_ROW && sqlite3_column_int(st.get(), 0) > 0)
            return;

        // sha256("admin123") = 240be518fabd2724ddb6f04eeb1da5967448d7e831c08c8fa822809f74c720a9
        const char* pwHash = "240be518fabd2724ddb6f04eeb1da5967448d7e831c08c8fa822809f74c720a9";
        exec(std::string("INSERT INTO users (username,password_hash,email,full_name,role) VALUES") +
            "('admin','" + pwHash + "','admin@company.com','System Admin','admin'),"
            "('recruiter1','" + pwHash + "','recruiter1@company.com','Alice Johnson','recruiter'),"
            "('recruiter2','" + pwHash + "','recruiter2@company.com','Dave Wilson','recruiter'),"
            "('interviewer1','" + pwHash + "','interviewer1@company.com','Bob Smith','interviewer'),"
            "('interviewer2','" + pwHash + "','interviewer2@company.com','Carol Davis','interviewer'),"
            "('interviewer3','" + pwHash + "','interviewer3@company.com','Eve Martinez','interviewer')");

        exec(R"(INSERT INTO skills (name) VALUES
            ('C++'),('Java'),('Python'),('JavaScript'),('React'),
            ('Node.js'),('SQL'),('System Design'),('Data Structures'),('Machine Learning'))");

        exec(R"(INSERT INTO interviewer_skills (interviewer_id, skill_id)
            SELECT u.id, s.id FROM users u, skills s
            WHERE u.username='interviewer1'
              AND s.name IN ('C++','System Design','Data Structures'))");
        exec(R"(INSERT INTO interviewer_skills (interviewer_id, skill_id)
            SELECT u.id, s.id FROM users u, skills s
            WHERE u.username='interviewer2'
              AND s.name IN ('Java','Python','SQL','Machine Learning'))");
        exec(R"(INSERT INTO interviewer_skills (interviewer_id, skill_id)
            SELECT u.id, s.id FROM users u, skills s
            WHERE u.username='interviewer3'
              AND s.name IN ('JavaScript','React','Node.js','System Design'))");
    }

public:
    // ──────────────────────────────────────────
    //  USER operations
    // ──────────────────────────────────────────
    bool createUser(const std::string& username, const std::string& pwHash,
                    const std::string& email, const std::string& fullName,
                    const std::string& role) {
        Stmt st;
        const char* sql =
            "INSERT INTO users (username,password_hash,email,full_name,role) VALUES(?,?,?,?,?)";
        if (sqlite3_prepare_v2(db_, sql, -1, st.ptr(), nullptr) != SQLITE_OK) return false;
        sqlite3_bind_text(st.get(), 1, username.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st.get(), 2, pwHash.c_str(),   -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st.get(), 3, email.c_str(),    -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st.get(), 4, fullName.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st.get(), 5, role.c_str(),     -1, SQLITE_TRANSIENT);
        return sqlite3_step(st.get()) == SQLITE_DONE;
    }

    std::optional<User> getUserByUsername(const std::string& username) {
        Stmt st;
        const char* sql =
            "SELECT id,username,password_hash,email,full_name,role FROM users"
            " WHERE username=? AND deleted_at IS NULL";
        if (sqlite3_prepare_v2(db_, sql, -1, st.ptr(), nullptr) != SQLITE_OK) return {};
        sqlite3_bind_text(st.get(), 1, username.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(st.get()) != SQLITE_ROW) return {};
        User u; u.id=sqlite3_column_int(st.get(),0); u.username=col(st.get(),1);
        u.passwordHash=col(st.get(),2); u.email=col(st.get(),3);
        u.fullName=col(st.get(),4); u.role=col(st.get(),5);
        return u;
    }

    std::optional<User> getUserById(int id) {
        Stmt st;
        const char* sql =
            "SELECT id,username,password_hash,email,full_name,role FROM users"
            " WHERE id=? AND deleted_at IS NULL";
        if (sqlite3_prepare_v2(db_, sql, -1, st.ptr(), nullptr) != SQLITE_OK) return {};
        sqlite3_bind_int(st.get(), 1, id);
        if (sqlite3_step(st.get()) != SQLITE_ROW) return {};
        User u; u.id=sqlite3_column_int(st.get(),0); u.username=col(st.get(),1);
        u.passwordHash=col(st.get(),2); u.email=col(st.get(),3);
        u.fullName=col(st.get(),4); u.role=col(st.get(),5);
        return u;
    }

    std::vector<User> getInterviewers() {
        Stmt st;
        const char* sql =
            "SELECT id,username,password_hash,email,full_name,role FROM users"
            " WHERE role='interviewer' AND deleted_at IS NULL ORDER BY full_name";
        std::vector<User> res;
        if (sqlite3_prepare_v2(db_, sql, -1, st.ptr(), nullptr) != SQLITE_OK) return res;
        while (sqlite3_step(st.get()) == SQLITE_ROW) {
            User u; u.id=sqlite3_column_int(st.get(),0); u.username=col(st.get(),1);
            u.email=col(st.get(),3); u.fullName=col(st.get(),4); u.role=col(st.get(),5);
            res.push_back(u);
        }
        return res;
    }

    std::vector<User> getAllUsers() {
        Stmt st;
        const char* sql =
            "SELECT id,username,password_hash,email,full_name,role FROM users"
            " WHERE deleted_at IS NULL ORDER BY role,full_name";
        std::vector<User> res;
        if (sqlite3_prepare_v2(db_, sql, -1, st.ptr(), nullptr) != SQLITE_OK) return res;
        while (sqlite3_step(st.get()) == SQLITE_ROW) {
            User u; u.id=sqlite3_column_int(st.get(),0); u.username=col(st.get(),1);
            u.passwordHash=col(st.get(),2); u.email=col(st.get(),3);
            u.fullName=col(st.get(),4); u.role=col(st.get(),5);
            res.push_back(u);
        }
        return res;
    }

    // ──────────────────────────────────────────
    //  SKILL operations
    // ──────────────────────────────────────────
    int createOrGetSkill(const std::string& name) {
        {   // check existing
            Stmt st;
            const char* sql = "SELECT id FROM skills WHERE name=? COLLATE NOCASE";
            if (sqlite3_prepare_v2(db_, sql, -1, st.ptr(), nullptr) == SQLITE_OK) {
                sqlite3_bind_text(st.get(), 1, name.c_str(), -1, SQLITE_TRANSIENT);
                if (sqlite3_step(st.get()) == SQLITE_ROW)
                    return sqlite3_column_int(st.get(), 0);
            }
        }
        Stmt st;
        const char* sql = "INSERT INTO skills (name) VALUES (?)";
        if (sqlite3_prepare_v2(db_, sql, -1, st.ptr(), nullptr) != SQLITE_OK) return -1;
        sqlite3_bind_text(st.get(), 1, name.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(st.get()) == SQLITE_DONE)
            return static_cast<int>(sqlite3_last_insert_rowid(db_));
        return -1;
    }

    std::vector<Skill> getAllSkills() {
        Stmt st;
        std::vector<Skill> res;
        if (sqlite3_prepare_v2(db_, "SELECT id,name FROM skills ORDER BY name",
                               -1, st.ptr(), nullptr) != SQLITE_OK) return res;
        while (sqlite3_step(st.get()) == SQLITE_ROW)
            res.push_back({sqlite3_column_int(st.get(),0), col(st.get(),1)});
        return res;
    }

    bool addInterviewerSkill(int interviewerId, int skillId) {
        Stmt st;
        const char* sql =
            "INSERT OR IGNORE INTO interviewer_skills (interviewer_id,skill_id) VALUES(?,?)";
        if (sqlite3_prepare_v2(db_, sql, -1, st.ptr(), nullptr) != SQLITE_OK) return false;
        sqlite3_bind_int(st.get(), 1, interviewerId);
        sqlite3_bind_int(st.get(), 2, skillId);
        return sqlite3_step(st.get()) == SQLITE_DONE;
    }

    bool removeInterviewerSkill(int interviewerId, int skillId) {
        Stmt st;
        const char* sql =
            "DELETE FROM interviewer_skills WHERE interviewer_id=? AND skill_id=?";
        if (sqlite3_prepare_v2(db_, sql, -1, st.ptr(), nullptr) != SQLITE_OK) return false;
        sqlite3_bind_int(st.get(), 1, interviewerId);
        sqlite3_bind_int(st.get(), 2, skillId);
        return sqlite3_step(st.get()) == SQLITE_DONE;
    }

    std::vector<Skill> getInterviewerSkills(int interviewerId) {
        Stmt st;
        std::vector<Skill> res;
        const char* sql =
            "SELECT s.id,s.name FROM skills s"
            " JOIN interviewer_skills isk ON s.id=isk.skill_id"
            " WHERE isk.interviewer_id=? ORDER BY s.name";
        if (sqlite3_prepare_v2(db_, sql, -1, st.ptr(), nullptr) != SQLITE_OK) return res;
        sqlite3_bind_int(st.get(), 1, interviewerId);
        while (sqlite3_step(st.get()) == SQLITE_ROW)
            res.push_back({sqlite3_column_int(st.get(),0), col(st.get(),1)});
        return res;
    }

    // ──────────────────────────────────────────
    //  SLOT operations
    // ──────────────────────────────────────────
    int createSlot(int interviewerId, const std::string& startTime, const std::string& endTime) {
        Stmt st;
        const char* sql =
            "INSERT INTO availability_slots (interviewer_id,start_time,end_time) VALUES(?,?,?)";
        if (sqlite3_prepare_v2(db_, sql, -1, st.ptr(), nullptr) != SQLITE_OK) return -1;
        sqlite3_bind_int (st.get(), 1, interviewerId);
        sqlite3_bind_text(st.get(), 2, startTime.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st.get(), 3, endTime.c_str(),   -1, SQLITE_TRANSIENT);
        if (sqlite3_step(st.get()) == SQLITE_DONE)
            return static_cast<int>(sqlite3_last_insert_rowid(db_));
        return -1;
    }

    bool updateSlotStatus(int slotId, const std::string& status) {
        Stmt st;
        const char* sql = "UPDATE availability_slots SET status=? WHERE id=?";
        if (sqlite3_prepare_v2(db_, sql, -1, st.ptr(), nullptr) != SQLITE_OK) return false;
        sqlite3_bind_text(st.get(), 1, status.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int (st.get(), 2, slotId);
        return sqlite3_step(st.get()) == SQLITE_DONE;
    }

    bool deleteSlot(int slotId, int ownerId) {
        Stmt st;
        const char* sql =
            "DELETE FROM availability_slots WHERE id=? AND interviewer_id=? AND status='available'";
        if (sqlite3_prepare_v2(db_, sql, -1, st.ptr(), nullptr) != SQLITE_OK) return false;
        sqlite3_bind_int(st.get(), 1, slotId);
        sqlite3_bind_int(st.get(), 2, ownerId);
        sqlite3_step(st.get());
        return sqlite3_changes(db_) > 0;
    }

    std::optional<json> getSlotById(int slotId) {
        Stmt st;
        const char* sql =
            "SELECT s.id,s.interviewer_id,s.start_time,s.end_time,s.status,u.full_name"
            " FROM availability_slots s JOIN users u ON s.interviewer_id=u.id WHERE s.id=?";
        if (sqlite3_prepare_v2(db_, sql, -1, st.ptr(), nullptr) != SQLITE_OK) return {};
        sqlite3_bind_int(st.get(), 1, slotId);
        if (sqlite3_step(st.get()) != SQLITE_ROW) return {};
        return json{{"id",sqlite3_column_int(st.get(),0)},
                    {"interviewer_id",sqlite3_column_int(st.get(),1)},
                    {"start_time",col(st.get(),2)},{"end_time",col(st.get(),3)},
                    {"status",col(st.get(),4)},{"interviewer_name",col(st.get(),5)}};
    }

    // All slots for one interviewer (with linked interview info)
    std::vector<json> getInterviewerSlots(int interviewerId) {
        Stmt st;
        const char* sql = R"(
            SELECT s.id, s.interviewer_id, s.start_time, s.end_time, s.status,
                   u.full_name,
                   COALESCE(i.id,0)             AS interview_id,
                   COALESCE(i.candidate_name,'') AS candidate_name,
                   COALESCE(i.status,'')         AS interview_status
            FROM availability_slots s
            JOIN users u ON s.interviewer_id = u.id
            LEFT JOIN interviews i ON s.id = i.slot_id AND i.status != 'declined'
            WHERE s.interviewer_id = ?
            ORDER BY s.start_time
        )";
        std::vector<json> res;
        if (sqlite3_prepare_v2(db_, sql, -1, st.ptr(), nullptr) != SQLITE_OK) return res;
        sqlite3_bind_int(st.get(), 1, interviewerId);
        while (sqlite3_step(st.get()) == SQLITE_ROW)
            res.push_back({
                {"id",sqlite3_column_int(st.get(),0)},
                {"interviewer_id",sqlite3_column_int(st.get(),1)},
                {"start_time",col(st.get(),2)},{"end_time",col(st.get(),3)},
                {"status",col(st.get(),4)},{"interviewer_name",col(st.get(),5)},
                {"interview_id",sqlite3_column_int(st.get(),6)},
                {"candidate_name",col(st.get(),7)},
                {"interview_status",col(st.get(),8)}
            });
        return res;
    }

    // Available slots for recruiters, with optional skill/date filter (parameterized)
    std::vector<json> getAvailableSlots(const std::string& skill,
                                         const std::string& date) {
        // Build dynamic but parameterized query
        std::string sql = R"(
            SELECT s.id, s.interviewer_id, s.start_time, s.end_time,
                   u.full_name AS interviewer_name,
                   GROUP_CONCAT(sk.name, ', ') AS skills
            FROM availability_slots s
            JOIN users u ON s.interviewer_id = u.id
            LEFT JOIN interviewer_skills isk ON u.id = isk.interviewer_id
            LEFT JOIN skills sk ON isk.skill_id = sk.id
            WHERE s.status = 'available'
              AND s.start_time >= datetime('now')
              AND s.start_time <= datetime('now','+30 days')
        )";

        int paramIdx = 1;
        bool hasSkill = !skill.empty();
        bool hasDate  = !date.empty();

        if (hasSkill)
            sql += " AND s.interviewer_id IN"
                   " (SELECT isk2.interviewer_id FROM interviewer_skills isk2"
                   "   JOIN skills sk2 ON isk2.skill_id=sk2.id WHERE sk2.name=? COLLATE NOCASE)";
        if (hasDate)
            sql += " AND date(s.start_time) = ?";

        sql += " GROUP BY s.id ORDER BY s.start_time";

        Stmt st;
        std::vector<json> res;
        if (sqlite3_prepare_v2(db_, sql.c_str(), -1, st.ptr(), nullptr) != SQLITE_OK) return res;
        if (hasSkill)
            sqlite3_bind_text(st.get(), paramIdx++, skill.c_str(), -1, SQLITE_TRANSIENT);
        if (hasDate)
            sqlite3_bind_text(st.get(), paramIdx,   date.c_str(),  -1, SQLITE_TRANSIENT);

        while (sqlite3_step(st.get()) == SQLITE_ROW)
            res.push_back({
                {"id",sqlite3_column_int(st.get(),0)},
                {"interviewer_id",sqlite3_column_int(st.get(),1)},
                {"start_time",col(st.get(),2)},{"end_time",col(st.get(),3)},
                {"interviewer_name",col(st.get(),4)},{"skills",col(st.get(),5)}
            });
        return res;
    }

    // ──────────────────────────────────────────
    //  INTERVIEW operations
    // ──────────────────────────────────────────
    int createInterview(int slotId, int recruiterId,
                        const std::string& candidateName,
                        const std::string& candidateEmail) {
        Stmt st;
        const char* sql =
            "INSERT INTO interviews (slot_id,recruiter_id,candidate_name,candidate_email)"
            " VALUES(?,?,?,?)";
        if (sqlite3_prepare_v2(db_, sql, -1, st.ptr(), nullptr) != SQLITE_OK) return -1;
        sqlite3_bind_int (st.get(), 1, slotId);
        sqlite3_bind_int (st.get(), 2, recruiterId);
        sqlite3_bind_text(st.get(), 3, candidateName.c_str(),  -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st.get(), 4, candidateEmail.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(st.get()) == SQLITE_DONE)
            return static_cast<int>(sqlite3_last_insert_rowid(db_));
        return -1;
    }

    bool updateInterviewStatus(int interviewId, const std::string& status,
                               const std::string& reason = "") {
        Stmt st;
        const char* sql = "UPDATE interviews SET status=?,decline_reason=? WHERE id=?";
        if (sqlite3_prepare_v2(db_, sql, -1, st.ptr(), nullptr) != SQLITE_OK) return false;
        sqlite3_bind_text(st.get(), 1, status.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st.get(), 2, reason.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int (st.get(), 3, interviewId);
        return sqlite3_step(st.get()) == SQLITE_DONE;
    }

    std::optional<json> getInterviewById(int id) {
        Stmt st;
        const char* sql = R"(
            SELECT i.id, i.slot_id, i.recruiter_id, i.candidate_name, i.candidate_email,
                   i.status, i.decline_reason, i.booked_at,
                   s.start_time, s.end_time, s.interviewer_id,
                   ui.full_name AS interviewer_name, ur.full_name AS recruiter_name
            FROM interviews i
            JOIN availability_slots s ON i.slot_id=s.id
            JOIN users ui ON s.interviewer_id=ui.id
            JOIN users ur ON i.recruiter_id=ur.id
            WHERE i.id=?
        )";
        if (sqlite3_prepare_v2(db_, sql, -1, st.ptr(), nullptr) != SQLITE_OK) return {};
        sqlite3_bind_int(st.get(), 1, id);
        if (sqlite3_step(st.get()) != SQLITE_ROW) return {};
        return json{
            {"id",sqlite3_column_int(st.get(),0)},
            {"slot_id",sqlite3_column_int(st.get(),1)},
            {"recruiter_id",sqlite3_column_int(st.get(),2)},
            {"candidate_name",col(st.get(),3)},{"candidate_email",col(st.get(),4)},
            {"status",col(st.get(),5)},{"decline_reason",col(st.get(),6)},
            {"booked_at",col(st.get(),7)},{"start_time",col(st.get(),8)},
            {"end_time",col(st.get(),9)},{"interviewer_id",sqlite3_column_int(st.get(),10)},
            {"interviewer_name",col(st.get(),11)},{"recruiter_name",col(st.get(),12)}
        };
    }

    // Role-filtered interview list for calendar view (past 1 month → next 1 month)
    std::vector<json> getInterviewsForUser(int userId, const std::string& role) {
        std::string sql = R"(
            SELECT i.id, i.slot_id, i.recruiter_id, i.candidate_name, i.candidate_email,
                   i.status, i.decline_reason, i.booked_at,
                   s.start_time, s.end_time, s.interviewer_id,
                   ui.full_name AS interviewer_name, ur.full_name AS recruiter_name
            FROM interviews i
            JOIN availability_slots s ON i.slot_id=s.id
            JOIN users ui ON s.interviewer_id=ui.id
            JOIN users ur ON i.recruiter_id=ur.id
            WHERE s.start_time >= datetime('now','-30 days')
              AND s.start_time <= datetime('now','+30 days')
        )";

        std::vector<std::string> conditions;
        if (role == "recruiter")   sql += " AND i.recruiter_id=" + std::to_string(userId);
        else if (role == "interviewer") sql += " AND s.interviewer_id=" + std::to_string(userId);
        // admin → no filter

        sql += " ORDER BY s.start_time DESC";

        Stmt st;
        std::vector<json> res;
        if (sqlite3_prepare_v2(db_, sql.c_str(), -1, st.ptr(), nullptr) != SQLITE_OK) return res;
        while (sqlite3_step(st.get()) == SQLITE_ROW)
            res.push_back({
                {"id",sqlite3_column_int(st.get(),0)},
                {"slot_id",sqlite3_column_int(st.get(),1)},
                {"recruiter_id",sqlite3_column_int(st.get(),2)},
                {"candidate_name",col(st.get(),3)},{"candidate_email",col(st.get(),4)},
                {"status",col(st.get(),5)},{"decline_reason",col(st.get(),6)},
                {"booked_at",col(st.get(),7)},{"start_time",col(st.get(),8)},
                {"end_time",col(st.get(),9)},
                {"interviewer_id",sqlite3_column_int(st.get(),10)},
                {"interviewer_name",col(st.get(),11)},
                {"recruiter_name",col(st.get(),12)}
            });
        return res;
    }

    // ──────────────────────────────────────────
    //  NOTIFICATION operations
    // ──────────────────────────────────────────
    int createNotification(int userId, const std::string& message,
                           const std::string& type, int referenceId = 0) {
        Stmt st;
        const char* sql =
            "INSERT INTO notifications (user_id,message,type,reference_id) VALUES(?,?,?,?)";
        if (sqlite3_prepare_v2(db_, sql, -1, st.ptr(), nullptr) != SQLITE_OK) return -1;
        sqlite3_bind_int (st.get(), 1, userId);
        sqlite3_bind_text(st.get(), 2, message.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st.get(), 3, type.c_str(),    -1, SQLITE_TRANSIENT);
        sqlite3_bind_int (st.get(), 4, referenceId);
        if (sqlite3_step(st.get()) == SQLITE_DONE)
            return static_cast<int>(sqlite3_last_insert_rowid(db_));
        return -1;
    }

    std::vector<json> getNotificationsForUser(int userId) {
        Stmt st;
        const char* sql = R"(
            SELECT id,user_id,message,type,reference_id,is_read,created_at
            FROM notifications WHERE user_id=?
            ORDER BY created_at DESC LIMIT 50
        )";
        std::vector<json> res;
        if (sqlite3_prepare_v2(db_, sql, -1, st.ptr(), nullptr) != SQLITE_OK) return res;
        sqlite3_bind_int(st.get(), 1, userId);
        while (sqlite3_step(st.get()) == SQLITE_ROW)
            res.push_back({
                {"id",sqlite3_column_int(st.get(),0)},
                {"user_id",sqlite3_column_int(st.get(),1)},
                {"message",col(st.get(),2)},{"type",col(st.get(),3)},
                {"reference_id",sqlite3_column_int(st.get(),4)},
                {"is_read",sqlite3_column_int(st.get(),5)==1},
                {"created_at",col(st.get(),6)}
            });
        return res;
    }

    int getUnreadCount(int userId) {
        Stmt st;
        const char* sql = "SELECT COUNT(*) FROM notifications WHERE user_id=? AND is_read=0";
        if (sqlite3_prepare_v2(db_, sql, -1, st.ptr(), nullptr) != SQLITE_OK) return 0;
        sqlite3_bind_int(st.get(), 1, userId);
        return (sqlite3_step(st.get()) == SQLITE_ROW) ? sqlite3_column_int(st.get(), 0) : 0;
    }

    bool markNotificationRead(int notificationId, int userId) {
        Stmt st;
        const char* sql = "UPDATE notifications SET is_read=1 WHERE id=? AND user_id=?";
        if (sqlite3_prepare_v2(db_, sql, -1, st.ptr(), nullptr) != SQLITE_OK) return false;
        sqlite3_bind_int(st.get(), 1, notificationId);
        sqlite3_bind_int(st.get(), 2, userId);
        return sqlite3_step(st.get()) == SQLITE_DONE;
    }

    bool markAllNotificationsRead(int userId) {
        Stmt st;
        const char* sql = "UPDATE notifications SET is_read=1 WHERE user_id=?";
        if (sqlite3_prepare_v2(db_, sql, -1, st.ptr(), nullptr) != SQLITE_OK) return false;
        sqlite3_bind_int(st.get(), 1, userId);
        return sqlite3_step(st.get()) == SQLITE_DONE;
    }

    // ──────────────────────────────────────────
    //  ADMIN STATISTICS
    // ──────────────────────────────────────────
    json getUtilizationStats() {
        Stmt st;
        const char* sql = R"(
            SELECT u.id, u.full_name,
                   COUNT(DISTINCT s.id)                                                  AS total_slots,
                   COUNT(DISTINCT CASE WHEN s.status='blocked'   THEN s.id END)          AS blocked_slots,
                   COUNT(DISTINCT CASE WHEN s.status='available' THEN s.id END)          AS available_slots,
                   COUNT(DISTINCT CASE WHEN i.status='confirmed' THEN i.id END)          AS confirmed,
                   COUNT(DISTINCT CASE WHEN i.status='completed' THEN i.id END)          AS completed,
                   COUNT(DISTINCT CASE WHEN i.status='declined'  THEN i.id END)          AS declined
            FROM users u
            LEFT JOIN availability_slots s ON u.id=s.interviewer_id
            LEFT JOIN interviews i ON s.id=i.slot_id
            WHERE u.role='interviewer'
            GROUP BY u.id ORDER BY u.full_name
        )";
        json res = json::array();
        if (sqlite3_prepare_v2(db_, sql, -1, st.ptr(), nullptr) != SQLITE_OK) return res;
        while (sqlite3_step(st.get()) == SQLITE_ROW)
            res.push_back({
                {"interviewer_id",sqlite3_column_int(st.get(),0)},
                {"interviewer_name",col(st.get(),1)},
                {"total_slots",sqlite3_column_int(st.get(),2)},
                {"blocked_slots",sqlite3_column_int(st.get(),3)},
                {"available_slots",sqlite3_column_int(st.get(),4)},
                {"confirmed",sqlite3_column_int(st.get(),5)},
                {"completed",sqlite3_column_int(st.get(),6)},
                {"declined",sqlite3_column_int(st.get(),7)}
            });
        return res;
    }

    json getSkillDistribution() {
        Stmt st;
        const char* sql = R"(
            SELECT sk.name, COUNT(DISTINCT i.id) AS cnt
            FROM skills sk
            JOIN interviewer_skills isk ON sk.id=isk.skill_id
            JOIN availability_slots s   ON isk.interviewer_id=s.interviewer_id
            JOIN interviews i           ON s.id=i.slot_id AND i.status IN ('confirmed','completed')
            GROUP BY sk.id ORDER BY cnt DESC
        )";
        json res = json::array();
        if (sqlite3_prepare_v2(db_, sql, -1, st.ptr(), nullptr) != SQLITE_OK) return res;
        while (sqlite3_step(st.get()) == SQLITE_ROW)
            res.push_back({{"skill",col(st.get(),0)},{"count",sqlite3_column_int(st.get(),1)}});
        return res;
    }

    json getUnutilizedSlots() {
        Stmt st;
        const char* sql = R"(
            SELECT u.id, u.full_name,
                   COUNT(s.id) AS cnt,
                   MIN(s.start_time) AS earliest,
                   MAX(s.start_time) AS latest
            FROM users u
            JOIN availability_slots s ON u.id=s.interviewer_id
            WHERE s.status='available' AND s.start_time < datetime('now')
            GROUP BY u.id ORDER BY cnt DESC
        )";
        json res = json::array();
        if (sqlite3_prepare_v2(db_, sql, -1, st.ptr(), nullptr) != SQLITE_OK) return res;
        while (sqlite3_step(st.get()) == SQLITE_ROW)
            res.push_back({
                {"interviewer_id",sqlite3_column_int(st.get(),0)},
                {"interviewer_name",col(st.get(),1)},
                {"count",sqlite3_column_int(st.get(),2)},
                {"earliest",col(st.get(),3)},{"latest",col(st.get(),4)}
            });
        return res;
    }

    // ──────────────────────────────────────────
    //  PASSWORD
    // ──────────────────────────────────────────
    // ──────────────────────────────────────────
    //  INTERVIEW FEEDBACK
    // ──────────────────────────────────────────
    bool submitFeedback(int interviewId, int rating, const std::string& notes) {
        Stmt st;
        const char* sql =
            "INSERT INTO interview_feedback (interview_id,rating,notes) VALUES(?,?,?)"
            " ON CONFLICT(interview_id) DO UPDATE SET rating=excluded.rating,notes=excluded.notes,submitted_at=datetime('now')";
        if (sqlite3_prepare_v2(db_, sql, -1, st.ptr(), nullptr) != SQLITE_OK) return false;
        sqlite3_bind_int (st.get(), 1, interviewId);
        sqlite3_bind_int (st.get(), 2, rating);
        sqlite3_bind_text(st.get(), 3, notes.c_str(), -1, SQLITE_TRANSIENT);
        return sqlite3_step(st.get()) == SQLITE_DONE;
    }

    std::optional<json> getFeedback(int interviewId) {
        Stmt st;
        const char* sql =
            "SELECT rating,notes,submitted_at FROM interview_feedback WHERE interview_id=?";
        if (sqlite3_prepare_v2(db_, sql, -1, st.ptr(), nullptr) != SQLITE_OK) return {};
        sqlite3_bind_int(st.get(), 1, interviewId);
        if (sqlite3_step(st.get()) != SQLITE_ROW) return {};
        return json{{"rating",       sqlite3_column_int(st.get(),0)},
                    {"notes",        col(st.get(),1)},
                    {"submitted_at", col(st.get(),2)}};
    }

    // All feedback visible to recruiter: returns interviews with feedback joined
    std::vector<json> getFeedbackForRecruiter(int recruiterId) {
        Stmt st;
        const char* sql = R"(
            SELECT i.id, i.candidate_name, i.candidate_email, i.status,
                   s.start_time, ui.full_name AS interviewer_name,
                   f.rating, f.notes, f.submitted_at
            FROM interviews i
            JOIN availability_slots s ON i.slot_id=s.id
            JOIN users ui ON s.interviewer_id=ui.id
            JOIN interview_feedback f ON f.interview_id=i.id
            WHERE i.recruiter_id=?
            ORDER BY f.submitted_at DESC
        )";
        std::vector<json> res;
        if (sqlite3_prepare_v2(db_, sql, -1, st.ptr(), nullptr) != SQLITE_OK) return res;
        sqlite3_bind_int(st.get(), 1, recruiterId);
        while (sqlite3_step(st.get()) == SQLITE_ROW)
            res.push_back({
                {"interview_id",    sqlite3_column_int(st.get(),0)},
                {"candidate_name",  col(st.get(),1)},
                {"candidate_email", col(st.get(),2)},
                {"status",          col(st.get(),3)},
                {"start_time",      col(st.get(),4)},
                {"interviewer_name",col(st.get(),5)},
                {"rating",          sqlite3_column_int(st.get(),6)},
                {"notes",           col(st.get(),7)},
                {"submitted_at",    col(st.get(),8)}
            });
        return res;
    }

    // Admin sees all feedback
    std::vector<json> getAllFeedback() {
        Stmt st;
        const char* sql = R"(
            SELECT i.id, i.candidate_name, i.candidate_email, i.status,
                   s.start_time, ui.full_name AS interviewer_name,
                   ur.full_name AS recruiter_name,
                   f.rating, f.notes, f.submitted_at
            FROM interviews i
            JOIN availability_slots s ON i.slot_id=s.id
            JOIN users ui ON s.interviewer_id=ui.id
            JOIN users ur ON i.recruiter_id=ur.id
            JOIN interview_feedback f ON f.interview_id=i.id
            ORDER BY f.submitted_at DESC
        )";
        std::vector<json> res;
        if (sqlite3_prepare_v2(db_, sql, -1, st.ptr(), nullptr) != SQLITE_OK) return res;
        while (sqlite3_step(st.get()) == SQLITE_ROW)
            res.push_back({
                {"interview_id",    sqlite3_column_int(st.get(),0)},
                {"candidate_name",  col(st.get(),1)},
                {"candidate_email", col(st.get(),2)},
                {"status",          col(st.get(),3)},
                {"start_time",      col(st.get(),4)},
                {"interviewer_name",col(st.get(),5)},
                {"recruiter_name",  col(st.get(),6)},
                {"rating",          sqlite3_column_int(st.get(),7)},
                {"notes",           col(st.get(),8)},
                {"submitted_at",    col(st.get(),9)}
            });
        return res;
    }

    // ──────────────────────────────────────────
    //  REMINDER ENGINE
    // ──────────────────────────────────────────
    // Returns interviews that are due for a reminder (24h or 1h before start)
    // and have NOT yet had that reminder sent.
    struct ReminderItem {
        int         interviewId;
        std::string candidateName;
        std::string candidateEmail;
        std::string startTime;
        std::string endTime;
        std::string interviewerName;
        std::string interviewerEmail;
        int         interviewerUserId;
        std::string reminderType; // "24h" or "1h"
    };

    std::vector<ReminderItem> getPendingReminders() {
        const char* sql = R"(
            SELECT i.id, i.candidate_name, i.candidate_email,
                   s.start_time, s.end_time,
                   ui.full_name AS iname, ui.email AS iemail, ui.id AS iuid,
                   CASE
                     WHEN (strftime('%s', s.start_time) - strftime('%s','now')) BETWEEN 3000 AND 90000
                     THEN '24h'
                     WHEN (strftime('%s', s.start_time) - strftime('%s','now')) BETWEEN 0 AND 3900
                     THEN '1h'
                   END AS rtype
            FROM interviews i
            JOIN availability_slots s ON i.slot_id=s.id
            JOIN users ui ON s.interviewer_id=ui.id
            WHERE i.status='confirmed'
              AND rtype IS NOT NULL
              AND NOT EXISTS (
                  SELECT 1 FROM reminder_log rl
                  WHERE rl.interview_id=i.id AND rl.reminder_type=rtype
              )
        )";
        std::vector<ReminderItem> res;
        Stmt st;
        if (sqlite3_prepare_v2(db_, sql, -1, st.ptr(), nullptr) != SQLITE_OK) return res;
        while (sqlite3_step(st.get()) == SQLITE_ROW) {
            ReminderItem r;
            r.interviewId         = sqlite3_column_int(st.get(), 0);
            r.candidateName       = col(st.get(), 1);
            r.candidateEmail      = col(st.get(), 2);
            r.startTime           = col(st.get(), 3);
            r.endTime             = col(st.get(), 4);
            r.interviewerName     = col(st.get(), 5);
            r.interviewerEmail    = col(st.get(), 6);
            r.interviewerUserId   = sqlite3_column_int(st.get(), 7);
            r.reminderType        = col(st.get(), 8);
            res.push_back(r);
        }
        return res;
    }

    void markReminderSent(int interviewId, const std::string& type) {
        Stmt st;
        const char* sql =
            "INSERT OR IGNORE INTO reminder_log (interview_id,reminder_type) VALUES(?,?)";
        if (sqlite3_prepare_v2(db_, sql, -1, st.ptr(), nullptr) != SQLITE_OK) return;
        sqlite3_bind_int (st.get(), 1, interviewId);
        sqlite3_bind_text(st.get(), 2, type.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(st.get());
    }

    bool updatePassword(int userId, const std::string& newHash) {
        Stmt st;
        const char* sql = "UPDATE users SET password_hash=? WHERE id=?";
        if (sqlite3_prepare_v2(db_, sql, -1, st.ptr(), nullptr) != SQLITE_OK) return false;
        sqlite3_bind_text(st.get(), 1, newHash.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int (st.get(), 2, userId);
        return sqlite3_step(st.get()) == SQLITE_DONE;
    }

    bool updateUser(int userId, const std::string& fullName,
                    const std::string& email, const std::string& role) {
        Stmt st;
        const char* sql =
            "UPDATE users SET full_name=?,email=?,role=?,updated_at=datetime('now')"
            " WHERE id=? AND deleted_at IS NULL";
        if (sqlite3_prepare_v2(db_, sql, -1, st.ptr(), nullptr) != SQLITE_OK) return false;
        sqlite3_bind_text(st.get(), 1, fullName.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st.get(), 2, email.c_str(),    -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st.get(), 3, role.c_str(),     -1, SQLITE_TRANSIENT);
        sqlite3_bind_int (st.get(), 4, userId);
        sqlite3_step(st.get());
        return sqlite3_changes(db_) > 0;
    }

    bool deleteUser(int userId) {
        Stmt st;
        const char* sql = "DELETE FROM users WHERE id=?";
        if (sqlite3_prepare_v2(db_, sql, -1, st.ptr(), nullptr) != SQLITE_OK) return false;
        sqlite3_bind_int(st.get(), 1, userId);
        sqlite3_step(st.get());
        return sqlite3_changes(db_) > 0;
    }

    // ──────────────────────────────────────────
    //  SESSION PERSISTENCE
    // ──────────────────────────────────────────
    void persistSession(const std::string& token, int userId,
                        const std::string& username, const std::string& fullName,
                        const std::string& role) {
        Stmt st;
        const char* sql =
            "INSERT OR REPLACE INTO sessions"
            " (token,user_id,username,full_name,role,expires_at)"
            " VALUES (?,?,?,?,?,datetime('now','+24 hours'))";
        if (sqlite3_prepare_v2(db_, sql, -1, st.ptr(), nullptr) != SQLITE_OK) return;
        sqlite3_bind_text(st.get(), 1, token.c_str(),    -1, SQLITE_TRANSIENT);
        sqlite3_bind_int (st.get(), 2, userId);
        sqlite3_bind_text(st.get(), 3, username.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st.get(), 4, fullName.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st.get(), 5, role.c_str(),     -1, SQLITE_TRANSIENT);
        sqlite3_step(st.get());
    }

    // Returns (userId, username, fullName, role) if token is valid and not expired.
    std::optional<std::tuple<int,std::string,std::string,std::string>>
    lookupSession(const std::string& token) {
        Stmt st;
        const char* sql =
            "SELECT user_id,username,full_name,role FROM sessions"
            " WHERE token=? AND expires_at > datetime('now')";
        if (sqlite3_prepare_v2(db_, sql, -1, st.ptr(), nullptr) != SQLITE_OK)
            return std::nullopt;
        sqlite3_bind_text(st.get(), 1, token.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(st.get()) != SQLITE_ROW) return std::nullopt;
        return std::make_tuple(
            sqlite3_column_int(st.get(), 0),
            col(st.get(), 1),
            col(st.get(), 2),
            col(st.get(), 3)
        );
    }

    void deleteSession(const std::string& token) {
        Stmt st;
        const char* sql = "DELETE FROM sessions WHERE token=?";
        if (sqlite3_prepare_v2(db_, sql, -1, st.ptr(), nullptr) != SQLITE_OK) return;
        sqlite3_bind_text(st.get(), 1, token.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(st.get());
    }

    void purgeExpiredSessions() {
        exec("DELETE FROM sessions WHERE expires_at <= datetime('now')");
        exec("DELETE FROM password_reset_tokens WHERE expires_at <= datetime('now')");
    }

    // ──────────────────────────────────────────
    //  USER PROFILE (self-edit, non-admin)
    // ──────────────────────────────────────────
    bool updateUserProfile(int userId, const std::string& fullName,
                           const std::string& email) {
        Stmt st;
        const char* sql =
            "UPDATE users SET full_name=?,email=?,updated_at=datetime('now')"
            " WHERE id=? AND deleted_at IS NULL";
        if (sqlite3_prepare_v2(db_, sql, -1, st.ptr(), nullptr) != SQLITE_OK) return false;
        sqlite3_bind_text(st.get(), 1, fullName.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st.get(), 2, email.c_str(),    -1, SQLITE_TRANSIENT);
        sqlite3_bind_int (st.get(), 3, userId);
        sqlite3_step(st.get());
        return sqlite3_changes(db_) > 0;
    }

    // ──────────────────────────────────────────
    //  SOFT DELETE
    // ──────────────────────────────────────────
    bool softDeleteUser(int userId) {
        Stmt st;
        const char* sql =
            "UPDATE users SET deleted_at=datetime('now')"
            " WHERE id=? AND deleted_at IS NULL";
        if (sqlite3_prepare_v2(db_, sql, -1, st.ptr(), nullptr) != SQLITE_OK) return false;
        sqlite3_bind_int(st.get(), 1, userId);
        sqlite3_step(st.get());
        return sqlite3_changes(db_) > 0;
    }

    // ──────────────────────────────────────────
    //  USER LOOKUP BY EMAIL
    // ──────────────────────────────────────────
    std::optional<User> getUserByEmail(const std::string& email) {
        Stmt st;
        const char* sql =
            "SELECT id,username,password_hash,email,full_name,role FROM users"
            " WHERE email=? AND deleted_at IS NULL";
        if (sqlite3_prepare_v2(db_, sql, -1, st.ptr(), nullptr) != SQLITE_OK) return {};
        sqlite3_bind_text(st.get(), 1, email.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(st.get()) != SQLITE_ROW) return {};
        User u;
        u.id=sqlite3_column_int(st.get(),0); u.username=col(st.get(),1);
        u.passwordHash=col(st.get(),2);      u.email=col(st.get(),3);
        u.fullName=col(st.get(),4);          u.role=col(st.get(),5);
        return u;
    }

    // ──────────────────────────────────────────
    //  PASSWORD RESET TOKENS
    // ──────────────────────────────────────────
    std::string createPasswordResetToken(int userId) {
        // Revoke any existing token for this user first
        {
            Stmt st;
            const char* sql = "DELETE FROM password_reset_tokens WHERE user_id=?";
            if (sqlite3_prepare_v2(db_, sql, -1, st.ptr(), nullptr) == SQLITE_OK) {
                sqlite3_bind_int(st.get(), 1, userId);
                sqlite3_step(st.get());
            }
        }
        // Generate a 64-char hex token (32 random bytes)
        std::random_device rd;
        std::string token;
        token.reserve(64);
        for (int i = 0; i < 8; ++i) {
            char buf[9];
            std::snprintf(buf, sizeof(buf), "%08x", rd());
            token += buf;
        }
        Stmt st;
        const char* sql =
            "INSERT INTO password_reset_tokens (token,user_id,expires_at)"
            " VALUES(?,?,datetime('now','+1 hour'))";
        if (sqlite3_prepare_v2(db_, sql, -1, st.ptr(), nullptr) != SQLITE_OK) return "";
        sqlite3_bind_text(st.get(), 1, token.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int (st.get(), 2, userId);
        if (sqlite3_step(st.get()) != SQLITE_DONE) return "";
        return token;
    }

    // Returns user_id for a valid (non-expired) reset token, or nullopt.
    std::optional<int> validateResetToken(const std::string& token) {
        // Accept only hex strings to prevent injection
        if (token.empty() || token.size() > 128) return {};
        for (char c : token)
            if (!std::isxdigit(static_cast<unsigned char>(c))) return {};
        Stmt st;
        const char* sql =
            "SELECT user_id FROM password_reset_tokens"
            " WHERE token=? AND expires_at > datetime('now')";
        if (sqlite3_prepare_v2(db_, sql, -1, st.ptr(), nullptr) != SQLITE_OK) return {};
        sqlite3_bind_text(st.get(), 1, token.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(st.get()) != SQLITE_ROW) return {};
        return sqlite3_column_int(st.get(), 0);
    }

    void consumeResetToken(const std::string& token) {
        Stmt st;
        const char* sql = "DELETE FROM password_reset_tokens WHERE token=?";
        if (sqlite3_prepare_v2(db_, sql, -1, st.ptr(), nullptr) != SQLITE_OK) return;
        sqlite3_bind_text(st.get(), 1, token.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(st.get());
    }

    // ──────────────────────────────────────────
    //  PAGINATED USER LIST
    // ──────────────────────────────────────────
    struct UserPage {
        std::vector<User> users;
        int               total = 0;
    };

    UserPage getUsersPaged(const std::string& search, int page, int pageSize) {
        const std::string like = "%" + search + "%";
        bool hasSearch = !search.empty();

        std::string where = "WHERE deleted_at IS NULL";
        if (hasSearch)
            where += " AND (username LIKE ? COLLATE NOCASE"
                     "  OR  full_name LIKE ? COLLATE NOCASE"
                     "  OR  email LIKE ? COLLATE NOCASE)";

        UserPage result;
        {
            Stmt st;
            std::string sql = "SELECT COUNT(*) FROM users " + where;
            if (sqlite3_prepare_v2(db_, sql.c_str(), -1, st.ptr(), nullptr) == SQLITE_OK) {
                if (hasSearch) {
                    sqlite3_bind_text(st.get(), 1, like.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(st.get(), 2, like.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(st.get(), 3, like.c_str(), -1, SQLITE_TRANSIENT);
                }
                if (sqlite3_step(st.get()) == SQLITE_ROW)
                    result.total = sqlite3_column_int(st.get(), 0);
            }
        }

        int offset = std::max(0, (page - 1) * pageSize);
        std::string sql =
            "SELECT id,username,password_hash,email,full_name,role FROM users "
            + where + " ORDER BY role,full_name LIMIT ? OFFSET ?";
        Stmt st;
        if (sqlite3_prepare_v2(db_, sql.c_str(), -1, st.ptr(), nullptr) != SQLITE_OK)
            return result;
        int idx = 1;
        if (hasSearch) {
            sqlite3_bind_text(st.get(), idx++, like.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st.get(), idx++, like.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st.get(), idx++, like.c_str(), -1, SQLITE_TRANSIENT);
        }
        sqlite3_bind_int(st.get(), idx++, pageSize);
        sqlite3_bind_int(st.get(), idx,   offset);
        while (sqlite3_step(st.get()) == SQLITE_ROW) {
            User u;
            u.id=sqlite3_column_int(st.get(),0); u.username=col(st.get(),1);
            u.passwordHash=col(st.get(),2);      u.email=col(st.get(),3);
            u.fullName=col(st.get(),4);          u.role=col(st.get(),5);
            result.users.push_back(u);
        }
        return result;
    }

    // ──────────────────────────────────────────
    //  SESSION REFRESH
    // ──────────────────────────────────────────
    bool refreshSession(const std::string& token) {
        Stmt st;
        const char* sql =
            "UPDATE sessions SET expires_at=datetime('now','+24 hours')"
            " WHERE token=? AND expires_at > datetime('now')";
        if (sqlite3_prepare_v2(db_, sql, -1, st.ptr(), nullptr) != SQLITE_OK) return false;
        sqlite3_bind_text(st.get(), 1, token.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(st.get());
        return sqlite3_changes(db_) > 0;
    }
};
