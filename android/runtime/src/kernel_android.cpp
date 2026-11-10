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
#include "nxrt/dispatch.h"
#include "nxrt/log.h"
#include "kernel_android_internal.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unistd.h>
#include <pthread.h>
#include <bit>
#include <stdexcept>
#include <condition_variable>

namespace nxrt {
namespace services { void register_android_log_service(); }
namespace {
std::mutex failure_lock;
std::string worker_failure;
}
std::string android_thread_failure() { std::lock_guard lock(failure_lock); return worker_failure; }
void android_check_thread_failure() {
    const auto failure=android_thread_failure();
    if(!failure.empty()) trap(failure.c_str(),0);
}
void android_configure_fefw() {
    static const game::Info info{
        .id="fefw", .title="Fire Emblem: Fortune's Weave", .product="FEFW Recomp",
        .program_id=0x04007EF01F8AE000ull, .version="131072", .platform=game::Platform::Switch2};
    game::set_current(&info);
    svc_init();
    services::register_android_log_service();
}
}
namespace nxrt::kernel {
namespace {
std::mutex table_lock;
std::unordered_map<uint32_t, std::shared_ptr<KObject>> handles;
uint32_t next_handle = 0x10000;
std::atomic<uint64_t> next_id{0x50}, adopted_count{0};
thread_local std::shared_ptr<KThread> self;
std::mutex list_lock;
std::vector<std::shared_ptr<KThread>> all_threads;
struct HostThread {
    pthread_t thread{};
    std::atomic<bool> created{false};
    bool joined=false;
    uint64_t stack_bytes=64ull<<20;
    std::mutex join_lock;
    std::mutex exit_lock;
    std::condition_variable exit_changed;
    bool finished=false;
    ~HostThread() {
        if(!created || joined) return;
        if(pthread_equal(pthread_self(),thread)) pthread_detach(thread);
        else pthread_join(thread,nullptr);
    }
};
void finish_current() {
    if(!self) return;
    detail::thread_mark_exited(*self); --adopted_count;
    auto* state=static_cast<HostThread*>(self->host_handle);
    { std::lock_guard lock(state->exit_lock); state->finished=true; }
    state->exit_changed.notify_all();
    self.reset();
}
void* run_thread(void* opaque) {
    auto* box=static_cast<std::shared_ptr<KThread>*>(opaque);
    self=std::move(*box); delete box;
    self->host_tid=static_cast<uint32_t>(gettid());
    log_set_thread_name(self->name.c_str());
    try { dispatch_call(&self->ctx,self->entry); }
    catch(const AndroidThreadExit&) {}
    catch(const std::exception& error) {
        { std::lock_guard lock(failure_lock); if(worker_failure.empty()) worker_failure=self->name+": "+error.what(); }
        detail::notify_failure();
    }
    catch(...) {
        { std::lock_guard lock(failure_lock); if(worker_failure.empty()) worker_failure="Unknown guest worker failure"; }
        detail::notify_failure();
    }
    finish_current(); return nullptr;
}
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
KThread::~KThread() {
    delete static_cast<HostThread*>(host_handle);
    detail::destroy_waiter(*this);
}
KThread* current_thread() { return self.get(); }
::Ctx* current_ctx() { return self ? &self->ctx : nullptr; }

uint32_t thread_create(const ThreadParams& parameters) {
    if(parameters.prio<0 || parameters.prio>63 || parameters.core < -2 || parameters.core>7) return 0;
    auto thread = std::make_shared<KThread>();
    const TlsInfo& image = module_tls_template();
    if (image.tp_offset > mem::kKernelSize || image.mem_size > mem::kKernelSize - image.tp_offset
        || image.file_size > image.mem_size) return 0;
    const uint64_t tls_bytes = std::max<uint64_t>(0x1000, image.tp_offset + image.mem_size);
    thread->tls = mem::alloc_kernel(0x1000);
    thread->tp_block = mem::alloc_kernel(tls_bytes);
    if (!thread->tls || !thread->tp_block) return 0;
    if (image.file_size) {
        if (!mem::is_mapped(image.init_addr, image.file_size)) return 0;
        std::memcpy(reinterpret_cast<void*>(thread->tp_block + image.tp_offset),
                    reinterpret_cast<const void*>(image.init_addr), image.file_size);
    }
    thread->self = thread;
    thread->id = next_id.fetch_add(1);
    thread->name = parameters.name.empty()?"guest-"+std::to_string(thread->id):parameters.name;
    thread->stack_top = parameters.stack_top;
    thread->entry=parameters.entry; thread->arg=parameters.arg; thread->prio=parameters.prio;
    thread->ideal_core=parameters.core==-2?0:std::min(parameters.core,app_core_count()-1);
    thread->core_mask=1ull<<(thread->ideal_core<0?0:thread->ideal_core);
    thread->host_handle=new HostThread;
    if(parameters.host_stack_bytes) static_cast<HostThread*>(thread->host_handle)->stack_bytes=parameters.host_stack_bytes;
    thread->ctx.ex_addr = ~0ull;
    thread->ctx.x[31] = thread->stack_top;
    thread->ctx.x[0] = thread->arg;
    thread->ctx.tpidr = thread->tp_block;
    thread->ctx.tpidrro = thread->tls;
    thread->handle = handle_add(thread);
    if (!thread->handle) return 0;
    *reinterpret_cast<uint32_t*>(thread->tls + 0x110) = thread->handle;
    *reinterpret_cast<uint64_t*>(thread->tls + 0x1F0) = thread->tp_block;
    mem::vm_set(thread->tls, mem::kPage, mem::kMemThreadLocal, 3);
    const uint32_t value = thread->handle;
    {
        std::lock_guard lock(list_lock);
        std::erase_if(all_threads,[](const auto& previous) { return previous->exited && previous.use_count()==1; });
        all_threads.push_back(std::move(thread));
    }
    return value;
}
uint32_t thread_start(uint32_t handle) {
    auto thread=handle_as<KThread>(handle); if(!thread) return kInvalidHandle;
    if(!thread->entry || !fn_lookup(thread->entry)) return kInvalidAddress;
    std::lock_guard lock(list_lock);
    if(thread->started || thread->exited) return kInvalidState;
    auto* state=static_cast<HostThread*>(thread->host_handle);
    pthread_attr_t attributes;
    if(pthread_attr_init(&attributes)!=0) return kOutOfResource;
    const uint64_t bytes=(state->stack_bytes+mem::kPage-1)&~(mem::kPage-1);
    if(bytes<0x100000 || bytes>(128ull<<20) || pthread_attr_setstacksize(&attributes,bytes)!=0) {
        pthread_attr_destroy(&attributes); return kOutOfResource;
    }
    auto* box=new std::shared_ptr<KThread>(thread);
    thread->started=true; ++adopted_count;
    const int result=pthread_create(&state->thread,&attributes,run_thread,box);
    pthread_attr_destroy(&attributes);
    if(result!=0) { thread->started=false; --adopted_count; delete box; return kOutOfResource; }
    state->created=true; return kOk;
}
uint32_t main_thread_adopt(uint64_t stack_size,const char* name) {
    if(self || !stack_size || stack_size>mem::kKernelSize || stack_size%16) return 0;
    const auto stack=mem::alloc_kernel(stack_size,mem::kAllocGranularity); if(!stack) return 0;
    ThreadParams parameters; parameters.stack_top=stack+stack_size; parameters.name=name?name:"main";
    const auto handle=thread_create(parameters); if(!handle) return 0;
    self=handle_as<KThread>(handle); self->started=true; self->host_tid=static_cast<uint32_t>(gettid());
    mem::vm_set(stack,(stack_size+mem::kPage-1)&~(mem::kPage-1),mem::kMemStack,3);
    ++adopted_count; return handle;
}
void main_thread_release() {
    finish_current();
}
[[noreturn]] void thread_exit() { throw AndroidThreadExit{}; }
std::vector<std::shared_ptr<KThread>> threads() { std::lock_guard lock(list_lock); return all_threads; }
bool thread_join(uint32_t handle,uint32_t timeout_ms) {
    auto thread=handle_as<KThread>(handle);
    if(!thread || !thread->started) return true;
    if(thread.get()==current_thread()) return false;
    auto* state=static_cast<HostThread*>(thread->host_handle);
    if(!state->created) return thread->exited;
    {
        std::unique_lock lock(state->exit_lock);
        if(timeout_ms==0xffffffffu) state->exit_changed.wait(lock,[&]{return state->finished;});
        else if(!state->exit_changed.wait_for(lock,std::chrono::milliseconds(timeout_ms),[&]{return state->finished;})) return false;
    }
    std::lock_guard lock(state->join_lock);
    if(!state->joined) {
        if(pthread_join(state->thread,nullptr)!=0) return false;
        state->joined=true;
    }
    return true;
}
void kernel_reset() {
    const auto snapshot=threads();
    for(const auto& thread:snapshot)
        if(thread->started && !thread->exited) trap("Cannot reset Android guest memory while a thread is running",thread->entry);
    for(const auto& thread:snapshot) if(thread->started && !thread_join(thread->handle,1000))
        trap("Cannot join an Android guest thread during reset",thread->entry);
    detail::sync_reset(); handles_reset();
    { std::lock_guard lock(list_lock); all_threads.clear(); }
    { std::lock_guard lock(failure_lock); worker_failure.clear(); }
}
bool thread_set_priority(KThread& thread,int priority) {
    if(priority<0 || priority>63) return false;
    std::lock_guard lock(detail::kernel_lock); thread.prio=priority; return true;
}
bool thread_set_core_mask(KThread& thread,int ideal,uint64_t mask) {
    std::lock_guard lock(detail::kernel_lock);
    if(ideal==-2) { ideal=0; mask=1; }
    if(ideal==-3) ideal=thread.ideal_core;
    const uint64_t allowed=(1ull<<app_core_count())-1;
    mask &= allowed;
    if(ideal < -1 || ideal>=app_core_count() || !mask || (ideal>=0 && !(mask&(1ull<<ideal)))) return false;
    thread.ideal_core=ideal; thread.core_mask=mask; return true;
}
int sched_current_core() {
    std::lock_guard lock(detail::kernel_lock);
    return self?(self->ideal_core<0?static_cast<int>(std::countr_zero(self->core_mask)):self->ideal_core):0;
}
void yield_host() { std::this_thread::yield(); }
uint64_t thread_count_running() { return adopted_count.load(); }
int app_core_count() { return 6; }
uint64_t tick_hz() { return kAndroidSwitch2TickHz; }
uint64_t system_tick() { return android_system_tick(); }
void sleep_ns(int64_t value) {
    if (value <= 0) std::this_thread::yield();
    else std::this_thread::sleep_for(std::chrono::nanoseconds(value));
}
} // namespace nxrt::kernel
