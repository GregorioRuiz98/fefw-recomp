// SPDX-License-Identifier: GPL-3.0-or-later
#include "test_framework.h"
#include "../src/fefw_movie.h"
#include "nxrt/hooks.h"
#include "nxrt/memory.h"
#include <array>
#include <cstring>
#include <stdexcept>

NXTEST_GROUP(movie_player)
namespace {
constexpr uint64_t kBase = 0x7100000000ull;
struct Fixture {
    Fixture() {
        nxrt::hooks_clear();
        mapped = nxrt::mem::reserve_fixed(kBase + 0x1900000, 0x10000, "movie-test");
        if (mapped) mapped = nxrt::mem::commit(kBase + 0x1900000, 0x10000);
        if (mapped) {
            put(kBase + 0x1906550, uint32_t{0x9404f500});
            put(kBase + 0x1906634, uint32_t{0xb98f5261});
        }
    }
    ~Fixture() { nxrt::hooks_clear(); nxrt::mem::release_fixed(kBase + 0x1900000); }
    template<class T> static void put(uint64_t address, T value) {
        std::memcpy(reinterpret_cast<void*>(address), &value, sizeof value);
    }
    bool mapped = false;
};
}

NXTEST(host_decoder_selects_cpu_frames_and_allocates_four_before_registration) {
    Fixture fixture;
    REQUIRE(fixture.mapped);
    fefw::pc::movie_install(kBase);
    ::Ctx ctx{};
    ctx.x[21] = 0; // Retail default: native GPU images, no CPU buffer.
    nx_site_inline(&ctx, kBase + 0x1906550);
    CHECK_EQ(ctx.x[21], uint64_t{1});
    std::array<std::byte, 0x1400> player{};
    ctx.x[19] = reinterpret_cast<uint64_t>(player.data());
    Fixture::put(ctx.x[19] + 0xc10, int32_t{1});
    Fixture::put(ctx.x[19] + 0xff8, uint64_t{2}); // Retail allocation precedes registration of four.
    nx_site_inline(&ctx, kBase + 0x1906634);
    uint64_t frames = 0;
    std::memcpy(&frames, player.data() + 0xff8, sizeof frames);
    CHECK_EQ(frames, uint64_t{4});
    // Other player storage modes must not be modified.
    Fixture::put(ctx.x[19] + 0xc10, int32_t{3});
    Fixture::put(ctx.x[19] + 0xff8, uint64_t{2});
    nx_site_inline(&ctx, kBase + 0x1906634);
    std::memcpy(&frames, player.data() + 0xff8, sizeof frames);
    CHECK_EQ(frames, uint64_t{2});
    ctx.x[19] = 0;
    nx_site_inline(&ctx, kBase + 0x1906634); // No write through an invalid player.
}

NXTEST(unexpected_instruction_refuses_both_hooks) {
    Fixture fixture;
    REQUIRE(fixture.mapped);
    Fixture::put(kBase + 0x1906634, uint32_t{0});
    bool refused = false;
    try { fefw::pc::movie_install(kBase); }
    catch (const std::runtime_error&) { refused = true; }
    CHECK(refused);
    CHECK_EQ(nxrt::hook_count(), size_t{0});
}
