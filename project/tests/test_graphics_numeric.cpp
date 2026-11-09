// SPDX-License-Identifier: GPL-3.0-or-later
#include <array>
#include "nvn_app.h"
using namespace nvnapp;

NXTEST_GROUP(graphics_numeric)
NXTEST(fragment_sampler_reads_narrow_integer_bits_with_linear_guest_sampler) {
    const std::string dir=FEFW_ARRAY_CACHE_DIR "/../integer-graphics";
    _putenv_s("NXRT_NVN_SHADER_CACHE",dir.c_str());_putenv_s("NXRT_NVN_PIPELINE_CACHE","");
    App app;AppOpts opts;opts.width=opts.height=16;REQUIRE(app.init(opts));
    auto load=[&](const char* name) {App::ShaderBlob b;std::ifstream code(dir+"/"+name+".code",std::ios::binary),ctrl(dir+"/"+name+".ctrl",std::ios::binary);b.code.assign(std::istreambuf_iterator<char>(code),{});b.control.assign(std::istreambuf_iterator<char>(ctrl),{});return b;};
    for(const bool cube:{false,true}) {
    auto vs=load("vert"),fs=load(cube?"cube":"frag");REQUIRE(!vs.code.empty()&&!fs.code.empty());
    auto program=app.make_program({&vs,&fs});REQUIRE(program!=nullptr);
    auto make_input=[&](NVNformat format) {
        if(!cube) return app.make_texture_2d(format,1,1);
        auto t=std::make_unique<Tex>();t->obj=std::make_unique<NVNtexture>();NVNtextureBuilder b{};
        app.api.nvnTextureBuilderSetDevice(&b,app.device.get());app.api.nvnTextureBuilderSetDefaults(&b);
        app.api.nvnTextureBuilderSetTarget(&b,NVN_TEXTURE_TARGET_CUBEMAP);app.api.nvnTextureBuilderSetSize2D(&b,1,1);app.api.nvnTextureBuilderSetFormat(&b,format);
        t->size=app.api.nvnTextureBuilderGetStorageSize(&b);t->offset=app.pool_main->take(t->size,std::max<size_t>(256,app.api.nvnTextureBuilderGetStorageAlignment(&b)));
        app.api.nvnTextureBuilderSetStorage(&b,app.pool_main->obj.get(),t->offset);
        if(!app.api.nvnTextureInitialize(t->obj.get(),&b)) return std::unique_ptr<Tex>{};
        t->id=app.next_tex_id++;app.api.nvnTexturePoolRegisterTexture(app.tex_pool.get(),t->id,t->obj.get(),nullptr);return t;
    };
    auto input=make_input(NVN_FORMAT_RGBA8UI),normalized=make_input(NVN_FORMAT_RGBA8);REQUIRE(input&&normalized);
    const int sampler=app.make_sampler([&](NVNsamplerBuilder& b){app.api.nvnSamplerBuilderSetMinMagFilter(&b,NVN_MIN_FILTER_LINEAR,NVN_MAG_FILTER_LINEAR);});
    const NVNcopyRegion region{0,0,0,1,1,cube?6:1};
    for(int pass=0;pass<3;++pass) {
        const std::array<uint8_t,4> bytes=pass==1?std::array<uint8_t,4>{255,127,1,0}:std::array<uint8_t,4>{0,1,127,255};
        std::vector<uint8_t> texels((cube?6:1)*4);for(size_t i=0;i<texels.size();++i) texels[i]=bytes[i%4];
        auto* selected=(pass==2?normalized:input).get();
        app.api.nvnTextureWriteTexels(selected->obj.get(),nullptr,&region,texels.data());
        const int index=0;auto cmd=app.make_cmdbuf();REQUIRE(cmd!=nullptr);
        auto* c=cmd->obj.get();NVNtexture* targets[]={app.window_textures[index]->obj.get()};
        app.api.nvnCommandBufferBeginRecording(c);app.api.nvnCommandBufferSetRenderTargets(c,1,targets,nullptr,nullptr,nullptr);
        app.api.nvnCommandBufferSetViewport(c,0,0,16,16);app.api.nvnCommandBufferSetScissor(c,0,0,16,16);
        const float clear[4]={0,0,0,0};app.api.nvnCommandBufferClearColor(c,0,clear,NVN_CLEAR_COLOR_MASK_RGBA);
        app.api.nvnCommandBufferSetTexturePool(c,app.tex_pool.get());app.api.nvnCommandBufferSetSamplerPool(c,app.smp_pool.get());
        NVNpolygonState polygon{};app.api.nvnPolygonStateSetDefaults(&polygon);app.api.nvnPolygonStateSetCullFace(&polygon,NVN_FACE_NONE);app.api.nvnCommandBufferBindPolygonState(c,&polygon);
        NVNcolorState color{};app.api.nvnColorStateSetDefaults(&color);app.api.nvnCommandBufferBindColorState(c,&color);
        NVNchannelMaskState mask{};app.api.nvnChannelMaskStateSetDefaults(&mask);app.api.nvnCommandBufferBindChannelMaskState(c,&mask);
        app.api.nvnCommandBufferBindProgram(c,program.get(),NVN_SHADER_STAGE_VERTEX_BIT|NVN_SHADER_STAGE_FRAGMENT_BIT);
        app.api.nvnCommandBufferBindTexture(c,NVN_SHADER_STAGE_FRAGMENT,0,app.texture_handle(selected->id,sampler));
        app.api.nvnCommandBufferDrawArrays(c,NVN_DRAW_PRIMITIVE_TRIANGLES,0,3);
        app.end_and_submit(*cmd);app.finish();std::vector<uint8_t> pixels(16*16*4);
        const NVNcopyRegion screen{0,0,0,16,16,1};app.api.nvnTextureReadTexels(targets[0],nullptr,&screen,pixels.data());
        size_t wrong=0;for(size_t i=0;i<pixels.size();++i) wrong+=pixels[i]!=(pass==2?(bytes[i%4]?255:0):bytes[i%4]);
        std::printf("    first pixel %u %u %u %u; wrong components %zu; draws %llu skipped %llu\n",pixels[0],pixels[1],pixels[2],pixels[3],wrong,(unsigned long long)nxrt::nvn::backend_stats().draws,(unsigned long long)nxrt::nvn::backend_stats().draws_skipped);
        CHECK_EQ(wrong,0u);
    }
    }
    app.finish();CHECK_EQ(app.validation().errors,0ull);
}
