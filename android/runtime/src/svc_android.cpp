// SPDX-License-Identifier: GPL-3.0-or-later
// Android bootstrap dispatcher, preserving nxrt's SVC ABI and unknown-call records.
#include "nxrt/svc.h"
#include "nxrt/kernel.h"
#include "nxrt/log.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <map>
#include <mutex>

namespace nxrt {
void svc_register_android_threads();
namespace {
std::array<std::atomic<SvcHandler>, kSvcCount> handlers{};
std::array<std::atomic<uint64_t>, kSvcCount> calls{};
std::once_flag initialized;
std::atomic<void(*)()> busy{nullptr};
std::mutex unknown_lock;
std::map<uint32_t, SvcUnknownRecord> unknown;
std::atomic<uint64_t> unknown_count{0};
}
const char* svc_name(uint32_t number) {
    switch (number) {
#define NXRT_SVC(n, name) case n: return name;
#include "svc_names.inc"
#undef NXRT_SVC
        default: return "?";
    }
}
void svc_register(uint32_t number, SvcHandler handler) {
    if (number < kSvcCount) handlers[number].store(handler);
}
SvcHandler svc_handler(uint32_t number) { return number < kSvcCount ? handlers[number].load() : nullptr; }
void svc_init() {
    std::call_once(initialized, [] {
        svc_register_memory_group(); svc_register_info_group(); svc_register_thread_group();
        svc_register_android_threads();
        svc_register_ipc_group();
    });
}
void svc_set_busy_hook(void(*hook)()) { busy.store(hook); }
void svc_dispatch(::Ctx* context, uint32_t number, uint64_t pc) {
    if (auto hook = busy.load()) hook();
    if (number < kSvcCount) calls[number].fetch_add(1);
    if (auto handler = svc_handler(number)) { handler(context); return; }
    {
        std::lock_guard lock(unknown_lock);
        auto& record = unknown[number];
        if (!record.count) {
            record.number = number;
            std::copy_n(context->x, 8, record.first_args);
            record.first_lr = context->x[30]; record.first_fn = context->fn; record.first_pc = pc;
        }
        ++record.count;
        if (record.count == 1)
            NXLOG_W("svc", "Unsupported Android SVC %x (%s), function %llx", number, svc_name(number),
                    static_cast<unsigned long long>(context->fn));
    }
    ++unknown_count;
    svc_ret(context, kernel::kNotImplemented);
}
std::vector<SvcUnknownRecord> svc_unknown_records() {
    std::lock_guard lock(unknown_lock);
    std::vector<SvcUnknownRecord> records;
    for (const auto& entry : unknown) records.push_back(entry.second);
    return records;
}
uint64_t svc_unknown_total() { return unknown_count.load(); }
uint64_t svc_call_count(uint32_t number) { return number < kSvcCount ? calls[number].load() : 0; }
void svc_reset_counters() {
    std::lock_guard lock(unknown_lock); unknown.clear(); unknown_count = 0;
    for (auto& value : calls) value = 0;
}
std::string svc_summary() {
    uint64_t total = 0;
    for (const auto& value : calls) total += value.load();
    return "svc: " + std::to_string(total) + " calls, " + std::to_string(unknown_count.load()) + " unsupported";
}
}
extern "C" void guest_svc(Ctx* context, uint32_t number) { nxrt::svc_dispatch(context, number); }
