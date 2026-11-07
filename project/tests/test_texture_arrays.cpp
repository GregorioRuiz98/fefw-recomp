#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include "nvn_app.h"
#include "nvn_pipeline.h"

NXTEST_GROUP(texture_arrays)
using namespace nvnapp;

NXTEST(all_32_texture_handles_and_rebinding_through_nvn_replay) {
    const std::string dir = FEFW_ARRAY_CACHE_DIR;
    _putenv_s("NXRT_NVN_SHADER_CACHE", dir.c_str());
    _putenv_s("NXRT_NVN_PIPELINE_CACHE", "");
    App app;
    AppOpts opts;
    opts.width = 16;
    opts.height = 16;
    REQUIRE(app.init(opts));
    std::printf("    Vulkan validation layer: %s\n", app.dev->validation_enabled() ? "enabled" : "unavailable");
    App::ShaderBlob blob;
    auto read = [](const std::string& path, std::vector<uint8_t>& dst) {
        std::ifstream f(path, std::ios::binary);
        dst.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        return !dst.empty();
    };
    REQUIRE(read(dir + "/code.bin", blob.code));
    REQUIRE(read(dir + "/ctrl.bin", blob.control));
    auto program = app.make_program({&blob});
    REQUIRE(program != nullptr);
    std::vector<std::unique_ptr<Tex>> cubes;
    auto option = [](const char* name, int fallback) { const char* value = std::getenv(name); return value ? std::atoi(value) : fallback; };
    const bool pooled = option("FEFW_POOL_ARRAY", 0) != 0;
    const int literal = option("FEFW_LITERAL_TEXTURE", -1);
    const int bank = option("FEFW_ARRAY_BANK", 6);
    const int base = option("FEFW_ARRAY_BASE", 0x340);
    const bool cube = option("FEFW_ARRAY_CUBE", 1) != 0;
    REQUIRE(bank >= 3 && bank <= 18);
    REQUIRE(base >= 0 && base <= 0x1000 && base % 8 == 0);
    const size_t table_start = size_t(base) / 8;
    std::vector<uint64_t> handles(table_start + 32), reversed(table_start + 32);
    std::unique_ptr<NVNtexturePool> alternate_pool;
    if (pooled) {
        const size_t texture_offset = app.pool_main->take(32768 * 32, 256);
        const size_t sampler_offset = app.pool_main->take(512 * 32, 256);
        REQUIRE(app.api.nvnTexturePoolInitialize(app.tex_pool.get(), app.pool_main->obj.get(), texture_offset, 32768));
        REQUIRE(app.api.nvnSamplerPoolInitialize(app.smp_pool.get(), app.pool_main->obj.get(), sampler_offset, 512));
        alternate_pool = std::make_unique<NVNtexturePool>();
        const size_t other_offset = app.pool_main->take(32768 * 32, 256);
        REQUIRE(app.api.nvnTexturePoolInitialize(alternate_pool.get(), app.pool_main->obj.get(), other_offset, 32768));
    }
    // The prefix is not part of the handle table; each case selects its own base.
    if ((literal == 7 || pooled) && !cube) {
        const int unused_sampler = app.make_sampler([&](NVNsamplerBuilder& builder) {
            app.api.nvnSamplerBuilderSetMinMagFilter(&builder, NVN_MIN_FILTER_NEAREST, NVN_MAG_FILTER_NEAREST);
            app.api.nvnSamplerBuilderSetWrapMode(&builder, NVN_WRAP_MODE_CLAMP_TO_BORDER, NVN_WRAP_MODE_CLAMP_TO_BORDER, NVN_WRAP_MODE_CLAMP_TO_BORDER);
            const float border[4] = {};
            app.api.nvnSamplerBuilderSetBorderColor(&builder, border);
        });
        REQUIRE(unused_sampler == 0);
    }
    if (pooled) app.next_smp_id = 511;
    const int sampler = app.make_nearest_sampler();
    NVNtextureView cube_view;
    app.api.nvnTextureViewSetDefaults(&cube_view);
    app.api.nvnTextureViewSetTarget(&cube_view, NVN_TEXTURE_TARGET_CUBEMAP);
    for (int i = 0; i < 32; ++i) {
        auto t = std::make_unique<Tex>();
        t->obj = std::make_unique<NVNtexture>();
        NVNtextureBuilder builder;
        app.api.nvnTextureBuilderSetDevice(&builder, app.device.get());
        app.api.nvnTextureBuilderSetDefaults(&builder);
        app.api.nvnTextureBuilderSetTarget(&builder, cube ? (pooled ? NVN_TEXTURE_TARGET_2D_ARRAY : NVN_TEXTURE_TARGET_CUBEMAP) : NVN_TEXTURE_TARGET_2D);
        if (cube && pooled) app.api.nvnTextureBuilderSetSize3D(&builder, 1, 1, 6);
        else app.api.nvnTextureBuilderSetSize2D(&builder, 1, 1);
        app.api.nvnTextureBuilderSetFormat(&builder, NVN_FORMAT_RGBA8);
        app.api.nvnTextureBuilderSetLevels(&builder, 1);
        t->size = app.api.nvnTextureBuilderGetStorageSize(&builder);
        t->offset = app.pool_main->take(t->size, std::max<size_t>(256, app.api.nvnTextureBuilderGetStorageAlignment(&builder)));
        t->pool = app.pool_main.get();
        app.api.nvnTextureBuilderSetStorage(&builder, t->pool->obj.get(), t->offset);
        REQUIRE(app.api.nvnTextureInitialize(t->obj.get(), &builder));
        t->id = pooled ? (i == 31 ? 32767 : (i == 3 ? 4095 : i * 1024)) : app.next_tex_id++;
        app.api.nvnTexturePoolRegisterTexture(app.tex_pool.get(), t->id, t->obj.get(), pooled && cube ? &cube_view : nullptr);
        std::array<uint8_t, 24> pixels{};
        for (int face = 0; face < 6; ++face) {
            pixels[4 * face] = uint8_t(8 * i);
            pixels[4 * face + 1] = uint8_t(255 - 8 * i);
            pixels[4 * face + 2] = 64;
            pixels[4 * face + 3] = 255;
        }
        NVNcopyRegion region{0, 0, 0, 1, 1, cube ? 6 : 1};
        app.api.nvnTextureWriteTexels(t->obj.get(), nullptr, &region, pixels.data());
        handles[table_start + i] = app.texture_handle(t->id, sampler);
        cubes.push_back(std::move(t));
    }
    if (pooled) for (int i = 0; i < 32; ++i)
        app.api.nvnTexturePoolRegisterTexture(alternate_pool.get(), cubes[i]->id, cubes[31-i]->obj.get(), cube ? &cube_view : nullptr);
    for (int i = 0; i < 32; ++i) reversed[table_start + i] = handles[table_start + 31 - i];
    const auto uniform_a = app.make_buffer(handles.size() * sizeof(uint64_t), handles.data());
    const auto uniform_b = app.make_buffer(reversed.size() * sizeof(uint64_t), reversed.data());
    auto out_a = app.make_buffer(32 * 16 + 256);
    auto out_b = app.make_buffer(32 * 16 + 256);
    std::memset(out_a.ptr, 0xcd, out_a.size);
    std::memset(out_b.ptr, 0xcd, out_b.size);
    auto commands = app.make_cmdbuf();
    REQUIRE(commands != nullptr);
    auto* cmd = commands->obj.get();
    app.api.nvnCommandBufferBeginRecording(cmd);
    app.api.nvnCommandBufferSetTexturePool(cmd, app.tex_pool.get());
    app.api.nvnCommandBufferSetSamplerPool(cmd, app.smp_pool.get());
    app.api.nvnCommandBufferBindProgram(cmd, program.get(), NVN_SHADER_STAGE_COMPUTE_BIT);
    for (int pass = 0; pass < 2; ++pass) {
        const auto& u = pass && !pooled ? uniform_b : uniform_a;
        if (pooled) app.api.nvnCommandBufferSetTexturePool(cmd, pass ? alternate_pool.get() : app.tex_pool.get());
        const auto& output = pass ? out_b : out_a;
        app.api.nvnCommandBufferBindUniformBuffer(cmd, NVN_SHADER_STAGE_COMPUTE, bank - 3, u.address, u.size);
        app.api.nvnCommandBufferBindStorageBuffer(cmd, NVN_SHADER_STAGE_COMPUTE, 0, output.address, output.size);
        app.api.nvnCommandBufferDispatchCompute(cmd, pooled ? 1 : 32, 1, 1);
    }
    NVNtexturePool oversized_texture{};
    NVNsamplerPool oversized_sampler{};
    App::Buf rejected;
    if (pooled) {
        rejected = app.make_buffer(768);
        std::memset(rejected.ptr, 0xcd, rejected.size);
        REQUIRE(app.api.nvnTexturePoolInitialize(&oversized_texture, app.pool_main->obj.get(), app.pool_main->take(32769 * 32, 256), 32769));
        REQUIRE(app.api.nvnSamplerPoolInitialize(&oversized_sampler, app.pool_main->obj.get(), app.pool_main->take(513 * 32, 256), 513));
        app.api.nvnCommandBufferBindStorageBuffer(cmd, NVN_SHADER_STAGE_COMPUTE, 0, rejected.address, rejected.size);
        app.api.nvnCommandBufferSetTexturePool(cmd, &oversized_texture);
        app.api.nvnCommandBufferDispatchCompute(cmd, 1, 1, 1);
        app.api.nvnCommandBufferSetTexturePool(cmd, app.tex_pool.get());
        app.api.nvnCommandBufferSetSamplerPool(cmd, &oversized_sampler);
        app.api.nvnCommandBufferDispatchCompute(cmd, 1, 1, 1);
    }
    app.end_and_submit(*commands);
    app.finish();
    for (int pass = 0; pass < 2; ++pass) {
        const auto& output = pass ? out_b : out_a;
        for (int i = 0; i < 32; ++i) {
            const float* rgba = reinterpret_cast<const float*>(output.ptr + 16 * i);
            const int index = literal >= 0 ? literal : (pass ? 31 - i : i);
            CHECK(std::isfinite(rgba[0]) && std::abs(rgba[0] - 8 * index / 255.0f) < 0.00001f);
            CHECK(std::isfinite(rgba[1]) && std::abs(rgba[1] - (255 - 8 * index) / 255.0f) < 0.00001f);
            CHECK(std::isfinite(rgba[2]) && std::abs(rgba[2] - 64 / 255.0f) < 0.00001f);
            CHECK_EQ(rgba[3], 1.0f);
        }
        for (size_t i = 512; i < output.size; ++i) CHECK_EQ(output.ptr[i], uint8_t(0xcd));
    }
    if (pooled) {
        for (size_t i = 0; i < rejected.size; ++i) CHECK_EQ(rejected.ptr[i], uint8_t(0xcd));
        app.api.nvnTexturePoolFinalize(&oversized_texture);
        app.api.nvnSamplerPoolFinalize(&oversized_sampler);
    }
    if (alternate_pool) app.api.nvnTexturePoolFinalize(alternate_pool.get());
    const auto stats = nxrt::nvn::backend_stats();
    CHECK_EQ(stats.dispatches, 2ull);
    CHECK_EQ(stats.textures_failed, 0ull);
    CHECK_EQ(app.validation().errors, 0ull);
    // Unsupported contracts must still fail before any Vulkan layout or incomplete descriptors are created.
    nxrt::nvn::be::ShaderInfo bad;
    bad.stage = nxrt::nvn::be::ShaderStage::Compute;
    nxrt::nvn::be::DescriptorBinding binding;
    binding.set = 2;
    binding.binding = 640;
    binding.used = true;
    binding.type = nxrt::nvn::be::DescriptorType::CombinedImageSampler;
    binding.nvn.source = nxrt::nvn::be::NvnSource::HandleInUniform;
    binding.nvn.index = 3;
    binding.nvn.bank = 6;
    binding.nvn.word = 208;
    bad.refl.bindings.push_back(binding);
    const nxrt::nvn::be::ShaderInfo* stages[6] = {nullptr, nullptr, nullptr, nullptr, nullptr, &bad};
    for (uint32_t count : {0u, 33u, 4096u}) {
        bad.refl.bindings[0].count = count;
        CHECK(nxrt::nvn::be::gpu::prog_layout(stages) == nullptr);
    }
    bad.refl.bindings[0].count = 32;
    bad.refl.bindings[0].nvn.source = nxrt::nvn::be::NvnSource::Unknown;
    CHECK(nxrt::nvn::be::gpu::prog_layout(stages) == nullptr);
    std::printf("    64 texture samples through NVN recording/replay: all array elements, table base, reversed rebind, guards\n");
}
