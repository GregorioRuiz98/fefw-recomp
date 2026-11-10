// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
namespace nxrt {
inline constexpr uint64_t kAndroidSwitch2TickHz = 31250000;
// Shared timer epoch for MRS and future SVC/NVN time consumers.
uint64_t android_system_tick();
}
