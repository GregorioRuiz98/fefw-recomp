// Runs the real Ampere translator's synthetic compute SPIR-V against CPU oracle files.
#include <fstream>
#include <string>
#include <vector>
#include <cstring>
#include <stdexcept>
#include "test_framework.h"
#include "nxrt/gfx.h"
#include "nvn_gpu_internal.h"
#include "nvn_pipeline.h"
NXTEST_GROUP(compute_numeric)
using namespace nxrt;
using namespace nxrt::nvn::be::gpu;
static std::vector<uint32_t> read_words(const std::string& path) {
    std::ifstream f(path, std::ios::binary|std::ios::ate); if(!f.good()) throw std::runtime_error(path);
    const auto n=static_cast<size_t>(f.tellg()); if(n%4) throw std::runtime_error(path);
    std::vector<uint32_t> v(n/4); f.seekg(0); f.read(reinterpret_cast<char*>(v.data()),n); if(!f.good()) throw std::runtime_error(path); return v;
}
NXTEST(translated_ampere_matches_integer_oracles) {
    gfx::DeviceDesc dd; dd.headless=1; dd.validation=1; std::string error;
    auto dev=gfx::Device::create(dd,&error); REQUIRE(dev); REQUIRE(init_for_test(*dev));
    const auto d=dev->device();
    std::printf("GPU %s; validation %s\n",dev->properties().deviceName,dev->validation_enabled()?"enabled":"unavailable");
    VkDescriptorSetLayout sets[2]{};
    VkDescriptorSetLayoutBinding ub[]={{0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
        {1,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{4,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}};
    VkDescriptorSetLayoutBinding sb{0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
    for(int i=0;i<2;i++){VkDescriptorSetLayoutCreateInfo si{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};si.bindingCount=i?1:3;si.pBindings=i?&sb:ub;REQUIRE(vkCreateDescriptorSetLayout(d,&si,nullptr,&sets[i])==VK_SUCCESS);}
    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};pl.setLayoutCount=2;pl.pSetLayouts=sets;
    VkPipelineLayout layout{};REQUIRE(vkCreatePipelineLayout(d,&pl,nullptr,&layout)==VK_SUCCESS);
    std::ifstream cases("cases.txt"); REQUIRE(cases.good());std::string name;uint32_t count,rows;unsigned total=0;
    while(cases>>name>>count>>rows){
        auto code=read_words(name+".spv"), input=read_words(name+".input"), expected=read_words(name+".expected");
        REQUIRE(expected.size()==rows*8); REQUIRE(input.size()*4<=65536);
        VkShaderModuleCreateInfo sm{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};sm.codeSize=code.size()*4;sm.pCode=code.data();
        VkShaderModule module{};REQUIRE(vkCreateShaderModule(d,&sm,nullptr,&module)==VK_SUCCESS);
        VkComputePipelineCreateInfo pc{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};pc.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};pc.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;pc.stage.module=module;pc.stage.pName="main";pc.layout=layout;
        VkPipeline pipeline{};REQUIRE(vkCreateComputePipelines(d,VK_NULL_HANDLE,1,&pc,nullptr,&pipeline)==VK_SUCCESS);
        gfx::Buffer output;REQUIRE(dev->create_buffer(expected.size()*4+256,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,gfx::MemoryUse::Readback,&output)==VK_SUCCESS);
        run_frame_sync([&](Frame& frame){
            auto data=frame.upload(65536,256),zero=frame.upload(65536,256);REQUIRE(data);REQUIRE(zero);
            std::memset(data.ptr,0,65536);std::memset(zero.ptr,0,65536);std::memcpy(data.ptr,input.data(),input.size()*4);
            VkDescriptorSet ds[2]={frame.alloc_set(sets[0]),frame.alloc_set(sets[1])};REQUIRE(ds[0]);REQUIRE(ds[1]);
            VkDescriptorBufferInfo bi[]={{zero.buffer,zero.offset,65536},{zero.buffer,zero.offset,65536},{data.buffer,data.offset,65536},{output.buffer,0,output.size}};
            VkWriteDescriptorSet writes[4]{};
            for(int i=0;i<4;i++){writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[i].dstSet=ds[i==3];writes[i].dstBinding=i==3?0:ub[i].binding;writes[i].descriptorCount=1;writes[i].descriptorType=i==3?VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;writes[i].pBufferInfo=&bi[i];}
            vkUpdateDescriptorSets(d,4,writes,0,nullptr);
            vkCmdFillBuffer(frame.cb,output.buffer,0,output.size,0xcdcdcdcd);memory_barrier(frame.cb);
            vkCmdBindPipeline(frame.cb,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);vkCmdBindDescriptorSets(frame.cb,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,2,ds,0,nullptr);vkCmdDispatch(frame.cb,count,1,1);memory_barrier(frame.cb);
        });
        dev->invalidate_buffer(output);const auto* actual=static_cast<const uint32_t*>(output.mapped);size_t failures=0;
        for(size_t i=0;i<output.size/4;i++){const auto want=i<expected.size()?expected[i]:0xcdcdcdcd;if(actual[i]!=want){if(failures<8)std::printf("FAIL %s row %zu word %zu actual %08x expected %08x\n",name.c_str(),i/8,i%8,actual[i],want);failures++;}}
        std::printf("%s: %u rows, %zu mismatches, guards checked\n",name.c_str(),rows,failures);CHECK_EQ(failures,0u);total+=rows;
        dev->destroy_buffer(output);vkDestroyPipeline(d,pipeline,nullptr);vkDestroyShaderModule(d,module,nullptr);
    }
    std::printf("Total: %u rows\n",total);const auto stats=dev->validation_stats();CHECK_EQ(stats.errors,0ull);
    vkDestroyPipelineLayout(d,layout,nullptr);shutdown_for_test();for(auto s:sets)vkDestroyDescriptorSetLayout(d,s,nullptr);
}
