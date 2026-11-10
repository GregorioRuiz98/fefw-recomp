// SPDX-License-Identifier: GPL-3.0-or-later
#include "test_framework.h"
#include "nxrt/dispatch.h"
#include "nxrt/android_trap.h"
#include "nxrt/memory.h"
#include "nxrt/gauge.h"
#include <future>
#include <algorithm>
NXTEST_GROUP(dispatch_android)
namespace {
void first(Ctx* c) { c->x[0] = 17; }
void second(Ctx* c) { c->x[0] = 29; }
struct Reset {
    Reset() { nxrt::fn_clear(); nxrt::hle_clear(); }
    ~Reset() { nxrt::fn_clear(); nxrt::hle_clear(); }
};
}
NXTEST(main_flat_table_and_sdk_hash_fallback) {
    Reset reset;
    Ctx c{};
    nxrt::fn_register(nxrt::mem::kMainBase + 4, first);
    nxrt::fn_register(nxrt::mem::kSdkBase + 4, second);
    call_indirect(&c, nxrt::mem::kMainBase + 4); CHECK_EQ(c.x[0], 17u);
    call_indirect(&c, nxrt::mem::kSdkBase + 4); CHECK_EQ(c.x[0], 29u);
}
NXTEST(replacement_invalidates_each_host_thread_cache) {
    Reset reset;
    const uint64_t target = nxrt::mem::kSdkBase + 4;
    nxrt::fn_register(target, first);
    Ctx c{}; call_indirect(&c, target); CHECK_EQ(c.x[0], 17u);
    nxrt::fn_register(target, second);
    call_indirect(&c, target); CHECK_EQ(c.x[0], 29u);
    auto other = std::async(std::launch::async, [target] { Ctx value{}; call_indirect(&value, target); return value.x[0]; });
    CHECK_EQ(other.get(), 29u);
}
NXTEST(clear_cannot_dispatch_stale_cached_pointer) {
    Reset reset;
    const uint64_t target = nxrt::mem::kMainBase + 4;
    nxrt::fn_register(target, first);
    Ctx c{}; call_indirect(&c, target);
    nxrt::fn_clear();
    bool trapped = false;
    try { call_indirect(&c, target); } catch (const nxrt::AndroidGuestTrap& error) { trapped = error.address() == target; }
    CHECK(trapped);
    nxrt::fn_register(target, second);
    call_indirect(&c, target); CHECK_EQ(c.x[0], 29u);
}
NXTEST(zero_or_unaligned_hle_target_is_never_called) {
    Reset reset;
    const uint64_t thunk = nxrt::hle_register("test.valid", first);
    Ctx c{};
    for (uint64_t address : {uint64_t(0), thunk + 1}) {
        bool trapped = false;
        try { call_indirect(&c, address); } catch (const nxrt::AndroidGuestTrap&) { trapped = true; }
        CHECK(trapped);
    }
}
NXTEST(hash_growth_preserves_all_function_targets) {
    Reset reset;
    constexpr uint64_t base = nxrt::mem::kSdkBase;
    for (uint64_t i = 1; i <= 12000; ++i) nxrt::fn_register(base + i * 4, i % 2 ? first : second);
    CHECK_EQ(nxrt::fn_count(), 12000u);
    Ctx c{};
    for (uint64_t i = 1; i <= 12000; ++i) {
        call_indirect(&c, base + i * 4);
        CHECK_EQ(c.x[0], i % 2 ? 17u : 29u);
    }
}
NXTEST(registered_gauges_report_live_values) {
    nxrt::GaugeCounter counter("android.test");
    counter.add(3); --counter;
    auto values = nxrt::gauge_read_all();
    auto item = std::find_if(values.begin(), values.end(), [](const auto& value) { return value.first == "android.test"; });
    REQUIRE(item != values.end()); CHECK_EQ(item->second, 2);
}
