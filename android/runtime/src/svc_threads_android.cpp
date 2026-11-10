// SPDX-License-Identifier: GPL-3.0-or-later
// Android thread/event/synchronization SVC ABI, adapted from the pinned nxrt
// svc_thread.cpp. No desktop priority, process termination or no-op handlers.
#include "nxrt/android_kernel.h"
#include "nxrt/kernel.h"
#include "nxrt/svc.h"
#include "nxrt/memory.h"
#include "nxrt/dispatch.h"
#include "kernel_android_internal.h"
#include <array>

namespace nxrt {
namespace {
using namespace kernel;
void create_thread(Ctx* c) {
    const auto priority=static_cast<int32_t>(c->x[4]),core=static_cast<int32_t>(c->x[5]);
    if(priority<0 || priority>63) return svc_ret(c,kInvalidPriority);
    if(core!=-2 && (core<0 || core>7)) return svc_ret(c,kInvalidCoreId);
    if(!c->x[1] || !fn_lookup(c->x[1]) || c->x[3]<16 || c->x[3]%16 || !mem::is_writable(c->x[3]-16,16))
        return svc_ret(c,kInvalidAddress);
    ThreadParams params; params.entry=c->x[1]; params.arg=c->x[2]; params.stack_top=c->x[3]; params.prio=priority; params.core=core;
    const auto handle=thread_create(params); c->x[1]=handle; svc_ret(c,handle?kOk:kOutOfResource);
}
void start_thread(Ctx* c) { svc_ret(c,thread_start(static_cast<uint32_t>(c->x[0]))); }
void exit_thread(Ctx*) { thread_exit(); }
void get_priority(Ctx* c) {
    auto thread=handle_as<KThread>(static_cast<uint32_t>(c->x[1]));
    if(!thread) return svc_ret(c,kInvalidHandle);
    std::lock_guard lock(detail::kernel_lock); c->x[1]=static_cast<uint32_t>(thread->prio); svc_ret(c,kOk);
}
void set_priority(Ctx* c) {
    auto thread=handle_as<KThread>(static_cast<uint32_t>(c->x[0]));
    svc_ret(c,!thread?kInvalidHandle:thread_set_priority(*thread,static_cast<int32_t>(c->x[1]))?kOk:kInvalidPriority);
}
void get_core_mask(Ctx* c) {
    auto thread=handle_as<KThread>(static_cast<uint32_t>(c->x[2]));
    if(!thread) return svc_ret(c,kInvalidHandle);
    std::lock_guard lock(detail::kernel_lock); c->x[1]=static_cast<uint32_t>(thread->ideal_core); c->x[2]=thread->core_mask;
    svc_ret(c,kOk);
}
void set_core_mask(Ctx* c) {
    auto thread=handle_as<KThread>(static_cast<uint32_t>(c->x[0]));
    if(!thread) return svc_ret(c,kInvalidHandle);
    const auto ideal=static_cast<int32_t>(c->x[1]);
    if(ideal<-3 || ideal>7) return svc_ret(c,kInvalidCoreId);
    svc_ret(c,thread_set_core_mask(*thread,ideal,c->x[2])?kOk:kInvalidCombination);
}
void processor(Ctx* c) { c->x[0]=static_cast<uint32_t>(sched_current_core()); }
void create_event(Ctx* c) {
    auto state=std::make_shared<EventState>(); auto write=std::make_shared<KEvent>(),read=std::make_shared<KEvent>();
    write->state=read->state=state; write->writable=true;
    const auto wh=handle_add(write),rh=handle_add(read);
    if(!wh || !rh) { if(wh) handle_close(wh); if(rh) handle_close(rh); return svc_ret(c,kOutOfHandles); }
    c->x[1]=wh; c->x[2]=rh; svc_ret(c,kOk);
}
void signal_event(Ctx* c) {
    auto event=handle_as<KEvent>(static_cast<uint32_t>(c->x[0]));
    if(!event || !event->writable) return svc_ret(c,kInvalidHandle);
    event->state->signaled=true; notify_waitable_changed(); svc_ret(c,kOk);
}
void clear_event(Ctx* c) {
    auto event=handle_as<KEvent>(static_cast<uint32_t>(c->x[0]));
    if(!event) return svc_ret(c,kInvalidHandle);
    event->state->signaled=false; svc_ret(c,kOk);
}
void reset_signal(Ctx* c) {
    auto event=handle_as<KEvent>(static_cast<uint32_t>(c->x[0]));
    if(!event) return svc_ret(c,kInvalidHandle);
    svc_ret(c,event->state->signaled.exchange(false)?kOk:kInvalidState);
}
void wait_sync(Ctx* c) {
    const auto count=static_cast<uint32_t>(c->x[2]);
    if(count>64) return svc_ret(c,kOutOfRange);
    if(count && !mem::is_readable(c->x[1],count*4ull)) return svc_ret(c,kInvalidPointer);
    std::array<uint32_t,64> handles{};
    for(unsigned i=0;i<count;++i) handles[i]=*reinterpret_cast<const uint32_t*>(c->x[1]+i*4ull);
    int32_t index=-1;
    const auto result=wait_synchronization(handles.data(),static_cast<int32_t>(count),static_cast<int64_t>(c->x[3]),&index);
    c->x[1]=static_cast<uint32_t>(index); svc_ret(c,result);
}
void cancel(Ctx* c) { svc_ret(c,cancel_synchronization(static_cast<uint32_t>(c->x[0]))); }
bool word(uint64_t address) { return !(address&3) && mem::is_writable(address,4); }
void lock(Ctx* c) { svc_ret(c,word(c->x[1])?arbitrate_lock(static_cast<uint32_t>(c->x[0]),c->x[1],static_cast<uint32_t>(c->x[2])):kInvalidAddress); }
void unlock(Ctx* c) { svc_ret(c,word(c->x[0])?arbitrate_unlock(c->x[0]):kInvalidAddress); }
void wait_key(Ctx* c) { svc_ret(c,word(c->x[0]) && word(c->x[1])?wait_process_wide_key(c->x[0],c->x[1],static_cast<uint32_t>(c->x[2]),static_cast<int64_t>(c->x[3])):kInvalidAddress); }
void signal_key(Ctx* c) {
    if(!word(c->x[0])) return trap("Invalid SignalProcessWideKey address",c->x[0]);
    (void)signal_process_wide_key(c->x[0],static_cast<int32_t>(c->x[1]));
}
void wait_address(Ctx* c) { svc_ret(c,word(c->x[0])?wait_for_address(c->x[0],static_cast<uint32_t>(c->x[1]),static_cast<int32_t>(c->x[2]),static_cast<int64_t>(c->x[3])):kInvalidAddress); }
void signal_address(Ctx* c) { svc_ret(c,word(c->x[0])?signal_to_address(c->x[0],static_cast<uint32_t>(c->x[1]),static_cast<int32_t>(c->x[2]),static_cast<int32_t>(c->x[3])):kInvalidAddress); }
}
void svc_register_android_threads() {
    svc_register(0x08,create_thread); svc_register(0x09,start_thread); svc_register(0x0a,exit_thread);
    svc_register(0x0c,get_priority); svc_register(0x0d,set_priority); svc_register(0x0e,get_core_mask);
    svc_register(0x0f,set_core_mask); svc_register(0x10,processor);
    svc_register(0x11,signal_event); svc_register(0x12,clear_event); svc_register(0x17,reset_signal);
    svc_register(0x18,wait_sync); svc_register(0x19,cancel); svc_register(0x1a,lock); svc_register(0x1b,unlock);
    svc_register(0x1c,wait_key); svc_register(0x1d,signal_key); svc_register(0x34,wait_address);
    svc_register(0x35,signal_address); svc_register(0x45,create_event);
}
}
