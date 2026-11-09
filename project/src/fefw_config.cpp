// SPDX-License-Identifier: GPL-3.0-or-later
#include "fefw_config.h"
#include "fefw_mods.h"
#include "fefw_supported.h"
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cwctype>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace fefw::setup {
namespace {
std::string trim(std::string v) {
    const auto a = v.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    return v.substr(a, v.find_last_not_of(" \t\r\n") - a + 1);
}
std::string lower(std::string v) { for (char& c : v) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return v; }
std::string section(const std::string& line) {
    const std::string s = trim(line);
    return s.size() > 2 && s.front() == '[' && s.back() == ']' ? lower(trim(s.substr(1, s.size() - 2))) : "";
}
std::string hex(const unsigned char* p, size_t n) {
    const char* h = "0123456789abcdef"; std::string s;
    for (size_t i = 0; i < n; ++i) { s += h[p[i] >> 4]; s += h[p[i] & 15]; }
    return s;
}
std::string sha(const fs::path& path, uint64_t offset = 0, uint64_t size = ~0ull) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot read: " + path_text(path));
    in.seekg(static_cast<std::streamoff>(offset));
    BCRYPT_ALG_HANDLE alg{}; BCRYPT_HASH_HANDLE hash{};
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) throw std::runtime_error("SHA-256 is unavailable");
    if (BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) < 0) { BCryptCloseAlgorithmProvider(alg, 0); throw std::runtime_error("SHA-256 is unavailable"); }
    std::array<unsigned char, 65536> block{}; bool ok = true;
    while (in && size) {
        const auto n = static_cast<std::streamsize>(std::min<uint64_t>(size, block.size()));
        in.read(reinterpret_cast<char*>(block.data()), n);
        const auto got = in.gcount(); if (!got) break;
        if (BCryptHashData(hash, block.data(), static_cast<ULONG>(got), 0) < 0) { ok = false; break; }
        size -= static_cast<uint64_t>(got);
    }
    unsigned char digest[32]{};
    ok = ok && BCryptFinishHash(hash, digest, sizeof digest, 0) >= 0;
    BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(alg, 0);
    if (!ok) throw std::runtime_error("Could not verify " + path_text(path));
    return hex(digest, sizeof digest);
}
std::vector<unsigned char> header(const fs::path& path, size_t n) {
    std::ifstream in(path, std::ios::binary); std::vector<unsigned char> b(n);
    if (!in.read(reinterpret_cast<char*>(b.data()), static_cast<std::streamsize>(n))) return {};
    return b;
}
uint32_t u32(const unsigned char* b) { uint32_t v; std::memcpy(&v, b, 4); return v; }
uint64_t u64(const unsigned char* b) { uint64_t v; std::memcpy(&v, b, 8); return v; }
Validation invalid(const std::string& s) { return {false, s}; }
fs::path resolved(const fs::path& dir, const std::string& v) {
    const fs::path p(wide(v)); return p.is_absolute() ? p : dir / p;
}
int number(const std::string& s, int fallback) { try { return std::stoi(s); } catch (...) { return fallback; } }
void backup(const fs::path& p) {
    if (fs::is_regular_file(p) && !fs::exists(p.wstring() + L".before-setup"))
        fs::copy_file(p, p.wstring() + L".before-setup");
}
}
std::string utf8(const std::wstring& s) {
    if (s.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    if (!n) throw std::runtime_error("Invalid Unicode text");
    std::string out(n, '\0'); WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n, nullptr, nullptr); return out;
}
std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (!n) throw std::runtime_error("Invalid UTF-8 text");
    std::wstring out(n, L'\0'); MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n); return out;
}
std::string path_text(const fs::path& p) { return utf8(p.wstring()); }
std::wstring quote_arg(const std::wstring& v) {
    std::wstring out = L"\""; size_t slashes = 0;
    for (wchar_t c : v) {
        if (c == L'\\') { ++slashes; continue; }
        if (c == L'\"') out.append(slashes * 2 + 1, L'\\'); else out.append(slashes, L'\\');
        slashes = 0; out += c;
    }
    out.append(slashes * 2, L'\\'); return out + L'\"';
}
fs::path executable_dir() {
    std::wstring p(32768, L'\0'); const DWORD n = GetModuleFileNameW(nullptr, p.data(), static_cast<DWORD>(p.size()));
    if (!n || n == p.size()) throw std::runtime_error("Cannot locate the executable");
    p.resize(n); return fs::path(p).parent_path();
}
std::wstring game_mutex_name(const fs::path& dir) {
    std::wstring p(32768, L'\0'); const DWORD n = GetFullPathNameW(dir.c_str(), static_cast<DWORD>(p.size()), p.data(), nullptr);
    if (!n || n >= p.size()) throw std::runtime_error("Path is too long");
    p.resize(n); unsigned long long h = 1469598103934665603ull;
    for (wchar_t c : p) h = (h ^ static_cast<unsigned long long>(std::towlower(c))) * 1099511628211ull;
    return L"Local\\FireEmblemRecomp_" + std::to_wstring(h);
}
bool game_running(const fs::path& dir) {
    HANDLE m = OpenMutexW(SYNCHRONIZE, FALSE, game_mutex_name(dir).c_str());
    if (!m) return false; CloseHandle(m); return true;
}
Ini::Ini(const fs::path& p) {
    if (!fs::exists(p)) return;
    std::ifstream in(p, std::ios::binary); if (!in) throw std::runtime_error("Cannot read " + path_text(p));
    std::string line; bool first = true;
    while (std::getline(in, line)) {
        if (first && line.compare(0, 3, "\xef\xbb\xbf") == 0) line.erase(0, 3);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        first = false; lines_.push_back(line);
    }
}
std::string Ini::get(const std::string& sec, const std::string& key, const std::string& fallback) const {
    std::string current, out = fallback;
    for (const auto& line : lines_) {
        const auto s = section(line); if (!s.empty()) { current = s; continue; }
        const auto t = trim(line); if (t.empty() || t.front() == ';' || t.front() == '#') continue;
        const auto eq = line.find('='); if (eq != std::string::npos && current == lower(sec) && lower(trim(line.substr(0, eq))) == lower(key)) out = trim(line.substr(eq + 1));
    }
    return out;
}
void Ini::set(const std::string& sec, const std::string& key, const std::string& value) {
    if (value.find_first_of("\r\n") != std::string::npos) throw std::runtime_error("Invalid configuration value");
    std::string current; size_t insert = lines_.size(); bool have = false, found = false;
    for (size_t i = 0; i < lines_.size(); ++i) {
        const auto s = section(lines_[i]);
        if (!s.empty()) { current = s; if (s == lower(sec)) { have = true; insert = i + 1; } continue; }
        if (current != lower(sec)) continue;
        insert = i + 1; const auto t = trim(lines_[i]);
        if (t.empty() || t.front() == '#' || t.front() == ';') continue;
        const auto eq = lines_[i].find('=');
        if (eq != std::string::npos && lower(trim(lines_[i].substr(0, eq))) == lower(key)) { lines_[i] = key + " = " + value; found = true; }
    }
    if (found) return;
    if (!have) { lines_.push_back(""); lines_.push_back("[" + sec + "]"); insert = lines_.size(); }
    lines_.insert(lines_.begin() + insert, key + " = " + value);
}
void Ini::save(const fs::path& p) const {
    fs::create_directories(p.parent_path());
    const fs::path tmp(p.wstring() + L".tmp-" + std::to_wstring(GetCurrentProcessId()));
    { std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
      for (const auto& line : lines_) out << line << '\n';
      out.close(); if (!out) throw std::runtime_error("Cannot save " + path_text(p)); }
    if (!MoveFileExW(tmp.c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD error = GetLastError();
        DeleteFileW(tmp.c_str()); throw std::runtime_error("Cannot replace " + path_text(p) + " (Windows " + std::to_string(error) + ")");
    }
}
const std::vector<Language>& languages() {
    static const std::vector<Language> ls = {
        {"Spanish", L"Spanish (Spain)", L"EUes"}, {"LatinAmericanSpanish", L"Spanish (Latin America)", L"USes"},
        {"AmericanEnglish", L"English (US)", L"USen"}, {"BritishEnglish", L"English (UK)", L"EUen"},
        {"French", L"French (France)", L"EUfr"}, {"CanadianFrench", L"French (Canada)", L"USfr"},
        {"German", L"German", L"EUde"}, {"Italian", L"Italian", L"EUit"},
        {"Japanese", L"Japanese", L"JPja"}, {"Korean", L"Korean", L"KRko"},
        {"SimplifiedChinese", L"Simplified Chinese", L"CNzh"}, {"TraditionalChinese", L"Traditional Chinese", L"TWzh"}
    }; return ls;
}
std::vector<Language> available_languages(const fs::path& source) {
    std::vector<Language> out;
    for (const auto& l : languages()) if (fs::is_directory(source / L"romfs" / L"Msg" / l.resource)) out.push_back(l);
    return out;
}
std::string system_language() {
    switch (PRIMARYLANGID(GetUserDefaultUILanguage())) {
        case LANG_SPANISH: return "Spanish"; case LANG_FRENCH: return "French"; case LANG_GERMAN: return "German";
        case LANG_ITALIAN: return "Italian"; case LANG_JAPANESE: return "Japanese"; case LANG_KOREAN: return "Korean";
        default: return "AmericanEnglish";
    }
}
Settings load(const fs::path& dir) {
    Ini ini(dir / L"fefw.ini"), controls(dir / L"controls.ini"); Settings s;
    s.romfs = resolved(dir, ini.get("fs", "romfs", "romfs"));
    s.source = resolved(dir, ini.get("launcher", "source_dir", path_text(s.romfs.parent_path())));
    s.exefs = resolved(dir, ini.get("fs", "exefs", "exefs"));
    s.shaders = resolved(dir, ini.get("nvn", "shader_cache", "shader_cache/spirv"));
    s.width = number(controls.get("display", "width"), 1280); s.height = number(controls.get("display", "height"), 720);
    s.window_mode = controls.get("display", "window_mode", "windowed");
    s.output = ini.get("fefw", "output", "console");
    try { s.render_ratio = std::stod(ini.get("fefw", "render_ratio", "0")); } catch (...) { s.render_ratio = 0; }
    s.language = ini.get("system", "language", system_language());
    s.skip = ini.get("launcher", "skip_setup", "1") == "1";
    s.completed = ini.get("launcher", "setup_complete", "0") == "1";
    s.upscaler = ini.get("dlss", "upscaler", "taau") == "bilinear" ? "bilinear" : "taau";
    s.check_updates = ini.get("launcher", "check_updates", "0") == "1";
    s.fps120 = ini.get("mods", "fps120", "0") == "1";
    s.fps60 = !s.fps120 && ini.get("mods", "fps60", "0") == "1";
    s.vsync = s.completed ? controls.get("display", "vsync", "1") != "0" : true;
    s.ui_language = ini.get("launcher", "ui_language", "en") == "es" ? "es" : "en";
    auto real = [](const std::string& v, double fallback, double max) {
        try { size_t n=0; double d=std::stod(v,&n); return n==v.size()&&std::isfinite(d)&&d>=0&&d<=max?d:fallback; } catch (...) { return fallback; }
    };
    s.volume = real(ini.get("audio", "volume", controls.get("audio", "volume", "1")),1,1);
    s.mute_unfocused = controls.get("audio", "mute_unfocused", "0") == "1";
    s.deadzone = real(controls.get("pad", "deadzone", "0.10"),0.10,0.5);
    s.right_deadzone = real(controls.get("pad", "right_deadzone", std::to_string(s.deadzone)),s.deadzone,0.5);
    return s;
}
std::vector<fs::path> candidates(const fs::path& selected) {
    std::vector<fs::path> out;
    if (fs::is_regular_file(selected / L"exefs/main")) out.push_back(selected);
    std::error_code ec;
    if (fs::is_directory(selected / L"updates", ec))
        for (const auto& p : fs::directory_iterator(selected / L"updates"))
            if (p.is_directory() && fs::is_regular_file(p.path() / L"exefs/main")) out.push_back(p.path());
    return out;
}
Validation validate_source(const fs::path& source, bool hashes) {
    try {
        if (!fs::is_directory(source / L"romfs")) return invalid("The selected version is missing its romfs folder.");
        auto npdm = header(source / L"exefs/main.npdm", 0x80);
        if (npdm.empty() || std::memcmp(npdm.data(), "META", 4)) return invalid("main.npdm is missing or its header is invalid.");
        const uint32_t aci = u32(npdm.data() + 0x70);
        if (aci < 0x80 || aci > 16 * 1024 * 1024) return invalid("main.npdm contains an invalid ACI0 range.");
        npdm = header(source / L"exefs/main.npdm", static_cast<size_t>(aci) + 0x38);
        if (npdm.empty() || std::memcmp(npdm.data() + aci, "ACI0", 4) || u64(npdm.data() + aci + 0x10) != kTitleId)
            return invalid("This folder does not match the supported Fire Emblem title.");
        for (const auto& m : kModules) {
            const fs::path path = source / L"exefs" / m.name;
            const auto h = header(path, 0x100);
            if (h.empty() || std::memcmp(h.data(), "NSO0", 4)) return invalid(std::string("Missing a valid NSO module: ") + m.name);
            if (hex(h.data() + 0x40, 32) != lower(m.build)) return invalid(std::string(m.name) + ": unsupported version. Build DB93B012 / update 131072 is required.");
            if (hashes && sha(path) != lower(m.sha)) return invalid(std::string(m.name) + ": content does not match the supported build.");
        }
        if (available_languages(source).empty()) return invalid("Language resources were not found in romfs/Msg. Check that the base game and update are complete.");
        return {true, {}};
    } catch (const std::exception& e) { return invalid(e.what()); }
}
Validation validate_prepared(const fs::path& exefs) {
    try {
        for (const auto& m : kModules) {
            const auto path = exefs / m.name; const auto h = header(path, 0x100);
            if (h.empty() || std::memcmp(h.data(), "NSO0", 4) || (u32(h.data() + 12) & 0x87) || hex(h.data() + 0x40, 32) != lower(m.build))
                return invalid(std::string("Module has not been prepared: ") + m.name);
            const auto bytes = fs::file_size(path);
            for (int i = 0; i < 3; ++i) {
                const uint64_t offset = u32(h.data() + 0x10 + 0x10 * i), size = u32(h.data() + 0x18 + 0x10 * i);
                if (offset > bytes || size > bytes - offset || sha(path, offset, size) != hex(h.data() + 0xa0 + i * 32, 32))
                    return invalid(std::string("The prepared copy is damaged: ") + m.name);
            }
        }
        return {true, {}};
    } catch (const std::exception& e) { return invalid(e.what()); }
}
Validation validate_ready(const fs::path& dir, const Settings& s, bool hashes) {
    if (!s.completed) return invalid("Complete the initial setup.");
    if (const auto v = validate_source(s.source, hashes); !v.ok) return v;
    std::error_code path_error;
    if (!fs::is_directory(s.romfs, path_error) || !fs::equivalent(s.romfs, s.source / L"romfs", path_error))
        return invalid("The romfs location has changed. Select the game folder again.");
    if (s.width < 640 || s.height < 360 || s.width > 7680 || s.height > 4320) return invalid("The selected resolution is invalid.");
    const auto ls = available_languages(s.source);
    if (std::none_of(ls.begin(), ls.end(), [&](const auto& l) { return s.language == l.value; })) return invalid("The selected language is not available in this copy.");
    if (!fs::is_regular_file(dir / L"fefw.exe")) return invalid("The local executable must be generated from your copy of the game.");
    std::ifstream features(dir / L"fefw.features"); std::string revision;
    std::getline(features, revision);
    if (revision != fefw::mods::kFeatures) return invalid("Update local preparation for the current graphics and frame-rate options.");
    if (const auto v = validate_prepared(s.exefs); !v.ok) return v;
    size_t count = 0; std::error_code ec;
    if (fs::is_directory(s.shaders, ec)) for (const auto& p : fs::directory_iterator(s.shaders)) if (p.is_regular_file() && p.path().extension() == L".spv") ++count;
    if (count != 11977) return invalid("The complete shader cache for this copy must be prepared.");
    return {true, {}};
}
bool needs_setup(const fs::path& dir) {
    try { const auto s = load(dir); return !s.skip || !validate_ready(dir, s).ok; } catch (...) { return true; }
}
void edit_settings(const Settings& s, Ini& ini, Ini& controls) {
    if ((s.ui_language!="en"&&s.ui_language!="es") || !std::isfinite(s.volume) || s.volume<0 || s.volume>1 ||
        !std::isfinite(s.deadzone) || s.deadzone<0 || s.deadzone>0.5 ||
        !std::isfinite(s.right_deadzone) || s.right_deadzone<0 || s.right_deadzone>0.5)
        throw std::runtime_error("Invalid interface language, volume or controller deadzone.");
    ini.set("launcher", "ui_language", s.ui_language);
    ini.set("audio", "volume", std::to_string(s.volume));
    controls.set("audio", "volume", std::to_string(s.volume));
    controls.set("audio", "mute_unfocused", s.mute_unfocused?"1":"0");
    controls.set("pad", "deadzone", std::to_string(s.deadzone));
    controls.set("pad", "right_deadzone", std::to_string(s.right_deadzone));
    if (s.window_mode != "windowed" && s.window_mode != "borderless" && s.window_mode != "exclusive") throw std::runtime_error("Unknown display mode.");
    if (s.fps60 && s.fps120) throw std::runtime_error("Select one frame-rate target.");
    if (!std::isfinite(s.render_ratio) || (s.render_ratio != 0 && (s.render_ratio < 0.25 || s.render_ratio > 1))) throw std::runtime_error("Internal scale must be between 0.25 and 1.0, or 0 for the original size.");
    if (s.output != "console") {
        int w = 0, h = 0; char tail = 0;
        if (std::sscanf(s.output.c_str(), "%dx%d%c", &w, &h, &tail) != 2 || w < 640 || h < 360 || w > 3840 || h > 2160 || (w & 1) || (h & 1))
            throw std::runtime_error("Game output must be even WxH dimensions between 640x360 and 3840x2160.");
    }
    ini.set("fs", "exefs", path_text(s.exefs)); ini.set("fs", "romfs", path_text(s.source / L"romfs"));
    if (ini.get("fs", "save_dir").empty()) ini.set("fs", "save_dir", "save");
    ini.set("nvn", "shader_cache", path_text(s.shaders));
    ini.set("system", "language", s.language);
    // The runtime falls back to English when the title has no language mask.
    // Derive that mask from the selected copy rather than guessing from NACP.
    std::string supported;
    for (const auto& l : available_languages(s.source)) { if (!supported.empty()) supported += ','; supported += l.value; }
    ini.set("title", "supported_languages", supported);
    controls.set("tweaks", "language", s.language);
    ini.set("launcher", "source_dir", path_text(s.source)); ini.set("launcher", "skip_setup", s.skip ? "1" : "0");
    ini.set("launcher", "setup_complete", "1"); ini.set("launcher", "configuration_version", "1");
    if (s.upscaler != "taau" && s.upscaler != "bilinear") throw std::runtime_error("Unsupported 3D reconstruction mode");
    ini.set("dlss", "upscaler", s.upscaler);
    ini.set("mods", "fps60", s.fps60 ? "1" : "0");
    ini.set("mods", "fps120", s.fps120 ? "1" : "0");
    ini.set("launcher", "check_updates", s.check_updates ? "1" : "0");
    ini.set("fefw", "output", s.output);
    ini.set("fefw", "render_ratio", std::to_string(s.render_ratio));
    // Explicit dimensions from an older manual profile must not override this selection.
    ini.set("fefw", "render", "");
    ini.set("boot", "gfx", "window");
    if (ini.get("boot", "max_seconds").empty()) ini.set("boot", "max_seconds", "0");
    if (ini.get("play", "service_input_fallback").empty()) ini.set("play", "service_input_fallback", "1");
    if (ini.get("audio", "revision16_probe").empty()) ini.set("audio", "revision16_probe", "1");
    if (ini.get("nvn", "dlss_spatial_fallback").empty()) ini.set("nvn", "dlss_spatial_fallback", "1");
    if (ini.get("title", "account_save_size").empty()) ini.set("title", "account_save_size", "67108864");
    controls.set("display", "width", std::to_string(s.width)); controls.set("display", "height", std::to_string(s.height));
    controls.set("display", "window_mode", s.window_mode);
    controls.set("display", "vsync", s.vsync ? "1" : "0");
    controls.set("display", "fps_limit", s.fps120 ? "120" : "60");
}
void save(const fs::path& dir, const Settings& s) {
    if (game_running(dir)) throw std::runtime_error("Close the game before saving settings.");
    if (const auto v = validate_ready(dir, s, true); !v.ok) throw std::runtime_error(v.error);
    Ini ini(dir / L"fefw.ini"), controls(dir / L"controls.ini");
    edit_settings(s, ini, controls);
    auto portable = [&](const fs::path& path) {
        const auto absolute = fs::absolute(path).lexically_normal();
        const auto rel = absolute.lexically_relative(fs::absolute(dir).lexically_normal());
        return !rel.empty() && *rel.begin() != L".." ? path_text(rel) : path_text(absolute);
    };
    ini.set("fs", "exefs", portable(s.exefs)); ini.set("fs", "romfs", portable(s.source / L"romfs"));
    ini.set("nvn", "shader_cache", portable(s.shaders)); ini.set("launcher", "source_dir", portable(s.source));
    backup(dir / L"controls.ini"); backup(dir / L"fefw.ini");
    const bool had_controls = fs::exists(dir / L"controls.ini");
    const Ini previous_controls(dir / L"controls.ini");
    controls.save(dir / L"controls.ini");
    try { ini.save(dir / L"fefw.ini"); }
    catch (...) {
        if (had_controls) previous_controls.save(dir / L"controls.ini");
        else DeleteFileW((dir / L"controls.ini").c_str());
        throw;
    }
}
}
