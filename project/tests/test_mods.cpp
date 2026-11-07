// SPDX-License-Identifier: GPL-3.0-or-later
#include "fefw_mods.h"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
int main() {
    std::vector<std::byte> image(0x7bf1b0);
    const auto put = [&](size_t offset, uint32_t value) { std::memcpy(image.data() + offset, &value, 4); };
    const auto word = [&](size_t offset) { uint32_t value; std::memcpy(&value, image.data() + offset, 4); return value; };
    int failed = 0;
    const auto check = [&](bool ok, const char* message) { if (!ok) { ++failed; std::printf("FAIL: %s\n", message); } };
    put(0x5d18c, 0x2a0403f6); put(0x7bf1ac, 0x528003c9);
    const auto original = image;
    std::string error;
    check(!fefw::mods::apply_fps60(image, "other-build", true, error) && image == original, "wrong build changes nothing");
    check(fefw::mods::apply_fps60(image, fefw::mods::kBuild, true, error), "enable supported patch");
    check(word(0x5d18c) == 0x52800036 && word(0x7bf1ac) == 0x52800789, "presentation and timing change together");
    check(fefw::mods::apply_fps60(image, fefw::mods::kBuild, true, error), "enable is idempotent");
    check(fefw::mods::apply_fps60(image, fefw::mods::kBuild, false, error) && image == original, "disable restores both instructions");
    put(0x7bf1ac, 0);
    const auto invalid = image;
    check(!fefw::mods::apply_fps60(image, fefw::mods::kBuild, true, error) && image == invalid, "second-site mismatch rolls back entire patch");
    check(!fefw::mods::apply_fps60(std::span(image).first(100), fefw::mods::kBuild, true, error), "truncated image rejected");
    std::printf("Mods: %d failures\n", failed);
    return failed ? 1 : 0;
}
