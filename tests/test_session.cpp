// =============================================================
//  test_session.cpp
//  Unit tests for LoginRateLimiter and SessionManager.
//  SessionManager tests use an in-memory SQLite DB.
// =============================================================
#include <gtest/gtest.h>
#include "session.h"
#include "database.h"

// ─────────────────────────────────────────────────────────────
//  Helper: spin up a fresh in-memory database
// ─────────────────────────────────────────────────────────────
static Database makeTestDb() {
    Database db(":memory:");
    db.initialize();
    return db;
}

// =============================================================
//  LoginRateLimiter
// =============================================================
class RateLimiterTest : public ::testing::Test {
protected:
    LoginRateLimiter limiter;
};

TEST_F(RateLimiterTest, AllowsFirstAttempt) {
    EXPECT_EQ(limiter.check("alice"), 0);
}

TEST_F(RateLimiterTest, CountsFailures) {
    limiter.recordFailure("alice");
    limiter.recordFailure("alice");
    EXPECT_EQ(limiter.failureCount("alice"), 2);
}

TEST_F(RateLimiterTest, LocksAfterFourFailures) {
    for (int i = 0; i < 3; ++i)
        EXPECT_FALSE(limiter.recordFailure("bob"));   // not locked yet
    bool locked = limiter.recordFailure("bob");        // 4th failure
    EXPECT_TRUE(locked);
    EXPECT_GT(limiter.check("bob"), 0);                // returns remaining lockout secs
}

TEST_F(RateLimiterTest, ReturnsPositiveSecondsWhenLocked) {
    for (int i = 0; i < 4; ++i)
        limiter.recordFailure("carol");
    int secs = limiter.check("carol");
    EXPECT_GT(secs, 0);
    EXPECT_LE(secs, 300);   // max lockout is 300 s
}

TEST_F(RateLimiterTest, ResetClearsLock) {
    for (int i = 0; i < 4; ++i)
        limiter.recordFailure("dave");
    EXPECT_GT(limiter.check("dave"), 0);

    limiter.reset("dave");

    EXPECT_EQ(limiter.check("dave"), 0);
    EXPECT_EQ(limiter.failureCount("dave"), 0);
}

TEST_F(RateLimiterTest, IndependentUsersDoNotInterfere) {
    for (int i = 0; i < 4; ++i)
        limiter.recordFailure("locked_user");

    EXPECT_GT(limiter.check("locked_user"), 0);
    EXPECT_EQ(limiter.check("clean_user"), 0);   // unaffected
}

TEST_F(RateLimiterTest, FailureCountForUnknownUserIsZero) {
    EXPECT_EQ(limiter.failureCount("unknown"), 0);
}

// =============================================================
//  SessionManager::extractToken
// =============================================================
TEST(ExtractToken, ValidBearerHeader) {
    EXPECT_EQ(SessionManager::extractToken("Bearer abc123"), "abc123");
}

TEST(ExtractToken, EmptyHeader) {
    EXPECT_EQ(SessionManager::extractToken(""), "");
}

TEST(ExtractToken, NoBearerPrefix) {
    EXPECT_EQ(SessionManager::extractToken("abc123"), "");
}

TEST(ExtractToken, BearerCaseSensitive) {
    // Must be exactly "Bearer " (capital B)
    EXPECT_EQ(SessionManager::extractToken("bearer abc123"), "");
}

TEST(ExtractToken, BearerWithLongToken) {
    std::string token(64, 'a');
    EXPECT_EQ(SessionManager::extractToken("Bearer " + token), token);
}

// =============================================================
//  SessionManager (create / lookup / remove)
// =============================================================
class SessionManagerTest : public ::testing::Test {
protected:
    Database db   { ":memory:" };
    SessionManagerTest() { db.initialize(); }
    SessionManager sessions { &db };
};

TEST_F(SessionManagerTest, CreateAndLookupSession) {
    std::string token = sessions.createSession(1, "alice", "Alice Smith", "recruiter");
    EXPECT_FALSE(token.empty());

    auto sess = sessions.getSession(token);
    ASSERT_TRUE(sess.has_value());
    EXPECT_EQ(sess->userId,   1);
    EXPECT_EQ(sess->username, "alice");
    EXPECT_EQ(sess->fullName, "Alice Smith");
    EXPECT_EQ(sess->role,     "recruiter");
}

TEST_F(SessionManagerTest, LookupNonExistentTokenReturnsNullopt) {
    auto sess = sessions.getSession("nonexistent_token_xyz");
    EXPECT_FALSE(sess.has_value());
}

TEST_F(SessionManagerTest, LookupEmptyTokenReturnsNullopt) {
    auto sess = sessions.getSession("");
    EXPECT_FALSE(sess.has_value());
}

TEST_F(SessionManagerTest, RemoveSessionInvalidatesLookup) {
    std::string token = sessions.createSession(2, "bob", "Bob Jones", "interviewer");
    ASSERT_TRUE(sessions.getSession(token).has_value());

    sessions.removeSession(token);
    EXPECT_FALSE(sessions.getSession(token).has_value());
}

TEST_F(SessionManagerTest, MultipleSessionsAreIndependent) {
    std::string t1 = sessions.createSession(1, "alice", "Alice",  "recruiter");
    std::string t2 = sessions.createSession(2, "bob",   "Bob",    "interviewer");

    EXPECT_NE(t1, t2);
    EXPECT_EQ(sessions.getSession(t1)->username, "alice");
    EXPECT_EQ(sessions.getSession(t2)->username, "bob");

    sessions.removeSession(t1);
    EXPECT_FALSE(sessions.getSession(t1).has_value());
    EXPECT_TRUE(sessions.getSession(t2).has_value());  // t2 unaffected
}

TEST_F(SessionManagerTest, TokensAreUnique) {
    std::vector<std::string> tokens;
    for (int i = 0; i < 10; ++i)
        tokens.push_back(sessions.createSession(i + 1, "u" + std::to_string(i),
                                                "User", "recruiter"));
    // No duplicates
    std::sort(tokens.begin(), tokens.end());
    EXPECT_EQ(std::adjacent_find(tokens.begin(), tokens.end()), tokens.end());
}
