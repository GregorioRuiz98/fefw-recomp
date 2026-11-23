// SPDX-License-Identifier: GPL-3.0-or-later
#include "test_framework.h"
#include "nvn_shaders.h"
#include <vector>
#include <filesystem>
#include <fstream>
#include <windows.h>
#include "nxrt/config.h"
NXTEST_GROUP(fefw_shader_cache_safety)
using namespace nxrt::nvn::be;
namespace {
std::vector<uint32_t> array_module(bool cycle, bool input) {
    // Original synthetic declarations, not a commercial shader. Reflection
    // must reject a damaged array chain before it reaches Vulkan.
    const uint32_t storage = input ? 1u : 0u;
    return {0x07230203, 0x00010000, 0, 20, 0,
        (5u << 16) | 15, 5, 10, 0x6e69616d, 0,
        (4u << 16) | 21, 2, 32, 0,
        (4u << 16) | 43, 2, 5, 4,
        (4u << 16) | 28, 1, cycle ? 1u : 6u, 5,
        (2u << 16) | 26, 6,
        (4u << 16) | 32, 3, storage, 1,
        (4u << 16) | 59, 3, 4, storage,
        (4u << 16) | 71, 4, 34, 0,
        (4u << 16) | 71, 4, 33, 0};
}
}
NXTEST(valid_descriptor_array_still_reflects) {
    auto words = array_module(false, false);
    ShaderReflection r; std::string error;
    REQUIRE(reflect_spirv(words.data(), words.size(), r, &error));
    REQUIRE(r.bindings.size() == 1);
    CHECK(r.bindings[0].count == 4);
}
NXTEST(cyclic_descriptor_array_is_rejected) {
    auto words = array_module(true, false);
    ShaderReflection r; std::string error;
    CHECK(!reflect_spirv(words.data(), words.size(), r, &error));
    CHECK(!error.empty());
}
NXTEST(cyclic_input_array_is_rejected) {
    auto words = array_module(true, true);
    ShaderReflection r; std::string error;
    CHECK(!reflect_spirv(words.data(), words.size(), r, &error));
    CHECK(!error.empty());
}
NXTEST(mutually_recursive_descriptor_arrays_are_rejected) {
    auto words = array_module(false, false);
    for (size_t i = 5; i < words.size(); i += words[i] >> 16) {
        if ((words[i] & 0xffff) == 26) {
            words.erase(words.begin() + i, words.begin() + i + 2);
            words.insert(words.begin() + i, {(4u << 16) | 28, 6, 1, 5});
            break;
        }
    }
    ShaderReflection r; std::string error;
    CHECK(!reflect_spirv(words.data(), words.size(), r, &error));
    CHECK(!error.empty());
}
NXTEST(valid_long_array_chain_is_not_rejected_by_an_arbitrary_depth_limit) {
    auto words = array_module(false, false);
    words[3] = 100;
    // Insert 64 ordinary one-element array declarations before the sampler.
    for (size_t i = 5; i < words.size(); i += words[i] >> 16) {
        if ((words[i] & 0xffff) != 26) continue;
        std::vector<uint32_t> chain{(4u << 16) | 43, 2, 7, 1};
        for (uint32_t id = 20; id < 84; ++id) {
            const uint32_t next = id == 83 ? 6 : id + 1;
            chain.insert(chain.end(), {(4u << 16) | 28, id, next, 7});
        }
        words.insert(words.begin() + i, chain.begin(), chain.end());
        break;
    }
    // Make the outer fixed array refer to the new valid chain.
    for (size_t i = 5; i < words.size(); i += words[i] >> 16)
        if ((words[i] & 0xffff) == 28 && words[i + 1] == 1) words[i + 2] = 20;
    ShaderReflection r; std::string error;
    CHECK(reflect_spirv(words.data(), words.size(), r, &error));
    REQUIRE(r.bindings.size() == 1);
    CHECK(r.bindings[0].count == 4);
}
NXTEST(cache_load_rejects_circular_types_read_from_disk) {
    namespace fs = std::filesystem;
    const auto dir = fs::current_path() / ("shader-safety-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
    REQUIRE(fs::create_directory(dir));
    struct Cleanup { fs::path p; ~Cleanup() { std::error_code ec; fs::remove_all(p, ec); } } cleanup{dir};
    const std::string hash = "0123456789abcdef";
    { std::ofstream out(dir / "results.tsv"); out << "hash\tresources\n" << hash << "\t\n"; }
    const auto words = array_module(true, false);
    { std::ofstream out(dir / (hash + ".spv"), std::ios::binary);
      out.write(reinterpret_cast<const char*>(words.data()), words.size() * sizeof(uint32_t)); }
    nxrt::config_set("nvn", "shader_order", "");
    nxrt::config_set("nvn", "shader_pack", "0");
    ShaderCache cache;
    REQUIRE(cache.open(dir.string()));
    CHECK(cache.load_stub(hash) == nullptr);
}
