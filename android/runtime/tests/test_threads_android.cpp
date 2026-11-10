// SPDX-License-Identifier: GPL-3.0-or-later
#include "test_framework.h"
#include "nxrt/android_kernel.h"
#include "nxrt/android_trap.h"
#include "nxrt/dispatch.h"
#include "nxrt/kernel.h"
#include "nxrt/memory.h"
#include "nxrt/svc.h"
#include "nxrt/ipc.h"
#include "nxrt/hipc.h"
#include "nxrt/config.h"
#include <atomic>
#include <functional>
#include <pthread.h>
#include <thread>
#include <chrono>
#include <cstring>

NXTEST_GROUP(threads_android)
namespace k=nxrt::kernel;
namespace m=nxrt::mem;
namespace {
constexpr uint64_t entry_address=0x7100001000ull;
struct Session {
    Session() { nxrt::android_configure_fefw(); k::main_thread_release(); k::kernel_reset(); m::shutdown(); CHECK(m::init()); }
    ~Session() { k::main_thread_release(); k::kernel_reset(); nxrt::fn_clear(); nxrt::ipc::registry_reset(); m::shutdown(); }
};
using Body=std::function<void(Ctx*)>;
void entry(Ctx* c) { (*reinterpret_cast<Body*>(c->x[0]))(c); }
uint32_t create(Body& body) {
    nxrt::fn_register(entry_address,entry); const auto stack=m::alloc_kernel(0x10000); CHECK(stack);
    k::ThreadParams params; params.entry=entry_address; params.arg=reinterpret_cast<uintptr_t>(&body); params.stack_top=stack+0x10000;
    return k::thread_create(params);
}
Ctx svc(uint32_t number,uint64_t a=0,uint64_t b=0,uint64_t c=0,uint64_t d=0) {
    Ctx state{};state.x[0]=a;state.x[1]=b;state.x[2]=c;state.x[3]=d;guest_svc(&state,number);return state;
}
}
NXTEST(worker_has_isolated_tls_argument_stack_and_large_native_stack) {
    Session session;
    bool valid=false; size_t native_bytes=0;
    Body body=[&](Ctx* c) {
        auto* thread=k::current_thread();
        valid=thread && c==k::current_ctx() && c->tpidrro==thread->tls && c->tpidr==thread->tp_block
            && c->x[31]==thread->stack_top && *reinterpret_cast<uint32_t*>(c->tpidrro+0x110)==thread->handle;
        pthread_attr_t attributes;
        if(pthread_getattr_np(pthread_self(),&attributes)==0) {
            pthread_attr_getstacksize(&attributes,&native_bytes); pthread_attr_destroy(&attributes);
        }
    };
    const auto handle=create(body); CHECK(handle); CHECK_EQ(k::thread_start(handle),k::kOk);
    CHECK(k::thread_join(handle,2000)); CHECK(valid); CHECK(native_bytes>=64ull<<20);
    CHECK_EQ(k::thread_count_running(),0u); CHECK(nxrt::android_thread_failure().empty());
}
NXTEST(exit_thread_unwinds_without_running_following_guest_code) {
    Session session; bool continued=false;
    Body body=[&](Ctx* c) { guest_svc(c,0x0a); continued=true; };
    const auto handle=create(body); CHECK_EQ(k::thread_start(handle),k::kOk); CHECK(k::thread_join(handle,2000));
    CHECK(!continued); CHECK(k::handle_get(handle)->signaled()); CHECK(nxrt::android_thread_failure().empty());
}
NXTEST(start_once_and_join_timeout_preserve_running_worker) {
    Session session; std::atomic<bool> release=false;
    Body body=[&](Ctx*) { while(!release.load()) std::this_thread::yield(); };
    const auto handle=create(body); CHECK_EQ(k::thread_start(handle),k::kOk);
    const auto duplicate=k::thread_start(handle); const bool premature=k::thread_join(handle,1);
    release=true; CHECK(k::thread_join(handle,2000)); CHECK_EQ(duplicate,k::kInvalidState); CHECK(!premature);
}
NXTEST(reset_refuses_live_threads_before_unmapping_guest_memory) {
    Session session; std::atomic<bool> release=false;
    Body body=[&](Ctx*) { while(!release.load()) std::this_thread::yield(); };
    const auto handle=create(body); CHECK_EQ(k::thread_start(handle),k::kOk);
    bool refused=false; try { k::kernel_reset(); } catch(const nxrt::AndroidGuestTrap&) { refused=true; }
    release=true; CHECK(k::thread_join(handle,2000)); CHECK(refused); CHECK(m::initialised());
}
NXTEST(worker_failure_is_reported_and_join_does_not_hide_it) {
    Session session; Body body=[](Ctx*) { nxrt::trap("synthetic worker failure",entry_address); };
    const auto handle=create(body); CHECK_EQ(k::thread_start(handle),k::kOk); CHECK(k::thread_join(handle,2000));
    CHECK(nxrt::android_thread_failure().find("synthetic worker failure")!=std::string::npos);
    bool reported=false; try { nxrt::android_check_thread_failure(); } catch(const nxrt::AndroidGuestTrap&) { reported=true; }
    CHECK(reported); k::kernel_reset(); CHECK(nxrt::android_thread_failure().empty());
}
NXTEST(priority_affinity_and_processor_svc_use_real_thread_metadata) {
    Session session; const auto handle=k::main_thread_adopt(0x10000); CHECK(handle);
    CHECK_EQ(svc(0xc,0,handle).x[1],44u); CHECK_EQ(svc(0xd,handle,31).x[0],k::kOk);
    CHECK_EQ(svc(0xc,0,k::kPseudoCurrentThread).x[1],31u);
    CHECK_EQ(svc(0xf,handle,2,4).x[0],k::kOk); CHECK_EQ(svc(0x10).x[0],2u);
    auto result=svc(0xe,0,0,handle); CHECK_EQ(result.x[1],2u); CHECK_EQ(result.x[2],4u);
    CHECK_EQ(svc(0xd,handle,64).x[0],k::kInvalidPriority);
    CHECK_EQ(svc(0xf,handle,2,1).x[0],k::kInvalidCombination);
    CHECK_EQ(svc(0xc,0,0x123).x[0],k::kInvalidHandle);
}
NXTEST(create_start_and_wait_thread_through_svc_abi) {
    Session session; bool ran=false; Body body=[&](Ctx*){ran=true;}; nxrt::fn_register(entry_address,entry);
    Ctx c{}; c.x[1]=entry_address;c.x[2]=reinterpret_cast<uintptr_t>(&body);c.x[3]=m::alloc_kernel(0x10000)+0x10000;c.x[4]=44;c.x[5]=0;
    guest_svc(&c,8);CHECK_EQ(c.x[0],k::kOk);const auto handle=static_cast<uint32_t>(c.x[1]);
    CHECK_EQ(svc(9,handle).x[0],k::kOk);
    const auto output=m::alloc_kernel(0x1000);*reinterpret_cast<uint32_t*>(output)=handle;
    CHECK_EQ(svc(0x18,0,output,1,2000000000).x[0],k::kOk);CHECK(k::thread_join(handle,2000));CHECK(ran);
}
NXTEST(event_write_and_read_handles_share_state_without_auto_reset) {
    Session session;const auto event=svc(0x45);CHECK_EQ(event.x[0],k::kOk);
    const auto write=event.x[1],read=event.x[2],buffer=m::alloc_kernel(0x1000);
    *reinterpret_cast<uint32_t*>(buffer)=static_cast<uint32_t>(read);
    CHECK_EQ(svc(0x18,0,buffer,1,0).x[0],k::kTimedOut);
    CHECK_EQ(svc(0x11,read).x[0],k::kInvalidHandle);CHECK_EQ(svc(0x11,write).x[0],k::kOk);
    CHECK_EQ(svc(0x18,0,buffer,1,0).x[0],k::kOk);CHECK_EQ(svc(0x18,0,buffer,1,0).x[0],k::kOk);
    CHECK_EQ(svc(0x17,read).x[0],k::kOk);CHECK_EQ(svc(0x17,read).x[0],k::kInvalidState);
}
NXTEST(synchronization_svc_rejects_unmapped_and_readonly_words) {
    Session session;const auto word=m::alloc_kernel(0x1000);CHECK(m::protect(word,0x1000,m::Prot::R));
    CHECK_EQ(svc(0x34,word,2,0,0).x[0],k::kInvalidAddress);
    CHECK_EQ(svc(0x35,0xfffffffffffff000,0,0,1).x[0],k::kInvalidAddress);
    CHECK_EQ(svc(0x18,0,0xfffffffffffff000,1,0).x[0],k::kInvalidPointer);
}
NXTEST(ipc_rejects_invalid_names_and_readonly_message_buffers) {
    Session session;uint32_t handle=0;CHECK_EQ(nxrt::ipc::connect_named_port("sm:",&handle),k::kOk);
    CHECK_EQ(svc(0x1f,0,0xfffffffffffff000).x[0],k::kInvalidAddress);
    const auto message=m::alloc_kernel(0x1000);CHECK(m::protect(message,0x1000,m::Prot::R));
    CHECK_EQ(nxrt::ipc::send_sync_request(nullptr,handle,message,0x100),k::kInvalidPointer);
}
NXTEST(unknown_service_cannot_be_changed_to_empty_success_by_configuration) {
    Session session;nxrt::config_set("ipc","stub_result","0");
    auto service=nxrt::ipc::make_service("not:ported");nxrt::ipc::Request request;nxrt::ipc::Response response;
    const auto result=service->handle(request,response);nxrt::config_clear();
    CHECK_EQ(result,k::kStubService);CHECK(response.out.empty());CHECK(response.out_objects.empty());
}
