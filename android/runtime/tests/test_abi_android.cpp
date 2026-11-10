// SPDX-License-Identifier: GPL-3.0-or-later
#include "test_framework.h"
#include "nxrt/abi.h"
#include "nxrt/dispatch.h"
#include "nxrt/android_trap.h"
#include "../src/eh_internal.h"
#include <unistd.h>
NXTEST_GROUP(abi_android)
NXTEST(switch2_timer_frequency_and_monotonic_counter) {
    Ctx context{};
    CHECK_EQ(guest_mrs(&context,0xdf00),31250000u);
    const uint64_t first=guest_mrs(&context,0xdf01);
    usleep(2000);
    const uint64_t second=guest_mrs(&context,0xdf02);
    CHECK(second > first); CHECK(second-first>=31250);
}
NXTEST(unknown_system_registers_are_explicit_engine_errors) {
    Ctx context{};
    for(bool write:{false,true}) {
        bool trapped=false;
        try { if(write) guest_msr(&context,0xffff,0); else (void)guest_mrs(&context,0xffff); }
        catch(const nxrt::AndroidGuestTrap&) { trapped=true; }
        CHECK(trapped);
    }
}
NXTEST(untranslated_instruction_reports_its_guest_pc) {
    bool trapped=false;
    try { guest_unimpl(0x7100001234,0xffffffff,"synthetic test"); }
    catch(const nxrt::AndroidGuestTrap& error) { trapped=error.address()==0x7100001234; }
    CHECK(trapped);
}
NXTEST(exclusive_store_diagnostic_counts_failures_without_modifying_registers) {
    Ctx context{}; context.x[0]=77;
    stx_diag(&context,1,2,3,4,32);
    CHECK_EQ(context.ex_fail,1u); CHECK_EQ(context.x[0],77u);
}
NXTEST(host_engine_error_removes_guest_exception_activation) {
    Ctx context{};
    const size_t before=nxrt::eh::detail::catch_stack().size();
    bool trapped=false;
    try { nx_catch_call(&context,[](Ctx*,uint64_t) { nxrt::trap("Synthetic engine error inside guest catch frame",0); }); }
    catch(const nxrt::AndroidGuestTrap&) { trapped=true; }
    CHECK(trapped); CHECK_EQ(nxrt::eh::detail::catch_stack().size(),before);
    nx_catch_call(&context,[](Ctx* state,uint64_t) { state->x[0]=91; });
    CHECK_EQ(context.x[0],91u); CHECK_EQ(nxrt::eh::detail::catch_stack().size(),before);
}
