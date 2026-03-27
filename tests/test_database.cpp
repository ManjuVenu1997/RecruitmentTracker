// =============================================================
//  test_database.cpp
//  Unit tests for the Database class: user CRUD, soft-delete,
//  availability slots, interviews, notification, reset tokens,
//  and schema migrations.
//  All tests use an in-memory SQLite database (:memory:).
// =============================================================
#include <gtest/gtest.h>
#include "database.h"
#include "session.h"
#include "sha256.h"

// ─────────────────────────────────────────────────────────────
//  Shared fixture — fresh in-memory DB for each test
// ─────────────────────────────────────────────────────────────
class DbTest : public ::testing::Test {
protected:
    Database db { ":memory:" };
    DbTest() { db.initialize(); }

    // Convenience: insert a user and return their id
    int addUser(const std::string& username,
                const std::string& email,
                const std::string& fullName,
                const std::string& role,
                const std::string& pw = "testpass") {
        db.createUser(username, sha256::hash(pw), email, fullName, role);
        auto u = db.getUserByUsername(username);
        return u ? u->id : -1;
    }
};

// =============================================================
//  Schema / initialization
// =============================================================
TEST_F(DbTest, InitializeDoesNotThrow) {
    // A second call to initialize() on a fresh DB must not throw
    EXPECT_NO_THROW(db.initialize());
}

TEST_F(DbTest, SeedCreatesDefaultAdmin) {
    auto admin = db.getUserByUsername("admin");
    ASSERT_TRUE(admin.has_value());
    EXPECT_EQ(admin->role, "admin");
}

TEST_F(DbTest, SeedCreatesDefaultSkills) {
    auto skills = db.getAllSkills();
    EXPECT_GE(skills.size(), 5u);  // at least C++, Java, Python, JS, SQL
}

// =============================================================
//  User — create & lookup
// =============================================================
TEST_F(DbTest, CreateAndFetchUserByUsername) {
    bool ok = db.createUser("jdoe", sha256::hash("pass"), "jdoe@test.com", "Jane Doe", "recruiter");
    EXPECT_TRUE(ok);

    auto u = db.getUserByUsername("jdoe");
    ASSERT_TRUE(u.has_value());
    EXPECT_EQ(u->username, "jdoe");
    EXPECT_EQ(u->email,    "jdoe@test.com");
    EXPECT_EQ(u->fullName, "Jane Doe");
    EXPECT_EQ(u->role,     "recruiter");
    EXPECT_EQ(u->passwordHash, sha256::hash("pass"));
}

TEST_F(DbTest, FetchUserById) {
    int id = addUser("uid_test", "uid@test.com", "UID User", "interviewer");
    ASSERT_GT(id, 0);

    auto u = db.getUserById(id);
    ASSERT_TRUE(u.has_value());
    EXPECT_EQ(u->id,       id);
    EXPECT_EQ(u->username, "uid_test");
}

TEST_F(DbTest, FetchNonExistentUsernameReturnsNullopt) {
    EXPECT_FALSE(db.getUserByUsername("does_not_exist").has_value());
}

TEST_F(DbTest, FetchNonExistentIdReturnsNullopt) {
    EXPECT_FALSE(db.getUserById(999999).has_value());
}

TEST_F(DbTest, DuplicateUsernameIsRejected) {
    db.createUser("dup", sha256::hash("pw"), "dup@a.com", "Dup A", "recruiter");
    bool ok = db.createUser("dup", sha256::hash("pw"), "dup@b.com", "Dup B", "recruiter");
    EXPECT_FALSE(ok);
}

TEST_F(DbTest, DuplicateEmailIsRejected) {
    db.createUser("user_a", sha256::hash("pw"), "shared@test.com", "A", "recruiter");
    bool ok = db.createUser("user_b", sha256::hash("pw"), "shared@test.com", "B", "recruiter");
    EXPECT_FALSE(ok);
}

// =============================================================
//  User — update
// =============================================================
TEST_F(DbTest, UpdateUserFields) {
    int id = addUser("upd_user", "upd@test.com", "Old Name", "recruiter");
    bool ok = db.updateUser(id, "New Name", "new@test.com", "interviewer");
    EXPECT_TRUE(ok);

    auto u = db.getUserById(id);
    ASSERT_TRUE(u.has_value());
    EXPECT_EQ(u->fullName, "New Name");
    EXPECT_EQ(u->email,    "new@test.com");
    EXPECT_EQ(u->role,     "interviewer");
}

TEST_F(DbTest, UpdateNonExistentUserReturnsFalse) {
    EXPECT_FALSE(db.updateUser(999999, "Nobody", "no@one.com", "admin"));
}

TEST_F(DbTest, UpdatePasswordChangesHash) {
    int id = addUser("pw_test", "pw@test.com", "PW Test", "recruiter", "oldpass");
    db.updatePassword(id, sha256::hash("newpass"));
    auto u = db.getUserById(id);
    ASSERT_TRUE(u.has_value());
    EXPECT_EQ(u->passwordHash, sha256::hash("newpass"));
    EXPECT_NE(u->passwordHash, sha256::hash("oldpass"));
}

// =============================================================
//  User — profile edit (self-update, no role change)
// =============================================================
TEST_F(DbTest, UpdateUserProfile) {
    int id = addUser("prof_user", "prof@test.com", "Old Name", "recruiter");
    EXPECT_TRUE(db.updateUserProfile(id, "Updated Name", "updated@test.com"));

    auto u = db.getUserById(id);
    ASSERT_TRUE(u.has_value());
    EXPECT_EQ(u->fullName, "Updated Name");
    EXPECT_EQ(u->email,    "updated@test.com");
    EXPECT_EQ(u->role,     "recruiter");  // role unchanged
}

// =============================================================
//  User — soft delete
// =============================================================
TEST_F(DbTest, SoftDeleteHidesUser) {
    int id = addUser("del_user", "del@test.com", "Delete Me", "recruiter");
    ASSERT_TRUE(db.getUserById(id).has_value());

    EXPECT_TRUE(db.softDeleteUser(id));
    EXPECT_FALSE(db.getUserById(id).has_value());
    EXPECT_FALSE(db.getUserByUsername("del_user").has_value());
}

TEST_F(DbTest, SoftDeleteUserExcludedFromAllUsersList) {
    int id = addUser("deleted_list_user", "ddl@test.com", "DDL", "recruiter");
    db.softDeleteUser(id);

    auto users = db.getAllUsers();
    for (auto& u : users)
        EXPECT_NE(u.username, "deleted_list_user");
}

TEST_F(DbTest, SoftDeleteNonExistentUserReturnsFalse) {
    EXPECT_FALSE(db.softDeleteUser(999999));
}

TEST_F(DbTest, SoftDeleteTwiceReturnsFalse) {
    int id = addUser("double_del", "ddel@test.com", "DDel", "recruiter");
    EXPECT_TRUE(db.softDeleteUser(id));
    EXPECT_FALSE(db.softDeleteUser(id));  // already deleted
}

// =============================================================
//  User — lookup by email
// =============================================================
TEST_F(DbTest, GetUserByEmail) {
    addUser("email_user", "findme@test.com", "Find Me", "admin");
    auto u = db.getUserByEmail("findme@test.com");
    ASSERT_TRUE(u.has_value());
    EXPECT_EQ(u->username, "email_user");
}

TEST_F(DbTest, GetUserByEmailAfterSoftDeleteReturnsNullopt) {
    int id = addUser("gone_user", "gone@test.com", "Gone", "recruiter");
    db.softDeleteUser(id);
    EXPECT_FALSE(db.getUserByEmail("gone@test.com").has_value());
}

// =============================================================
//  User — paginated list
// =============================================================
TEST_F(DbTest, GetUsersPagedReturnsAll) {
    // Seed already has 6 users; add a few more
    addUser("extra1", "e1@test.com", "Extra 1", "recruiter");
    addUser("extra2", "e2@test.com", "Extra 2", "interviewer");

    auto page = db.getUsersPaged("", 1, 100);
    EXPECT_GE(page.total, 8);
    EXPECT_EQ((int)page.users.size(), page.total);
}

TEST_F(DbTest, GetUsersPagedPaginates) {
    auto page1 = db.getUsersPaged("", 1, 3);
    auto page2 = db.getUsersPaged("", 2, 3);

    EXPECT_EQ((int)page1.users.size(), 3);
    EXPECT_LE((int)page2.users.size(), 3);
    // ensure no same username appears in both pages
    for (auto& u1 : page1.users)
        for (auto& u2 : page2.users)
            EXPECT_NE(u1.username, u2.username);
}

TEST_F(DbTest, GetUsersPagedSearchFilters) {
    addUser("search_target", "target@test.com", "Searchable User", "admin");
    auto page = db.getUsersPaged("search_target", 1, 50);
    EXPECT_EQ(page.total, 1);
    EXPECT_EQ(page.users[0].username, "search_target");
}

TEST_F(DbTest, GetUsersPagedSearchByFullName) {
    addUser("fn_user", "fn@test.com", "UniqueFullName999", "recruiter");
    auto page = db.getUsersPaged("UniqueFullName999", 1, 50);
    EXPECT_EQ(page.total, 1);
}

TEST_F(DbTest, GetUsersPagedExcludesSoftDeleted) {
    int id = addUser("deleted_page", "dp@test.com", "Deleted Page", "recruiter");
    db.softDeleteUser(id);
    auto page = db.getUsersPaged("deleted_page", 1, 50);
    EXPECT_EQ(page.total, 0);
}

// =============================================================
//  Skills
// =============================================================
TEST_F(DbTest, CreateOrGetSkillReturnsId) {
    int id = db.createOrGetSkill("Golang");
    EXPECT_GT(id, 0);
}

TEST_F(DbTest, CreateOrGetSkillIdempotent) {
    int id1 = db.createOrGetSkill("Rust");
    int id2 = db.createOrGetSkill("Rust");
    EXPECT_EQ(id1, id2);
}

TEST_F(DbTest, CreateOrGetSkillCaseInsensitive) {
    int id1 = db.createOrGetSkill("typescript");
    int id2 = db.createOrGetSkill("TypeScript");
    EXPECT_EQ(id1, id2);
}

TEST_F(DbTest, AddAndRemoveInterviewerSkill) {
    int ivr   = addUser("skill_ivr", "si@t.com", "Skill IVR", "interviewer");
    int skill = db.createOrGetSkill("Kotlin");
    EXPECT_TRUE(db.addInterviewerSkill(ivr, skill));

    auto skills = db.getInterviewerSkills(ivr);
    ASSERT_EQ(skills.size(), 1u);
    EXPECT_EQ(skills[0].name, "Kotlin");

    EXPECT_TRUE(db.removeInterviewerSkill(ivr, skill));
    EXPECT_TRUE(db.getInterviewerSkills(ivr).empty());
}

// =============================================================
//  Availability Slots
// =============================================================
TEST_F(DbTest, CreateAndFetchSlot) {
    int ivr = addUser("slot_ivr", "sv@t.com", "Slot IVR", "interviewer");
    int sid = db.createSlot(ivr, "2099-06-01 09:00", "2099-06-01 10:00");
    EXPECT_GT(sid, 0);

    auto slot = db.getSlotById(sid);
    ASSERT_TRUE(slot.has_value());
    EXPECT_EQ((*slot)["interviewer_id"].get<int>(), ivr);
    EXPECT_EQ((*slot)["start_time"].get<std::string>(), "2099-06-01 09:00");
    EXPECT_EQ((*slot)["status"].get<std::string>(), "available");
}

TEST_F(DbTest, UpdateSlotStatus) {
    int ivr = addUser("sts_ivr", "sts@t.com", "STS IVR", "interviewer");
    int sid = db.createSlot(ivr, "2099-07-01 14:00", "2099-07-01 15:00");
    EXPECT_TRUE(db.updateSlotStatus(sid, "blocked"));

    auto slot = db.getSlotById(sid);
    ASSERT_TRUE(slot.has_value());
    EXPECT_EQ((*slot)["status"].get<std::string>(), "blocked");
}

TEST_F(DbTest, DeleteAvailableSlot) {
    int ivr = addUser("del_slot_ivr", "dsi@t.com", "Del Slot IVR", "interviewer");
    int sid = db.createSlot(ivr, "2099-08-01 10:00", "2099-08-01 11:00");
    EXPECT_TRUE(db.deleteSlot(sid, ivr));
    EXPECT_FALSE(db.getSlotById(sid).has_value());
}

TEST_F(DbTest, CannotDeleteBookedSlot) {
    int ivr  = addUser("nodelbook_ivr", "ndb@t.com", "NDB IVR",  "interviewer");
    int rec  = addUser("nodelbook_rec", "nbr@t.com", "NDB Rec",  "recruiter");
    int sid  = db.createSlot(ivr, "2099-09-01 10:00", "2099-09-01 11:00");
    db.updateSlotStatus(sid, "blocked");

    // deleteSlot only works for status='available'
    EXPECT_FALSE(db.deleteSlot(sid, ivr));
}

// =============================================================
//  Interviews
// =============================================================
struct InterviewFixture : public DbTest {
    int ivrId, recId, slotId;
    InterviewFixture() {
        ivrId  = addUser("ivr_fix", "ivrf@t.com", "IVR Fix", "interviewer");
        recId  = addUser("rec_fix", "recf@t.com", "REC Fix", "recruiter");
        slotId = db.createSlot(ivrId, "2099-10-01 09:00", "2099-10-01 10:00");
    }
};

TEST_F(InterviewFixture, CreateInterviewReturnsPositiveId) {
    int id = db.createInterview(slotId, recId, "Candidate A", "canda@test.com");
    EXPECT_GT(id, 0);
}

TEST_F(InterviewFixture, FetchInterviewById) {
    int id = db.createInterview(slotId, recId, "Test Candidate", "tc@test.com");
    auto iv = db.getInterviewById(id);
    ASSERT_TRUE(iv.has_value());
    EXPECT_EQ((*iv)["candidate_name"].get<std::string>(), "Test Candidate");
    EXPECT_EQ((*iv)["status"].get<std::string>(),         "confirmed");
}

TEST_F(InterviewFixture, DeclineInterviewWithReason) {
    int id = db.createInterview(slotId, recId, "Decline Me", "dm@test.com");
    EXPECT_TRUE(db.updateInterviewStatus(id, "declined", "Out of office"));

    auto iv = db.getInterviewById(id);
    ASSERT_TRUE(iv.has_value());
    EXPECT_EQ((*iv)["status"].get<std::string>(),         "declined");
    EXPECT_EQ((*iv)["decline_reason"].get<std::string>(), "Out of office");
}

TEST_F(InterviewFixture, CompleteInterview) {
    int id = db.createInterview(slotId, recId, "Complete Me", "cm@test.com");
    EXPECT_TRUE(db.updateInterviewStatus(id, "completed"));

    auto iv = db.getInterviewById(id);
    ASSERT_TRUE(iv.has_value());
    EXPECT_EQ((*iv)["status"].get<std::string>(), "completed");
}

// =============================================================
//  Notifications
// =============================================================
TEST_F(DbTest, CreateAndFetchNotification) {
    int uid = addUser("notif_user", "nu@t.com", "Notif User", "recruiter");
    int nid = db.createNotification(uid, "Interview booked", "booking", 42);
    EXPECT_GT(nid, 0);

    auto notifs = db.getNotificationsForUser(uid);
    ASSERT_EQ(notifs.size(), 1u);
    EXPECT_EQ(notifs[0]["message"].get<std::string>(), "Interview booked");
    EXPECT_FALSE(notifs[0]["is_read"].get<bool>());
}

TEST_F(DbTest, UnreadCountIncrementsAndDecrements) {
    int uid = addUser("cnt_user", "cu@t.com", "Cnt User", "recruiter");
    EXPECT_EQ(db.getUnreadCount(uid), 0);

    int n1 = db.createNotification(uid, "msg1", "test", 0);
    int n2 = db.createNotification(uid, "msg2", "test", 0);
    EXPECT_EQ(db.getUnreadCount(uid), 2);

    db.markNotificationRead(n1, uid);
    EXPECT_EQ(db.getUnreadCount(uid), 1);

    db.markAllNotificationsRead(uid);
    EXPECT_EQ(db.getUnreadCount(uid), 0);
}

// =============================================================
//  Password Reset Tokens
// =============================================================
TEST_F(DbTest, CreateAndValidateResetToken) {
    int uid   = addUser("reset_usr", "reset@t.com", "Reset User", "recruiter");
    auto tok  = db.createPasswordResetToken(uid);
    EXPECT_FALSE(tok.empty());
    EXPECT_EQ(tok.size(), 64u);

    auto result = db.validateResetToken(tok);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, uid);
}

TEST_F(DbTest, ConsumedTokenIsInvalid) {
    int uid  = addUser("consume_usr", "consume@t.com", "Consume User", "recruiter");
    auto tok = db.createPasswordResetToken(uid);

    db.consumeResetToken(tok);
    EXPECT_FALSE(db.validateResetToken(tok).has_value());
}

TEST_F(DbTest, ValidateNonExistentTokenReturnsNullopt) {
    EXPECT_FALSE(db.validateResetToken("0000000000000000000000000000000000000000000000000000000000000000").has_value());
}

TEST_F(DbTest, ValidateTokenWithNonHexCharsReturnsNullopt) {
    // Security: reject any token containing non-hex characters
    EXPECT_FALSE(db.validateResetToken("../../../../etc/passwd").has_value());
    EXPECT_FALSE(db.validateResetToken("' OR 1=1; --").has_value());
    EXPECT_FALSE(db.validateResetToken("<script>alert(1)</script>").has_value());
}

TEST_F(DbTest, CreatingNewTokenRevokesOldOne) {
    int uid    = addUser("revoke_usr", "revoke@t.com", "Revoke User", "admin");
    auto tok1  = db.createPasswordResetToken(uid);
    auto tok2  = db.createPasswordResetToken(uid);  // new token, old revoked

    EXPECT_FALSE(db.validateResetToken(tok1).has_value());  // old one gone
    EXPECT_TRUE(db.validateResetToken(tok2).has_value());   // new one valid
}

// =============================================================
//  Session refresh
// =============================================================
TEST_F(DbTest, RefreshValidSessionReturnsTrue) {
    SessionManager sm(&db);
    std::string token = sm.createSession(1, "admin", "System Admin", "admin");
    EXPECT_TRUE(db.refreshSession(token));
}

TEST_F(DbTest, RefreshNonExistentSessionReturnsFalse) {
    EXPECT_FALSE(db.refreshSession("nonexistent_token_000"));
}
