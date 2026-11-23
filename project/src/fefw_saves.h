// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "fefw_config.h"
namespace fefw::setup {
struct SavedGame { fs::path root, installation; size_t files = 0; };
struct SaveImport { fs::path destination, backup; size_t files = 0; };
SavedGame inspect_saved_game(const fs::path& selected);
SaveImport import_saved_game(const fs::path& game, const fs::path& selected);
}
