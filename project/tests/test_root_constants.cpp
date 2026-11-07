#include <array>
#include <cmath>
#include <limits>
#include "nvn_app.h"
#include "nxrt/config.h"
#include "nxrt/dispatch.h"
#include "nxrt/game.h"

namespace nxrt { void install_switch2_hle(); }

NXTEST_GROUP(root_constants)
using namespace nvnapp;

NXTEST(host_display_query_initializes_sdr_outputs_without_overwrite) {
    const auto* previous = nxrt::game::current();
    nxrt::game::Info info;
    info.platform = nxrt::game::Platform::Switch2;
    nxrt::game::set_current(&info);
    nxrt::install_switch2_hle();
    nxrt::game::set_current(previous);
    auto open = nxrt::hle_lookup("_ZN2nn2vi18OpenDefaultDisplayEPPNS0_7DisplayE");
    auto query = nxrt::hle_lookup("_ZN2nn2vi21QueryDisplayHdrStatusEPbPNS0_15HdrOutputFormatEPtPKNS0_7DisplayE");
    REQUIRE(open && query);
    uint64_t display = 0;
    Ctx ctx{};
    ctx.x[0] = reinterpret_cast<uint64_t>(&display);
    open(&ctx);
    REQUIRE(display != 0);
    std::array<uint8_t, 32> data;
    data.fill(0xcd);
    auto invoke = [&](uint64_t handle) {
        ctx.x[0] = reinterpret_cast<uint64_t>(data.data() + 4);
        ctx.x[1] = reinterpret_cast<uint64_t>(data.data() + 8);
        ctx.x[2] = reinterpret_cast<uint64_t>(data.data() + 16);
        ctx.x[3] = handle;
        query(&ctx);
    };
    invoke(display);
    CHECK_EQ(ctx.x[0], 0ull);
    for (size_t i = 0; i < data.size(); ++i) {
        const bool output = i == 4 || (i >= 8 && i < 12) || (i >= 16 && i < 18);
        CHECK_EQ(data[i], uint8_t(output ? 0 : 0xcd));
    }
    data.fill(0xcd);
    invoke(0);
    CHECK_EQ(ctx.x[0], 0x272ull);
    for (auto byte : data) CHECK_EQ(byte, uint8_t(0xcd));
}

NXTEST(recorded_partial_updates_and_compute_table_offsets) {
    const std::string dir = FEFW_ARRAY_CACHE_DIR "/../root-constants";
    _putenv_s("NXRT_NVN_SHADER_CACHE", dir.c_str());
    _putenv_s("NXRT_NVN_PIPELINE_CACHE", "");
    App app;
    AppOpts opts; opts.width = opts.height = 16;
    REQUIRE(app.init(opts));
    App::ShaderBlob blob;
    auto read = [&](const char* name, std::vector<uint8_t>& dst) {
        std::ifstream f(dir + name, std::ios::binary);
        dst.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        REQUIRE(!dst.empty());
    };
    read("/code.bin", blob.code); read("/ctrl.bin", blob.control);
    auto program = app.make_program({&blob}); REQUIRE(program != nullptr);
    Caller<void(NVNcommandBuffer*, int, int, int, const void*)> compute(nxrt::nvn::proc_address("nvnCommandBufferPushComputeConstants"));
    Caller<void(NVNcommandBuffer*, int, int, int, const void*)> graphics(nxrt::nvn::proc_address("nvnCommandBufferPushGraphicsConstants"));
    auto a = app.make_buffer(256), b = app.make_buffer(256);
    std::memset(a.ptr, 0xcd, a.size); std::memset(b.ptr, 0xcd, b.size);
    auto commands = app.make_cmdbuf(); REQUIRE(commands != nullptr);
    auto* cmd = commands->obj.get();
    app.api.nvnCommandBufferBeginRecording(cmd);
    app.api.nvnCommandBufferBindProgram(cmd, program.get(), NVN_SHADER_STAGE_COMPUTE_BIT);
    std::array<uint32_t, 4> values{11, 22, 33, 44};
    compute(cmd, 0, 16, 16, values.data());
    values.fill(999); // the recorder must own its snapshot
    graphics(cmd, 0, 16, 16, values.data());
    app.api.nvnCommandBufferBindStorageBuffer(cmd, NVN_SHADER_STAGE_COMPUTE, 0, a.address, a.size);
    app.api.nvnCommandBufferDispatchCompute(cmd, 1, 1, 1);
    uint32_t replacement = 77;
    compute(cmd, 0, 20, 4, &replacement);
    compute(cmd, 0, -1, 4, &replacement); // invalid ranges must not overwrite state
    compute(cmd, 8, 20, 4, &replacement);
    compute(cmd, 0, 1022, 4, &replacement);
    app.api.nvnCommandBufferBindStorageBuffer(cmd, NVN_SHADER_STAGE_COMPUTE, 0, b.address, b.size);
    app.api.nvnCommandBufferDispatchCompute(cmd, 1, 1, 1);
    app.end_and_submit(*commands); app.finish();
    for (int pass = 0; pass < 2; ++pass) {
        const auto& out = pass ? b : a;
        const uint32_t expected[] = {11, pass ? 77u : 22u, 33, 44};
        for (int block = 0; block < 2; ++block)
            for (int i = 0; i < 4; ++i)
                CHECK_EQ(reinterpret_cast<const uint32_t*>(out.ptr)[block * 4 + i], expected[i]);
        for (size_t i = 32; i < out.size; ++i) CHECK_EQ(out.ptr[i], uint8_t(0xcd));
    }
    CHECK_EQ(app.validation().errors, 0ull);
    std::printf("    NVN2 root constants: 16 GPU words, snapshots, partial updates, graphics isolation and shifted storage binding\n");
    // Observe backend-owned allocations while the Vulkan device still exists:
    // the validation layer reports leaks at vkDestroyDevice, after a normal
    // in-test validation() check would already have passed.
    nxrt::nvn::shutdown_backend();
    app.queue.reset();
    app.rend.reset();
    CHECK_EQ(app.dev->memory_stats().allocations, 0ull);
}

NXTEST(spatial_diagnostic_copy_and_disabled_path) {
    App app; AppOpts opts; opts.width = opts.height = 16;
    REQUIRE(app.init(opts));
    auto input = app.make_texture_2d(NVN_FORMAT_RGBA8, 2, 2);
    auto output = app.make_texture_2d(NVN_FORMAT_RGBA8, 4, 4);
    REQUIRE(input && output);
    const NVNcopyRegion src{0, 0, 0, 2, 2, 1}, dst{0, 0, 0, 4, 4, 1};
    std::array<uint8_t, 16> red{255,0,0,255,255,0,0,255,255,0,0,255,255,0,0,255};
    app.api.nvnTextureWriteTexels(input->obj.get(), nullptr, &src, red.data());
    app.api.nvnTextureFlushTexels(input->obj.get(), nullptr, &src);
    Caller<void(void*)> defaults(nxrt::nvn::proc_address("nvnDlssParametersSetDefaults"));
    Caller<void(void*, const NVNtexture*, const NVNtextureView*)> set_input(nxrt::nvn::proc_address("nvnDlssParametersSetInputColor")), set_output(nxrt::nvn::proc_address("nvnDlssParametersSetOutputColor"));
    Caller<void(void*, int, int)> size(nxrt::nvn::proc_address("nvnDlssParametersSetInputSubrectangleSize"));
    Caller<void(void*, float, float)> jitter(nxrt::nvn::proc_address("nvnDlssParametersSetJitterOffset"));
    Caller<void(NVNcommandBuffer*, const void*, const void*)> evaluate(nxrt::nvn::proc_address("nvnCommandBufferEvaluateDlss"));
    uint64_t params = 0;
    for (int pass = 0; pass < 5; ++pass) {
        nxrt::config_set("nvn", "dlss_spatial_fallback", pass ? "1" : "0");
        defaults(&params); set_input(&params, input->obj.get(), nullptr); set_output(&params, output->obj.get(), nullptr);
        size(&params, pass == 2 ? -1 : 2, 2);
        if (pass == 3) jitter(&params, std::numeric_limits<float>::quiet_NaN(), 0);
        if (pass == 4) jitter(&params, 2, 0);
        auto commands = app.make_cmdbuf(); REQUIRE(commands != nullptr);
        auto* cmd = commands->obj.get();
        app.api.nvnCommandBufferBeginRecording(cmd);
        const float green[4] = {0, 1, 0, 1};
        app.api.nvnCommandBufferClearTexture(cmd, output->obj.get(), nullptr, &dst, green, 15);
        evaluate(cmd, nullptr, &params);
        defaults(&params); // changing the parameter object must not change recorded work
        app.end_and_submit(*commands); app.finish();
        std::array<uint8_t, 64> pixels{};
        app.api.nvnTextureReadTexels(output->obj.get(), nullptr, &dst, pixels.data());
        for (int i = 0; i < 16; ++i) {
            CHECK_EQ(pixels[i*4], pass == 1 ? 255 : 0);
            CHECK_EQ(pixels[i*4+1], pass == 1 ? 0 : 255);
            CHECK_EQ(pixels[i*4+2], 0); CHECK_EQ(pixels[i*4+3], 255);
        }
    }
    nxrt::config_set("nvn", "dlss_spatial_fallback", "0");
    CHECK_EQ(app.validation().errors, 0ull);
}

NXTEST(spatial_jitter_preserves_stationary_gradient) {
    App app; AppOpts opts; opts.width = opts.height = 16;
    REQUIRE(app.init(opts));
    auto input = app.make_texture_2d(NVN_FORMAT_RGBA8, 10, 10);
    auto output = app.make_texture_2d(NVN_FORMAT_RGBA8, 8, 8);
    REQUIRE(input && output);
    const NVNcopyRegion src{0,0,0,10,10,1}, dst{0,0,0,8,8,1};
    Caller<void(void*)> defaults(nxrt::nvn::proc_address("nvnDlssParametersSetDefaults"));
    Caller<void(void*, const NVNtexture*, const NVNtextureView*)> in(nxrt::nvn::proc_address("nvnDlssParametersSetInputColor")), out(nxrt::nvn::proc_address("nvnDlssParametersSetOutputColor"));
    Caller<void(void*, int, int)> size(nxrt::nvn::proc_address("nvnDlssParametersSetInputSubrectangleSize")), base(nxrt::nvn::proc_address("nvnDlssParametersSetInputColorSubrectangleBase"));
    Caller<void(void*, float, float)> jitter(nxrt::nvn::proc_address("nvnDlssParametersSetJitterOffset"));
    Caller<void(NVNcommandBuffer*, const void*, const void*)> evaluate(nxrt::nvn::proc_address("nvnCommandBufferEvaluateDlss"));
    nxrt::config_set("nvn", "dlss_spatial_fallback", "1");
    nxrt::config_set("nvn", "dlss_dejitter", "1");
    uint64_t params = 0;
    for (int phase : {-1, 0, 1}) {
        // A stationary linear scene rendered at camera offsets (+/-0.25,+/-0.5).
        // Subrectangle borders are excluded: clamping cannot reconstruct missing pixels.
        std::array<uint8_t, 400> pixels{};
        for (int y = 0; y < 10; ++y) for (int x = 0; x < 10; ++x) {
            pixels[(y*10+x)*4] = static_cast<uint8_t>(12 + x*16 + y*8 - phase*8);
            pixels[(y*10+x)*4+3] = 255;
        }
        app.api.nvnTextureWriteTexels(input->obj.get(), nullptr, &src, pixels.data());
        app.api.nvnTextureFlushTexels(input->obj.get(), nullptr, &src);
        defaults(&params); in(&params, input->obj.get(), nullptr); out(&params, output->obj.get(), nullptr);
        size(&params, 6, 6); base(&params, 2, 2); jitter(&params, phase*0.25f, phase*0.5f);
        auto commands = app.make_cmdbuf(); REQUIRE(commands != nullptr);
        app.api.nvnCommandBufferBeginRecording(commands->obj.get());
        evaluate(commands->obj.get(), nullptr, &params);
        defaults(&params); // shader constants must be recorded by value too
        app.end_and_submit(*commands); app.finish();
        std::array<uint8_t, 256> result{};
        app.api.nvnTextureReadTexels(output->obj.get(), nullptr, &dst, result.data());
        for (int y = 1; y < 7; ++y) for (int x = 1; x < 7; ++x) {
            const float sample_x = 2 + (x+0.5f)*6/8 - 0.5f;
            const float sample_y = 2 + (y+0.5f)*6/8 - 0.5f;
            const float expected = 12 + sample_x*16 + sample_y*8;
            CHECK(std::abs(static_cast<float>(result[(y*8+x)*4]) - expected) <= 1.1f);
            CHECK_EQ(result[(y*8+x)*4+3], 255);
        }
    }
    nxrt::config_set("nvn", "dlss_spatial_fallback", "0");
    CHECK_EQ(app.validation().errors, 0ull);
}
