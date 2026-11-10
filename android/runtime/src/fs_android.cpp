// SPDX-License-Identifier: GPL-3.0-or-later
// Android read-only RomFS/storage adaptation of Goldoire nxrt fs.cpp (a538375a).
// Save-data, DLC and writable filesystem commands remain explicit missing commands.
#include "nxrt/android_services.h"
#include "nxrt/config.h"
#include "nxrt/log.h"
#include "fs_romfs_android.h"
#include <mutex>
namespace nxrt::services {
namespace {
constexpr uint32_t path_missing=0x7D402, invalid_argument=0x2EE202, out_of_range=0x177A02, read_only=0x313802, failure=0x3E802;
struct State {
    std::mutex lock;
    std::shared_ptr<fs::RomfsImage> image;
    uint64_t pid=0;
    uint32_t log_mode=0;
    bool auto_create_save=true;
};
std::shared_ptr<ipc::Service> storage(std::shared_ptr<fs::RomfsImage> image) {
    return make_iface("nn::fssrv::sf::IStorage",[image](ipc::Service& service) {
        service.add_cmd(0,"Read",[image](ipc::Request& req,ipc::Response&)->uint32_t {
            const auto offset=req.arg<int64_t>(0),length=req.arg<int64_t>(8);const auto out=req.out_buf(0);
            if(offset<0 || length<0 || static_cast<uint64_t>(offset)>image->size() || static_cast<uint64_t>(length)>image->size()-static_cast<uint64_t>(offset)) return out_of_range;
            if(!length) return 0;
            if(!out.addr || static_cast<uint64_t>(length)>out.size) return invalid_argument;
            return image->read(static_cast<uint64_t>(offset),reinterpret_cast<void*>(out.addr),static_cast<uint64_t>(length))?0:failure;
        });
        service.add_cmd(1,"Write",[](ipc::Request&,ipc::Response&)->uint32_t { return read_only; });
        service.add_cmd(2,"Flush",[](ipc::Request&,ipc::Response&)->uint32_t { return 0; });
        service.add_cmd(3,"SetSize",[](ipc::Request&,ipc::Response&)->uint32_t { return read_only; });
        service.add_cmd(4,"GetSize",[image](ipc::Request&,ipc::Response& rsp)->uint32_t { rsp.push<int64_t>(image->size());return 0; });
    });
}
}
void register_android_fs() {
    auto state=std::make_shared<State>();
    add("fsp-srv",[state] {return make_iface("fsp-srv",[state](ipc::Service& service) {
        service.add_cmd(1,"SetCurrentProcess",[state](ipc::Request& req,ipc::Response&)->uint32_t { std::lock_guard guard(state->lock);state->pid=req.arg<uint64_t>(0);return 0; });
        for(auto id:{200u,205u}) service.add_cmd(id,"OpenDataStorageByCurrentProcess",[state](ipc::Request&,ipc::Response& rsp)->uint32_t {
            std::lock_guard guard(state->lock);
            if(!state->image) {
                const auto path=config_get_str("fs","romfs","");auto image=std::make_shared<fs::RomfsImage>();std::string error;
                if(path.empty() || !image->build(path,&error)) { NXLOG_E("fs","Android RomFS folder unavailable: %s",error.c_str());return path_missing; }
                state->image=std::move(image);
            }
            rsp.push_object(storage(state->image));return 0;
        });
        service.add_cmd(1003,"DisableAutoSaveDataCreation",[state](ipc::Request&,ipc::Response&)->uint32_t { std::lock_guard guard(state->lock);state->auto_create_save=false;return 0; });
        service.add_cmd(1004,"SetGlobalAccessLogMode",[state](ipc::Request& req,ipc::Response&)->uint32_t { std::lock_guard guard(state->lock);const auto mode=req.arg<uint32_t>(0);if(mode>3)return invalid_argument;state->log_mode=mode;return 0; });
        service.add_cmd(1005,"GetGlobalAccessLogMode",[state](ipc::Request&,ipc::Response& rsp)->uint32_t { std::lock_guard guard(state->lock);rsp.push(state->log_mode);return 0; });
        service.add_cmd(1011,"GetProgramIndexForAccessLog",[](ipc::Request&,ipc::Response& rsp)->uint32_t { rsp.push<uint32_t>(0);rsp.push<uint32_t>(0);return 0; });
    });});
}
}
