// SPDX-License-Identifier: GPL-3.0-or-later
// Android bootstrap adaptation of nxrt/kernel/{handles,thread,time}.cpp.
// A guest main thread is the calling host thread, with real guest stack and TLS.
#include "nxrt/android_kernel.h"
#include "nxrt/android_time.h"
#include "nxrt/kernel.h"
#include "nxrt/game.h"
#include "nxrt/memory.h"
#include "nxrt/module.h"
#include "nxrt/svc.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unistd.h>

namespace nxrt {
void android_configure_fefw() {
    static const game::Info info{
        .id="fefw", .title="Fire Emblem: Fortune's Weave", .product="FEFW Recomp",
        .program_id=0x04007EF01F8AE000ull, .version="131072", .platform=game::Platform::Switch2};
    game::set_current(&info);
    svc_init();
}
}
namespace nxrt::kernel {
namespace {
std::mutex table_lock;
std::unordered_map<uint32_t, std::shared_ptr<KObject>> handles;
uint32_t next_handle = 0x10000;
std::atomic<uint64_t> next_id{0x50}, adopted_count{0};
thread_local std::shared_ptr<KThread> self;
}

uint32_t handle_add(std::shared_ptr<KObject> object) {
    if (!object) return 0;
    std::lock_guard lock(table_lock);
    if (handles.size() >= 0x3000) return 0;
    while (handles.contains(next_handle)) {
        if (++next_handle >= kHandleWaitMask) next_handle = 0x10000;
    }
    const uint32_t value = next_handle++;
    if (next_handle >= kHandleWaitMask) next_handle = 0x10000;
    handles.emplace(value, std::move(object));
    return value;
}
std::shared_ptr<KObject> handle_get(uint32_t value) {
    if (value == kPseudoCurrentThread) return self;
    std::lock_guard lock(table_lock);
    auto found = handles.find(value);
    return found == handles.end() ? nullptr : found->second;
}
void handle_close(uint32_t value) {
    std::shared_ptr<KObject> dying;
    {
        std::lock_guard lock(table_lock);
        auto found = handles.find(value);
        if (found == handles.end()) return;
        dying = std::move(found->second); handles.erase(found);
    }
}
size_t handle_count() { std::lock_guard lock(table_lock); return handles.size(); }
void handles_reset() {
    std::unordered_map<uint32_t, std::shared_ptr<KObject>> old;
    { std::lock_guard lock(table_lock); handles.swap(old); next_handle = 0x10000; }
}
KThread::~KThread() = default;
KThread* current_thread() { return self.get(); }
::Ctx* current_ctx() { return self ? &self->ctx : nullptr; }

uint32_t main_thread_adopt(uint64_t stack_size, const char* name) {
    if (self || !stack_size || stack_size > mem::kKernelSize || stack_size % 16) return 0;
    auto thread = std::make_shared<KThread>();
    const uint64_t stack = mem::alloc_kernel(stack_size, mem::kAllocGranularity);
    const TlsInfo& image = module_tls_template();
    if (image.tp_offset > mem::kKernelSize || image.mem_size > mem::kKernelSize - image.tp_offset
        || image.file_size > image.mem_size) return 0;
    const uint64_t tls_bytes = std::max<uint64_t>(0x1000, image.tp_offset + image.mem_size);
    thread->tls = mem::alloc_kernel(0x1000);
    thread->tp_block = mem::alloc_kernel(tls_bytes);
    if (!stack || !thread->tls || !thread->tp_block) return 0;
    if (image.file_size) {
        if (!mem::is_mapped(image.init_addr, image.file_size)) return 0;
        std::memcpy(reinterpret_cast<void*>(thread->tp_block + image.tp_offset),
                    reinterpret_cast<const void*>(image.init_addr), image.file_size);
    }
    thread->self = thread;
    thread->id = next_id.fetch_add(1);
    thread->name = name ? name : "main";
    thread->stack_top = stack + stack_size;
    thread->host_tid = static_cast<uint32_t>(gettid());
    thread->started = true;
    thread->ctx.ex_addr = ~0ull;
    thread->ctx.x[31] = thread->stack_top;
    thread->ctx.tpidr = thread->tp_block;
    thread->ctx.tpidrro = thread->tls;
    thread->handle = handle_add(thread);
    if (!thread->handle) return 0;
    *reinterpret_cast<uint32_t*>(thread->tls + 0x110) = thread->handle;
    *reinterpret_cast<uint64_t*>(thread->tls + 0x1F0) = thread->tp_block;
    mem::vm_set(stack, (stack_size + mem::kPage - 1) & ~(mem::kPage - 1), mem::kMemStack, 3);
    mem::vm_set(thread->tls, mem::kPage, mem::kMemThreadLocal, 3);
    const uint32_t value = thread->handle;
    self = std::move(thread); ++adopted_count;
    return value;
}
void main_thread_release() {
    if (!self) return;
    self->exited = true;
    self.reset(); --adopted_count;
}
uint64_t thread_count_running() { return adopted_count.load(); }
int app_core_count() { return 6; }
uint64_t tick_hz() { return kAndroidSwitch2TickHz; }
uint64_t system_tick() { return android_system_tick(); }
void sleep_ns(int64_t value) {
    if (value <= 0) std::this_thread::yield();
    else std::this_thread::sleep_for(std::chrono::nanoseconds(value));
}
} // namespace nxrt::kernel
