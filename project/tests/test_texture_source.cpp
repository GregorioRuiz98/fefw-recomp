// SPDX-License-Identifier: GPL-3.0-or-later
// Synthetic reproduction of a source page disappearing before CPU texture untile.
#include <windows.h>
#include <algorithm>
#include "nvn_texdata.h"
#include "nvn_texture_layout.h"
#include "test_framework.h"
NXTEST_GROUP(texture_source)
NXTEST(unavailable_source_pages_return_failure_instead_of_crashing) {
    using namespace nxrt::nvn::be;
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    Texture texture;texture.b.target=NVN_TEXTURE_TARGET_2D;texture.b.format=NVN_FORMAT_RGBA8;
    texture.b.width=64;texture.b.height=32;texture.b.depth=1;texture.b.levels=1;
    const auto layout=compute_texture_layout(texture.b);REQUIRE(layout.valid&&layout.storage_size>=8192);
    struct Allocation {uint8_t* p=nullptr;~Allocation(){if(p) VirtualFree(p,0,MEM_RELEASE);}} storage;
    storage.p=static_cast<uint8_t*>(VirtualAlloc(nullptr,layout.storage_size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));REQUIRE(storage.p!=nullptr);
    std::memset(storage.p,0x42,layout.storage_size);texture.mem=storage.p;texture.storage_size=layout.storage_size;texture.fmt=layout.fmt;texture.block_linear=layout.block_linear;texture.levels=layout.levels;texture.layers=layout.layers;
    PreparedTexture output;TexCaps caps;
    REQUIRE(prepare_texture(texture,caps,output));CHECK(!output.data.empty()&&std::all_of(output.data.begin(),output.data.end(),[](uint8_t b){return b==0x42;}));
    DWORD previous=0;REQUIRE(VirtualProtect(storage.p+4096,4096,PAGE_NOACCESS,&previous)!=0);
    CHECK(!prepare_texture(texture,caps,output));CHECK(output.data.empty());CHECK(output.error.find("source storage")!=std::string::npos);
    REQUIRE(VirtualFree(storage.p,layout.storage_size,MEM_DECOMMIT)!=0);
    CHECK(!prepare_texture(texture,caps,output));CHECK(output.data.empty());
}
