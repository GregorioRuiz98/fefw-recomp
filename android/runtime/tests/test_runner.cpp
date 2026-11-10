// SPDX-License-Identifier: GPL-3.0-or-later
#include "test_runner.h"
#include "test_framework.h"
#include "nxrt/memory.h"
#include "nxrt/module.h"
#include "nxrt/dispatch.h"
#include "nxrt/android_session.h"
#include <cstring>
#include <unistd.h>
#include <mutex>
#include <sstream>
#include <stdexcept>

namespace nxtest {
std::vector<Case>& cases() { static std::vector<Case> value; return value; }
std::vector<Child>& children() { static std::vector<Child> value; return value; }
void fail(const char*, int line, const char* expression, const std::string& detail) {
    throw std::runtime_error(std::to_string(line) + ": " + expression + " " + detail);
}
void skip(const char* why) { throw std::runtime_error(std::string("Unexpected skipped test: ") + why); }
static std::string run_tests(bool core) {
    std::lock_guard lock(nxrt::android_runtime_mutex());
    if (nxrt::mem::initialised() || !nxrt::modules().empty() || nxrt::fn_count() || nxrt::hle_count())
        return "{\"status\":\"BUSY\",\"game_runtime_available\":false}";
    if (sysconf(_SC_PAGESIZE) != static_cast<long>(nxrt::mem::kPage))
        return "{\"status\":\"BLOCKED_HOST_PAGE_SIZE\",\"memory_backend_available\":true,\"game_runtime_available\":false}";
    unsigned passed = 0, failed = 0;
    std::ostringstream errors;
    for (const auto& item : cases()) {
        if ((std::strcmp(item.group, "memory") != 0) != core) continue;
        try { item.fn(); ++passed; }
        catch (const std::exception& error) {
            if (failed++) errors << ',';
            std::string message = std::string(item.name) + ": " + error.what();
            errors << '"';
            for (const char character : message) {
                if (character == '"' || character == '\\') errors << '\\';
                if (character >= 32) errors << character;
            }
            errors << '"';
        }
        nxrt::modules_unload_all();
        nxrt::hle_clear();
        nxrt::mem::shutdown();
    }
    std::ostringstream report;
    report << "{\"status\":\"" << (failed ? "FAIL" : "PASS") << "\",\"memory_backend_available\":true"
           << ",\"module_loader_available\":true,\"abi_version\":3"
           << ",\"suite\":\"" << (core ? "loader_dispatch" : "memory") << "\""
           << ",\"cases_passed\":" << passed << ",\"cases_failed\":" << failed
           << ",\"cleanup_verified\":" << (!nxrt::mem::initialised() && !nxrt::mem::heap_exists() ? "true" : "false")
           << ",\"game_runtime_available\":false,\"failures\":[" << errors.str() << "]}";
    return report.str();
}
std::string run_memory_tests() { return run_tests(false); }
std::string run_core_tests() { return run_tests(true); }
}  // namespace nxtest

#ifdef NXRT_ANDROID_TEST_CLI
int main(int argc, char** argv) {
    const std::string result = argc == 2 && std::strcmp(argv[1], "--core") == 0
        ? nxtest::run_core_tests() : nxtest::run_memory_tests();
    std::puts(result.c_str());
    return result.find("\"status\":\"PASS\"") == std::string::npos ? 1 : 0;
}
#endif
