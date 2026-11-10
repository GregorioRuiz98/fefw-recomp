// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
namespace nxtest { void set_temporary_directory(std::string path); const std::string& temporary_directory(); std::string run_memory_tests(); std::string run_core_tests(); }
