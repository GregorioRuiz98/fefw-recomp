// SPDX-License-Identifier: GPL-3.0-or-later
// Bootstrap subset of upstream svc_thread.cpp. Missing worker-thread and IPC
// operations remain unregistered and return ResultNotImplemented, never success.
#include "nxrt/svc.h"
#include "nxrt/kernel.h"
#include "nxrt/dispatch.h"
#include "nxrt/memory.h"
#include "nxrt/log.h"
#include <cstdio>
#include <string>

namespace nxrt {
namespace {
void system_tick(::Ctx* c) { c->x[0] = kernel::system_tick(); }
void sleep_thread(::Ctx* c) { kernel::sleep_ns(static_cast<int64_t>(c->x[0])); }
void close_handle(::Ctx* c) {
    const uint32_t value = static_cast<uint32_t>(c->x[0]);
    if (value >= kernel::kPseudoCurrentThread || !kernel::handle_get(value))
        return svc_ret(c, kernel::kInvalidHandle);
    kernel::handle_close(value); svc_ret(c, kernel::kOk);
}
void break_process(::Ctx* c) {
    if (c->x[0] & 0x80000000u) return;
    char description[192];
    uint32_t argument = 0;
    if (c->x[2] == 4 && mem::is_readable(c->x[1], 4))
        argument = *reinterpret_cast<const uint32_t*>(c->x[1]);
    std::snprintf(description, sizeof description, "Guest Break: reason %llx, argument %08x, lr %llx",
        static_cast<unsigned long long>(c->x[0]), argument, static_cast<unsigned long long>(c->x[30]));
    trap(description, c->fn);
}
void debug_string(::Ctx* c) {
    const uint64_t pointer = c->x[0], length = c->x[1];
    if (length > 0x100000 || (length && !mem::is_readable(pointer, length)))
        return svc_ret(c, kernel::kInvalidPointer);
    const std::string message = length ? std::string(reinterpret_cast<const char*>(pointer), length) : "";
    NXLOG_I("guest", "%s", message.c_str()); svc_ret(c, kernel::kOk);
}
void return_from_exception(::Ctx* c) { trap("Guest ReturnFromException is not implemented on Android", c->fn); }
void exit_process(::Ctx* c) { trap("Guest requested ExitProcess", c->fn); }
}
void svc_register_thread_group() {
    svc_register(0x07, exit_process);
    svc_register(0x0B, sleep_thread);
    svc_register(0x16, close_handle);
    svc_register(0x1E, system_tick);
    svc_register(0x26, break_process);
    svc_register(0x27, debug_string);
    svc_register(0x28, return_from_exception);
}
}
