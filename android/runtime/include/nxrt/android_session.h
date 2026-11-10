// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <mutex>
#include <string>

namespace nxrt {
// Serializes destructive diagnostic operations against future game sessions.
std::recursive_mutex& android_runtime_mutex();
// Borrow six read-only regular-file descriptors: main, rtld, sdk, subsdk0..2.
// Maps, decompresses and links data only. Never executes NSO instructions.
// All mappings are released before returning; the caller retains descriptor ownership.
std::string android_inspect_modules(const std::array<int, 6>& descriptors);
}
