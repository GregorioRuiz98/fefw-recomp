// SPDX-License-Identifier: GPL-3.0-or-later
// Original synthetic NSO containers; they contain no game code or data.
#include "test_framework.h"
#include "nxrt/module.h"
#include "nxrt/memory.h"
#include "nxrt/dispatch.h"
#include "nxrt/android_session.h"
#include <array>
#include <cstring>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/syscall.h>
NXTEST_GROUP(nso_android)
namespace {
void put(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
    std::memcpy(bytes.data() + offset, &value, sizeof value);
}
std::vector<uint8_t> synthetic(bool compressed = false) {
    std::vector<uint8_t> bytes(0x130, 0);
    std::memcpy(bytes.data(), "NSO0", 4);
    put(bytes, 0xc, compressed ? 1 : 0);
    put(bytes, 0x10, 0x100); put(bytes, 0x14, 0); put(bytes, 0x18, 16);
    put(bytes, 0x20, 0x120); put(bytes, 0x24, 0x1000); put(bytes, 0x28, 4);
    put(bytes, 0x30, 0x124); put(bytes, 0x34, 0x2000); put(bytes, 0x38, 4);
    put(bytes, 0x3c, 0x1000);
    put(bytes, 0x60, 18);
    if (compressed) { bytes[0x100] = 0xf0; bytes[0x101] = 1; bytes[0x102] = 31; }
    else bytes[0x100] = 31;
    bytes[0x120] = 47; bytes[0x124] = 59;
    return bytes;
}
struct Cleanup {
    ~Cleanup() { nxrt::modules_unload_all(); nxrt::hle_clear(); nxrt::mem::shutdown(); }
};
struct File {
    int descriptor;
    explicit File(const std::vector<uint8_t>& bytes)
        : descriptor(static_cast<int>(syscall(SYS_memfd_create, "fefw-synthetic-nso", 1))) {
        REQUIRE(descriptor >= 0);
        REQUIRE(write(descriptor, bytes.data(), bytes.size()) == static_cast<ssize_t>(bytes.size()));
    }
    ~File() { close(descriptor); }
};
}
NXTEST(raw_nso_maps_bytes_and_zero_bss) {
    Cleanup cleanup;
    auto bytes = synthetic(); std::string error;
    auto* image = nxrt::module_load_nso_memory(bytes.data(), bytes.size(), {}, &error);
    REQUIRE(image); CHECK(nxrt::module_link_all());
    CHECK_EQ(*reinterpret_cast<uint8_t*>(image->base()), 31u);
    CHECK_EQ(*reinterpret_cast<uint8_t*>(image->base() + 0x1000), 47u);
    CHECK_EQ(*reinterpret_cast<uint8_t*>(image->base() + 0x2000), 59u);
    CHECK_EQ(*reinterpret_cast<uint8_t*>(image->base() + 0x2004), 0u);
}
NXTEST(lz4_literals_decode_to_expected_data) {
    Cleanup cleanup;
    auto bytes = synthetic(true);
    auto* image = nxrt::module_load_nso_memory(bytes.data(), bytes.size());
    REQUIRE(image); CHECK_EQ(*reinterpret_cast<uint8_t*>(image->base()), 31u);
}
NXTEST(lz4_invalid_backreference_is_rejected) {
    Cleanup cleanup;
    auto bytes = synthetic(true);
    put(bytes, 0x60, 3); bytes[0x100] = 0; bytes[0x101] = 0; bytes[0x102] = 0;
    std::string error;
    CHECK(!nxrt::module_load_nso_memory(bytes.data(), bytes.size(), {}, &error));
    CHECK(error.find("LZ4") != std::string::npos); CHECK(nxrt::modules().empty());
}
NXTEST(zbic_segment_decodes_synthetic_bytes) {
    Cleanup cleanup;
    // zbic 1.0.1.post1 compression of {31, followed by fifteen zero bytes}.
    constexpr uint8_t block[] = {90,66,73,67,32,16,69,0,0,16,31,0,1,0,50,192,2};
    auto bytes = synthetic(); put(bytes, 0xc, 0x81); put(bytes, 0x60, sizeof block);
    std::memcpy(bytes.data() + 0x100, block, sizeof block);
    auto* image = nxrt::module_load_nso_memory(bytes.data(), bytes.size());
    REQUIRE(image); CHECK_EQ(*reinterpret_cast<uint8_t*>(image->base()), 31u);
    for (unsigned i=1; i<16; ++i) CHECK_EQ(*reinterpret_cast<uint8_t*>(image->base()+i), 0u);
}
NXTEST(zbic_corruption_and_wrong_decoded_size_are_rejected) {
    Cleanup cleanup;
    constexpr uint8_t block[] = {90,66,73,67,32,16,69,0,0,16,31,0,1,0,50,192,2};
    for (int variant=0; variant<3; ++variant) {
        auto bytes = synthetic(); put(bytes, 0xc, 0x81); put(bytes, 0x60, sizeof block);
        std::memcpy(bytes.data() + 0x100, block, sizeof block);
        if (variant==0) bytes[0x100] = 'X';
        if (variant==1) put(bytes, 0x60, sizeof block - 1);
        if (variant==2) put(bytes, 0x18, 15);
        std::string error;
        CHECK(!nxrt::module_load_nso_memory(bytes.data(), bytes.size(), {}, &error));
        CHECK(error.find("ZBIC") != std::string::npos);
        CHECK(nxrt::modules().empty());
    }
}
NXTEST(truncated_bad_magic_overlap_and_oversize_are_rejected) {
    Cleanup cleanup;
    auto original = synthetic();
    CHECK(!nxrt::module_load_nso_memory(original.data(), 255));
    for (int variant = 0; variant < 4; ++variant) {
        auto bytes = original;
        if (variant == 0) bytes[0] = 'X';
        if (variant == 1) put(bytes, 0x10, 0xffffffff);
        if (variant == 2) put(bytes, 0x24, 4);
        if (variant == 3) put(bytes, 0x3c, 0xffffffff);
        CHECK(!nxrt::module_load_nso_memory(bytes.data(), bytes.size()));
    }
    CHECK(nxrt::modules().empty());
}
NXTEST(segment_end_overflow_is_rejected_before_allocating) {
    Cleanup cleanup;
    auto bytes = synthetic();
    put(bytes, 0x14, 0xfffffff0); put(bytes, 0x18, 0x30);
    CHECK(!nxrt::module_load_nso_memory(bytes.data(), bytes.size()));
    CHECK(nxrt::modules().empty());
}
NXTEST(negative_mod0_dynamic_pointer_does_not_escape_image) {
    Cleanup cleanup;
    auto bytes = synthetic(); put(bytes, 0x104, 8);
    std::memcpy(bytes.data() + 0x108, "MOD0", 4);
    put(bytes, 0x10c, 0xfffffff4); // -12 + MOD0(8) = -4
    auto* image = nxrt::module_load_nso_memory(bytes.data(), bytes.size());
    REQUIRE(image); CHECK(nxrt::module_link_all());
}
NXTEST(six_readonly_descriptors_load_and_unload_twice) {
    File file(synthetic());
    std::array<int, 6> descriptors; descriptors.fill(file.descriptor);
    for (int attempt = 0; attempt < 2; ++attempt) {
        const std::string report = nxrt::android_inspect_modules(descriptors);
        CHECK_MSG(report.find("\"status\":\"PASS\"") != std::string::npos, report);
        CHECK(report.find("\"modules_loaded\":6") != std::string::npos);
        CHECK(report.find("\"cleanup_verified\":true") != std::string::npos);
        CHECK(nxrt::modules().empty()); CHECK(!nxrt::mem::initialised());
        CHECK_EQ(lseek(file.descriptor, 0, SEEK_CUR), static_cast<off_t>(synthetic().size()));
    }
}
NXTEST(partial_load_failure_rolls_back_all_mappings) {
    File file(synthetic());
    std::array<int, 6> descriptors; descriptors.fill(file.descriptor); descriptors[2] = -1;
    const std::string report = nxrt::android_inspect_modules(descriptors);
    CHECK(report.find("\"status\":\"FAIL\"") != std::string::npos);
    CHECK(report.find("\"modules_loaded\":2") != std::string::npos);
    CHECK(report.find("\"cleanup_verified\":true") != std::string::npos);
    CHECK(nxrt::modules().empty()); CHECK(!nxrt::mem::initialised());
}
