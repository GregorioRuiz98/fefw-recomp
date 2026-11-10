// SPDX-License-Identifier: GPL-3.0-or-later
// Android adaptation of Goldoire/nxrt kernel_sync.cpp. Guest mutex handoff,
// condition-key and address-arbitration algorithms retain the upstream protocol
// (original MIT-derived logic credited in THIRD_PARTY_NOTICES.md).
#include "kernel_android_internal.h"
#include "nxrt/android_kernel.h"
#include "nxrt/memory.h"
#include "nxrt/dispatch.h"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <limits>
#include <vector>
#include <unistd.h>

namespace nxrt::kernel {
namespace detail {
std::mutex kernel_lock;
namespace {
using Clock = std::chrono::steady_clock;
using Guard = std::lock_guard<std::mutex>;
std::vector<KThread*> sync_waiters, mutex_waiters, key_waiters, address_waiters;
std::atomic<uint32_t> blocked_tid{0};
std::atomic<void(*)()> blocked_fn{nullptr};
std::condition_variable_any& condition(KThread& thread) {
    if (!thread.cv_storage[0]) thread.cv_storage[0] = new std::condition_variable_any;
    return *static_cast<std::condition_variable_any*>(thread.cv_storage[0]);
}
KThread& self() {
    if (auto* thread=current_thread()) return *thread;
    static thread_local KThread host; return host;
}
// Native callers own their buffers. The SVC boundary checks guest page access;
// repeatedly parsing /proc/self/maps here would dominate host mutex handoffs.
bool valid_word(uint64_t address) { return address && !(address & 3); }
uint32_t load32(uint64_t a) { return __atomic_load_n(reinterpret_cast<uint32_t*>(a), __ATOMIC_SEQ_CST); }
void store32(uint64_t a,uint32_t v) { __atomic_store_n(reinterpret_cast<uint32_t*>(a),v,__ATOMIC_SEQ_CST); }
bool cas32(uint64_t a,uint32_t from,uint32_t to) {
    return __atomic_compare_exchange_n(reinterpret_cast<uint32_t*>(a),&from,to,false,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST);
}
void remove(std::vector<KThread*>& queue,KThread* thread) {
    queue.erase(std::remove(queue.begin(),queue.end(),thread),queue.end());
}
struct Enrollment {
    std::vector<KThread*>& queue; KThread& thread;
    Enrollment(std::vector<KThread*>& list,KThread& t):queue(list),thread(t) { queue.push_back(&thread); }
    ~Enrollment() { remove(queue,&thread); }
};
KThread* best(std::vector<KThread*>& queue,uint64_t KThread::*field,uint64_t address) {
    KThread* chosen=nullptr;
    for(auto* thread:queue)
        if(thread->*field==address && (!chosen || thread->prio<chosen->prio)) chosen=thread;
    return chosen;
}
Clock::time_point deadline(int64_t nanoseconds) {
    if(nanoseconds<0) return Clock::time_point::max();
    const auto now=Clock::now();
    const auto available=Clock::time_point::max()-now;
    return std::chrono::nanoseconds(nanoseconds)>=available ? Clock::time_point::max() : now+std::chrono::nanoseconds(nanoseconds);
}
void sleep_locked(KThread& thread,Clock::time_point until) {
    auto& cv=condition(thread);
    // Poll only when a UI hook or worker failure needs delivery. A monotonic
    // absolute deadline is retained across spurious wakes and hook callbacks.
    android_check_thread_failure();
    const bool hook_here=blocked_fn.load() && blocked_tid.load()==static_cast<uint32_t>(gettid());
    const auto slice=hook_here?std::min(until,Clock::now()+std::chrono::milliseconds(10)):until;
    if(slice==Clock::time_point::max()) cv.wait(kernel_lock);
    else cv.wait_until(kernel_lock,slice);
    if(auto hook=blocked_fn.load(); hook && blocked_tid.load()==static_cast<uint32_t>(gettid())) {
        kernel_lock.unlock();
        try { hook(); } catch(...) { kernel_lock.lock(); throw; }
        kernel_lock.lock();
    }
    android_check_thread_failure();
}
bool block_on_wake(KThread& thread,int64_t nanoseconds) {
    const auto until=deadline(nanoseconds);
    while(!thread.wake) {
        if(Clock::now()>=until) return false;
        sleep_locked(thread,until);
    }
    return true;
}
void wake(KThread& thread,uint32_t result=kOk) {
    thread.wait_result=result; thread.wake=true; condition(thread).notify_one();
}
void unlock_mutex(uint64_t address) {
    auto* next=best(mutex_waiters,&KThread::wait_mutex,address);
    if(!next) { store32(address,0); return; }
    remove(mutex_waiters,next);
    const bool more=std::any_of(mutex_waiters.begin(),mutex_waiters.end(),[&](KThread* t){return t->wait_mutex==address;});
    store32(address,next->wait_tag|(more?kHandleWaitMask:0)); wake(*next);
}
}
void thread_mark_exited(KThread& thread) {
    Guard lock(kernel_lock); thread.exited=true;
    for(auto* waiter:sync_waiters) condition(*waiter).notify_one();
}
void notify_failure() {
    Guard lock(kernel_lock);
    for(const auto* queue:{&sync_waiters,&mutex_waiters,&key_waiters,&address_waiters})
        for(auto* thread:*queue) condition(*thread).notify_one();
}
void destroy_waiter(KThread& thread) {
    delete static_cast<std::condition_variable_any*>(thread.cv_storage[0]); thread.cv_storage[0]=nullptr;
}
void sync_reset() {
    Guard lock(kernel_lock);
    if(!sync_waiters.empty() || !mutex_waiters.empty() || !key_waiters.empty() || !address_waiters.empty())
        trap("Cannot reset an Android kernel with active waiters",0);
    blocked_fn=nullptr; blocked_tid=0;
}
} // namespace detail
using namespace detail;

uint32_t arbitrate_lock(uint32_t owner,uint64_t address,uint32_t tag) {
    if(!valid_word(address)) return kInvalidAddress;
    Guard lock(kernel_lock); auto& me=self();
    if(load32(address)!=(owner|kHandleWaitMask)) return kOk;
    me.wake=false; me.wait_mutex=address; me.wait_tag=tag;
    Enrollment queued(mutex_waiters,me); block_on_wake(me,-1); return me.wait_result;
}
uint32_t arbitrate_unlock(uint64_t address) {
    if(!valid_word(address)) return kInvalidAddress;
    Guard lock(kernel_lock); unlock_mutex(address); return kOk;
}
uint32_t wait_process_wide_key(uint64_t mutex,uint64_t key,uint32_t tag,int64_t nanoseconds) {
    if(!valid_word(mutex) || !valid_word(key)) return kInvalidAddress;
    Guard lock(kernel_lock); auto& me=self();
    me.wake=false; me.wait_mutex=mutex; me.wait_key=key; me.wait_tag=tag; me.wait_result=0;
    Enrollment queued(key_waiters,me);
    // Publish waiters before unlocking: SDK Signal checks this guest word first.
    store32(key,1); unlock_mutex(mutex);
    bool awoke=false;
    try { awoke=nanoseconds!=0 && block_on_wake(me,nanoseconds); }
    catch(...) {
        remove(key_waiters,&me); remove(mutex_waiters,&me);
        if(std::none_of(key_waiters.begin(),key_waiters.end(),[&](KThread* t){return t->wait_key==key;})) store32(key,0);
        throw;
    }
    if(!awoke) {
        remove(key_waiters,&me); remove(mutex_waiters,&me);
        if(std::none_of(key_waiters.begin(),key_waiters.end(),[&](KThread* t){return t->wait_key==key;})) store32(key,0);
        return kTimedOut;
    }
    return me.wait_result;
}
uint32_t signal_process_wide_key(uint64_t key,int32_t count) {
    if(!valid_word(key)) return kInvalidAddress;
    Guard lock(kernel_lock);
    for(int n=0;count<=0 || n<count;++n) {
        auto* next=best(key_waiters,&KThread::wait_key,key); if(!next) break;
        remove(key_waiters,next);
        for(;;) {
            const auto value=load32(next->wait_mutex);
            if(value==0) { if(!cas32(next->wait_mutex,0,next->wait_tag)) continue; wake(*next); break; }
            if(!(value&kHandleWaitMask) && !cas32(next->wait_mutex,value,value|kHandleWaitMask)) continue;
            mutex_waiters.push_back(next); break;
        }
    }
    if(std::none_of(key_waiters.begin(),key_waiters.end(),[&](KThread* t){return t->wait_key==key;})) store32(key,0);
    return kOk;
}
uint32_t wait_for_address(uint64_t address,uint32_t type,int32_t value,int64_t nanoseconds) {
    if(!valid_word(address)) return kInvalidAddress;
    if(type>2) return kInvalidEnum;
    Guard lock(kernel_lock); auto& me=self();
    int32_t current=static_cast<int32_t>(load32(address));
    if(type==2) { if(current!=value) return kInvalidState; }
    else if(type==0) { if(current>=value) return kInvalidState; }
    else for(;;) {
        if(current>=value) return kInvalidState;
        if(cas32(address,static_cast<uint32_t>(current),static_cast<uint32_t>(current)-1)) break;
        current=static_cast<int32_t>(load32(address));
    }
    if(!nanoseconds) return kTimedOut;
    me.wake=false; me.wait_addr=address; me.wait_result=0;
    Enrollment queued(address_waiters,me);
    return block_on_wake(me,nanoseconds)?me.wait_result:kTimedOut;
}
uint32_t signal_to_address(uint64_t address,uint32_t type,int32_t value,int32_t count) {
    if(!valid_word(address)) return kInvalidAddress;
    if(type>2) return kInvalidEnum;
    Guard lock(kernel_lock);
    const int waiting=static_cast<int>(std::count_if(address_waiters.begin(),address_waiters.end(),[&](KThread* t){return t->wait_addr==address;}));
    const uint32_t expected=static_cast<uint32_t>(value);
    uint32_t updated=expected;
    if(type==1) updated=expected+1;
    if(type==2) updated=count<=0?(waiting?expected-2:expected+1):(waiting?(waiting<=count?expected-1:expected):expected+1);
    if(type && !cas32(address,expected,updated)) return kInvalidState;
    for(int n=0;count<=0 || n<count;++n) {
        auto* next=best(address_waiters,&KThread::wait_addr,address); if(!next) break;
        remove(address_waiters,next); wake(*next);
    }
    return kOk;
}
uint32_t wait_synchronization(const uint32_t* values,int32_t count,int64_t nanoseconds,int32_t* index) {
    if(index) *index=-1;
    if(count<0 || count>0x40) return kOutOfRange;
    if(count && !values) return kInvalidPointer;
    std::vector<std::shared_ptr<KObject>> objects;
    for(int i=0;i<count;++i) {
        auto object=handle_get(values[i]); if(!object) return kInvalidHandle;
        objects.push_back(std::move(object));
    }
    Guard lock(kernel_lock); auto& me=self(); const auto until=deadline(nanoseconds);
    Enrollment queued(sync_waiters,me);
    for(;;) {
        if(me.cancel_sync) { me.cancel_sync=false; return kCancelled; }
        for(int i=0;i<count;++i) if(objects[i]->signaled()) {
            if(index) *index=i; objects[i]->consume(); return kOk;
        }
        if(!nanoseconds || Clock::now()>=until) return kTimedOut;
        sleep_locked(me,until);
    }
}
uint32_t cancel_synchronization(uint32_t handle) {
    auto thread=handle_as<KThread>(handle); if(!thread) return kInvalidHandle;
    Guard lock(kernel_lock); thread->cancel_sync=true; condition(*thread).notify_one(); return kOk;
}
void notify_waitable_changed() {
    Guard lock(kernel_lock); for(auto* thread:sync_waiters) condition(*thread).notify_one();
}
void set_blocked_hook(uint32_t tid,void(*hook)()) { blocked_tid=tid; blocked_fn=hook; }
std::string sync_wait_report() {
    std::unique_lock lock(kernel_lock,std::try_to_lock);
    if(!lock.owns_lock()) return "Android kernel lock is busy";
    return "waiters: synchronization="+std::to_string(sync_waiters.size())+", mutex="+std::to_string(mutex_waiters.size())
        +", key="+std::to_string(key_waiters.size())+", address="+std::to_string(address_waiters.size());
}
} // namespace nxrt::kernel
