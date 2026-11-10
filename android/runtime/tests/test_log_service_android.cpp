// SPDX-License-Identifier: GPL-3.0-or-later
#include "test_framework.h"
#include "nxrt/android_kernel.h"
#include "nxrt/ipc.h"
#include "nxrt/log.h"
#include <cstring>
#include <string>
#include <vector>
NXTEST_GROUP(log_service_android)
namespace {
using Service=nxrt::ipc::Service;
struct Capture {
    std::vector<std::string> lines;
    Capture() { nxrt::log_set_sink([](nxrt::LogLevel,const char* text,void* data) {
        static_cast<Capture*>(data)->lines.emplace_back(text);
    },this); }
    ~Capture() { nxrt::log_set_sink(nullptr,nullptr); }
    bool contains(const char* expected) const {
        for(const auto& line:lines) if(line.find(expected)!=std::string::npos) return true;
        return false;
    }
};
std::shared_ptr<Service> logger() {
    nxrt::android_configure_fefw(); auto service=nxrt::ipc::make_service("lm");
    nxrt::ipc::Request request; nxrt::ipc::Response response;
    CHECK_EQ(service->handle(request,response),0u); CHECK_EQ(response.out_objects.size(),1u);
    CHECK(!response.out_objects[0]->is_stub()); return response.out_objects[0];
}
uint32_t log(std::shared_ptr<Service> service,uint8_t flags,const std::string& text) {
    std::vector<uint8_t> packet(0x18+2+text.size());packet[0x10]=flags;packet[0x12]=1;
    const auto bytes=static_cast<uint32_t>(packet.size()-0x18);std::memcpy(packet.data()+0x14,&bytes,4);
    packet[0x18]=2;packet[0x19]=static_cast<uint8_t>(text.size());std::memcpy(packet.data()+0x1a,text.data(),text.size());
    nxrt::ipc::Request request;nxrt::ipc::Response response;
    request.a.push_back({reinterpret_cast<uintptr_t>(packet.data()),packet.size()});
    return service->handle(request,response);
}
}
NXTEST(open_logger_returns_actual_service) { auto service=logger(); CHECK_EQ(service->name(),"lm:ILogger"); }
NXTEST(complete_packet_reaches_android_log_sink) {
    auto service=logger();Capture capture;CHECK_EQ(log(service,3,"synthetic Android log message"),0u);
    CHECK(capture.contains("synthetic Android log message"));
}
NXTEST(head_and_tail_are_assembled_before_logging) {
    auto service=logger();Capture capture;CHECK_EQ(log(service,1,"fragment-"),0u);CHECK(!capture.contains("fragment-"));
    CHECK_EQ(log(service,2,"joined"),0u);CHECK(capture.contains("fragment-joined"));
}
NXTEST(invalid_log_buffer_is_refused_without_dereferencing_it) {
    auto service=logger();nxrt::ipc::Request request;nxrt::ipc::Response response;request.a.push_back({1,0x100});
    CHECK_EQ(service->handle(request,response),nxrt::kernel::kInvalidPointer);
}
NXTEST(logger_sessions_keep_packet_assembly_separate) {
    auto first=logger(),second=logger();Capture capture;
    CHECK_EQ(log(first,1,"first-"),0u);CHECK_EQ(log(second,3,"second-complete"),0u);
    CHECK(!capture.contains("first-second-complete"));CHECK_EQ(log(first,2,"complete"),0u);
    CHECK(capture.contains("first-complete"));CHECK(capture.contains("second-complete"));
}
