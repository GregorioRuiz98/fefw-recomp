// SPDX-License-Identifier: GPL-3.0-or-later
// Contracts from Goldoire v0.1.5, preserving our integer compute shader variants.
#include "nvn_app.h"
#include "nvn_gpu_internal.h"
NXTEST_GROUP(fefw_render_portability)
using namespace nvnapp;
namespace gpu=nxrt::nvn::be::gpu;
NXTEST(downsample_builders_keep_nonzero_storage_dimensions) {
    App app;AppOpts opts;opts.width=opts.height=16;REQUIRE(app.init(opts));
    NVNtextureBuilder b{};app.api.nvnTextureBuilderSetDevice(&b,app.device.get());app.api.nvnTextureBuilderSetDefaults(&b);
    app.api.nvnTextureBuilderSetSize2D(&b,0,0);
    CHECK_EQ(app.api.nvnTextureBuilderGetWidth(&b),1);CHECK_EQ(app.api.nvnTextureBuilderGetHeight(&b),1);
    app.api.nvnTextureBuilderSetWidth(&b,-1);app.api.nvnTextureBuilderSetHeight(&b,0);
    CHECK_EQ(app.api.nvnTextureBuilderGetWidth(&b),1);CHECK_EQ(app.api.nvnTextureBuilderGetHeight(&b),1);
    app.api.nvnTextureBuilderSetSize3D(&b,0,-1,1);
    CHECK_EQ(app.api.nvnTextureBuilderGetWidth(&b),1);CHECK_EQ(app.api.nvnTextureBuilderGetHeight(&b),1);
    app.api.nvnTextureBuilderSetSize1D(&b,0);CHECK_EQ(app.api.nvnTextureBuilderGetWidth(&b),1);
    app.finish();CHECK_EQ(app.validation().errors,0ull);
}
NXTEST(integer_sampler_rejects_normalized_storage_without_changing_float_pool_compatibility) {
    App app;AppOpts opts;opts.width=opts.height=16;REQUIRE(app.init(opts));
    auto texture=app.make_texture_2d(NVN_FORMAT_RGBA8,4,4);REQUIRE(texture!=nullptr);
    bool rejected=false,matched=false;
    gpu::run_frame_sync([&](gpu::Frame&) {
        auto* t=nxrt::nvn::be::host_of<nxrt::nvn::be::Texture>(texture->obj.get());
        gpu::ViewRequest request;request.component=3;
        rejected=gpu::tex_sampled_view(t,request)==VK_NULL_HANDLE;
        request.component=1;matched=gpu::tex_sampled_view(t,request)!=VK_NULL_HANDLE;
    });
    CHECK(rejected&&matched);
    app.finish();CHECK_EQ(app.validation().errors,0ull);
}
