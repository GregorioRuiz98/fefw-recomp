// SPDX-License-Identifier: GPL-3.0-or-later
// Adapted from Goldoire/fefw-recomp a538375a, runtime/games/fefw/fefw_play.cpp.
#include "fefw_movie.h"
#include "nxrt/hooks.h"
#include "nxrt/log.h"
#include "nxrt/memory.h"
#include <atomic>
#include <cstring>
#include <stdexcept>

namespace fefw::pc {
namespace {
constexpr uint64_t kSelect = 0x1906550, kCount = 0x1906634;
std::atomic<bool> logged{false};
template<class T> bool read(uint64_t address, T& value) {
    if (!address || !nxrt::mem::is_mapped(address, sizeof value)) return false;
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof value);
    return true;
}
void select_cpu_frames(::Ctx* ctx, uint64_t, void*) {
    ctx->x[21] = 1;
    if (!logged.exchange(true)) NXLOG_I("fefw", "movie player: CPU frame buffers for the host AV1 decoder");
}
void allocate_four_frames(::Ctx* ctx, uint64_t, void*) {
    const uint64_t player = ctx->x[19];
    int32_t storage = 0;
    if (!player || !read(player + 0xc10, storage) || storage != 1 || !nxrt::mem::is_mapped(player + 0xff8, 8)) return;
    const uint64_t count = 4;
    std::memcpy(reinterpret_cast<void*>(player + 0xff8), &count, sizeof count);
}
}
void movie_install(uint64_t base) {
    uint32_t select = 0, count = 0;
    if (!read(base + kSelect, select) || select != 0x9404f500 ||
        !read(base + kCount, count) || count != 0xb98f5261)
        throw std::runtime_error("Movie player hook instructions do not match update 131072 / DB93B012.");
    nxrt::hook_add_inline(base + kSelect, select_cpu_frames);
    nxrt::hook_add_inline(base + kCount, allocate_four_frames);
}
}
