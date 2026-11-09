// SPDX-License-Identifier: GPL-3.0-or-later
// FPS hooks adapted from Goldoire/fefw-recomp fefw_play.cpp, 1d82e428.
#include "fefw_pc.h"
#include "fefw_pc_policy.h"
#include "fefw_mods.h"
#include "nxrt/config.h"
#include "nxrt/frame_limiter.h"
#include "nxrt/hooks.h"
#include "nxrt/log.h"
#include "nxrt/memory.h"
#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace fefw::pc {
void res_install(uint64_t base);
namespace {
constexpr uint64_t base = 0x7100000000ULL;
template<class T> bool read(uint64_t a, T& v) {
    if (!a || !nxrt::mem::is_mapped(a, sizeof v)) return false;
    std::memcpy(&v, reinterpret_cast<const void*>(a), sizeof v); return true;
}
bool logged_target = false, logged_step = false;
float debt = 0;
void display_rate(uint64_t gfx) {
    int rate = 0, longest = 0;
    if (!gfx || !read(gfx + 0x1d8, rate) || !read(gfx + 0x1e0, longest) || rate <= 0 || rate > 240 || longest < 0 || longest > 120) return;
    if (rate != 120) {
        const int new_rate = 120, new_longest = longest * 120 / rate;
        std::memcpy(reinterpret_cast<void*>(gfx + 0x1d8), &new_rate, 4);
        std::memcpy(reinterpret_cast<void*>(gfx + 0x1e0), &new_longest, 4);
        NXLOG_I("fefw", "display rate %d -> 120, max interval %d -> %d", rate, longest, new_longest);
    }
    nxrt::set_present_interval_hz(120);
}
void target_hook(::Ctx* c, uint64_t, void*) {
    display_rate(c->x[8]); c->x[9] = 120;
    if (!logged_target) { logged_target = true; NXLOG_I("fefw", "gameplay target 120 FPS; menus retain their 60 FPS target"); }
}
void step_hook(::Ctx* c, uint64_t, void*) {
    uint64_t tm = 0, gfx = 0; int target = 0;
    if (!read(base + 0x22a6130, tm) || !tm || c->x[19] != tm || !read(base + 0x21e60d8, gfx)) return;
    display_rate(gfx);
    if (!read(gfx + 0x1dc, target) || target <= 0 || target > 120) return;
    c->v[1].f[0] = smooth_step(c->v[0].f[0], c->v[1].f[0], target, debt);
    if (!logged_step) { logged_step = true; NXLOG_I("fefw", "120 FPS measured-time correction active (target %d)", target); }
}
}
void install(std::string_view build) {
    if (build != mods::kBuild) throw std::runtime_error("PC options require update 131072 / DB93B012.");
    if (nxrt::config_get_bool("mods", "fps120", false)) {
        uint32_t target = 0, step = 0;
        if (!read(base + 0x7bf1c0, target) || target != 0xb901dd09 || !read(base + 0x180ef44, step) || step != 0xbd416a60)
            throw std::runtime_error("120 FPS hook instructions do not match the supported build.");
        nxrt::hook_add_inline(base + 0x7bf1c0, target_hook);
        nxrt::hook_add_inline(base + 0x180ef44, step_hook);
        NXLOG_I("fefw", "120 FPS hooks installed; performance is hardware and scene dependent");
    }
    res_install(base);
}
}
