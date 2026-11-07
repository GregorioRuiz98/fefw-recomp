// SPDX-License-Identifier: GPL-3.0-or-later
// Patch locations: Kompas9/Switch-Game-Patches, 60FPS v1.0, commit 9e553571c986.
#include "fefw_mods.h"
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>

namespace fefw::mods {
namespace {
struct Site { size_t offset; uint32_t original, replacement; };
constexpr Site kSites[] = {{0x5d18c, 0x2a0403f6, 0x52800036}, {0x7bf1ac, 0x528003c9, 0x52800789}};
}
std::string nso_build_id(const std::filesystem::path& path) {
    std::array<unsigned char, 0x60> h{};
    std::ifstream in(path, std::ios::binary);
    if (!in.read(reinterpret_cast<char*>(h.data()), h.size()) || std::memcmp(h.data(), "NSO0", 4)) return {};
    std::string result;
    for (size_t i = 0x40; i < 0x60; ++i) {
        result += "0123456789abcdef"[h[i] >> 4];
        result += "0123456789abcdef"[h[i] & 15];
    }
    return result;
}
bool apply_fps60(std::span<std::byte> image, std::string_view build, bool enabled, std::string& error) {
    error.clear();
    if (build != kBuild) { error = "El parche de 60 FPS no corresponde al Build ID de main."; return false; }
    for (const auto& site : kSites) {
        if (site.offset > image.size() || image.size() - site.offset < sizeof(uint32_t)) {
            error = "El modulo main no contiene los puntos del parche de 60 FPS."; return false;
        }
        uint32_t word; std::memcpy(&word, image.data() + site.offset, sizeof word);
        if (word != site.original && word != site.replacement) {
            error = "Las instrucciones originales del parche de 60 FPS no coinciden."; return false;
        }
    }
    for (const auto& site : kSites) {
        const auto word = enabled ? site.replacement : site.original;
        std::memcpy(image.data() + site.offset, &word, sizeof word);
    }
    return true;
}
}
