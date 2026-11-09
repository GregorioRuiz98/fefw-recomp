// SPDX-License-Identifier: GPL-3.0-or-later
// Real IPC save-volume operations with synthetic slot data. No commercial saves.
#include <windows.h>
#include <filesystem>
#include "svc_test_util.h"
#include "nxrt/config.h"
#include "nxrt/services.h"
NXTEST_GROUP(fefw_save_reset)
NXTEST(delete_slot_is_visible_without_restarting_the_process) {
    namespace fs=std::filesystem;using nxsvc::Args;
    nxsvc::Env env;
    const auto root=fs::temp_directory_path()/("fefw-save-reset-"+std::to_string(GetCurrentProcessId()));
    fs::create_directories(root);nxrt::config_set("fs","save_dir",root.string().c_str());nxrt::services::reset_all();
    {
        auto fsp=nxsvc::connect("fsp-srv");REQUIRE(fsp.valid());
        std::vector<uint8_t> attr(0x40);attr[8]=3;attr[16]=4;attr[32]=1;
        Args creation;creation.bytes(attr.data(),attr.size()).i64(1<<20).i64(1<<18).u64(1<<20).u64(0).u32(0).u8(1).pad(0x1b).pad(0x10);
        REQUIRE(fsp.call(22,creation).ok());
        auto mount=fsp.call(51,Args().u8(1).pad(7).bytes(attr.data(),attr.size()));REQUIRE(mount.ok());auto volume=fsp.sub(mount);
        struct LowPath { char* p=static_cast<char*>(VirtualAlloc(reinterpret_cast<void*>(0x3000000000ull),4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)); ~LowPath(){if(p) VirtualFree(p,0,MEM_RELEASE);} } buffer;
        REQUIRE(buffer.p!=nullptr); // HIPC descriptors carry guest addresses below 39 bits.
        auto path=[&](uint32_t cmd,const char* p,Args a=Args{}) {std::memset(buffer.p,0,0x301);std::strncpy(buffer.p,p,0x300);a.buf_x(buffer.p,0x301);return volume.call(cmd,a);};
        REQUIRE(path(2,"/000").ok());REQUIRE(path(0,"/000/Auto",Args().u32(0).pad(4).i64(4)).ok());
        REQUIRE(path(0,"/000/Global",Args().u32(0).pad(4).i64(4)).ok());
        REQUIRE(volume.call(10).ok());
        CHECK(fs::exists(root/"account/03000000000000000400000000000000/000/Auto"));
        CHECK(path(7,"/000/Auto").ok());
        CHECK(path(8,"/000/Auto",Args().u32(1)).ok());
        REQUIRE(path(1,"/000/Auto").ok());
        CHECK_EQ(path(7,"/000/Auto").result,0x202u); // the same live mount must already see no slot
        CHECK_EQ(path(8,"/000/Auto",Args().u32(1)).result,0x202u); // loading a missing slot must report not-found
        CHECK(path(7,"/000/Global").ok());REQUIRE(volume.call(10).ok());
        auto reopened=fsp.call(51,Args().u8(1).pad(7).bytes(attr.data(),attr.size()));REQUIRE(reopened.ok());auto second=fsp.sub(reopened);
        std::strcpy(buffer.p,"/000/Auto");
        CHECK_EQ(second.call(7,Args().buf_x(buffer.p,0x301)).result,0x202u);
        CHECK_EQ(second.call(8,Args().u32(1).buf_x(buffer.p,0x301)).result,0x202u);
        CHECK(!fs::exists(root/"account/03000000000000000400000000000000/000/Auto"));
    }
    nxrt::config_clear();nxrt::services::reset_all();
    fs::remove_all(root);
}
