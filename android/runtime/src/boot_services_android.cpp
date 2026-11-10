// SPDX-License-Identifier: GPL-3.0-or-later
// SDK startup services adapted from Goldoire nxrt network.cpp/misc.cpp (a538375a).
// PCTL represents an unrestricted local application; no account/network requests are sent.
#include "nxrt/android_services.h"
#include "nxrt/game.h"
#include <sys/random.h>
#include <cerrno>
namespace nxrt::services {
namespace {
std::shared_ptr<ipc::Service> pctl() {
    return make_iface("nn::pctl::detail::ipc::IParentalControlService", [](ipc::Service& s) {
        for (auto id : {1u,1001u,1002u,1003u,1017u,1019u,1020u})
            s.add_cmd(id,"InitializeOrConfirmPermission",[](ipc::Request&,ipc::Response&) -> uint32_t { return 0; });
        s.add_cmd(1031,"IsRestrictionEnabled",[](ipc::Request&,ipc::Response& rsp)->uint32_t { rsp.push<uint8_t>(0); return 0; });
        s.add_cmd(1018,"IsFreeCommunicationAvailable",[](ipc::Request&,ipc::Response& rsp)->uint32_t { rsp.push<uint8_t>(1); return 0; });
    });
}
}
void register_android_boot_services() {
    add("csrng", [] { return make_iface("csrng",[](ipc::Service& s) {
        s.add_cmd(0,"GenerateRandomBytes",[](ipc::Request& req,ipc::Response&)->uint32_t {
            const auto output=req.out_buf(0);auto* data=reinterpret_cast<uint8_t*>(output.addr);uint64_t remaining=output.size;
            while(remaining) {
                const auto size=static_cast<size_t>(std::min<uint64_t>(remaining,262144));
                const auto got=getrandom(data,size,0);
                if(got<0 && errno==EINTR)continue;
                if(got<=0)return kernel::kInvalidState;
                data+=got;remaining-=static_cast<uint64_t>(got);
            }
            return 0;
        });
    }); });
    // This bootstrap title profile has no installed DLC. Mounting DLC remains unsupported.
    auto changed=std::make_shared<ServiceEvent>();
    add("aoc:u", [changed] { return make_iface("IAddOnContentManager", [changed](ipc::Service& s) {
        for(auto id : {0u,1u,2u,3u})
            s.add_cmd(id,"CountOrListAddOnContent",[](ipc::Request&,ipc::Response& rsp)->uint32_t { rsp.push<int32_t>(0);return 0; });
        s.add_cmd(5,"GetAddOnContentBaseId",[](ipc::Request&,ipc::Response& rsp)->uint32_t { rsp.push<uint64_t>(game::info().program_id+0x1000);return 0; });
        for(auto id : {8u,10u}) s.add_cmd(id,"GetAddOnContentListChangedEvent",[changed](ipc::Request&,ipc::Response& rsp)->uint32_t { rsp.out_copy.push_back(changed->new_handle());return 0; });
    }); });
    for (auto name : {"pctl","pctl:a","pctl:s","pctl:r"}) add(name, [] {
        return make_iface("nn::pctl::detail::ipc::IParentalControlServiceFactory",[](ipc::Service& s) {
            for(auto id : {0u,1u}) s.add_cmd(id,"CreateService",[](ipc::Request&,ipc::Response& rsp)->uint32_t { rsp.push_object(pctl()); return 0; });
        });
    });
    add("ectx:aw", [] { return make_iface("nn::err::context::IWriterForApplication",[](ipc::Service& s) {
        s.add_cmd(0,"CreateContextRegistrar",[](ipc::Request&,ipc::Response& rsp)->uint32_t {
            rsp.push_object(make_iface("nn::err::context::IContextRegistrar",[](ipc::Service& registrar) {
                registrar.add_cmd(0,"Complete",[](ipc::Request&,ipc::Response& output)->uint32_t { output.push<int32_t>(0); return 0; });
            })); return 0;
        });
    }); });
}
}
