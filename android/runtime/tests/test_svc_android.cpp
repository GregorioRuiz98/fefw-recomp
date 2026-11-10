// SPDX-License-Identifier: GPL-3.0-or-later
// Behavioural Android bootstrap tests using the actual ABI 3 guest_svc entry.
#include "test_framework.h"
#include "nxrt/android_kernel.h"
#include "nxrt/android_trap.h"
#include "nxrt/kernel.h"
#include "nxrt/memory.h"
#include "nxrt/svc.h"
#include "nxrt/config.h"
#include "nxrt/dispatch.h"
#include <chrono>
#include <cstring>
#include <thread>

NXTEST_GROUP(svc_android)
namespace m = nxrt::mem;
namespace k = nxrt::kernel;
namespace {
struct Session {
    Session() {
        nxrt::android_configure_fefw();
        k::main_thread_release(); k::kernel_reset(); nxrt::svc_memory_reset_for_tests(); nxrt::svc_info_reset_for_tests();
        m::shutdown(); CHECK(m::init()); nxrt::svc_reset_counters();
    }
    ~Session() {
        k::main_thread_release(); k::kernel_reset(); nxrt::svc_memory_reset_for_tests(); nxrt::svc_info_reset_for_tests();
        m::shutdown();
    }
};
Ctx call(uint32_t number, uint64_t x0=0, uint64_t x1=0, uint64_t x2=0, uint64_t x3=0) {
    Ctx c{}; c.x[0]=x0; c.x[1]=x1; c.x[2]=x2; c.x[3]=x3;
    guest_svc(&c, number); return c;
}
uint64_t heap(uint64_t size=0x200000) {
    const auto c=call(1, 0, size); CHECK_EQ(c.x[0],k::kOk); return c.x[1];
}
struct MemoryInfo { uint64_t base,size; uint32_t type,attributes,perm,ipc,device,padding; };
MemoryInfo query(uint64_t address) {
    const uint64_t output=m::alloc_kernel(sizeof(MemoryInfo)); CHECK(output);
    const auto c=call(6,output,0,address); CHECK_EQ(c.x[0],k::kOk); CHECK_EQ(c.x[1],0u);
    return *reinterpret_cast<const MemoryInfo*>(output);
}
uint64_t info(uint64_t type, uint64_t subtype=0) {
    const auto c=call(0x29,0,type,k::kPseudoCurrentProcess,subtype);
    CHECK_EQ(c.x[0],k::kOk); return c.x[1];
}
void fake_code(uint64_t size=0x10000) {
    CHECK(m::reserve_fixed(m::kMainBase,size,"synthetic RELRO"));
    CHECK(m::commit(m::kMainBase,size)); m::vm_set(m::kMainBase,size,m::kMemCodeData,3);
}
}

NXTEST(names_and_table_match_guest_numbers) {
    Session session;
    CHECK_EQ(std::string(nxrt::svc_name(1)),"SetHeapSize");
    CHECK_EQ(std::string(nxrt::svc_name(0x29)),"GetInfo");
    CHECK(nxrt::svc_handler(1)); CHECK(!nxrt::svc_handler(0x23));
    CHECK(!nxrt::svc_handler(0x10000));
}
NXTEST(unknown_service_returns_error_and_preserves_first_call) {
    Session session;
    Ctx c{}; c.x[0]=83; c.x[7]=99; c.x[30]=0x7100001004; c.fn=0x7100002000;
    nxrt::svc_dispatch(&c,0x23,0x7100001000); CHECK_EQ(c.x[0],k::kNotImplemented);
    nxrt::svc_dispatch(&c,0x23); const auto records=nxrt::svc_unknown_records();
    CHECK_EQ(records.size(),1u); CHECK_EQ(records[0].count,2u);
    CHECK_EQ(records[0].first_args[0],83u); CHECK_EQ(records[0].first_args[7],99u);
    CHECK_EQ(records[0].first_pc,0x7100001000u); CHECK_EQ(nxrt::svc_unknown_total(),2u);
}
NXTEST(out_of_range_service_does_not_index_table) {
    Session session;
    CHECK_EQ(call(0xffffffff).x[0],k::kNotImplemented);
    CHECK_EQ(nxrt::svc_call_count(0xffffffff),0u); CHECK_EQ(nxrt::svc_unknown_total(),1u);
}
NXTEST(heap_resize_preserves_prefix_and_zeros_recommitted_tail) {
    Session session; const auto base=heap(0x400000);
    *reinterpret_cast<uint64_t*>(base)=0x12345678;
    *reinterpret_cast<uint64_t*>(base+0x200000)=0x98765432;
    heap(0x200000); CHECK_EQ(nxrt::svc_current_heap_size(),0x200000u);
    CHECK(!m::is_mapped(base+0x200000,4)); heap(0x400000);
    CHECK_EQ(*reinterpret_cast<uint64_t*>(base),0x12345678u);
    CHECK_EQ(*reinterpret_cast<uint64_t*>(base+0x200000),0u);
}
NXTEST(heap_rejects_non_granular_size_and_resource_overflow) {
    Session session;
    CHECK_EQ(call(1,0,0x1000).x[0],k::kInvalidSize);
    CHECK_EQ(call(1,0,m::kHeapMax+0x200000).x[0],k::kOutOfMemory);
    CHECK_EQ(nxrt::svc_current_heap_size(),0u);
}
NXTEST(heap_zero_size_is_valid_and_reports_base) {
    Session session; heap();
    CHECK_EQ(call(1,0,0).x[1],m::kHeapBase); CHECK_EQ(nxrt::svc_current_heap_size(),0u);
    CHECK(!m::is_mapped(m::kHeapBase,1));
}
NXTEST(eight_gib_heap_is_sparse_and_exceeds_switch1_limit) {
    Session session; CHECK_EQ(heap(8ull<<30),m::kHeapBase);
    CHECK_EQ(nxrt::svc_current_heap_size(),8ull<<30);
    CHECK_EQ(*reinterpret_cast<uint64_t*>(m::kHeapBase),0u);
    CHECK_EQ(*reinterpret_cast<uint64_t*>(m::kHeapBase+(8ull<<30)-8),0u);
}
NXTEST(heap_shrink_refuses_live_alias_without_losing_contents) {
    Session session; const auto base=heap(0x400000);
    *reinterpret_cast<uint64_t*>(base+0x200000)=0x8765;
    const auto view=m::heap_alias(0x200000,0x10000); CHECK(view);
    CHECK_EQ(call(1,0,0x200000).x[0],k::kInvalidMemoryState);
    CHECK_EQ(*reinterpret_cast<uint64_t*>(view),0x8765u);
    CHECK(m::heap_alias_unmap(view)); CHECK_EQ(call(1,0,0x200000).x[0],k::kOk);
}
NXTEST(heap_shrink_refuses_a_mapped_stack_tail) {
    Session session; const auto base=heap(0x400000);
    CHECK_EQ(call(4,m::kStackBase,base+0x200000,0x1000).x[0],k::kOk);
    CHECK_EQ(call(1,0,0x200000).x[0],k::kInvalidMemoryState);
    CHECK_EQ(call(5,m::kStackBase,base+0x200000,0x1000).x[0],k::kOk);
    CHECK_EQ(call(1,0,0x200000).x[0],k::kOk);
}
NXTEST(heap_shrink_refuses_live_private_view) {
    Session session; heap(0x400000);
    void* view=m::heap_private_view(0x200000,0x10000); CHECK(view);
    CHECK_EQ(call(1,0,0x200000).x[0],k::kInvalidMemoryState);
    CHECK(m::heap_private_view_unmap(view)); CHECK_EQ(call(1,0,0x200000).x[0],k::kOk);
}
NXTEST(permission_changes_real_host_page_access) {
    Session session; const auto base=heap();
    CHECK_EQ(call(2,base,0x1000,1).x[0],k::kOk);
    CHECK(m::is_readable(base,0x1000)); CHECK(!m::is_writable(base,0x1000));
    CHECK_EQ(query(base).perm,1u);
    CHECK_EQ(call(2,base,0x1000,0).x[0],k::kOk); CHECK(!m::is_mapped(base,4));
    CHECK_EQ(call(2,base,0x1000,3).x[0],k::kOk); CHECK(m::is_writable(base,4));
}
NXTEST(permission_rejects_bad_addresses_sizes_and_execute_bits) {
    Session session; const auto base=heap();
    CHECK_EQ(call(2,base+1,0x1000,1).x[0],k::kInvalidAddress);
    CHECK_EQ(call(2,base,0,1).x[0],k::kInvalidSize);
    CHECK_EQ(call(2,base,0x1000,7).x[0],k::kInvalidPermission);
    CHECK_EQ(call(2,base+0x200000,0x1000,1).x[0],k::kInvalidMemoryState);
    CHECK(m::is_writable(base,4));
}
NXTEST(relro_lock_is_visible_and_cannot_be_reversed) {
    Session session; fake_code(); const auto base=m::kMainBase+0x1000;
    CHECK_EQ(call(2,base,0x1000,1).x[0],k::kOk);
    CHECK_EQ(call(3,base,0x1000,0x10,0x10).x[0],k::kOk);
    CHECK_EQ(query(base).attributes,0x10u);
    CHECK_EQ(call(2,base,0x1000,3).x[0],k::kInvalidMemoryState);
    CHECK_EQ(call(3,base,0x1000,0x10,0).x[0],k::kInvalidCombination);
}
NXTEST(attribute_boundaries_split_query_blocks) {
    Session session; fake_code(); const auto base=m::kMainBase;
    CHECK_EQ(call(3,base+0x1000,0x1000,0x10,0x10).x[0],k::kOk);
    auto q=query(base); CHECK_EQ(q.attributes,0u); CHECK_EQ(q.size,0x1000u);
    q=query(base+0x1000); CHECK_EQ(q.base,base+0x1000); CHECK_EQ(q.size,0x1000u);
    q=query(base+0x2000); CHECK_EQ(q.attributes,0u); CHECK_EQ(q.base,base+0x2000);
}
NXTEST(lock_rejects_heap_or_unknown_attribute_bits) {
    Session session; const auto base=heap();
    CHECK_EQ(call(3,base,0x1000,0x10,0x10).x[0],k::kInvalidMemoryState);
    CHECK_EQ(call(3,base,0x1000,0x80,0x80).x[0],k::kInvalidCombination);
}
NXTEST(query_rejects_unmapped_readonly_and_overflow_output) {
    Session session;
    CHECK_EQ(call(6,0,0,m::kMainBase).x[0],k::kInvalidAddress);
    CHECK_EQ(call(6,~0ull-15,0,m::kMainBase).x[0],k::kInvalidAddress);
    const auto output=m::alloc_kernel(0x1000); CHECK(m::protect(output,0x1000,m::Prot::R));
    CHECK_EQ(call(6,output,0,m::kMainBase).x[0],k::kInvalidAddress);
}
NXTEST(query_walks_heap_free_gap_and_module) {
    Session session; const auto base=heap(); fake_code();
    CHECK_EQ(query(base).type,m::kMemHeap); CHECK_EQ(query(m::kMainBase).type,m::kMemCodeData);
    const auto q=query(base+0x200000); CHECK_EQ(q.type,m::kMemFree);
    CHECK(q.base<=base+0x200000 && q.size>0);
}
NXTEST(stack_mapping_roundtrip_copies_changes_back_and_restores_source) {
    Session session; const auto base=heap(); const auto stack=m::kStackBase;
    *reinterpret_cast<uint64_t*>(base)=0x1111;
    CHECK_EQ(call(4,stack,base,0x2000).x[0],k::kOk);
    CHECK(!m::is_mapped(base,4)); CHECK_EQ(query(stack).type,m::kMemStack);
    CHECK_EQ(*reinterpret_cast<uint64_t*>(stack),0x1111u);
    *reinterpret_cast<uint64_t*>(stack)=0x2222;
    CHECK_EQ(call(5,stack,base,0x2000).x[0],k::kOk);
    CHECK_EQ(*reinterpret_cast<uint64_t*>(base),0x2222u); CHECK(!m::is_mapped(stack,4));
    CHECK_EQ(query(base).perm,3u);
}
NXTEST(stack_partial_unmap_keeps_remaining_mapping) {
    Session session; const auto base=heap(); const auto stack=m::kStackBase;
    CHECK_EQ(call(4,stack,base,0x3000).x[0],k::kOk);
    CHECK_EQ(call(5,stack,base,0x1000).x[0],k::kOk);
    CHECK(m::is_writable(base,4)); CHECK(!m::is_mapped(base+0x1000,4));
    CHECK_EQ(call(5,stack+0x2000,base+0x2000,0x1000).x[0],k::kOk);
    CHECK_EQ(call(5,stack+0x1000,base+0x1000,0x1000).x[0],k::kOk);
}
NXTEST(stack_mapping_refuses_occupied_destination_without_damage) {
    Session session; const auto base=heap(); const auto stack=m::kStackBase;
    CHECK(m::commit(stack,0x1000)); m::vm_set(stack,0x1000,m::kMemStack,3);
    *reinterpret_cast<uint64_t*>(stack)=0x7777;
    CHECK_EQ(call(4,stack,base,0x1000).x[0],k::kInvalidMemoryState);
    CHECK_EQ(*reinterpret_cast<uint64_t*>(stack),0x7777u); CHECK(m::is_writable(base,4));
}
NXTEST(stack_unmap_rejects_unknown_pair) {
    Session session; const auto base=heap();
    CHECK_EQ(call(5,m::kStackBase,base,0x1000).x[0],k::kInvalidMemoryState);
    CHECK_EQ(call(4,m::kStackBase,base,0).x[0],k::kInvalidSize);
}
NXTEST(memory_info_matches_switch2_address_space) {
    Session session;
    CHECK_EQ(info(0),0x3fu); CHECK_EQ(info(2),m::kAliasBase); CHECK_EQ(info(3),m::kAliasSize);
    CHECK_EQ(info(4),m::kHeapBase); CHECK_EQ(info(5),m::kHeapMax);
    CHECK_EQ(info(14),m::kStackBase); CHECK_EQ(info(15),m::kStackSize);
    CHECK_EQ(info(29),m::kRemoteBase); CHECK_EQ(info(30),m::kRemoteSize);
    CHECK_EQ(info(33),0x1000u); CHECK_EQ(info(36),1ull<<39);
}
NXTEST(process_title_and_memory_accounting) {
    Session session; CHECK_EQ(info(18),0x04007EF01F8AE000ull);
    CHECK_EQ(info(6),0x2a0000000ull); const auto before=info(7); heap();
    CHECK_EQ(info(7)-before,0x200000u); CHECK_EQ(info(22),info(7)); CHECK_EQ(info(21),info(6));
}
NXTEST(unknown_info_subtypes_return_errors_and_zero_output) {
    Session session;
    auto c=call(0x29,0,0xffff,0,0); CHECK_EQ(c.x[0],k::kInvalidCombination); CHECK_EQ(c.x[1],0u);
    c=call(0x29,0,29,0,1); CHECK_EQ(c.x[0],k::kInvalidCombination); CHECK_EQ(c.x[1],0u);
}
NXTEST(system_resource_limits_are_consistent_with_memory) {
    Session session; heap();
    CHECK_EQ(call(0x30,0,0,0).x[1],info(6)); CHECK_EQ(call(0x31,0,0,0).x[1],0x200000u);
    CHECK_EQ(call(0x30,0,0,10).x[0],k::kInvalidCombination);
}
NXTEST(adopted_main_thread_has_real_stack_and_current_tls_handle) {
    Session session; const auto h=k::main_thread_adopt(0x100000); CHECK(h);
    const auto* thread=k::current_thread(); CHECK(thread); const auto* c=k::current_ctx(); CHECK(c);
    CHECK_EQ(c->x[31]%16,0u); CHECK(m::is_writable(c->x[31]-0x100000,0x100000));
    CHECK_EQ(c->tpidrro,thread->tls); CHECK_EQ(c->tpidr,thread->tp_block);
    CHECK_EQ(*reinterpret_cast<uint32_t*>(c->tpidrro+0x110),h);
    CHECK_EQ(*reinterpret_cast<uint64_t*>(c->tpidrro+0x1f0),c->tpidr);
    CHECK_EQ(k::handle_get(k::kPseudoCurrentThread).get(),thread);
    CHECK_EQ(k::thread_count_running(),1u); CHECK_EQ(k::main_thread_adopt(0x100000),0u);
}
NXTEST(adopted_main_release_signals_object_and_clears_current_context) {
    Session session; const auto h=k::main_thread_adopt(0x10000); CHECK(h);
    auto object=k::handle_get(h); CHECK(!object->signaled()); k::main_thread_release();
    CHECK(object->signaled()); CHECK(!k::current_ctx()); CHECK_EQ(k::thread_count_running(),0u);
}
NXTEST(thread_and_process_id_validate_handles) {
    Session session; const auto h=k::main_thread_adopt(0x10000); CHECK(h);
    CHECK_EQ(call(0x25,0,k::kPseudoCurrentThread).x[1],k::current_thread()->id);
    CHECK_EQ(call(0x25,0,0x123).x[0],k::kInvalidHandle);
    CHECK_EQ(call(0x24,0,k::kPseudoCurrentProcess).x[0],k::kOk);
    CHECK_EQ(call(0x24,0,0x123).x[0],k::kInvalidHandle);
    CHECK_EQ(call(0x7c,0,0x123,0).x[0],k::kInvalidHandle);
}
NXTEST(close_handle_drops_real_object_and_rejects_pseudo_handle) {
    Session session; const auto h=k::handle_add(std::make_shared<k::KObject>()); CHECK(h);
    CHECK_EQ(call(0x16,h).x[0],k::kOk); CHECK(!k::handle_get(h));
    CHECK_EQ(call(0x16,h).x[0],k::kInvalidHandle);
    CHECK_EQ(call(0x16,k::kPseudoCurrentThread).x[0],k::kInvalidHandle);
}
NXTEST(system_tick_and_mrs_share_frequency_and_epoch) {
    Session session; Ctx c{}; const auto before=guest_mrs(&c,0xdf01);
    const auto tick=call(0x1e).x[0]; const auto after=guest_mrs(&c,0xdf02);
    CHECK(before<=tick && tick<=after); CHECK_EQ(k::tick_hz(),31250000u);
}
NXTEST(sleep_uses_host_time_without_overwriting_guest_registers) {
    Session session; const auto start=std::chrono::steady_clock::now();
    const auto c=call(0xb,2000000,0x1234); CHECK_EQ(c.x[0],2000000u); CHECK_EQ(c.x[1],0x1234u);
    CHECK(std::chrono::steady_clock::now()-start>=std::chrono::milliseconds(2));
}
NXTEST(debug_string_validates_readable_guest_range) {
    Session session; const auto p=m::alloc_kernel(0x1000); std::memcpy(reinterpret_cast<void*>(p),"test",4);
    CHECK_EQ(call(0x27,p,4).x[0],k::kOk); CHECK_EQ(call(0x27,0,0).x[0],k::kOk);
    CHECK_EQ(call(0x27,~0ull-1,4).x[0],k::kInvalidPointer);
    CHECK(m::protect(p,0x1000,m::Prot::None)); CHECK_EQ(call(0x27,p,4).x[0],k::kInvalidPointer);
}
NXTEST(guest_fatal_break_is_caught_without_exiting_launcher) {
    Session session; CHECK_EQ(call(0x26,0x80000000).x[0],0x80000000u);
    bool trapped=false; try { call(0x26,0); } catch (const nxrt::AndroidGuestTrap&) { trapped=true; }
    CHECK(trapped); CHECK_EQ(info(33),0x1000u);
}
NXTEST(exception_return_is_explicitly_unsupported) {
    Session session; bool trapped=false;
    try { call(0x28); } catch (const nxrt::AndroidGuestTrap&) { trapped=true; }
    CHECK(trapped);
}
NXTEST(kernel_can_reset_and_run_a_second_session) {
    for (unsigned attempt=0; attempt<2; ++attempt) {
        Session session; heap(); const auto h=k::main_thread_adopt(0x10000); CHECK(h);
        CHECK_EQ(k::handle_count(),1u); CHECK_EQ(info(7),0x4200000u);
    }
    CHECK(!m::initialised()); CHECK_EQ(k::handle_count(),0u); CHECK(!k::current_ctx());
}
NXTEST(exception_context_does_not_reuse_new_sessions_stack) {
    { Session session; CHECK(info(20)); }
    { Session session; const auto stack=m::alloc_kernel(0x10000);
      const auto context=info(20); CHECK(context>=stack+0x10000); CHECK(m::is_writable(context,0x1000)); }
}
