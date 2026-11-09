// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string_view>
namespace fefw::pc {
// Install only for the verified update, after registering modules and before linking.
void install(std::string_view build);
}
