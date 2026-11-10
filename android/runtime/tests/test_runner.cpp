// SPDX-License-Identifier: GPL-3.0-or-later
#include "test_runner.h"
#include "test_framework.h"
#include "nxrt/memory.h"
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
std::string run_memory_tests() {
    static std::mutex test_mutex;
    std::lock_guard lock(test_mutex);
    if (sysconf(_SC_PAGESIZE) != static_cast<long>(nxrt::mem::kPage))
        return "{\"status\":\"BLOCKED_HOST_PAGE_SIZE\",\"memory_backend_available\":true,\"game_runtime_available\":false}";
    unsigned passed = 0, failed = 0;
    std::ostringstream errors;
    for (const auto& item : cases()) {
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
        nxrt::mem::shutdown();
    }
    std::ostringstream report;
    report << "{\"status\":\"" << (failed ? "FAIL" : "PASS") << "\",\"memory_backend_available\":true"
           << ",\"cases_passed\":" << passed << ",\"cases_failed\":" << failed
           << ",\"cleanup_verified\":" << (!nxrt::mem::initialised() && !nxrt::mem::heap_exists() ? "true" : "false")
           << ",\"game_runtime_available\":false,\"failures\":[" << errors.str() << "]}";
    return report.str();
}
}  // namespace nxtest

#ifdef NXRT_ANDROID_TEST_CLI
int main() {
    const std::string result = nxtest::run_memory_tests();
    std::puts(result.c_str());
    return result.find("\"status\":\"PASS\"") == std::string::npos ? 1 : 0;
}
#endif
