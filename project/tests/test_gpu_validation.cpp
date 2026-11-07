#include <array>
#include <chrono>
#include <cmath>
#include <thread>
#include "nvn_app.h"
#include "nvn_gpu_internal.h"
#include "nvn_pipeline.h"

NXTEST_GROUP(gpu_validation)
using namespace nvnapp;
namespace gpu = nxrt::nvn::be::gpu;

NXTEST(integer_texels_keep_extended_bits_and_other_bindings_stay_normalized) {
    const std::string dir = FEFW_ARRAY_CACHE_DIR "/../integer-texture";
    _putenv_s("NXRT_NVN_SHADER_CACHE", dir.c_str());
    _putenv_s("NXRT_NVN_PIPELINE_CACHE", "");
    App app; AppOpts opts; opts.width = opts.height = 16;
    REQUIRE(app.init(opts));
    // Validate the actual rewritten modules as well as their GPU outputs.
    {
        std::vector<uint32_t> words;
        std::ifstream file(dir + "/44ac31f3bc2799f0.spv",std::ios::binary|std::ios::ate);
        REQUIRE(file.good());
        words.resize(size_t(file.tellg())/4); file.seekg(0);
        file.read(reinterpret_cast<char*>(words.data()),std::streamsize(words.size()*4));
        for (bool sign : {false,true}) {
            std::vector<uint32_t> patched;
            REQUIRE(gpu::spirv_int_textures(words,{{2,0,sign}},patched));
            std::ofstream out(dir + (sign ? "/signed.spv" : "/unsigned.spv"),std::ios::binary);
            out.write(reinterpret_cast<const char*>(patched.data()),std::streamsize(patched.size()*4));
        }
    }
    App::ShaderBlob blob;
    for (const auto& name : {"code.bin", "ctrl.bin"}) {
        std::ifstream f(dir + "/" + name, std::ios::binary);
        auto& dst = name[0] == 'c' && name[1] == 'o' ? blob.code : blob.control;
        dst.assign(std::istreambuf_iterator<char>(f), {});
        REQUIRE(!dst.empty());
    }
    auto program = app.make_program({&blob}); REQUIRE(program != nullptr);
    auto uint_tex = app.make_texture_2d(NVN_FORMAT_RGBA8UI, 1, 1);
    auto sint_tex = app.make_texture_2d(NVN_FORMAT_RGBA8I, 1, 1);
    auto norm_tex = app.make_texture_2d(NVN_FORMAT_RGBA8, 1, 1);
    REQUIRE(uint_tex && sint_tex && norm_tex);
    const std::array<uint8_t,4> up{0,1,127,255}, np{0,85,170,255};
    const std::array<int8_t,4> ip{-128,-1,0,127};
    const NVNcopyRegion region{0,0,0,1,1,1};
    app.api.nvnTextureWriteTexels(uint_tex->obj.get(), nullptr, &region, up.data());
    app.api.nvnTextureWriteTexels(sint_tex->obj.get(), nullptr, &region, ip.data());
    app.api.nvnTextureWriteTexels(norm_tex->obj.get(), nullptr, &region, np.data());
    const int sampler = app.make_nearest_sampler();
    std::array<App::Buf,3> outputs{app.make_buffer(256),app.make_buffer(256),app.make_buffer(256)};
    auto cmd = app.make_cmdbuf(); REQUIRE(cmd != nullptr);
    app.api.nvnCommandBufferBeginRecording(cmd->obj.get());
    app.api.nvnCommandBufferSetTexturePool(cmd->obj.get(), app.tex_pool.get());
    app.api.nvnCommandBufferSetSamplerPool(cmd->obj.get(), app.smp_pool.get());
    app.api.nvnCommandBufferBindProgram(cmd->obj.get(), program.get(), NVN_SHADER_STAGE_COMPUTE_BIT);
    for (int pass=0; pass<3; ++pass) {
        std::memset(outputs[pass].ptr, 0xcd, outputs[pass].size);
        auto& texture = pass == 1 ? sint_tex : uint_tex;
        app.api.nvnCommandBufferBindTexture(cmd->obj.get(), NVN_SHADER_STAGE_COMPUTE, 0, app.texture_handle(texture->id,sampler));
        app.api.nvnCommandBufferBindTexture(cmd->obj.get(), NVN_SHADER_STAGE_COMPUTE, 1, app.texture_handle(norm_tex->id,sampler));
        app.api.nvnCommandBufferBindStorageBuffer(cmd->obj.get(), NVN_SHADER_STAGE_COMPUTE, 0, outputs[pass].address, outputs[pass].size);
        app.api.nvnCommandBufferDispatchCompute(cmd->obj.get(),1,1,1);
    }
    app.end_and_submit(*cmd); app.finish();
    for (int pass=0; pass<3; ++pass) {
        const auto* result = reinterpret_cast<const uint32_t*>(outputs[pass].ptr);
        for (int i=0; i<4; ++i) {
            CHECK_EQ(result[i], pass==1 ? uint32_t(int32_t(ip[i])) : uint32_t(up[i]));
            const float expected = float(np[i])/255.0f;
            float actual; std::memcpy(&actual,result+4+i,4);
            CHECK(std::abs(actual-expected)<0.00001f);
        }
        for (size_t i=32;i<outputs[pass].size;++i) CHECK_EQ(outputs[pass].ptr[i],uint8_t(0xcd));
    }
    CHECK_EQ(app.validation().errors,0ull);
}

NXTEST(presented_texture_retirement_waits_for_renderer_submission) {
    App app; AppOpts opts; opts.width=opts.height=16;
    REQUIRE(app.init(opts));
    const int index=app.acquire(); REQUIRE(index>=0);
    // Upload and transition before holding the queue. Presentation has its own
    // submissions, so the NVN timeline alone cannot protect its sampled image.
    gpu::run_frame_sync([&](gpu::Frame& f) {
        auto* t=nxrt::nvn::be::host_of<nxrt::nvn::be::Texture>(app.window_textures[index]->obj.get());
        gpu::tex_transition(t,f,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    });
    VkSemaphoreTypeCreateInfo type{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
    type.semaphoreType=VK_SEMAPHORE_TYPE_TIMELINE;
    VkSemaphoreCreateInfo create{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO}; create.pNext=&type;
    VkSemaphore gate{};
    REQUIRE(vkCreateSemaphore(app.dev->device(),&create,nullptr,&gate)==VK_SUCCESS);
    VkSemaphoreSubmitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    wait.semaphore=gate; wait.value=1; wait.stageMask=VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2}; submit.waitSemaphoreInfoCount=1; submit.pWaitSemaphoreInfos=&wait;
    {
        std::lock_guard<std::mutex> lock(app.dev->queue_mutex());
        REQUIRE(vkQueueSubmit2(app.dev->queue(),1,&submit,VK_NULL_HANDLE)==VK_SUCCESS);
    }
    // Release from another host thread even if retirement blocks the render thread.
    std::thread release([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        VkSemaphoreSignalInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO}; signal.semaphore=gate; signal.value=1;
        vkSignalSemaphore(app.dev->device(),&signal);
    });
    gpu::run_on_render_thread([&] {
        gpu::present_on_render_thread(nxrt::nvn::be::host_of<nxrt::nvn::be::Window>(app.window.get()),index);
    });
    app.api.nvnWindowFinalize(app.window.get());
    app.api.nvnTextureFinalize(app.window_textures[index]->obj.get());
    // An empty completed NVN submission drains pending retirement callbacks.
    // The previously submitted NVN timeline is already complete while the
    // Renderer is still waiting at the gate.
    gpu::run_on_render_thread([] {});
    release.join();
    app.finish();
    app.rend->wait_idle();
    vkDestroySemaphore(app.dev->device(),gate,nullptr);
    CHECK_EQ(app.validation().errors,0ull);
}

NXTEST(sampled_2d_slice_of_3d_image_has_enabled_feature_and_creation_flag) {
    App app; AppOpts opts; opts.width=opts.height=16;
    REQUIRE(app.init(opts));
    if (!app.dev->features().sampler_2d_view_of_3d) { nxtest::skip("GPU lacks sampler2DViewOf3D"); return; }
    NVNtextureBuilder b;
    app.api.nvnTextureBuilderSetDevice(&b,app.device.get());
    app.api.nvnTextureBuilderSetDefaults(&b);
    app.api.nvnTextureBuilderSetTarget(&b,NVN_TEXTURE_TARGET_3D);
    app.api.nvnTextureBuilderSetSize3D(&b,4,4,2);
    app.api.nvnTextureBuilderSetFormat(&b,NVN_FORMAT_RGBA8);
    app.api.nvnTextureBuilderSetLevels(&b,1);
    const auto size=app.api.nvnTextureBuilderGetStorageSize(&b);
    const auto offset=app.pool_main->take(size,256);
    app.api.nvnTextureBuilderSetStorage(&b,app.pool_main->obj.get(),offset);
    NVNtexture texture{};
    REQUIRE(app.api.nvnTextureInitialize(&texture,&b));
    VkDescriptorSetLayout layout{};
    VkDescriptorSetLayoutBinding binding{0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
    VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    ci.bindingCount=1; ci.pBindings=&binding;
    REQUIRE(vkCreateDescriptorSetLayout(app.dev->device(),&ci,nullptr,&layout)==VK_SUCCESS);
    gpu::run_frame_sync([&](gpu::Frame& f) {
        gpu::ViewRequest request; request.base_layer=1; request.level_count=1;
        auto* t=nxrt::nvn::be::host_of<nxrt::nvn::be::Texture>(&texture);
        const auto view=gpu::tex_sampled_view(t,request);
        CHECK(view!=VK_NULL_HANDLE);
        const auto set=f.alloc_set(layout);
        VkDescriptorImageInfo image{gpu::dummy_sampler(),view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet=set; write.descriptorCount=1; write.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; write.pImageInfo=&image;
        vkUpdateDescriptorSets(app.dev->device(),1,&write,0,nullptr);
    });
    app.api.nvnTextureFinalize(&texture);
    app.finish();
    vkDestroyDescriptorSetLayout(app.dev->device(),layout,nullptr);
    CHECK_EQ(app.validation().errors,0ull);
}
