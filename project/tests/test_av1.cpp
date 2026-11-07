#include <array>
#include <fstream>
#include <iterator>
#include <vector>
#include "nxrt/dispatch.h"
#include "nxrt/game.h"
#include "nxrt/video_codec.h"
#include "test_framework.h"
namespace nxrt::nvn { void register_api_texture(); }

NXTEST_GROUP(av1)
using namespace nxrt;

NXTEST(first_packet_through_guest_decoder_contract) {
    const char* fixture = std::getenv("FEFW_AV1_PACKET");
    if (!fixture) { nxtest::skip("FEFW_AV1_PACKET is required (private first AV1 OBU packet)"); return; }
    std::ifstream input(fixture, std::ios::binary);
    std::vector<uint8_t> packet(std::istreambuf_iterator<char>(input), {});
    REQUIRE(!packet.empty());
    game::Info title; title.platform = game::Platform::Switch2;
    game::set_current(&title);
    const bool enabled = media::install_video_codec_hle();
    game::set_current(nullptr);
    REQUIRE(enabled);
    Ctx ctx{};
    std::array<uint64_t, 8> stack{};
    ctx.x[31] = reinterpret_cast<uint64_t>(stack.data());
    auto call = [&](const char* name, std::initializer_list<uint64_t> args, uint64_t expected = 0) {
        auto fn = hle_lookup(name); REQUIRE(fn != nullptr);
        size_t i = 0; for (auto arg : args) ctx.x[i++] = arg;
        fn(&ctx); CHECK_EQ(ctx.x[0], expected);
    };
    auto ptr = [](auto& x) { return reinterpret_cast<uint64_t>(&x); };
    std::array<uint8_t, 1024> decoder{}, pool{}, pref{};
    ctx.x[8] = ptr(pref);
    auto make = hle_lookup("_ZN2nn5codec25MakeDecodeImagePreferenceEttNS0_24VideoDecoderOutputFormatENS0_15ImageTilingModeE");
    REQUIRE(make); ctx.x[0] = 4096; ctx.x[1] = 4096; ctx.x[2] = 0; ctx.x[3] = 0; make(&ctx);
    call("_ZN2nn5codec26InitializeDecoderImagePoolEPNS0_25VideoDecoderImagePoolTypeEPvmRKNS0_27VideoDecoderImagePreferenceEj", {ptr(pool),0,0,ptr(pref),1,0});
    call("_ZN2nn5codec36InitializeAv1DecoderByTransferMemoryEPNS0_14Av1DecoderTypeEPvmmttjNS0_23VideoDecoderInputFormatENS0_24VideoDecoderOutputFormatENS0_22VideoDecoderOutputModeE", {ptr(decoder),0,0,0,4096,4096,0,0});
    call("_ZN2nn5codec24RegisterAv1DecoderBufferEPNS0_14Av1DecoderTypeEjPNS0_25VideoDecoderImagePoolTypeEj", {ptr(decoder),0,ptr(pool),0});
    uint32_t index = 99; std::array<uint64_t, 2> fence{};
    call("_ZN2nn5codec29AcquireAv1DecoderOutputBufferEPjPNS_2gs16NativeSyncHandleEPNS0_14Av1DecoderTypeE", {ptr(index),ptr(fence),ptr(decoder)});
    const auto before = media::video_codec_stats();
    uint64_t accepted = 0;
    call("_ZN2nn5codec27SubmitAv1DecoderInputBufferEPmPNS0_14Av1DecoderTypeEPKvmNS_8TimeSpanEb", {ptr(accepted),ptr(decoder),reinterpret_cast<uint64_t>(packet.data()),packet.size(),123456,0});
    const auto after = media::video_codec_stats();
    CHECK_EQ(after.decode_errors, before.decode_errors);
    CHECK_EQ(after.frames_decoded, before.frames_decoded + 1);
    CHECK_EQ(after.frames_delivered, before.frames_delivered + 1);
    uint16_t width=0,height=0,aw=0,ah=0; uint64_t pts=0; uint8_t eos=1;
    call("_ZN2nn5codec35GetAv1DecoderOutputBufferPropertiesEPtS1_S1_S1_PNS_8TimeSpanEPbPNS0_14Av1DecoderTypeEj", {ptr(width),ptr(height),ptr(aw),ptr(ah),ptr(pts),ptr(eos),ptr(decoder),index});
    CHECK(width > 0 && height > 0); CHECK_EQ(width,aw); CHECK_EQ(height,ah);
    CHECK_EQ(pts,123456ull); CHECK_EQ(eos,uint8_t(0));
    auto get_handle = hle_lookup("_ZN2nn5codec36GetDecoderImagePoolNativeImageHandleEPKNS0_25VideoDecoderImagePoolTypeEj");
    REQUIRE(get_handle); ctx.x[0] = ptr(pool); ctx.x[1] = 0; get_handle(&ctx);
    const uint32_t handle = static_cast<uint32_t>(ctx.x[0]);
    uint32_t native_w=0,native_h=0;
    REQUIRE(media::video_native_image_info(handle,&native_w,&native_h));
    std::vector<uint8_t> rgba(size_t(native_w)*native_h*4 + 16,0xcd);
    uint64_t generation=0;
    REQUIRE(media::video_native_image_rgba(handle,&generation,rgba.data(),rgba.size()-16,native_w,native_h));
    CHECK_EQ(rgba[3],uint8_t(255));
    for (size_t i=rgba.size()-16;i<rgba.size();++i) CHECK_EQ(rgba[i],uint8_t(0xcd));
    CHECK(!media::video_native_image_rgba(handle,&generation,rgba.data(),rgba.size()-16,native_w,native_h));
    nvn::register_api_texture();
    std::array<uint64_t,4> builder{0,uint64_t(handle)|(1ull<<32),0x38,0};
    std::array<uint8_t,1024> native{},texture{};
    call("_Z24nvnNativeImageInitializeP14NVNnativeImagePK21NVNnativeImageBuilder",{ptr(native),ptr(builder)},1);
    call("_Z35nvnTextureInitializeFromNativeImageP10NVNtextureP14NVNnativeImage",{ptr(texture),ptr(native)},1);
    call("nvnTextureGetWidth",{ptr(texture)},native_w);
    call("nvnTextureGetHeight",{ptr(texture)},native_h);
    call("nvnTextureGetFormat",{ptr(texture)},0x38);
    // The texture owns its storage after the native wrapper is finalized.
    call("_Z22nvnNativeImageFinalizeP14NVNnativeImage",{ptr(native)},ptr(native));
    call("nvnTextureGetWidth",{ptr(texture)},native_w);
    call("nvnTextureFinalize",{ptr(texture)},ptr(texture));
    builder[1]=0;
    call("_Z24nvnNativeImageInitializeP14NVNnativeImagePK21NVNnativeImageBuilder",{ptr(native),ptr(builder)},0);
    const auto queued_before = media::video_codec_stats();
    const char* submit = "_ZN2nn5codec27SubmitAv1DecoderInputBufferEPmPNS0_14Av1DecoderTypeEPKvmNS_8TimeSpanEb";
    for (int i = 0; i < 3; ++i)
        call(submit, {ptr(accepted),ptr(decoder),reinterpret_cast<uint64_t>(packet.data()),packet.size(),uint64_t(200000+i),0});
    call(submit, {ptr(accepted),ptr(decoder),reinterpret_cast<uint64_t>(packet.data()),packet.size(),300000,0}, 0x1c266f);
    CHECK_EQ(media::video_codec_stats().frames_decoded, queued_before.frames_decoded + 3);
    CHECK_EQ(media::video_codec_stats().packets_submitted, queued_before.packets_submitted + 3);
    call("_ZN2nn5codec29ReleaseAv1DecoderOutputBufferEPNS0_14Av1DecoderTypeEjNS_2gs16NativeSyncHandleE", {ptr(decoder),index,0,0});
    call("_ZN2nn5codec29AcquireAv1DecoderOutputBufferEPjPNS_2gs16NativeSyncHandleEPNS0_14Av1DecoderTypeE", {ptr(index),ptr(fence),ptr(decoder)});
    CHECK(media::video_native_image_rgba(handle,&generation,rgba.data(),rgba.size()-16,native_w,native_h));
    call(submit, {ptr(accepted),ptr(decoder),reinterpret_cast<uint64_t>(packet.data()),packet.size(),300000,0});
    CHECK_EQ(media::video_codec_stats().frames_decoded, queued_before.frames_decoded + 4);
    call("_ZN2nn5codec15FlushAv1DecoderEPNS0_14Av1DecoderTypeE", {ptr(decoder)});
    call("_ZN2nn5codec26UnregisterAv1DecoderBufferEPNS0_14Av1DecoderTypeEj", {ptr(decoder),index});
    call("_ZN2nn5codec18FinalizeAv1DecoderEPNS0_14Av1DecoderTypeE", {ptr(decoder)});
    call("_ZN2nn5codec24FinalizeDecoderImagePoolEPNS0_25VideoDecoderImagePoolTypeE", {ptr(pool)});
    CHECK(!media::video_native_image_info(handle,&native_w,&native_h));
}
