// SPDX-License-Identifier: GPL-3.0-or-later
// Regression for the >512 live-region failure fixed by Goldoire/fefw-recomp f4e78ae.
#include "nxrt/memory.h"
#include "nxrt/write_watch.h"
#include "test_framework.h"
#include <vector>
#include <atomic>
#include <thread>
NXTEST_GROUP(fefw_write_tracking)
NXTEST(all_live_texture_regions_keep_tracking_cpu_writes) {
    namespace mem=nxrt::mem;namespace ww=nxrt::mem::ww;
    mem::shutdown();REQUIRE(mem::init()&&mem::heap_create(64ull<<20));
    constexpr size_t count=1537,page=4096;
    REQUIRE(mem::heap_commit(0,count*page));
    std::vector<ww::Region*> regions;
    for(size_t i=0;i<count;++i) regions.push_back(ww::region_create(mem::kHeapBase+i*page,page));
    size_t missing=0,missed_writes=0;
    for(size_t i=0;i<count;++i) {
        if(!regions[i]) {++missing;continue;}
        ww::arm(regions[i],0,1);
        *reinterpret_cast<volatile uint32_t*>(mem::kHeapBase+i*page)=static_cast<uint32_t>(i);
        if(ww::page_valid(regions[i],0)) ++missed_writes;
        std::vector<uint64_t> written;ww::take_written(regions[i],written);
        if(written.size()!=1||written[0]!=0) ++missed_writes;
    }
    for(auto* r:regions) ww::region_destroy(r);
    mem::shutdown();
    CHECK_EQ(missing,0u);CHECK_EQ(missed_writes,0u);
}

// Concurrency/slot-reuse tests adapted from Goldoire f4e78ae (GPL-3.0-or-later).
namespace mem=nxrt::mem;namespace ww=nxrt::mem::ww;
constexpr uint64_t kPg=4096,kHeap=64ull<<20;
ww::Region* setup(){mem::shutdown();if(!mem::init()||!mem::heap_create(kHeap)||!mem::heap_commit(0,64*kPg))return nullptr;return ww::region_create(mem::kHeapBase,64*kPg);}
void teardown(ww::Region* r){ww::region_destroy(r);mem::shutdown();}

// More live pools than the old 512-entry cap, including faults and mapping changes
// in later blocks. Repeat with holes so destruction/recreation exercises slot reuse.
NXTEST(region_table_grows_and_reuses_slots) {
    mem::shutdown();
    REQUIRE(mem::init() && mem::heap_create(kHeap));
    constexpr size_t count = 1537;
    REQUIRE(mem::heap_commit(0, count * kPg));
    std::vector<ww::Region*> regions(count);
    for (size_t round = 0; round < 2; ++round) {
        for (size_t i = 0; i < count; ++i) {
            if (regions[i]) continue;
            regions[i] = ww::region_create(mem::kHeapBase + i * kPg, kPg);
            REQUIRE(regions[i] != nullptr);
        }
        for (size_t i = 0; i < count; ++i) {
            CHECK(ww::arm(regions[i], 0, 1));
            *reinterpret_cast<volatile uint32_t*>(mem::kHeapBase + i * kPg) = static_cast<uint32_t>(i);
            CHECK(!ww::page_valid(regions[i], 0));
            std::vector<uint64_t> written;
            ww::take_written(regions[i], written);
            CHECK(written.size() == 1 && written[0] == 0);
            CHECK(ww::arm(regions[i], 0, 1));
        }
        // Mapping changes must invalidate every block, not just the first 512.
        ww::forget(mem::kHeapBase, count * kPg, false);
        for (size_t i = 0; i < count; ++i) CHECK(!ww::page_valid(regions[i], 0));
        for (size_t i = 0; i < count; i += 2) {
            ww::region_destroy(regions[i]);
            regions[i] = nullptr;
        }
    }
    for (auto* r : regions) ww::region_destroy(r);
    mem::shutdown();
}

NXTEST(region_table_growth_while_faulting) {
    ww::Region* r = setup();
    REQUIRE(r != nullptr);
    std::atomic<bool> stop{false};
    std::atomic<uint64_t> writes{0};
    std::thread writer([&] {
        while (!stop.load()) {
            *reinterpret_cast<volatile uint32_t*>(mem::kHeapBase) = 42;
            writes.fetch_add(1);
        }
    });
    // Region creation and destruction race with faults on a separate, stable page.
    std::vector<ww::Region*> regions;
    for (size_t i = 0; i < 2305; ++i) {
        regions.push_back(ww::region_create(mem::kHeapBase + kPg, kPg));
        CHECK(regions.back() != nullptr);
        ww::arm(r, 0, 1);
    }
    for (auto* region : regions) {
        ww::region_destroy(region);
        ww::arm(r, 0, 1);
    }
    stop.store(true);
    writer.join();
    CHECK(writes.load() != 0);
    teardown(r);
}
