// SPDX-License-Identifier: GPL-3.0-or-later
// Fire Emblem: Fortune's Weave: output and render size (notes/resolution.md). Offsets are in main of update 131072.
//
// The game picks one of six render configs once at boot (0x1818350) and applies it once (0x64a660): DLSS output size, render size (half
// per side) and shadow-map size. The configs are built at 0xaa8f90 as std::shared_ptrs at 0x223e0b0 (16 bytes each); the data is the
// object + 0x18: output w/h (+0, +4), render w/h (+8, +0xc), shadow-map base size (+0x10), then quality factors and flags up to +0x7c.
// The selection calls nn::oe::GetCurrentGraphicsScale after the table is built and before it reads it, so the host's answer to that call
// rewrites the sizes first; the game then makes its targets, DLSS network, UI layers and jitter sequence (8 x ratio^2 phases) itself.
// The window textures are 3840x2160 (the graphics object's seed size, 0x18102a4), the cap for the output; the window crop is
// min(output, display size), so the vi answer must give 2160p for outputs above 1440p (the docked default does).
//
// [fefw] output = console | 720p | 1080p | 1440p | 2160p | 4k | window | WxH     (console: each config keeps its own)
// [fefw] render_ratio = 0 (the config's own, 0.5) | 0.25..1.0 per side            (1.0: DLAA)
// [fefw] render = WxH                                                              (wins over render_ratio)
// [fefw] shadow_size = 0 (the config's) | 512..8192                                (shadow-map base size)
// [fefw] quality = console | docked                                                (docked: config 0's quality fields in every config)
#include "fefw_pc.h"
#include "fefw_pc_policy.h"
// Adapted from Goldoire/fefw-recomp 1d82e428 (GPL-3.0-or-later).

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "nxrt/abi.h"
#include "nxrt/config.h"
#include "nxrt/dispatch.h"
#include "nxrt/log.h"
#include "nxrt/memory.h"
#include "nxrt/module.h"
#include "nxrt/platform.h"

namespace fefw::pc {
using namespace nxrt;

namespace {

constexpr const char* kTag = "fefw";
constexpr const char* kGetScale = "_ZN2nn2oe23GetCurrentGraphicsScaleEv";
constexpr uint64_t kConfigs = 0x223e0b0;   // 6 x std::shared_ptr<config>
constexpr int kConfigCount = 6;
constexpr uint64_t kConfigBytes = 0x80;
constexpr uint64_t kShadow = 0x10;
constexpr int kMaxW = 3840, kMaxH = 2160;

struct Res {
    uint64_t base = 0;
    uint64_t sdk_fn = 0;
    int out_w = 0, out_h = 0;      // 0: the config's own
    int render_w = 0, render_h = 0;
    double ratio = 0;              // 0: the config's own
    int shadow = 0;
    bool docked_quality = false;
    std::atomic<bool> logged{false};
};

Res& rs() {
    static Res r;
    return r;
}

bool parse_wxh(const std::string& v, int* w, int* h) {
    int a = 0, b = 0;
    if (std::sscanf(v.c_str(), "%dx%d", &a, &b) != 2 || a < 64 || b < 64) return false;
    *w = a;
    *h = b;
    return true;
}

// The window's client size at start, fitted to 16:9.
void window_output(int* w, int* h) {
    int pw = 0, ph = 0;
    platform::pixel_size(&pw, &ph);
    if (pw <= 0 || ph <= 0) return;
    int fw = std::min(pw, ph * 16 / 9);
    int fh = fw * 9 / 16;
    *w = fw & ~1;
    *h = fh & ~1;
}

void read_options(Res& r) {
    std::string o = config_get_str("fefw", "output", "console");
    for (char& ch : o) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if (o == "720p") r.out_w = 1280, r.out_h = 720;
    else if (o == "1080p") r.out_w = 1920, r.out_h = 1080;
    else if (o == "1440p") r.out_w = 2560, r.out_h = 1440;
    else if (o == "2160p" || o == "4k") r.out_w = 3840, r.out_h = 2160;
    else if (o == "window") window_output(&r.out_w, &r.out_h);
    else if (o != "console" && !o.empty() && !parse_wxh(o, &r.out_w, &r.out_h)) NXLOG_W(kTag, "[fefw] output = %s is not known: console", o.c_str());
    if (r.out_w > kMaxW || r.out_h > kMaxH) {
        NXLOG_W(kTag, "[fefw] output %dx%d is above the game's 3840x2160 window textures: 3840x2160", r.out_w, r.out_h);
        r.out_w = std::min(r.out_w, kMaxW);
        r.out_h = std::min(r.out_h, kMaxH);
    }
    double ratio = config_get_double("fefw", "render_ratio", 0.0);
    for (const double f : {1.0 / 3.0, 2.0 / 3.0})   // 0.33 / 0.67 mean thirds (1440p -> 1706x960, as the console's config 2)
        if (std::fabs(ratio - f) < 0.006) ratio = f;
    if (ratio > 0) r.ratio = std::clamp(ratio, 0.25, 1.0);
    parse_wxh(config_get_str("fefw", "render", ""), &r.render_w, &r.render_h);
    const int64_t sh = config_get_int("fefw", "shadow_size", 0);
    if (sh > 0) r.shadow = static_cast<int>(std::clamp<int64_t>(sh, 512, 8192));
    r.docked_quality = config_get_str("fefw", "quality", "console") == "docked";
}

bool any_option(const Res& r) { return r.out_w > 0 || r.ratio > 0 || r.render_w > 0 || r.shadow > 0 || r.docked_quality; }

uint64_t config_data(const Res& r, int i) {
    uint64_t p = 0;
    const uint64_t slot = r.base + kConfigs + 16ull * i;
    if (!mem::is_mapped(slot, 8)) return 0;
    std::memcpy(&p, reinterpret_cast<const void*>(slot), 8);
    return p && mem::is_mapped(p, kConfigBytes) ? p : 0;
}

// Rewrites the six configs (idempotent: every call gives the same values).
void apply(Res& r) {
    uint64_t cfg[kConfigCount];
    for (int i = 0; i < kConfigCount; ++i)
        if (!(cfg[i] = config_data(r, i))) return;   // not built yet
    const bool first = !r.logged.exchange(true);
    for (int i = 0; i < kConfigCount; ++i) {
        int32_t v[5];
        std::memcpy(v, reinterpret_cast<const void*>(cfg[i]), sizeof v);
        if (r.docked_quality && i != 0)
            std::memcpy(reinterpret_cast<void*>(cfg[i] + kShadow), reinterpret_cast<const void*>(cfg[0] + kShadow), kConfigBytes - kShadow);
        int32_t ow = r.out_w > 0 ? r.out_w : v[0], oh = r.out_h > 0 ? r.out_h : v[1];
        int32_t rw = v[2], rh = v[3];
        if (r.render_w > 0) rw = r.render_w, rh = r.render_h;
        else if (r.ratio > 0)   // even sizes: the SSAO pass works on 2x2 blocks
            rw = 2 * static_cast<int32_t>(std::lround(ow * r.ratio / 2)), rh = 2 * static_cast<int32_t>(std::lround(oh * r.ratio / 2));
        else if (r.out_w > 0) rw = (ow + 1) / 2, rh = (oh + 1) / 2;

        // The game's render targets live in one 0x3f000000-byte memory pool (made at 0x646f88). Above 1440p output it holds a render of at
        // most half the output per side: 3840x2160 from 2560x1440 runs out and the game's allocator gives the HUD and post targets
        // offset -1 (a black picture). 2560x1440 at full size fits.
        if (static_cast<int64_t>(ow) * oh > 2560 * 1440 && (rw > (ow + 1) / 2 || rh > (oh + 1) / 2)) {
            if (first)
                NXLOG_W(kTag, "render %dx%d for output %dx%d does not fit the game's render-target pool: %dx%d", rw, rh, ow, oh, (ow + 1) / 2,
                        (oh + 1) / 2);
            rw = (ow + 1) / 2;
            rh = (oh + 1) / 2;
        }
        safe_render_size(ow, oh, rw, rh);
        const int32_t sizes[4] = {ow, oh, rw, rh};
        std::memcpy(reinterpret_cast<void*>(cfg[i]), sizes, sizeof sizes);
        if (r.shadow > 0) std::memcpy(reinterpret_cast<void*>(cfg[i] + kShadow), &r.shadow, 4);
        int32_t shadow = 0;
        std::memcpy(&shadow, reinterpret_cast<const void*>(cfg[i] + kShadow), 4);
        if (first)
            NXLOG_I(kTag, "render config %d: output %dx%d from %dx%d, shadow %d (was %dx%d from %dx%d, shadow %d)%s", i, ow, oh, rw, rh, shadow, v[0],
                    v[1], v[2], v[3], v[4], r.docked_quality && i != 0 ? ", config 0's quality" : "");
    }
}

// nn::oe::GetCurrentGraphicsScale(): the SDK's own answer, after the configs are rewritten.
void get_current_graphics_scale(::Ctx* c) {
    Res& r = rs();
    apply(r);
    if (!r.sdk_fn) {
        const int64_t v = config_get_int("am", "graphics_scale", 10);
        c->x[0] = v == 20 ? 1 : v == 30 ? 2 : 0;
        return;
    }
    const uint64_t lr = c->x[30], fn = c->fn;
    dispatch_call(c, r.sdk_fn);
    c->x[30] = lr;
    c->fn = fn;
}

}  // namespace

void res_install(uint64_t base) {
    Res& r = rs();
    if (r.base) return;
    r.base = base;
    read_options(r);
    if (!any_option(r)) return;
    r.sdk_fn = module_find_symbol(kGetScale);
    hle_register(kGetScale, get_current_graphics_scale);
    module_override_symbol(kGetScale);
    NXLOG_I(kTag, "render size: output %s, render %s, shadow %s%s", r.out_w > 0 ? (std::to_string(r.out_w) + "x" + std::to_string(r.out_h)).c_str() : "console",
            r.render_w > 0 ? (std::to_string(r.render_w) + "x" + std::to_string(r.render_h)).c_str()
            : r.ratio > 0  ? ("x" + std::to_string(r.ratio)).c_str()
                           : "console (half)",
            r.shadow > 0 ? std::to_string(r.shadow).c_str() : "console", r.docked_quality ? ", docked quality" : "");
}

}  // namespace fefw::pc
