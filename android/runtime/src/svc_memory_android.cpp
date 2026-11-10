// SPDX-License-Identifier: GPL-3.0-or-later
// Semantics adapted from SuperMarioWonderRecomp runtime/src/kernel.cpp and svc_process.cpp (MIT, Copyright (c) 2026 BelmanteGu); see THIRD_PARTY_NOTICES.md.
// Android bootstrap memory SVC group: heap, MapMemory/UnmapMemory,
// QueryMemory and RELRO permission/attribute calls. IPC mappings remain pending.

#include <bit>

#include <cstddef>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <vector>

#include "nxrt/game.h"
#include "nxrt/abi.h"
#include "nxrt/kernel.h"
#include "nxrt/log.h"
#include "nxrt/memory.h"
#include "nxrt/module.h"
#include "nxrt/svc.h"

namespace nxrt {
namespace {

constexpr const char* kTag = "svc";
constexpr uint64_t kHeapGranule = 0x200000;        // SetHeapSize: size must be a multiple of 2 MiB
constexpr uint64_t kHeapOverhead = 0x4000000;       // same overhead GetInfo(UsedMemorySize) adds to the heap
constexpr uint64_t kMaxHeapSvc = 0x100000000ull;   // SetHeapSize: larger than 4 GiB is refused
constexpr uint64_t kPageMask = mem::kPage - 1;
constexpr uint32_t kPermR = 1;                     // svc permission bits, same as mem::vm_set perm bits (bit0 R, bit1 W)
constexpr uint32_t kPermRW = 3;

inline unsigned long long U(uint64_t v) { return v; }

// Guards the heap size, the MapMemory list and the alias-region windows below.
std::mutex g_mu;
uint64_t g_heap_cur = 0;                           // bytes; meaningful while mem::heap_exists()

// MapMemory mappings: dst (stack region) is a view of src (heap) that stays writable only through dst.
struct MapRec {
    uint64_t dst, src, size;
    uint32_t src_state, src_perm;   // what the source was before the map (heap, or CodeData for a module's static buffer): UnmapMemory puts it back
};
std::vector<MapRec> g_maps;
// Ranges given the PermissionLocked attribute (SetMemoryAttribute, 16.0.0+): start -> size. The SDK's rtld locks each module's
// RELRO after making it read-only; the kernel then refuses SetMemoryPermission there and QueryMemory reports the attribute.
std::map<uint64_t, uint64_t> g_perm_locked;
constexpr uint32_t kAttrUncached = 0x08, kAttrPermissionLocked = 0x10;   // svc::MemoryAttribute bits
bool overlaps_perm_locked(uint64_t addr, uint64_t size) {   // call with g_mu held
    auto it = g_perm_locked.upper_bound(addr);
    if (it != g_perm_locked.begin()) {
        auto p = std::prev(it);
        if (p->first + p->second > addr) return true;
    }
    return it != g_perm_locked.end() && it->first < addr + size;
}

uint64_t heap_now() { return mem::heap_exists() ? g_heap_cur : 0; }  // call with g_mu held

// Every byte of [addr, addr+size) lies inside [base, base+len). size is non-zero (callers reject zero sizes first).
bool within(uint64_t addr, uint64_t size, uint64_t base, uint64_t len) {
    return size <= len && addr >= base && addr - base <= len - size;
}
bool in_stack(uint64_t addr, uint64_t size) { return within(addr, size, mem::kStackBase, mem::kStackSize); }
bool in_heap(uint64_t addr, uint64_t size, uint64_t heap) { return within(addr, size, mem::kHeapBase, heap); }
// Every block of [addr, addr + size) is a module's .data/.bss (CodeData or AliasCodeData): states the kernel lets a process reprotect
// (mesosphere: KMemoryState_FlagCanReprotect; the SDK's rtld makes each module's RELRO read-only with SetMemoryPermission).
bool all_code_data(uint64_t addr, uint64_t size) {
    constexpr uint32_t kMemAliasCodeData = 0x09;   // svc::MemoryState_AliasCodeData (not in mem::)
    if (addr + size < addr) return false;
    for (uint64_t a = addr, end = addr + size; a < end;) {
        const mem::VmInfo b = mem::vm_query(a);
        if (b.state != mem::kMemCodeData && b.state != kMemAliasCodeData) return false;
        a = b.addr + b.size;
    }
    return true;
}

// Logs the refused call with its argument values and sets the result code.
void fail(::Ctx* c, const char* what, uint32_t rc, uint64_t a, uint64_t b, uint64_t d) {
    NXLOG_W(kTag, "%s returned 0x%X (args %llx, %llx, %llx)", what, rc, U(a), U(b), U(d));
    svc_ret(c, rc);
}

// ---- SetHeapSize (0x01) ------------------------------------------------------------------------------------------------------
void set_heap_size(::Ctx* c) {
    const uint64_t size = reg(c, 1);
    if (size % kHeapGranule) {
        fail(c, "SetHeapSize", kernel::kInvalidSize, size, 0, 0);
        return;
    }
    // Switch 1 kernels refuse more than 4 GiB; Switch 2 applications set heaps of several GiB (only nxrt's heap region bounds them)
    const bool s2 = game::info().platform == game::Platform::Switch2;
    if ((!s2 && size > kMaxHeapSvc) || size > mem::kHeapMax) {
        fail(c, "SetHeapSize", kernel::kOutOfMemory, size, 0, 0);
        return;
    }
    {   // the kernel's resource limit: heap + module images + overhead must fit the physical memory it advertises (GetInfo TotalMemorySize)
        uint64_t images = 0;
        for (Module* m : modules()) images += m->size();
        if (size + images + kHeapOverhead > svc_total_memory()) {
            fail(c, "SetHeapSize", kernel::kOutOfMemory, size, images, svc_total_memory());
            return;
        }
    }
    std::lock_guard lk(g_mu);
    if (!mem::heap_exists()) {
        if (!mem::heap_create(0)) {
            fail(c, "SetHeapSize", kernel::kOutOfMemory, size, 0, 0);
            return;
        }
        g_heap_cur = 0;  // a fresh heap (first call, or after mem::shutdown)
    }
    const uint64_t old = g_heap_cur;
    if (size < old) {
        if (mem::heap_view_overlaps(size, old - size))
            return fail(c, "SetHeapSize (live view)", kernel::kInvalidMemoryState, size, old, 0);
        for (const auto& mapping : g_maps)
            if (mapping.src < mem::kHeapBase + old && mem::kHeapBase + size < mapping.src + mapping.size)
                return fail(c, "SetHeapSize (mapped stack)", kernel::kInvalidMemoryState, size, old, 0);
    }
    if (size > old && !mem::heap_clear(old, size - old)) {
        fail(c, "SetHeapSize (clear)", kernel::kOutOfMemory, size, old, 0);
        return;
    }
    if (size > old && !mem::heap_commit(old, size - old)) {
        fail(c, "SetHeapSize (commit)", kernel::kOutOfMemory, size, old, 0);
        return;
    }
    if (size < old && !mem::heap_decommit(size, old - size)) {
        fail(c, "SetHeapSize (decommit)", kernel::kInvalidMemoryState, size, old, 0);
        return;
    }
    // Only the changed tail is touched in the vm map, so permissions set by SetMemoryPermission on the rest survive.
    if (size > old) mem::vm_set(mem::kHeapBase + old, size - old, mem::kMemHeap, kPermRW);
    if (size < old) mem::vm_set(mem::kHeapBase + size, old - size, mem::kMemFree, 0);
    g_heap_cur = size;
    reg(c, 1) = mem::kHeapBase;
    NXLOG_D(kTag, "SetHeapSize(%llx) -> %llx (was %llx)", U(size), U(mem::kHeapBase), U(old));
    svc_ret(c, kernel::kOk);
}

// ---- SetMemoryPermission (0x02) ----------------------------------------------------------------------------------------------
void set_memory_permission(::Ctx* c) {
    const uint64_t addr = reg(c, 0), size = reg(c, 1);
    const uint32_t perm = static_cast<uint32_t>(reg(c, 2));
    if (addr & kPageMask) {
        fail(c, "SetMemoryPermission", kernel::kInvalidAddress, addr, size, perm);
        return;
    }
    if (size == 0 || (size & kPageMask)) {
        fail(c, "SetMemoryPermission", kernel::kInvalidSize, addr, size, perm);
        return;
    }
    if (perm != 0 && perm != kPermR && perm != kPermRW) {
        fail(c, "SetMemoryPermission", kernel::kInvalidPermission, addr, size, perm);
        return;
    }
    std::lock_guard lk(g_mu);
    const bool inside = in_heap(addr, size, heap_now()) || in_stack(addr, size) || all_code_data(addr, size);
    if (!inside || !mem::is_committed(addr, size) || overlaps_perm_locked(addr, size)) {
        fail(c, "SetMemoryPermission", kernel::kInvalidMemoryState, addr, size, perm);
        return;
    }
    const mem::Prot p = perm == 0 ? mem::Prot::None : (perm == kPermR ? mem::Prot::R : mem::Prot::RW);
    if (!mem::protect(addr, size, p)) {
        fail(c, "SetMemoryPermission (protect)", kernel::kInvalidMemoryState, addr, size, perm);
        return;
    }
    mem::vm_set(addr, size, mem::vm_query(addr).state, perm);
    NXLOG_D(kTag, "SetMemoryPermission(%llx, %llx, %u)", U(addr), U(size), perm);
    svc_ret(c, kernel::kOk);
}

// ---- SetMemoryAttribute (0x03) -----------------------------------------------------------------------------------------------
// The mask and value checks are mesosphere's: the bits are Uncached (8) and PermissionLocked (16); PermissionLocked can only be set.
// Uncached is a no-op here. PermissionLocked needs module .data/.bss (CodeData, mesosphere FlagCanPermissionLock) and is recorded.
void set_memory_attribute(::Ctx* c) {
    const uint64_t addr = reg(c, 0), size = reg(c, 1);
    const uint32_t mask = static_cast<uint32_t>(reg(c, 2)), value = static_cast<uint32_t>(reg(c, 3));
    if (addr & kPageMask) {
        fail(c, "SetMemoryAttribute", kernel::kInvalidAddress, addr, size, mask);
        return;
    }
    if (size == 0 || (size & kPageMask)) {
        fail(c, "SetMemoryAttribute", kernel::kInvalidSize, addr, size, mask);
        return;
    }
    constexpr uint32_t kSupported = kAttrUncached | kAttrPermissionLocked;
    if ((mask | value) != mask || (mask | value | kSupported) != kSupported ||
        (mask & kAttrPermissionLocked) != (value & kAttrPermissionLocked)) {
        fail(c, "SetMemoryAttribute", kernel::kInvalidCombination, addr, mask, value);
        return;
    }
    if (addr + size < addr) {
        fail(c, "SetMemoryAttribute", kernel::kInvalidMemoryState, addr, mask, value);
        return;
    }
    if (mask & kAttrPermissionLocked) {
        std::lock_guard lk(g_mu);
        if (!all_code_data(addr, size) || overlaps_perm_locked(addr, size)) {
            fail(c, "SetMemoryAttribute (permission lock)", kernel::kInvalidMemoryState, addr, mask, value);
            return;
        }
        g_perm_locked.emplace(addr, size);
    }
    NXLOG_D(kTag, "SetMemoryAttribute(%llx, %llx, mask %u, value %u)", U(addr), U(size), mask, value);
    svc_ret(c, kernel::kOk);
}

// ---- MapMemory (0x04) / UnmapMemory (0x05) -----------------------------------------------------------------------------------
void map_memory(::Ctx* c) {
    const uint64_t dst = reg(c, 0), src = reg(c, 1), size = reg(c, 2);
    if ((dst | src) & kPageMask) {
        fail(c, "MapMemory", kernel::kInvalidAddress, dst, src, size);
        return;
    }
    if (size == 0 || (size & kPageMask)) {
        fail(c, "MapMemory", kernel::kInvalidSize, dst, src, size);
        return;
    }
    std::lock_guard lk(g_mu);
    // The source is heap memory or the .data/.bss of a loaded module (state CodeData): the SDK maps thread stacks that live in its
    // own static data (MapAliasStack of a static buffer), which the console allows.
    const mem::VmInfo sv = mem::vm_query(src);
    const bool src_code_data = sv.state == mem::kMemCodeData && src >= sv.addr && src + size <= sv.addr + sv.size;
    if (!in_stack(dst, size) || !(in_heap(src, size, heap_now()) || src_code_data) || !mem::is_mapped(src, size)) {
        fail(c, "MapMemory (range)", kernel::kInvalidMemoryState, dst, src, size);
        return;
    }
    // The destination must be free in the vm map, as on hardware: mapping over a live block would break its bookkeeping.
    const mem::VmInfo d = mem::vm_query(dst);
    if (d.state != mem::kMemFree || d.addr + d.size < dst + size) {
        fail(c, "MapMemory (destination in use)", kernel::kInvalidMemoryState, dst, src, size);
        return;
    }
    if (!mem::commit(dst, size, mem::Prot::RW)) {
        fail(c, "MapMemory (commit)", kernel::kOutOfMemory, dst, src, size);
        return;
    }
    std::memcpy(reinterpret_cast<void*>(dst), reinterpret_cast<const void*>(src), size);
    if (!mem::protect(src, size, mem::Prot::None)) {
        mem::decommit(dst, size);
        fail(c, "MapMemory (protect source)", kernel::kInvalidMemoryState, dst, src, size);
        return;
    }
    mem::vm_set(dst, size, mem::kMemStack, kPermRW);
    const mem::VmInfo src_before = mem::vm_query(src);
    mem::vm_set(src, size, src_before.state, 0);  // the source is no-access while mapped
    g_maps.push_back(MapRec{dst, src, size, src_before.state, src_before.perm});
    NXLOG_D(kTag, "MapMemory(dst %llx, src %llx, size %llx)", U(dst), U(src), U(size));
    svc_ret(c, kernel::kOk);
}

void unmap_memory(::Ctx* c) {
    const uint64_t dst = reg(c, 0), src = reg(c, 1), size = reg(c, 2);
    if ((dst | src) & kPageMask) {
        fail(c, "UnmapMemory", kernel::kInvalidAddress, dst, src, size);
        return;
    }
    if (size == 0 || (size & kPageMask)) {
        fail(c, "UnmapMemory", kernel::kInvalidSize, dst, src, size);
        return;
    }
    std::lock_guard lk(g_mu);
    for (size_t i = 0; i < g_maps.size(); i++) {
        MapRec& r = g_maps[i];
        // A whole mapping, or its first or last part (the rest stays mapped).
        const bool prefix = dst == r.dst && src == r.src && size <= r.size;
        const bool suffix = size <= r.size && dst + size == r.dst + r.size && src + size == r.src + r.size;
        if (!prefix && !suffix) continue;
        // The source is no-access while mapped: it must be made writable before the copy back.
        if (!mem::protect(src, size, mem::Prot::RW)) {
            fail(c, "UnmapMemory (protect source)", kernel::kInvalidMemoryState, dst, src, size);
            return;
        }
        std::memcpy(reinterpret_cast<void*>(src), reinterpret_cast<const void*>(dst), size);
        if (!mem::decommit(dst, size)) NXLOG_E(kTag, "UnmapMemory: decommit(%llx, %llx) failed", U(dst), U(size));
        if (r.src_perm != kPermRW) {   // the copy back needed it writable; the source had less (SetMemoryPermission before the map)
            const mem::Prot back = r.src_perm == 0 ? mem::Prot::None : (r.src_perm == kPermR ? mem::Prot::R : mem::Prot::RW);
            if (back != mem::Prot::RW && !mem::protect(src, size, back)) NXLOG_E(kTag, "UnmapMemory: protect(%llx, %llx) failed", U(src), U(size));
        }
        mem::vm_set(src, size, r.src_state, r.src_perm);
        mem::vm_set(dst, size, mem::kMemFree, 0);
        if (prefix) {
            r.dst += size;
            r.src += size;
        }
        r.size -= size;
        if (r.size == 0) g_maps.erase(g_maps.begin() + static_cast<std::ptrdiff_t>(i));
        NXLOG_D(kTag, "UnmapMemory(dst %llx, src %llx, size %llx)", U(dst), U(src), U(size));
        svc_ret(c, kernel::kOk);
        return;
    }
    fail(c, "UnmapMemory (no such mapping)", kernel::kInvalidMemoryState, dst, src, size);
}

// ---- QueryMemory (0x06) ------------------------------------------------------------------------------------------------------
// Writes the 0x28-byte MemoryInfo at x0; x1 (PageInfo) is 0.
struct MemoryInfo {
    uint64_t base_address;
    uint64_t size;
    uint32_t type;
    uint32_t attribute;
    uint32_t permission;
    uint32_t ipc_refcount;
    uint32_t device_refcount;
    uint32_t padding;
};
static_assert(sizeof(MemoryInfo) == 0x28, "MemoryInfo is 0x28 bytes");

void query_memory(::Ctx* c) {
    const uint64_t out = reg(c, 0), addr = reg(c, 2);
    if (!out || !mem::is_writable(out, sizeof(MemoryInfo))) {
        fail(c, "QueryMemory", kernel::kInvalidAddress, out, addr, 0);
        return;
    }
    const mem::VmInfo vi = mem::vm_query(addr);
    MemoryInfo info{};
    info.base_address = vi.addr;
    info.size = vi.size;
    info.type = vi.state;
    info.permission = vi.perm;
    {
        std::lock_guard lk(g_mu);
        // Attributes split a QueryMemory block even when its permissions match.
        for (const auto& [base, size] : g_perm_locked) {
            if (base <= addr && addr - base < size) {
                info.attribute |= kAttrPermissionLocked;
                const uint64_t end = std::min(info.base_address + info.size, base + size);
                info.base_address = std::max(info.base_address, base);
                info.size = end - info.base_address;
                break;
            }
            if (base > addr && base < info.base_address + info.size) info.size = base - info.base_address;
            if (base + size <= addr && base + size > info.base_address) {
                const uint64_t end = info.base_address + info.size;
                info.base_address = base + size; info.size = end - info.base_address;
            }
        }
    }
    std::memcpy(reinterpret_cast<void*>(out), &info, sizeof info);
    reg(c, 1) = 0;
    NXLOG_D(kTag, "QueryMemory(%llx) -> base %llx size %llx type %u perm %u", U(addr), U(vi.addr), U(vi.size), vi.state,
            vi.perm);
    svc_ret(c, kernel::kOk);
}

} // namespace
uint64_t svc_current_heap_size() { std::lock_guard lock(g_mu); return heap_now(); }
void svc_register_memory_group() {
    svc_register(0x01, set_heap_size);
    svc_register(0x02, set_memory_permission);
    svc_register(0x03, set_memory_attribute);
    svc_register(0x04, map_memory);
    svc_register(0x05, unmap_memory);
    svc_register(0x06, query_memory);
}
void svc_memory_reset_for_tests() {
    std::lock_guard lock(g_mu);
    g_maps.clear(); g_perm_locked.clear(); g_heap_cur = 0;
}
} // namespace nxrt
