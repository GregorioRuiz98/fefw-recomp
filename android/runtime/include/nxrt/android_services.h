// SPDX-License-Identifier: GPL-3.0-or-later
// Android service infrastructure adapted from nxrt services/common.cpp.
#pragma once
#include "nxrt/ipc.h"
#include "nxrt/android_lifecycle.h"
#include "nxrt/kernel.h"
#include "nxrt/memory.h"
#include <cstring>
#include <algorithm>
namespace nxrt::services {
constexpr uint32_t res(uint32_t module, uint32_t description) { return kernel::make_result(module, description); }
class CheckedService : public ipc::Service {
public:
    using Service::Service;
    uint32_t handle(ipc::Request& req, ipc::Response& rsp) override {
        for (const auto* list : {&req.a, &req.x})
            for (const auto& b : *list) if (b.size && !mem::is_readable(b.addr,b.size)) return kernel::kInvalidPointer;
        for (const auto* list : {&req.b, &req.c})
            for (const auto& b : *list) if (b.size && !mem::is_writable(b.addr,b.size)) return kernel::kInvalidPointer;
        for (const auto& b : req.w) if (b.size && (!mem::is_readable(b.addr,b.size) || !mem::is_writable(b.addr,b.size))) return kernel::kInvalidPointer;
        return Service::handle(req,rsp);
    }
};
inline uint64_t buf_write(const ipc::Buffer& b,const void* data,uint64_t size) {
    const auto n=std::min(b.size,size); if(n) std::memcpy(reinterpret_cast<void*>(b.addr),data,n); return n;
}
inline const uint8_t* buf_ptr(const ipc::Buffer& b) { return reinterpret_cast<const uint8_t*>(b.addr); }
class ServiceSharedMemory {
public:
    explicit ServiceSharedMemory(uint64_t size,const char* name="") {
        const auto rounded=(size+mem::kPage-1)&~(mem::kPage-1);
        const auto host=mem::alloc_kernel(rounded);
        if(host) { object_=std::make_shared<kernel::KSharedMemory>(); object_->host=host; object_->size=rounded; object_->name=name;
            object_->backing_fd=mem::create_shared_backing(host,rounded); if(object_->backing_fd<0) object_.reset(); }
    }
    uint64_t host() const { return object_?object_->host:0; }
    bool ok() const { return object_!=nullptr; }
    uint32_t new_handle() const { return object_?kernel::handle_add(object_):0; }
private: std::shared_ptr<kernel::KSharedMemory> object_;
};
void register_android_fs();
void register_time();
void time_reset();
void register_android_boot_services();
template<class F> std::shared_ptr<ipc::Service> make_iface(const char* name, F configure) {
    auto service=std::make_shared<CheckedService>(name); configure(*service); return service;
}
inline void add(const char* name, ipc::ServiceFactory factory) { ipc::register_service(name, std::move(factory)); }
class ServiceEvent {
public:
    ServiceEvent(): ServiceEvent(false) {}
    explicit ServiceEvent(bool signaled): state_(std::make_shared<kernel::EventState>()) { state_->signaled=signaled; }
    uint32_t new_handle() const {
        auto event=std::make_shared<kernel::KEvent>(); event->state=state_; event->writable=false;
        return kernel::handle_add(std::move(event));
    }
    void signal() { state_->signaled=true; kernel::notify_waitable_changed(); }
    void clear() { state_->signaled=false; }
    bool signaled() const { return state_->signaled; }
private: std::shared_ptr<kernel::EventState> state_;
};
void register_am();
void am_reset();
}
