// SPDX-License-Identifier: GPL-3.0-or-later
// Android implementation of the nxrt::mem contract from Goldoire/fefw-recomp.
// Guest data keeps its lifted addresses; host functions remain compiled code.
#include "nxrt/memory.h"

#include <sys/mman.h>
#include <sys/syscall.h>
#include <linux/falloc.h>
#include <fcntl.h>
#include <unistd.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <fstream>
#include <limits>
#include <map>
#include <mutex>
#include <string>

namespace nxrt::mem {
namespace {
constexpr uint64_t kAddressLimit = 0x800000000000ull;
constexpr uint64_t kVmEnd = 0x8000000000ull;
constexpr uint8_t kPresent = 0x80;
enum class Kind { Fixed, Heap, Alias, Private };

uint64_t rounded(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}
bool span(uint64_t address, uint64_t size, uint64_t& first, uint64_t& end) {
    if (address >= kAddressLimit || size > kAddressLimit - address) return false;
    first = address & ~(kPage - 1);
    end = size ? rounded(address + size, kPage) : first;
    return true;
}
uint8_t permissions(Prot protection) {
    switch (protection) {
        case Prot::R: return PROT_READ;
        case Prot::RW: return PROT_READ | PROT_WRITE;
        case Prot::RX: return PROT_READ | PROT_EXEC;
        case Prot::RWX: return PROT_READ | PROT_WRITE | PROT_EXEC;
        case Prot::None: return 0;
    }
    return 0;
}
int host_protection(uint8_t page) { return (page & kPresent) ? page & 7 : PROT_NONE; }
void* pointer(uint64_t address) { return reinterpret_cast<void*>(static_cast<uintptr_t>(address)); }

// Every mapping and descriptor is owned. Failed exact-address hints are unmapped
// rather than replacing somebody else's VMA with MAP_FIXED.
struct Mapping {
    uint64_t base = 0, size = 0, offset = 0;
    Kind kind = Kind::Fixed;
    std::vector<uint8_t> pages;
    Mapping() = default;
    ~Mapping() { if (base) munmap(pointer(base), size); }
    Mapping(const Mapping&) = delete;
    Mapping& operator=(const Mapping&) = delete;
    Mapping(Mapping&& other) noexcept
        : base(std::exchange(other.base, 0)), size(other.size), offset(other.offset),
          kind(other.kind), pages(std::move(other.pages)) {}
    Mapping& operator=(Mapping&&) = delete;
};
struct Descriptor {
    int value = -1;
    ~Descriptor() { if (value >= 0) close(value); }
    Descriptor() = default;
    Descriptor(const Descriptor&) = delete;
    Descriptor& operator=(const Descriptor&) = delete;
};
struct State {
    std::mutex mutex;
    std::map<uint64_t, Mapping> mappings;
    std::map<uint64_t, VmInfo> vm;
    Descriptor heap_fd;
    std::vector<uint8_t> backing, charged;
    uint64_t default_heap = 12ull << 30, committed = 0, kernel_next = kKernelBase;
    bool ready = false;
};
State state;
std::atomic<uint64_t> epoch{0};

Mapping* containing(uint64_t first, uint64_t end) {
    auto found = state.mappings.upper_bound(first);
    if (found == state.mappings.begin()) return nullptr;
    --found;
    return first >= found->first && end <= found->first + found->second.size ? &found->second : nullptr;
}
Mapping* heap() {
    auto found = state.mappings.find(kHeapBase);
    return found != state.mappings.end() && found->second.kind == Kind::Heap ? &found->second : nullptr;
}
bool map_new(uint64_t address, uint64_t size, Kind kind, int fd = -1, uint64_t offset = 0, uint64_t* chosen = nullptr) {
    Mapping mapping;
    mapping.size = size; mapping.kind = kind; mapping.offset = offset;
    mapping.pages.assign(static_cast<size_t>(size / kPage), PROT_READ | PROT_WRITE);
    const int flags = fd < 0 ? MAP_PRIVATE | MAP_ANONYMOUS : MAP_SHARED;
    void* mapped = mmap(address ? pointer(address) : nullptr, size, PROT_NONE, flags, fd, static_cast<off_t>(offset));
    if (mapped == MAP_FAILED) return false;
    mapping.base = reinterpret_cast<uintptr_t>(mapped);
    if (address && mapping.base != address) return false;
    const uint64_t key = mapping.base;
    const bool inserted = state.mappings.emplace(key, std::move(mapping)).second;
    if (inserted && chosen) *chosen = key;
    return inserted;
}
bool reserve(uint64_t address, uint64_t size) {
    uint64_t first = 0, end = 0;
    if (!address || !size || address % kAllocGranularity || !span(address, size, first, end)) return false;
    return map_new(address, end - first, Kind::Fixed);
}

struct Change {
    Mapping* mapping;
    uint64_t first;
    std::vector<uint8_t> before, after;
};
Change change(Mapping& mapping, uint64_t first, uint64_t count) {
    auto begin = mapping.pages.begin() + static_cast<std::ptrdiff_t>(first);
    std::vector<uint8_t> before(begin, begin + static_cast<std::ptrdiff_t>(count));
    return {&mapping, first, before, before};
}
bool set_host(const Change& item, const std::vector<uint8_t>& pages) {
    for (size_t first = 0; first < pages.size();) {
        const int protection = host_protection(pages[first]);
        size_t end = first + 1;
        while (end < pages.size() && host_protection(pages[end]) == protection) ++end;
        if (mprotect(pointer(item.mapping->base + (item.first + first) * kPage), (end - first) * kPage, protection) != 0) return false;
        first = end;
    }
    return true;
}
bool apply(std::vector<Change>& changes) {
    for (size_t index = 0; index < changes.size(); ++index) {
        if (!set_host(changes[index], changes[index].after)) {
            for (size_t undo = 0; undo <= index; ++undo) set_host(changes[undo], changes[undo].before);
            return false;
        }
    }
    for (const auto& item : changes)
        std::copy(item.after.begin(), item.after.end(), item.mapping->pages.begin() + static_cast<std::ptrdiff_t>(item.first));
    epoch.fetch_add(1);
    return true;
}
bool commit_pages(uint64_t address, uint64_t size, Prot protection) {
    uint64_t first = 0, end = 0;
    if (!span(address, size, first, end)) return false;
    if (first == end) return true;
    Mapping* target = containing(first, end);
    if (!target) return false;
    std::vector<Change> changes;
    const uint64_t offset = target->offset + first - target->base;
    if (target->kind == Kind::Fixed) {
        changes.push_back(change(*target, (first - target->base) / kPage, (end - first) / kPage));
        std::fill(changes.back().after.begin(), changes.back().after.end(), kPresent | permissions(protection));
    } else {
        const uint64_t limit = offset + end - first;
        for (auto& [base, mapping] : state.mappings) {
            (void)base;
            if (mapping.kind == Kind::Fixed) continue;
            const uint64_t begin = std::max(offset, mapping.offset);
            const uint64_t finish = std::min(limit, mapping.offset + mapping.size);
            if (begin >= finish) continue;
            changes.push_back(change(mapping, (begin - mapping.offset) / kPage, (finish - begin) / kPage));
            auto& item = changes.back();
            for (auto& page : item.after) {
                if (&mapping == target) page = kPresent | permissions(protection);
                else if (!(page & kPresent)) page |= kPresent;
            }
        }
    }
    if (!apply(changes)) return false;
    if (target->kind != Kind::Fixed)
        std::fill(state.backing.begin() + static_cast<std::ptrdiff_t>(offset / kPage),
                  state.backing.begin() + static_cast<std::ptrdiff_t>((offset + end - first) / kPage), 1);
    return true;
}
bool heap_range(uint64_t offset, uint64_t size, uint64_t& first, uint64_t& end) {
    auto* primary = heap();
    if (!primary || offset > primary->size || size > primary->size - offset) return false;
    first = offset & ~(kPage - 1);
    end = size ? rounded(offset + size, kPage) : first;
    return true;
}
bool make_view(uint64_t address, uint64_t offset, uint64_t size, Kind kind, Prot protection, uint64_t& result) {
    uint64_t key = 0;
    if (!map_new(address, size, kind, state.heap_fd.value, offset, &key)) return false;
    Mapping* view = &state.mappings.at(key);
    std::vector<Change> changes{change(*view, 0, size / kPage)};
    for (size_t page = 0; page < changes[0].after.size(); ++page)
        changes[0].after[page] = permissions(protection) | (state.backing[offset / kPage + page] ? kPresent : 0);
    if (!apply(changes)) { state.mappings.erase(key); return false; }
    result = key;
    return true;
}
bool host_mapped(uint64_t first, uint64_t end, uint8_t required = 0) {
    std::ifstream maps("/proc/self/maps");
    std::string line;
    while (first < end && std::getline(maps, line)) {
        unsigned long long lo = 0, hi = 0; char protection[5]{};
        if (std::sscanf(line.c_str(), "%llx-%llx %4s", &lo, &hi, protection) != 3 || hi <= first) continue;
        if (lo > first || (protection[0] == '-' && protection[1] == '-' && protection[2] == '-')) return false;
        if ((required & PROT_READ) && protection[0] != 'r') return false;
        if ((required & PROT_WRITE) && protection[1] != 'w') return false;
        first = std::min<uint64_t>(end, hi);
    }
    return first == end;
}
}  // namespace

bool init(const Config& config) {
    std::lock_guard lock(state.mutex);
    if (state.ready) return true;
    // Do not silently grant access to four guest pages at once on a 16 KB host.
    // Sub-page protection/alias emulation is a separate, unimplemented feature.
    if (sysconf(_SC_PAGESIZE) != static_cast<long>(kPage)) return false;
    const uint64_t desired = config.heap_reserve ? config.heap_reserve : 12ull << 30;
    if (desired > kHeapMax) return false;
    if (config.reserve_regions) {
        if (!reserve(kStackBase, kStackSize)) return false;
        if (!reserve(kKernelBase, kKernelSize)) { state.mappings.erase(kStackBase); return false; }
    }
    state.default_heap = rounded(desired, kAllocGranularity);
    state.kernel_next = kKernelBase;
    state.ready = true;
    return true;
}
void shutdown() {
    std::lock_guard lock(state.mutex);
    state.mappings.clear();
    state.vm.clear();
    if (state.heap_fd.value >= 0) close(std::exchange(state.heap_fd.value, -1));
    state.backing.clear(); state.charged.clear();
    state.committed = 0; state.kernel_next = kKernelBase; state.ready = false;
    epoch.fetch_add(1);
}
bool initialised() { std::lock_guard lock(state.mutex); return state.ready; }
bool reserve_fixed(uint64_t address, uint64_t size, const char*) {
    std::lock_guard lock(state.mutex);
    if (sysconf(_SC_PAGESIZE) != static_cast<long>(kPage)) return false;
    return reserve(address, size);
}
bool release_fixed(uint64_t address) {
    std::lock_guard lock(state.mutex);
    auto found = state.mappings.find(address);
    if (found == state.mappings.end() || found->second.kind != Kind::Fixed) return false;
    state.mappings.erase(found);
    return true;
}
bool commit(uint64_t address, uint64_t size, Prot protection) {
    std::lock_guard lock(state.mutex);
    return commit_pages(address, size, protection);
}
bool decommit(uint64_t address, uint64_t size) {
    std::lock_guard lock(state.mutex);
    uint64_t first = 0, end = 0;
    if (!span(address, size, first, end)) return false;
    if (first == end) return true;
    auto* mapping = containing(first, end);
    if (!mapping || mapping->kind != Kind::Fixed) return false;
    // MAP_FIXED is used only inside a reservation owned by this backend, with
    // its mutex held. It drops anonymous pages and keeps the reservation.
    if (mmap(pointer(first), end - first, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) == MAP_FAILED) return false;
    std::fill(mapping->pages.begin() + static_cast<std::ptrdiff_t>((first - mapping->base) / kPage),
              mapping->pages.begin() + static_cast<std::ptrdiff_t>((end - mapping->base) / kPage), PROT_READ | PROT_WRITE);
    return true;
}
bool protect(uint64_t address, uint64_t size, Prot protection) {
    std::lock_guard lock(state.mutex);
    uint64_t first = 0, end = 0;
    if (!span(address, size, first, end)) return false;
    if (first == end) return true;
    std::vector<Change> changes;
    while (first < end) {
        auto* mapping = containing(first, first + 1);
        if (!mapping) return false;
        const uint64_t limit = std::min(end, mapping->base + mapping->size);
        changes.push_back(change(*mapping, (first - mapping->base) / kPage, (limit - first) / kPage));
        for (auto& page : changes.back().after) {
            if (!(page & kPresent)) return false;
            page = kPresent | permissions(protection);
        }
        first = limit;
    }
    return apply(changes);
}
uint64_t alloc_kernel(uint64_t size, uint64_t alignment) {
    if (!initialised() && !init()) return 0;
    std::lock_guard lock(state.mutex);
    if (!alignment) alignment = kPage;
    if (alignment & (alignment - 1) || size > kKernelSize) return 0;
    alignment = std::max(alignment, kPage);
    const uint64_t address = rounded(state.kernel_next, alignment);
    size = rounded(std::max<uint64_t>(1, size), kPage);
    if (address < state.kernel_next || address > kKernelBase + kKernelSize || size > kKernelBase + kKernelSize - address) return 0;
    if (!commit_pages(address, size, Prot::RW)) return 0;
    state.kernel_next = address + size;
    return address;
}
bool is_mapped(uint64_t address, uint64_t size) {
    std::lock_guard lock(state.mutex);
    uint64_t first = 0, end = 0;
    if (!span(address, size, first, end)) return false;
    if (first == end) return true;
    if (auto* mapping = containing(first, end)) {
        for (uint64_t page = (first - mapping->base) / kPage; page < (end - mapping->base) / kPage; ++page)
            if (!(mapping->pages[page] & kPresent) || !(mapping->pages[page] & 7)) return false;
        return true;
    }
    return host_mapped(first, end);
}
namespace {
bool accessible(uint64_t address, uint64_t size, uint8_t required) {
    std::lock_guard lock(state.mutex);
    uint64_t first = 0, end = 0;
    if (!span(address, size, first, end)) return false;
    while (first < end) {
        auto* mapping = containing(first, first + 1);
        if (!mapping) return host_mapped(first, end, required);
        const uint64_t limit = std::min(end, mapping->base + mapping->size);
        for (uint64_t page = (first - mapping->base) / kPage; page < (limit - mapping->base) / kPage; ++page)
            if (!(mapping->pages[page] & kPresent) || (mapping->pages[page] & required) != required) return false;
        first = limit;
    }
    return true;
}
}
bool is_readable(uint64_t address, uint64_t size) { return accessible(address, size, PROT_READ); }
bool is_writable(uint64_t address, uint64_t size) { return accessible(address, size, PROT_READ | PROT_WRITE); }
bool is_committed(uint64_t address, uint64_t size) {
    std::lock_guard lock(state.mutex);
    uint64_t first = 0, end = 0;
    if (!span(address, size, first, end)) return false;
    while (first < end) {
        const auto* mapping = containing(first, first + 1);
        if (!mapping) return false;
        const uint64_t limit = std::min(end, mapping->base + mapping->size);
        for (uint64_t page = (first - mapping->base) / kPage; page < (limit - mapping->base) / kPage; ++page)
            if (!(mapping->pages[page] & kPresent)) return false;
        first = limit;
    }
    return true;
}
bool heap_clear(uint64_t offset, uint64_t size) {
    std::lock_guard lock(state.mutex);
    const auto* primary = heap();
    if (!primary || offset % kPage || size % kPage || offset > primary->size || size > primary->size - offset) return false;
    if (!size) return true;
    for (const auto& [base, mapping] : state.mappings) {
        if (mapping.kind != Kind::Alias && mapping.kind != Kind::Private) continue;
        if (mapping.offset < offset + size && offset < mapping.offset + mapping.size) return false;
    }
    // tmpfs/memfd hole punching returns zero on the next access without touching
    // gigabytes of physical RAM on the phone. Existing aliases are refused.
    return fallocate(state.heap_fd.value, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
                     static_cast<off_t>(offset), static_cast<off_t>(size)) == 0;
}
bool heap_view_overlaps(uint64_t offset, uint64_t size) {
    std::lock_guard lock(state.mutex);
    if (offset > kHeapMax || size > kHeapMax - offset) return true;
    for (const auto& [base, mapping] : state.mappings) {
        if (mapping.kind != Kind::Alias && mapping.kind != Kind::Private) continue;
        if (mapping.offset < offset + size && offset < mapping.offset + mapping.size) return true;
    }
    return false;
}
bool heap_create(uint64_t size) {
    std::lock_guard lock(state.mutex);
    if (heap()) return true;
    if (!state.ready) return false;
    if (!size) size = state.default_heap;
    if (size > kHeapMax) return false;
    size = rounded(size, kAllocGranularity);
    std::vector<uint8_t> backing(size / kPage), charged(size / kPage);
    Descriptor descriptor;
    descriptor.value = static_cast<int>(syscall(SYS_memfd_create, "nxrt-android-heap", 1));
    if (descriptor.value < 0 || ftruncate(descriptor.value, static_cast<off_t>(size)) != 0) return false;
    if (!map_new(kHeapBase, size, Kind::Heap, descriptor.value)) return false;
    state.heap_fd.value = std::exchange(descriptor.value, -1);
    state.backing = std::move(backing); state.charged = std::move(charged); state.committed = 0;
    return true;
}
bool heap_exists() { std::lock_guard lock(state.mutex); return heap() != nullptr; }
bool heap_has_section() { std::lock_guard lock(state.mutex); return state.heap_fd.value >= 0; }
uint64_t heap_reserved() { std::lock_guard lock(state.mutex); const auto* primary = heap(); return primary ? primary->size : 0; }
uint64_t heap_committed() { std::lock_guard lock(state.mutex); return state.committed; }
uint64_t heap_epoch() { return epoch.load(); }
bool heap_commit(uint64_t offset, uint64_t size) {
    std::lock_guard lock(state.mutex);
    uint64_t first = 0, end = 0;
    if (!heap_range(offset, size, first, end) || !commit_pages(kHeapBase + first, end - first, Prot::RW)) return false;
    for (uint64_t page = first / kPage; page < end / kPage; ++page)
        if (!state.charged[page]) { state.charged[page] = 1; state.committed += kPage; }
    return true;
}
bool heap_decommit(uint64_t offset, uint64_t size) {
    std::lock_guard lock(state.mutex);
    uint64_t first = 0, end = 0;
    if (!heap_range(offset, size, first, end)) return false;
    auto* primary = heap();
    std::vector<Change> changes{change(*primary, first / kPage, (end - first) / kPage)};
    for (auto& page : changes[0].after) if (page & kPresent) page = kPresent;
    if (!apply(changes)) return false;
    // Match the original section contract: aliases keep their contents and
    // protections; only the primary view becomes inaccessible.
    for (uint64_t page = first / kPage; page < end / kPage; ++page)
        if (state.charged[page]) { state.charged[page] = 0; state.committed -= kPage; }
    return true;
}
int heap_rw_pages(uint64_t address, uint64_t size) {
    std::lock_guard lock(state.mutex);
    uint64_t first = 0, end = 0;
    if (!span(address, size, first, end)) return 0;
    const auto* primary = heap();
    if (!primary || first < kHeapBase || end > kHeapBase + primary->size || first == end) return -1;
    for (uint64_t page = (first - kHeapBase) / kPage; page < (end - kHeapBase) / kPage; ++page)
        if (primary->pages[page] != (kPresent | PROT_READ | PROT_WRITE)) return 0;
    return 1;
}
uint64_t heap_alias(uint64_t offset, uint64_t size, uint64_t at, Prot protection) {
    std::lock_guard lock(state.mutex);
    uint64_t first = 0, end = 0;
    if (!size || offset % kAllocGranularity || size % kAllocGranularity || at % kAllocGranularity || !heap_range(offset, size, first, end)) return 0;
    uint64_t ignored_first = 0, ignored_end = 0, result = 0;
    if (at) return span(at, size, ignored_first, ignored_end) && make_view(at, offset, size, Kind::Alias, protection, result) ? result : 0;
    uint64_t candidate = kAliasBase;
    for (unsigned attempt = 0; attempt < 64 && candidate <= kAliasBase + kAliasSize - size; ++attempt) {
        for (;;) {
            auto next = state.mappings.upper_bound(candidate);
            if (next != state.mappings.begin()) {
                const auto& previous = *std::prev(next);
                if (previous.first + previous.second.size > candidate) { candidate = rounded(previous.first + previous.second.size, kAllocGranularity); continue; }
            }
            if (next != state.mappings.end() && next->first < candidate + size) { candidate = rounded(next->first + next->second.size, kAllocGranularity); continue; }
            break;
        }
        if (candidate > kAliasBase + kAliasSize - size) return 0;
        if (make_view(candidate, offset, size, Kind::Alias, protection, result)) return result;
        candidate += size;
    }
    return 0;
}
bool heap_alias_unmap(uint64_t address) {
    std::lock_guard lock(state.mutex);
    auto found = state.mappings.find(address);
    if (found == state.mappings.end() || found->second.kind != Kind::Alias) return false;
    state.mappings.erase(found);
    return true;
}
void* heap_private_view(uint64_t offset, uint64_t size) {
    std::lock_guard lock(state.mutex);
    uint64_t first = 0, end = 0, result = 0;
    if (!size || offset % kAllocGranularity || !heap_range(offset, size, first, end)) return nullptr;
    return make_view(0, offset, end - first, Kind::Private, Prot::RW, result) ? pointer(result) : nullptr;
}
bool heap_private_view_unmap(void* view) {
    std::lock_guard lock(state.mutex);
    auto found = state.mappings.find(reinterpret_cast<uintptr_t>(view));
    if (found == state.mappings.end() || found->second.kind != Kind::Private) return false;
    state.mappings.erase(found);
    return true;
}
size_t heap_alias_count() {
    std::lock_guard lock(state.mutex);
    return static_cast<size_t>(std::count_if(state.mappings.begin(), state.mappings.end(), [](const auto& item) { return item.second.kind == Kind::Alias; }));
}
void heap_alias_ranges(std::vector<std::pair<uint64_t, uint64_t>>& out) {
    std::lock_guard lock(state.mutex);
    for (const auto& [base, mapping] : state.mappings) {
        (void)base;
        if (mapping.kind == Kind::Alias) out.emplace_back(mapping.offset, mapping.size);
    }
}
bool heap_offset_of(uint64_t address, uint64_t* offset) {
    if (!offset || address >= kAddressLimit) return false;
    std::lock_guard lock(state.mutex);
    auto* mapping = containing(address, address + 1);
    if (!mapping || (mapping->kind != Kind::Heap && mapping->kind != Kind::Alias) || address - mapping->base >= mapping->size) return false;
    *offset = mapping->offset + address - mapping->base;
    return true;
}

// Horizon VM records preserve the original map/split/gap behavior; ownership of
// actual host VMAs is kept separately above.
void vm_set(uint64_t address, uint64_t size, uint32_t memory_state, uint32_t permission) {
    if (!size || address >= kVmEnd || size > kVmEnd - address) return;
    const uint64_t end = address + size;
    std::lock_guard lock(state.mutex);
    auto it = state.vm.lower_bound(address);
    if (it != state.vm.begin() && std::prev(it)->second.addr + std::prev(it)->second.size > address) --it;
    while (it != state.vm.end() && it->second.addr < end) {
        const auto block = it->second;
        it = state.vm.erase(it);
        if (block.addr < address) state.vm.emplace(block.addr, VmInfo{block.addr, address - block.addr, block.state, block.perm});
        if (block.addr + block.size > end) state.vm.emplace(end, VmInfo{end, block.addr + block.size - end, block.state, block.perm});
    }
    if (memory_state != kMemFree) state.vm.emplace(address, VmInfo{address, size, memory_state, permission});
}
VmInfo vm_query(uint64_t address) {
    std::lock_guard lock(state.mutex);
    if (address >= kVmEnd) return {kVmEnd, std::numeric_limits<uint64_t>::max() - kVmEnd, kMemInaccessible, 0};
    auto it = state.vm.upper_bound(address);
    if (it != state.vm.begin()) {
        const auto& previous = std::prev(it)->second;
        if (address < previous.addr + previous.size) return previous;
        const uint64_t first = previous.addr + previous.size;
        const uint64_t end = it == state.vm.end() ? kVmEnd : it->first;
        return {first, end - first, kMemFree, 0};
    }
    return {0, it == state.vm.end() ? kVmEnd : it->first, kMemFree, 0};
}
}  // namespace nxrt::mem
