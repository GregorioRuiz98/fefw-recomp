// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <exception>
#include <string>
namespace nxrt {
// Android memory/process SVCs, guest worker threads, synchronization and IPC.
// Applet bootstrap and clocks are integrated; game storage, graphics and audio remain pending.
void android_configure_fefw();
void svc_memory_reset_for_tests();
void svc_info_reset_for_tests();
struct AndroidThreadExit final : std::exception {
    const char* what() const noexcept override { return "Guest thread exited"; }
};
std::string android_thread_failure();
void android_check_thread_failure();
void android_request_stop();
bool android_stop_requested();
}
