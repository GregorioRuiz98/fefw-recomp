// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "fefw_config.h"
namespace fefw::setup {
void graphics_preset(Settings& settings, int preset);
void export_preferences(const fs::path& file, const Settings& settings);
Settings import_preferences(const fs::path& file, const Settings& current);
void controller_profile(const fs::path& dir, const std::string& guid, Settings& settings, bool write);
void export_diagnostics(const fs::path& file, const fs::path& dir, const Settings& settings, int controllers);
}
