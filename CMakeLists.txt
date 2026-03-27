cmake_minimum_required(VERSION 3.14)
project(RecruitmentTracker VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Option to enable/disable tests ───────────────────────
option(BUILD_TESTS "Build unit tests" ON)

# SQLite3 as a static library (amalgamation)
add_library(sqlite3 STATIC third_party/sqlite3.c)
target_include_directories(sqlite3 PUBLIC third_party)

# Disable SQLite3 warnings
if(MSVC)
    target_compile_options(sqlite3 PRIVATE /W0)
else()
    target_compile_options(sqlite3 PRIVATE -w)
endif()

# Main executable — entry point + one .cpp per route group
add_executable(recruitment_tracker
    src/main.cpp
    src/app_context.cpp
    src/routes/auth_routes.cpp
    src/routes/user_routes.cpp
    src/routes/skill_routes.cpp
    src/routes/slot_routes.cpp
    src/routes/interview_routes.cpp
    src/routes/notification_routes.cpp
    src/routes/admin_routes.cpp
    src/routes/smtp_routes.cpp
)

target_include_directories(recruitment_tracker PRIVATE src third_party src/routes)
target_link_libraries(recruitment_tracker PRIVATE sqlite3)
# Disable SSL in cpp-httplib (plain HTTP server only)
target_compile_definitions(recruitment_tracker PRIVATE CPPHTTPLIB_NO_EXCEPTIONS)

# Platform-specific socket libraries
if(WIN32)
    target_link_libraries(recruitment_tracker PRIVATE ws2_32)
elseif(UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(recruitment_tracker PRIVATE Threads::Threads)
endif()

# Copy frontend to build output directory
add_custom_command(TARGET recruitment_tracker POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/frontend
        $<TARGET_FILE_DIR:recruitment_tracker>/frontend
    COMMENT "Copying frontend files..."
)

# ─────────────────────────────────────────────────────────
#  Unit Tests (Google Test via FetchContent)
# ─────────────────────────────────────────────────────────
if(BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    # For Windows: prevent overriding the parent project's compiler/linker settings
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    enable_testing()

    # Shared "business logic" library — all route/db/util code without main()
    add_library(rt_core STATIC
        src/app_context.cpp
        src/routes/auth_routes.cpp
        src/routes/user_routes.cpp
        src/routes/skill_routes.cpp
        src/routes/slot_routes.cpp
        src/routes/interview_routes.cpp
        src/routes/notification_routes.cpp
        src/routes/admin_routes.cpp
        src/routes/smtp_routes.cpp
    )
    target_include_directories(rt_core PUBLIC src third_party src/routes)
    target_link_libraries(rt_core PUBLIC sqlite3)
    target_compile_definitions(rt_core PUBLIC CPPHTTPLIB_NO_EXCEPTIONS)
    if(WIN32)
        target_link_libraries(rt_core PUBLIC ws2_32)
    elseif(UNIX)
        find_package(Threads REQUIRED)
        target_link_libraries(rt_core PUBLIC Threads::Threads)
    endif()

    # Test executable
    add_executable(rt_tests
        tests/test_sha256.cpp
        tests/test_session.cpp
        tests/test_database.cpp
    )
    target_include_directories(rt_tests PRIVATE src third_party src/routes tests)
    target_link_libraries(rt_tests PRIVATE rt_core GTest::gtest_main)
    target_compile_definitions(rt_tests PRIVATE CPPHTTPLIB_NO_EXCEPTIONS)

    include(GoogleTest)
    gtest_discover_tests(rt_tests)
endif()
