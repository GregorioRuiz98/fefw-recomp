// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace fefw::mods {
inline constexpr const char* kFeatures = "fefw-runtime-6";
inline constexpr const char* kBuild = "db93b0125f4850601d61c3edde012f8eb2ecfe77000000000000000000000000";
static_assert(std::string_view(kBuild).size() == 64);
std::string nso_build_id(const std::filesystem::path& path);
// Applies both changes transactionally to the private mapped image only.
// Original and replacement bytes are accepted so reverting is also possible.
bool apply_fps60(std::span<std::byte> image, std::string_view build, bool enabled, std::string& error);
}
