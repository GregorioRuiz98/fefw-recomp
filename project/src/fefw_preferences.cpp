// SPDX-License-Identifier: GPL-3.0-or-later
#include "fefw_preferences.h"
#include <windows.h>
#include <cmath>
#include <cctype>
#include <stdexcept>
namespace fefw::setup {
namespace {
double number(const Ini& ini, const char* key, double max) {
    auto v=ini.get("preferences",key); size_t n=0; double d=std::stod(v,&n);
    if(n!=v.size()||!std::isfinite(d)||d<0||d>max) throw std::runtime_error("Invalid profile value.");
    return d;
}
bool flag(const Ini& ini,const char* key) {
    auto v=ini.get("preferences",key); if(v!="0"&&v!="1") throw std::runtime_error("Invalid profile flag."); return v=="1";
}
void small_file(const fs::path& p) {
    if(!fs::is_regular_file(p)||fs::file_size(p)>65536) throw std::runtime_error("Profile is missing or larger than 64 KiB.");
}
}
void graphics_preset(Settings& s,int p) {
    if(p<0||p>2) throw std::runtime_error("Unknown graphics preset.");
    s.output=p==0?"1280x720":p==1?"1920x1080":"2560x1440";
    s.render_ratio=p==0?0.5:p==1?2.0/3.0:1.0; s.upscaler="taau"; s.vsync=true; s.fps60=s.fps120=false;
}
void export_preferences(const fs::path& file,const Settings& s) {
    Ini check(fs::path{}),controls(fs::path{}); edit_settings(s,check,controls);
    // Explicit allowlist. Never copy a user's INI, paths, bindings or save data.
    Ini out(fs::path{}); auto put=[&](const char* k,const std::string& v){out.set("preferences",k,v);};
    put("schema","1"); put("window_mode",s.window_mode); put("width",std::to_string(s.width)); put("height",std::to_string(s.height));
    put("output",s.output); put("render_ratio",std::to_string(s.render_ratio)); put("upscaler",s.upscaler);
    put("fps",s.fps120?"120":s.fps60?"60":"0"); put("vsync",s.vsync?"1":"0");
    put("volume",std::to_string(s.volume)); put("mute_unfocused",s.mute_unfocused?"1":"0");
    put("deadzone",std::to_string(s.deadzone)); put("right_deadzone",std::to_string(s.right_deadzone));
    put("ui_language",s.ui_language); put("skip",s.skip?"1":"0"); put("check_updates",s.check_updates?"1":"0"); out.save(file);
}
Settings import_preferences(const fs::path& file,const Settings& current) {
    small_file(file); Ini in(file); if(in.get("preferences","schema")!="1") throw std::runtime_error("Unsupported settings profile.");
    Settings s=current;
    s.window_mode=in.get("preferences","window_mode"); s.output=in.get("preferences","output"); s.upscaler=in.get("preferences","upscaler");
    s.ui_language=in.get("preferences","ui_language");
    double w=number(in,"width",16384),h=number(in,"height",16384);
    if(w<640||h<360||w!=std::floor(w)||h!=std::floor(h)) throw std::runtime_error("Invalid window dimensions.");
    s.width=static_cast<int>(w); s.height=static_cast<int>(h); s.render_ratio=number(in,"render_ratio",1);
    auto fps=in.get("preferences","fps"); if(fps!="0"&&fps!="60"&&fps!="120") throw std::runtime_error("Invalid frame-rate target.");
    s.fps60=fps=="60"; s.fps120=fps=="120"; s.vsync=flag(in,"vsync");
    s.volume=number(in,"volume",1); s.mute_unfocused=flag(in,"mute_unfocused");
    s.deadzone=number(in,"deadzone",0.5); s.right_deadzone=number(in,"right_deadzone",0.5);
    s.skip=flag(in,"skip"); s.check_updates=flag(in,"check_updates");
    Ini dummy(fs::path{}),controls(fs::path{}); edit_settings(s,dummy,controls); return s;
}
void controller_profile(const fs::path& dir,const std::string& guid,Settings& s,bool write) {
    if(guid.size()!=32||guid.find_first_not_of("0123456789abcdef")!=std::string::npos) throw std::runtime_error("Select a connected controller.");
    auto p=dir/L"controller-profiles"/(guid+".ini");
    if(write) {
        if(!std::isfinite(s.deadzone)||!std::isfinite(s.right_deadzone)||s.deadzone<0||s.deadzone>0.5||s.right_deadzone<0||s.right_deadzone>0.5) throw std::runtime_error("Invalid controller deadzone.");
        fs::create_directories(p.parent_path()); Ini out(fs::path{}); out.set("preferences","schema","1");
        out.set("preferences","deadzone",std::to_string(s.deadzone)); out.set("preferences","right_deadzone",std::to_string(s.right_deadzone)); out.save(p);
    } else {
        small_file(p); Ini in(p); if(in.get("preferences","schema")!="1") throw std::runtime_error("Unsupported controller profile.");
        double left=number(in,"deadzone",0.5),right=number(in,"right_deadzone",0.5); s.deadzone=left; s.right_deadzone=right;
    }
}
void export_diagnostics(const fs::path& file,const fs::path& dir,const Settings& s,int controllers) {
    Ini check(fs::path{}),controls(fs::path{}); edit_settings(s,check,controls);
    // No log scraping: free-form logs may contain personal paths or game text.
    Ini out(fs::path{}); auto put=[&](const char* k,const std::string& v){out.set("diagnostics",k,v);};
    put("schema","1"); put("tools","v0.1.0-preview.12"); put("platform","Windows x64");
    put("ready",validate_ready(dir,s,false).ok?"1":"0"); put("controllers",std::to_string(controllers));
    put("prepared_exefs_present",fs::is_directory(s.exefs)?"1":"0"); put("shader_directory_present",fs::is_directory(s.shaders)?"1":"0");
    put("preparation_log_present",fs::is_regular_file(dir/L"preparation.log")?"1":"0");
    MEMORYSTATUSEX mem{sizeof mem}; if(GlobalMemoryStatusEx(&mem)) put("memory_gib",std::to_string(mem.ullTotalPhys/(1024ull*1024*1024)));
    DISPLAY_DEVICEW display{sizeof display};
    if(EnumDisplayDevicesW(nullptr,0,&display,0)) {
        // Hardware ID only, never DeviceKey or driver/user file locations.
        std::wstring id=display.DeviceID;
        if(id.find(L"PCI\\VEN_")==0) put("display_pci_id",utf8(id.substr(0,44)));
    }
    put("output",s.output); put("render_ratio",std::to_string(s.render_ratio)); put("upscaler",s.upscaler);
    put("fps",s.fps120?"120":s.fps60?"60":"original"); put("vsync",s.vsync?"1":"0");
    out.save(file);
}
}
