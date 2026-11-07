// Synthetic GPU regression for the same c0 snapshots used by NVN replay. No game shaders or assets.
#include <array>
#include <cstring>
#include <fstream>
#include <vector>

#include "test_framework.h"
#include "nxrt/gfx.h"
#include "nvn_gpu_internal.h"
#include "nvn_pipeline.h"

NXTEST_GROUP(compute_constants)
using namespace nxrt;
using namespace nxrt::nvn::be::gpu;

namespace {
void run(bool nvn2) {
    gfx::DeviceDesc desc;
    desc.headless = 1;
    desc.validation = 1;
    std::string error;
    auto dev = gfx::Device::create(desc, &error);
    REQUIRE(dev != nullptr);
    REQUIRE(init_for_test(*dev));
    const VkDevice d = dev->device();
    std::printf("    GPU %s; validation %s; layout %s\n", dev->properties().deviceName,
                dev->validation_enabled() ? "enabled" : "unavailable", nvn2 ? "NVN2" : "Maxwell");

    const auto path = std::string(FEFW_COMPUTE_SPV_DIR) + (nvn2 ? "/nvn2.spv" : "/maxwell.spv");
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    REQUIRE(file.good());
    const auto bytes = static_cast<size_t>(file.tellg());
    REQUIRE(bytes > 0 && bytes % 4 == 0);
    std::vector<uint32_t> words(bytes / 4);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(words.data()), bytes);
    REQUIRE(file.good());
    VkShaderModuleCreateInfo sm{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    sm.codeSize = bytes;
    sm.pCode = words.data();
    VkShaderModule shader{};
    REQUIRE(vkCreateShaderModule(d, &sm, nullptr, &shader) == VK_SUCCESS);
    VkDescriptorSetLayoutBinding bindings[2] = {
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
    VkDescriptorSetLayoutCreateInfo sl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    sl.bindingCount = 2;
    sl.pBindings = bindings;
    VkDescriptorSetLayout set_layout{};
    REQUIRE(vkCreateDescriptorSetLayout(d, &sl, nullptr, &set_layout) == VK_SUCCESS);
    VkPipelineLayoutCreateInfo lc{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    lc.setLayoutCount = 1;
    lc.pSetLayouts = &set_layout;
    VkPipelineLayout layout{};
    REQUIRE(vkCreatePipelineLayout(d, &lc, nullptr, &layout) == VK_SUCCESS);
    VkComputePipelineCreateInfo pc{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pc.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    pc.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pc.stage.module = shader;
    pc.stage.pName = "main";
    pc.layout = layout;
    VkPipeline pipe{};
    REQUIRE(vkCreateComputePipelines(d, VK_NULL_HANDLE, 1, &pc, nullptr, &pipe) == VK_SUCCESS);

    constexpr VkDeviceSize stride = 1024;
    constexpr uint32_t poison = 0xcdcdcdcd;
    gfx::Buffer output, args;
    REQUIRE(dev->create_buffer(stride * 3, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                              gfx::MemoryUse::Readback, &output) == VK_SUCCESS);
    REQUIRE(dev->create_buffer(32, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                              gfx::MemoryUse::GpuOnly, &args) == VK_SUCCESS);
    const uint32_t counts[3][3] = {{2, 3, 1}, {3, 2, 2}, {2, 2, 3}};
    run_frame_sync([&](Frame& frame) {
        vkCmdFillBuffer(frame.cb, output.buffer, 0, output.size, poison);
        // These arguments exist only on the GPU, at a nonzero offset, when the snapshots are recorded.
        vkCmdUpdateBuffer(frame.cb, args.buffer, 16, 12, counts[2]);
        // Exercise upload-ring snapshots for NVN2 and the separate staging fallback for Maxwell.
        if (!nvn2) REQUIRE(frame.upload(8ull << 20, 256));
        UploadAlloc snapshots[3];
        snapshots[0] = compute_driver_constants(frame, nvn2, 2, 3, 1);
        snapshots[1] = compute_driver_constants(frame, nvn2, 3, 2, 2);
        const BufRef indirect{args.buffer, 16, 16, true};
        snapshots[2] = compute_driver_constants(frame, nvn2, 0, 0, 0, &indirect);
        for (const auto& snapshot : snapshots) {
            REQUIRE(snapshot);
            CHECK_EQ(snapshot.size, 65536ull);
            float max_point = 0;
            std::memcpy(&max_point, snapshot.ptr + 0x610, 4);
            CHECK_EQ(max_point, 64.0f);
        }
        CHECK(snapshots[0].buffer != snapshots[1].buffer || snapshots[0].offset != snapshots[1].offset);
        CHECK(snapshots[1].buffer != snapshots[2].buffer || snapshots[1].offset != snapshots[2].offset);
        const BufRef invalid{args.buffer, 16, 8, true};
        CHECK(!compute_driver_constants(frame, nvn2, 0, 0, 0, &invalid));
        const BufRef unaligned{args.buffer, 17, 15, true};
        CHECK(!compute_driver_constants(frame, nvn2, 0, 0, 0, &unaligned));
        vkCmdBindPipeline(frame.cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
        memory_barrier(frame.cb);
        for (uint32_t i = 0; i < 3; ++i) {
            VkDescriptorSet set = frame.alloc_set(set_layout);
            REQUIRE(set != VK_NULL_HANDLE);
            VkDescriptorBufferInfo info[2] = {
                {snapshots[i].buffer, snapshots[i].offset, snapshots[i].size},
                {output.buffer, i * stride, stride}};
            VkWriteDescriptorSet writes[2] = {};
            for (uint32_t b = 0; b < 2; ++b) {
                writes[b].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[b].dstSet = set;
                writes[b].dstBinding = b;
                writes[b].descriptorCount = 1;
                writes[b].descriptorType = bindings[b].descriptorType;
                writes[b].pBufferInfo = &info[b];
            }
            vkUpdateDescriptorSets(d, 2, writes, 0, nullptr);
            vkCmdBindDescriptorSets(frame.cb, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &set, 0, nullptr);
            if (i == 2) vkCmdDispatchIndirect(frame.cb, args.buffer, 16);
            else vkCmdDispatch(frame.cb, counts[i][0], counts[i][1], counts[i][2]);
        }
        memory_barrier(frame.cb);
    });
    dev->invalidate_buffer(output);
    for (uint32_t i = 0; i < 3; ++i) {
        const auto* data = reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(output.mapped) + stride * i);
        const uint32_t groups = counts[i][0] * counts[i][1] * counts[i][2];
        for (uint32_t g = 0; g < groups; ++g) {
            CHECK_EQ(data[4 * g], counts[i][0]);
            CHECK_EQ(data[4 * g + 1], counts[i][1]);
            CHECK_EQ(data[4 * g + 2], counts[i][2]);
            CHECK_EQ(data[4 * g + 3], g);
        }
        for (uint32_t word = groups * 4; word < stride / 4; ++word) CHECK_EQ(data[word], poison);
        std::printf("    %s %u x %u x %u: %u workgroups match CPU; guard region intact\n",
                    i == 2 ? "GPU indirect" : "direct", counts[i][0], counts[i][1], counts[i][2], groups);
    }
    dev->destroy_buffer(output);
    dev->destroy_buffer(args);
    vkDestroyPipeline(d, pipe, nullptr);
    vkDestroyPipelineLayout(d, layout, nullptr);
    vkDestroyShaderModule(d, shader, nullptr);
    shutdown_for_test();
    vkDestroyDescriptorSetLayout(d, set_layout, nullptr);
    const auto validation = dev->validation_stats();
    std::printf("    validation: %llu errors, %llu warnings\n", validation.errors, validation.warnings);
    CHECK_EQ(validation.errors, 0ull);
}
}

NXTEST(nvn2_direct_and_gpu_indirect_are_isolated) { run(true); }
NXTEST(maxwell_direct_and_gpu_indirect_are_isolated) { run(false); }
