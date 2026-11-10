// SPDX-License-Identifier: GPL-3.0-or-later
// Android timer adapter and diagnostic portions of nxrt/core/abi_glue.cpp.
#include "nxrt/abi.h"
#include "nxrt/dispatch.h"
#include "nxrt/log.h"
#include "nxrt/android_time.h"
#include <time.h>
#include <cstdio>
#include <system_error>
#include <cerrno>

namespace nxrt {
uint64_t android_system_tick() {
    timespec now{};
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        throw std::system_error(errno, std::generic_category(), "Cannot read Android monotonic clock");
    return static_cast<uint64_t>(now.tv_sec) * kAndroidSwitch2TickHz
        + static_cast<uint64_t>(now.tv_nsec) * kAndroidSwitch2TickHz / 1000000000;
}
}
extern "C" {
uint64_t guest_mrs(Ctx*, uint32_t sysreg) {
    switch(sysreg) {
        case 0xdf00: return nxrt::kAndroidSwitch2TickHz;
        case 0xdf01:
        case 0xdf02: return nxrt::android_system_tick();
        default: nxrt::trap("System register read not implemented on Android", sysreg);
    }
}
void guest_msr(Ctx*, uint32_t sysreg, uint64_t) {
    nxrt::trap("System register write not implemented on Android", sysreg);
}
void guest_unimpl(uint64_t pc, uint32_t instruction, const char* reason) {
    char text[256];
    std::snprintf(text, sizeof text, "Untranslated instruction %08x: %s", instruction, reason);
    nxrt::trap(text, pc);
}
void stx_diag(Ctx* context, uint64_t address, uint64_t expected, uint64_t seen, uint64_t value, int bits) {
    const uint64_t attempts = ++context->ex_fail;
    if (attempts == 1000 || attempts == 100000)
        NXLOG_W("guest", "STXR failed %llu times: function=%llx address=%llx expected=%llx seen=%llx new=%llx bits=%d",
            static_cast<unsigned long long>(attempts), static_cast<unsigned long long>(context->fn),
            static_cast<unsigned long long>(address), static_cast<unsigned long long>(expected),
            static_cast<unsigned long long>(seen), static_cast<unsigned long long>(value), bits);
}
}
