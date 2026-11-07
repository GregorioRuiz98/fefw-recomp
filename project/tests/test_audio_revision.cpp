// Expectations independently observed in SDK 22 ARM64 with Unicorn; see
// reports/rev16-investigation.md. No game code is embedded in this test.
#include <cstddef>
#include <cstdint>
#include "test_framework.h"
#include "svc_test_util.h"
#include "services/audren/Server/BehaviourInfo.h"
#include "nxrt/config.h"

NXTEST_GROUP(audio_revision)
using B = nxrt::audren::BehaviourInfo;

NXTEST(revision_identity_and_boundaries) {
    for (uint32_t tag : {0x00564552u, 0x00764552u}) {
        for (int n = 1; n <= 16; ++n) {
            const int32_t value = static_cast<int32_t>((uint32_t('0' + n) << 24) | tag);
            CHECK_EQ(B::GetRevisionNumber(value), n);
            CHECK(B::CheckValidRevision(value));
        }
        for (int n : {0, 17, 100, 207}) {
            const int32_t value = static_cast<int32_t>((uint32_t('0' + n) << 24) | tag);
            CHECK_EQ(B::GetRevisionNumber(value), -1);
            CHECK(!B::CheckValidRevision(value));
        }
    }
    for (uint32_t value : {0u, 0xffffffffu, 0x40564553u, 0x40164552u}) {
        CHECK_EQ(B::GetRevisionNumber(static_cast<int32_t>(value)), -1);
        CHECK(!B::CheckValidRevision(static_cast<int32_t>(value)));
    }
}

NXTEST(invalid_revisions_never_enable_features) {
    CHECK(!B::CheckFeatureSupported(0, 0));
    CHECK(!B::CheckFeatureSupported(-1, 0x31564552));
    CHECK(!B::CheckFeatureSupported(0x41564552, 0x31564552));
    CHECK(B::CheckFeatureSupported(0x40764552, 0x3f564552));
    CHECK(!B::CheckFeatureSupported(0x31564552, 0x3f564552));
}

NXTEST(rev16_curve_has_exact_wire_offset_and_guard) {
    using V = nxrt::audren::VoiceInParameter2;
    CHECK_EQ(sizeof(V), size_t(0x188));
    CHECK_EQ(offsetof(V, biquad_filter_fade_curve), size_t(0x177));
    CHECK_EQ(offsetof(V, reserved_tail), size_t(0x178));
    B b;
    b.SetUserRevision(0x40764552);
    V voice{};
    voice.in_use = true;
    CHECK(b.IsVoiceParameterSupported(voice));
    for (int curve = 1; curve < 256; ++curve) {
        voice.biquad_filter_fade_curve = static_cast<uint8_t>(curve);
        CHECK(!b.IsVoiceParameterSupported(voice));
    }
    voice.in_use = false;
    CHECK(b.IsVoiceParameterSupported(voice));
    voice.in_use = true;
    b.SetUserRevision(0x3f564552);
    CHECK(b.IsVoiceParameterSupported(voice));
}

NXTEST(device_auto_tune_policy_round_trip_and_notification_handle) {
    nxsvc::Env env;
    auto manager = nxsvc::connect("audren:u");
    REQUIRE(manager.valid());
    auto reply = manager.call(4, nxsvc::Args().u32(0x40764552).u32(0).u64(0x51));
    REQUIRE(reply.result == 0);
    auto device = manager.sub(reply);
    REQUIRE(device.valid());
    for (uint32_t enabled : {0u, 1u, 0u}) {
        CHECK_EQ(device.call(19, nxsvc::Args().u32(enabled)).result, 0u);
        auto value = device.call(20);
        CHECK_EQ(value.result, 0u);
        CHECK_EQ(value.get<uint8_t>(0), uint8_t(enabled));
    }
    auto notification = device.call(17, nxsvc::Args().u64(0x1000));
    REQUIRE(notification.result == 0);
    REQUIRE(!notification.copy.empty());
    CHECK(nxrt::kernel::handle_as<nxrt::kernel::KEvent>(notification.copy[0]) != nullptr);
    CHECK_EQ(device.call(18, nxsvc::Args().u64(0x1000)).result, 0u);
}

NXTEST(rev16_admission_requires_explicit_probe_and_rejects_bad_requests) {
    nxsvc::Env env;
    auto manager = nxsvc::connect("audren:u");
    REQUIRE(manager.valid());
    nxrt::audren::AudioRendererConfiguration cfg{};
    cfg.sample_rate = 48000;
    cfg.sample_count = 240;
    cfg.mix_buffer_count = 30;
    cfg.sub_mix_buffer_count = 1;
    cfg.voice_count = 64;
    cfg.sink_count = 1;
    cfg.effect_count = 14;
    cfg.revision = 0x40764552;
    nxrt::config_set("audio", "revision16_probe", "0");
    CHECK(manager.call(1, nxsvc::Args().bytes(&cfg, sizeof cfg)).result != 0);
    nxrt::config_set("audio", "revision16_probe", "1");
    auto size = manager.call(1, nxsvc::Args().bytes(&cfg, sizeof cfg));
    CHECK_EQ(size.result, 0u);
    CHECK(size.get<uint64_t>(0) > 0);
    cfg.revision = 0;
    CHECK(manager.call(1, nxsvc::Args().bytes(&cfg, sizeof cfg)).result != 0);
    CHECK(manager.call(1).result != 0);
    nxrt::config_set("audio", "revision16_probe", "0");
}
