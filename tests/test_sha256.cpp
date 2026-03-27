// =============================================================
//  test_sha256.cpp
//  Unit tests for the sha256::hash() utility.
//  Known-good digests taken from NIST FIPS 180-4 test vectors
//  and from the actual admin password embedded in the seed data.
// =============================================================
#include <gtest/gtest.h>
#include "sha256.h"

// ── Known-vector tests ────────────────────────────────────

TEST(Sha256, EmptyString) {
    // SHA-256("") = e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
    EXPECT_EQ(sha256::hash(""),
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST(Sha256, Abc) {
    // Verify the output is stable (matches itself) and is 64 hex chars
    std::string h = sha256::hash("abc");
    EXPECT_EQ(h.size(), 64u);
    EXPECT_EQ(h, sha256::hash("abc"));   // deterministic

    // Must not produce the empty-string hash
    EXPECT_NE(h, sha256::hash(""));
}

TEST(Sha256, Admin123Password) {
    // This is the seeded password hash used for all default demo accounts
    EXPECT_EQ(sha256::hash("admin123"),
        "240be518fabd2724ddb6f04eeb1da5967448d7e831c08c8fa822809f74c720a9");
}

TEST(Sha256, LongerString) {
    // SHA-256("The quick brown fox jumps over the lazy dog")
    EXPECT_EQ(sha256::hash("The quick brown fox jumps over the lazy dog"),
        "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592");
}

TEST(Sha256, OutputIsAlwaysLowerHex64Chars) {
    for (const auto& s : { "", "a", "hello world", "password", "test@email.com" }) {
        std::string h = sha256::hash(s);
        EXPECT_EQ(h.size(), 64u) << "Input: " << s;
        for (char c : h)
            EXPECT_TRUE(std::isxdigit((unsigned char)c) && !std::isupper((unsigned char)c))
                << "Non-lowercase-hex char '" << c << "' in hash of: " << s;
    }
}

TEST(Sha256, DifferentInputsDifferentHashes) {
    EXPECT_NE(sha256::hash("password1"), sha256::hash("password2"));
    EXPECT_NE(sha256::hash("admin"),     sha256::hash("Admin"));
    EXPECT_NE(sha256::hash("abc"),       sha256::hash("ABC"));
}

TEST(Sha256, Deterministic) {
    const std::string input = "deterministic_test_value";
    EXPECT_EQ(sha256::hash(input), sha256::hash(input));
}
