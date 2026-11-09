// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace fefw::setup {
namespace fs = std::filesystem;
std::string utf8(const std::wstring& value);
std::wstring wide(const std::string& value);
std::string path_text(const fs::path& value);
std::wstring quote_arg(const std::wstring& value);
fs::path executable_dir();
std::wstring game_mutex_name(const fs::path& dir);
bool game_running(const fs::path& dir);

// Small preserving INI editor. Files remain UTF-8 for the runtime; unrelated
// keys, controller bindings and save paths survive configuration changes.
class Ini {
public:
    explicit Ini(const fs::path& path);
    std::string get(const std::string& section, const std::string& key, const std::string& fallback = "") const;
    void set(const std::string& section, const std::string& key, const std::string& value);
    void save(const fs::path& path) const;
private:
    std::vector<std::string> lines_;
};

struct Language { const char* value; const wchar_t* label; const wchar_t* resource; };
const std::vector<Language>& languages();
std::vector<Language> available_languages(const fs::path& source);
std::string system_language();
struct Settings {
    fs::path source, exefs, romfs, shaders;
    int width = 1280, height = 720;
    std::string window_mode = "windowed", output = "console";
    double render_ratio = 0;
    bool fps120 = false, check_updates = false;
    bool skip = true, completed = false, vsync = true, fps60 = false;
    std::string language;
    std::string upscaler = "taau";
};
struct Validation { bool ok = false; std::string error; };
Settings load(const fs::path& dir);
std::vector<fs::path> candidates(const fs::path& selected);
Validation validate_source(const fs::path& source, bool hashes = true);
Validation validate_prepared(const fs::path& exefs);
Validation validate_ready(const fs::path& dir, const Settings& settings, bool hashes = false);
bool needs_setup(const fs::path& dir);
void edit_settings(const Settings& settings, Ini& game, Ini& controls);
void save(const fs::path& dir, const Settings& settings);
}
