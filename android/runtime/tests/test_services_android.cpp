// SPDX-License-Identifier: GPL-3.0-or-later
#include "test_framework.h"
#include "test_runner.h"
#include "../src/fs_romfs_android.h"
#include <filesystem>
#include <fstream>
#include <unistd.h>
#include "nxrt/android_services.h"
#include "nxrt/android_kernel.h"
#include "nxrt/config.h"
#include "nxrt/dispatch.h"
#include "nxrt/svc.h"
#include <cstring>
#include <thread>
#include <chrono>
#include <atomic>
NXTEST_GROUP(services_android)
namespace k=nxrt::kernel;
namespace m=nxrt::mem;
namespace a=nxrt::services;
namespace i=nxrt::ipc;
namespace {
struct Session {
    Session() { nxrt::config_clear(); nxrt::android_configure_fefw(); CHECK(m::init()); }
    ~Session() { nxrt::config_clear(); }
};
i::Response call(const std::shared_ptr<i::Service>& service,uint32_t cmd,uint32_t expected=0,std::vector<uint8_t> args={}) {
    i::Request req; i::Response rsp; req.cmd=cmd; req.in=args.data(); req.in_size=args.size();
    CHECK_EQ(service->handle(req,rsp),expected); return rsp;
}
template<class T> std::vector<uint8_t> bytes(T value) { std::vector<uint8_t> result(sizeof value); std::memcpy(result.data(),&value,sizeof value); return result; }
template<class T> T value(const i::Response& rsp,size_t offset=0) { CHECK(offset+sizeof(T)<=rsp.out.size()); T result; std::memcpy(&result,rsp.out.data()+offset,sizeof result); return result; }
std::shared_ptr<i::Service> object(const i::Response& rsp) { CHECK_EQ(rsp.out_objects.size(),1u); return rsp.out_objects[0]; }
std::shared_ptr<i::Service> proxy(uint32_t command=10) { return object(call(i::make_service("appletOE"),command)); }
std::shared_ptr<i::Service> common() { return object(call(proxy(),0)); }
std::shared_ptr<k::KEvent> event(const i::Response& rsp) { CHECK_EQ(rsp.out_copy.size(),1u); auto e=k::handle_as<k::KEvent>(rsp.out_copy[0]); CHECK(e); CHECK(!e->writable); return e; }
Ctx svc(uint32_t command,uint64_t a0,uint64_t a1,uint64_t a2,uint64_t a3=0) {
    Ctx c{}; c.x[0]=a0;c.x[1]=a1;c.x[2]=a2;c.x[3]=a3; guest_svc(&c,command); return c;
}
std::shared_ptr<k::KSharedMemory> shared(uint64_t size=0x1000) {
    a::ServiceSharedMemory memory(size,"test"); CHECK(memory.ok()); return k::handle_as<k::KSharedMemory>(memory.new_handle());
}
constexpr uint64_t view=0x2000000000ull;
}
NXTEST(applet_open_variants_return_real_proxy_interfaces) {
    Session s;
    for(auto cmd:{0u,10u}) { auto p=proxy(cmd); CHECK_EQ(p->name(),"IApplicationProxy");
        for(auto id:{0u,1u,2u,3u,11u,20u,60u}) CHECK(!object(call(p,id))->is_stub()); }
}
NXTEST(startup_focus_message_signals_event_until_drained) {
    Session s; auto getter=common(); auto ev=event(call(getter,0)); CHECK(ev->signaled());
    CHECK_EQ(value<uint32_t>(call(getter,1)),15u); CHECK(!ev->signaled()); call(getter,1,a::res(128,3));
}
NXTEST(android_defaults_to_handheld_and_720p) {
    Session s; auto getter=common(); CHECK_EQ(value<uint8_t>(call(getter,5)),0u); CHECK_EQ(value<uint32_t>(call(getter,6)),0u);
    auto resolution=call(getter,60); CHECK_EQ(value<uint32_t>(resolution),1280u); CHECK_EQ(value<uint32_t>(resolution,4),720u);
}
NXTEST(focus_changes_update_getter_and_queue_message) {
    Session s; auto getter=common(); call(getter,1); auto ev=event(call(getter,0));
    a::amctl::set_focus(false); CHECK(ev->signaled()); CHECK_EQ(value<uint8_t>(call(getter,9)),2u); CHECK_EQ(value<uint32_t>(call(getter,1)),15u);
    a::amctl::set_focus(true); CHECK_EQ(value<uint8_t>(call(getter,9)),1u);
}
NXTEST(mode_notifications_follow_enabled_flags) {
    Session s; auto p=proxy(); auto getter=object(call(p,0)); auto self=object(call(p,1)); call(getter,1);
    a::amctl::set_docked(true); call(getter,1,a::res(128,3));
    call(self,11,0,bytes<uint8_t>(1)); call(self,12,0,bytes<uint8_t>(1)); a::amctl::set_docked(false);
    CHECK_EQ(value<uint32_t>(call(getter,1)),30u); CHECK_EQ(value<uint32_t>(call(getter,1)),31u);
}
NXTEST(lock_accessor_event_tracks_acquire_and_release) {
    Session s; auto lock=object(call(common(),30)); auto ev=event(call(lock,3)); CHECK(ev->signaled());
    CHECK_EQ(value<uint8_t>(call(lock,1,0,bytes<uint8_t>(0))),1u); CHECK(!ev->signaled());
    CHECK_EQ(value<uint8_t>(call(lock,1,0,bytes<uint8_t>(0))),0u); call(lock,2); CHECK(ev->signaled());
}
NXTEST(storage_read_write_and_bounds) {
    Session s; auto creator=object(call(proxy(),11)); auto storage=object(call(creator,10,0,bytes<int64_t>(8))); auto accessor=object(call(storage,0));
    CHECK_EQ(value<int64_t>(call(accessor,0)),8); auto data=m::alloc_kernel(0x1000); std::memcpy(reinterpret_cast<void*>(data),"test",4);
    i::Request req; i::Response rsp; req.cmd=10;auto offset=bytes<int64_t>(2);req.in=offset.data();req.in_size=offset.size();req.a.push_back({data,4}); CHECK_EQ(accessor->handle(req,rsp),0u);
    req.cmd=11;req.a.clear();req.b.push_back({data+16,4}); CHECK_EQ(accessor->handle(req,rsp),0u); CHECK(std::memcmp(reinterpret_cast<void*>(data+16),"test",4)==0);
    offset=bytes<int64_t>(7);req.in=offset.data(); CHECK_EQ(accessor->handle(req,rsp),a::res(128,503));
}
NXTEST(storage_invalid_buffers_are_rejected_without_dereference) {
    Session s; auto creator=object(call(proxy(),11));auto accessor=object(call(object(call(creator,10,0,bytes<int64_t>(8))),0));
    i::Request req;i::Response rsp;req.cmd=10;auto offset=bytes<int64_t>(0);req.in=offset.data();req.in_size=offset.size();req.a.push_back({1,4});CHECK_EQ(accessor->handle(req,rsp),k::kInvalidPointer);
}
NXTEST(storage_size_and_unsupported_library_applet_fail_explicitly) {
    Session s; auto creator=object(call(proxy(),11));call(creator,10,a::res(128,600),bytes<int64_t>(-1));call(creator,0,a::res(128,712));
    call(object(call(proxy(),20)),9999,k::kStubService);
}
NXTEST(application_fault_event_is_readable_and_initially_unsignaled) {
    Session s; auto functions=object(call(proxy(),20));auto first=event(call(functions,130));auto second=event(call(functions,130));
    CHECK(!first->signaled()); CHECK(first->state==second->state);
}
NXTEST(parental_control_factory_initializes_without_online_requests) {
    Session s;for(auto name:{"pctl","pctl:s","pctl:a","pctl:r"}) {auto service=object(call(i::make_service(name),1));call(service,1);CHECK_EQ(value<uint8_t>(call(service,1031)),0u);call(service,9999,k::kStubService);}
}
NXTEST(shared_service_view_is_coherent_with_readonly_guest_permissions) {
    Session s; auto memory=shared();auto handle=k::handle_add(memory);
    CHECK_EQ(svc(0x13,handle,view,0x1000,1).x[0],0u); CHECK(m::is_readable(view,4));CHECK(!m::is_writable(view,4));
    *reinterpret_cast<uint32_t*>(memory->host)=0x12345678;CHECK_EQ(*reinterpret_cast<uint32_t*>(view),0x12345678u);
    CHECK_EQ(m::vm_query(view).state,m::kMemShared);CHECK_EQ(m::vm_query(view).perm,1u);
    CHECK_EQ(svc(0x14,handle,view,0x1000).x[0],0u);CHECK(!m::is_mapped(view,4));CHECK(m::is_writable(memory->host,4));
}
NXTEST(shared_rw_multiple_views_remain_coherent_after_one_unmap) {
    Session s;auto memory=shared();auto handle=k::handle_add(memory);
    CHECK_EQ(svc(0x13,handle,view,0x1000,3).x[0],0u);CHECK_EQ(svc(0x13,handle,view+0x10000,0x1000,1).x[0],0u);
    *reinterpret_cast<uint32_t*>(view)=99;CHECK_EQ(*reinterpret_cast<uint32_t*>(memory->host),99u);CHECK_EQ(*reinterpret_cast<uint32_t*>(view+0x10000),99u);
    CHECK_EQ(svc(0x14,handle,view,0x1000).x[0],0u);*reinterpret_cast<uint32_t*>(memory->host)=100;CHECK_EQ(*reinterpret_cast<uint32_t*>(view+0x10000),100u);
}
NXTEST(shared_mapping_rejects_bad_handle_alignment_size_permission_and_destination) {
    Session s;auto memory=shared();auto handle=k::handle_add(memory);
    CHECK_EQ(svc(0x13,0,view,0x1000,1).x[0],k::kInvalidHandle);CHECK_EQ(svc(0x13,handle,view+1,0x1000,1).x[0],k::kInvalidAddress);
    CHECK_EQ(svc(0x13,handle,view,0,1).x[0],k::kInvalidSize);CHECK_EQ(svc(0x13,handle,view,0x2000,1).x[0],k::kInvalidSize);
    CHECK_EQ(svc(0x13,handle,view,0x1000,7).x[0],k::kInvalidPermission);CHECK_EQ(svc(0x13,handle,m::kKernelBase,0x1000,1).x[0],k::kInvalidMemoryState);
}
NXTEST(shared_mapping_never_overwrites_a_foreign_reservation) {
    Session s;CHECK(m::reserve_fixed(view,0x10000,"foreign"));CHECK(m::commit(view,0x1000));*reinterpret_cast<uint32_t*>(view)=42;
    auto handle=k::handle_add(shared());CHECK_EQ(svc(0x13,handle,view,0x1000,1).x[0],k::kInvalidMemoryState);CHECK_EQ(*reinterpret_cast<uint32_t*>(view),42u);
}
NXTEST(shared_unmap_requires_the_original_object) {
    Session s;auto first=k::handle_add(shared()),second=k::handle_add(shared());CHECK_EQ(svc(0x13,first,view,0x1000,1).x[0],0u);
    CHECK_EQ(svc(0x14,second,view,0x1000).x[0],k::kInvalidMemoryState);CHECK(m::is_readable(view,4));
}
NXTEST(clock_context_uses_coherent_shared_memory_and_readonly_client_permissions) {
    Session s;auto service=i::make_service("time:u");auto reply=call(service,20);CHECK_EQ(reply.out_copy.size(),1u);
    auto handle=reply.out_copy[0];CHECK_EQ(svc(0x13,handle,view,0x1000,1).x[0],0u);
    const auto active=*reinterpret_cast<uint32_t*>(view)&1u;
    CHECK_EQ(*reinterpret_cast<uint64_t*>(view+8+active*0x18),3600000000000ull);
    auto user=object(call(service,0));CHECK(value<int64_t>(call(user,0))>1700000000);call(user,1,a::res(116,1),bytes<int64_t>(1));
}
NXTEST(administrative_clock_update_is_visible_to_existing_readonly_view) {
    Session s;auto service=i::make_service("time:a");auto reply=call(service,20);CHECK_EQ(svc(0x13,reply.out_copy[0],view,0x1000,1).x[0],0u);
    auto user=object(call(service,0));call(user,1,0,bytes<int64_t>(1800000000));const auto now=value<int64_t>(call(user,0));CHECK(now>=1800000000 && now<=1800000001);
    CHECK(*reinterpret_cast<uint32_t*>(view+0x38)>0u);
}
NXTEST(timezone_rule_roundtrip_is_utc_on_android) {
    Session s;auto zone=object(call(i::make_service("time:u"),3));auto name=call(zone,0);CHECK(name.out.size()>=3);CHECK(std::memcmp(name.out.data(),"UTC",3)==0);
    auto calendar=call(zone,101,0,bytes<int64_t>(0));CHECK_EQ(value<int16_t>(calendar),1970);CHECK_EQ(calendar.out[2],1);CHECK_EQ(calendar.out[3],1);
}
NXTEST(clock_service_rejects_invalid_descriptors) {
    Session s;auto zone=object(call(i::make_service("time:u"),3));i::Request req;i::Response rsp;req.cmd=100;req.a.push_back({1,0x4000});CHECK_EQ(zone->handle(req,rsp),k::kInvalidPointer);
}
NXTEST(cooperative_stop_wakes_a_blocked_guest_worker) {
    Session s; auto stack=m::alloc_kernel(0x10000);auto adopted=k::main_thread_adopt(0x10000);CHECK(adopted);
    nxrt::fn_register(0x7100001000ull,[](Ctx*) { int32_t index=-1;k::wait_synchronization(nullptr,0,-1,&index); });
    k::ThreadParams params;params.entry=0x7100001000ull;params.stack_top=stack+0x10000;auto thread=k::thread_create(params);CHECK_EQ(k::thread_start(thread),0u);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));nxrt::android_request_stop();CHECK(k::thread_join(thread,1000));CHECK(nxrt::android_thread_failure().empty());
    k::main_thread_release();k::kernel_reset();CHECK(!nxrt::android_stop_requested());
}

NXTEST(applet_reset_preserves_android_background_state) {
    Session s; a::amctl::set_focus(false);a::am_reset();CHECK(!a::amctl::in_focus());a::amctl::set_focus(true);
}

NXTEST(cooperative_stop_interrupts_a_long_guest_sleep) {
    Session s;auto stack=m::alloc_kernel(0x10000);
    nxrt::fn_register(0x7100001000ull,[](Ctx*) { k::sleep_ns(3600000000000ll); });
    k::ThreadParams params;params.entry=0x7100001000ull;params.stack_top=stack+0x10000;auto thread=k::thread_create(params);CHECK_EQ(k::thread_start(thread),0u);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));nxrt::android_request_stop();CHECK(k::thread_join(thread,1000));CHECK(nxrt::android_thread_failure().empty());
}
NXTEST(base_game_addon_list_is_empty_and_mounting_fails_explicitly) {
    Session s;auto service=i::make_service("aoc:u");CHECK_EQ(value<int32_t>(call(service,2)),0);CHECK(!event(call(service,8))->signaled());call(service,7,k::kStubService);
}

namespace {
struct Folder {
    std::string path;
    Folder() {
        auto pattern=nxtest::temporary_directory()+"/fefw-romfs-XXXXXX";std::vector<char> name(pattern.begin(),pattern.end());name.push_back(0);
        const auto* created=mkdtemp(name.data());CHECK(created);path=created;
        std::filesystem::create_directory(path+"/sub");std::ofstream(path+"/sub/data.bin",std::ios::binary)<<"synthetic ROMFS bytes";
    }
    ~Folder() { std::error_code error;std::filesystem::remove_all(path,error); }
};
}
NXTEST(romfs_folder_builds_exact_tables_and_reads_original_file_bytes) {
    Folder folder;a::fs::RomfsImage image;std::string error;CHECK_MSG(image.build(folder.path,&error),error);
    CHECK_EQ(image.stats().files,1u);CHECK_EQ(image.stats().dirs,2u);uint64_t offset=0,size=0;CHECK(image.file_extent("/sub/data.bin",&offset,&size));
    std::vector<char> content(size);CHECK(image.read(offset,content.data(),size));CHECK_EQ(std::string(content.begin(),content.end()),"synthetic ROMFS bytes");
    uint64_t header=0;CHECK(image.read(0,&header,8));CHECK_EQ(header,0x50u);CHECK(!image.read(image.size(),&header,1));
}
NXTEST(romfs_shrinking_source_is_an_error_instead_of_silent_zero_data) {
    Folder folder;a::fs::RomfsImage image;CHECK(image.build(folder.path));uint64_t offset=0,size=0;CHECK(image.file_extent("/sub/data.bin",&offset,&size));
    std::ofstream(folder.path+"/sub/data.bin",std::ios::trunc)<<"x";std::vector<char> bytes(size);CHECK(!image.read(offset,bytes.data(),size));
}
NXTEST(romfs_rejects_symlinks_in_selected_folder) {
    Folder folder;CHECK_EQ(symlink("data.bin",(folder.path+"/sub/link.bin").c_str()),0);a::fs::RomfsImage image;CHECK(!image.build(folder.path));
}
NXTEST(filesystem_storage_is_real_readonly_and_validates_output_buffer) {
    Session s;Folder folder;nxrt::config_set("fs","romfs",folder.path.c_str());auto file=object(call(i::make_service("fsp-srv"),200));
    auto buffer=m::alloc_kernel(0x1000);i::Request req;i::Response rsp;i::Response args;args.push<int64_t>(0);args.push<int64_t>(8);
    req.cmd=0;req.in=args.out.data();req.in_size=args.out.size();req.b.push_back({buffer,8});CHECK_EQ(file->handle(req,rsp),0u);CHECK_EQ(*reinterpret_cast<uint64_t*>(buffer),0x50u);
    req.b[0]={1,8};CHECK_EQ(file->handle(req,rsp),k::kInvalidPointer);call(file,1,0x313802);call(file,3,0x313802);
}
NXTEST(filesystem_without_romfs_fails_with_target_not_found) { Session s;call(i::make_service("fsp-srv"),200,0x7D402); }
NXTEST(filesystem_access_log_setting_survives_a_second_session) {
    Session s;auto first=i::make_service("fsp-srv");call(first,1004,0,bytes<uint32_t>(2));CHECK_EQ(value<uint32_t>(call(i::make_service("fsp-srv"),1005)),2u);
    call(first,1004,0x2EE202,bytes<uint32_t>(4));CHECK_EQ(value<uint32_t>(call(first,1005)),2u);call(first,51,k::kStubService);
}
NXTEST(random_service_checks_buffer_and_does_not_touch_surrounding_bytes) {
    Session s;auto service=i::make_service("csrng");auto buffer=m::alloc_kernel(0x1000);std::memset(reinterpret_cast<void*>(buffer),0x5a,0x1000);
    i::Request req;i::Response rsp;req.b.push_back({buffer+8,128});CHECK_EQ(service->handle(req,rsp),0u);
    CHECK_EQ(*reinterpret_cast<uint64_t*>(buffer),0x5a5a5a5a5a5a5a5aull);CHECK_EQ(*reinterpret_cast<uint64_t*>(buffer+136),0x5a5a5a5a5a5a5a5aull);
    req.b[0]={1,128};CHECK_EQ(service->handle(req,rsp),k::kInvalidPointer);
}
NXTEST(language_query_follows_configured_supported_language_code) {
    Session s;auto functions=object(call(proxy(),20));const auto english=value<uint64_t>(call(functions,21));CHECK_EQ(english,0x53552d6e65ull);
    nxrt::config_set("system","language","Spanish");CHECK_EQ(value<uint64_t>(call(functions,21)),0x7365ull);
}
