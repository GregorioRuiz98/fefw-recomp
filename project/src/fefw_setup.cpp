// SPDX-License-Identifier: GPL-3.0-or-later
#include "fefw_config.h"
#include "fefw_preferences.h"
#include "fefw_controllers.h"
#include <windows.h>
#include <commctrl.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <set>
#include <cmath>
#include <map>
#include <memory>
#include <optional>
#include "fefw_setup_theme.h"
#include "fefw_setup_language.h"

namespace s = fefw::setup;
namespace fs = std::filesystem;
namespace {
enum { Folder = 100, Browse, Versions, Resolution, Mode, Lang, Skip, Save, SavePlay, Status, Progress, Vsync, Fps60, OpenLog, Upscaler,
    Tabs, Title, Subtitle, StatusTitle, UpscalerHelp, Recommended, OpenInstall, GameOutput, RenderRatio, CheckUpdates, InstallUpdate, UpdateNotes, UpdateText, AutoCheck, Revert, Dirty, RenderPreview, FrameHelp, Brand, BrandSub, BuildLabel,
    Pads, Rescan, PadState, PadValues, Deadzone, RightDeadzone, SavePad, LoadPad, Volume, Mute, UiLanguage, Export, Import, Diagnostics, Preset, ApplyPreset, ResetSection, Stages, NavBase = 300 };
fs::path game_dir;
s::Settings settings, pending;
HWND window{};
HFONT font{}, title_font{}, heading_font{}, small_font{};
constexpr int page_count = 9;
const int nav_order[] = {0,1,4,5,6,7,2,3,8};
const wchar_t* page_names[] = {L"Game files",L"Picture",L"Startup",L"Updates",L"Performance",L"Window",L"Controllers",L"Audio",L"Tools"};
const wchar_t* page_descriptions[] = {
    L"Connect your extracted copy and choose its language.", L"Choose 3D detail and how frames are reconstructed.",
    L"Choose how the game opens and reconnect your controller.", L"Keep project tools current while preserving your local data.",
    L"Choose a gameplay target. Performance depends on your PC.", L"Choose how the game appears on your display.",
    L"Test your controller and tune its sticks before playing.",L"Set master volume and background audio behavior.",L"Manage portable settings and export a private-data-free report."};
namespace ui = fefw::setup::ui;
struct SavedField { std::wstring value; int choice=-1; LRESULT checked=0; };
std::map<int,SavedField> original_fields;
bool form_ready=false, dirty=false;
void changed();
void select_page(int p);
void localize();
s::Settings read_form();
constexpr const wchar_t* tools_version = L"v0.1.0-preview.11";
std::map<HWND,std::wstring> captions;
std::map<HWND,std::vector<std::wstring>> combo_labels;
std::unique_ptr<s::Controllers> controllers;
std::vector<s::Controller> pads;
HWND pages[page_count]{}, parent{};
HWND folder_tip{};
std::wstring tip_text;
struct Placement { HWND handle; int page, x, y, width, height; bool right_half; };
std::vector<Placement> placements;
std::vector<HWND> headings, small_text;
int active_page = 0, creating_page = -1, scroll_offset[page_count]{};
bool folder_scanned = false;
std::wstring last_folder;
int dpi = 96, result = 3;
bool start_game = false, test_ui = false;
std::vector<fs::path> versions;
std::vector<s::Language> langs;
std::vector<std::pair<int, int>> sizes;
HANDLE preparation{}, job{}, update_process{};

int px(int v) { return MulDiv(v, dpi, 96); }
HWND item(int id) {
    if (HWND h = GetDlgItem(window, id)) return h;
    for (HWND p : pages) if (p) if (HWND h = GetDlgItem(p, id)) return h;
    return nullptr;
}
std::wstring text(HWND h) {
    const int n = GetWindowTextLengthW(h); std::wstring v(n + 1, L'\0');
    GetWindowTextW(h, v.data(), n + 1); v.resize(n); return v;
}
void caption(HWND h,const wchar_t* value) {
    captions[h]=value?value:L""; const auto v=ui::translate(captions[h]);
    if(text(h)!=v) SetWindowTextW(h,v.c_str());
}
#define SetWindowTextW caption
void status(const std::wstring& v) { SetWindowTextW(item(Status), v.c_str()); }
void state(const std::wstring& v) { SetWindowTextW(item(StatusTitle), v.c_str()); }
void error(const std::string& v) { MessageBoxW(window, s::wide(v).c_str(), L"Fire Emblem Recomp", MB_OK | MB_ICONERROR); }
void choose(HWND h, int n) { SendMessageW(h, CB_SETCURSEL, n, 0); }
int selection(int id) { return static_cast<int>(SendMessageW(item(id), CB_GETCURSEL, 0, 0)); }
void add(HWND h, const std::wstring& v) {
    auto& labels=combo_labels[h]; if(SendMessageW(h,CB_GETCOUNT,0,0)==0) labels.clear();
    labels.push_back(v); const auto translated=ui::translate(v); SendMessageW(h, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(translated.c_str()));
}
void enabled(bool on) {
    for (int id : {Folder, Browse, Versions, Resolution, Mode, Lang, Skip, Save, SavePlay, Vsync, Fps60, Upscaler, GameOutput, RenderRatio, AutoCheck, CheckUpdates,
        Deadzone,RightDeadzone,Volume,Mute,UiLanguage,Export,Import,Diagnostics,SavePad,LoadPad,Preset,ApplyPreset,ResetSection}) EnableWindow(item(id), on);
    if (on) EnableWindow(item(Resolution), selection(Mode) == 0);
    EnableWindow(item(Save), on && !versions.empty() && !langs.empty());
    EnableWindow(item(SavePlay), on && !versions.empty() && !langs.empty());
    EnableWindow(item(Versions), on && !versions.empty());
    EnableWindow(item(Lang), on && !langs.empty());
    EnableWindow(item(Recommended), on);
    EnableWindow(item(Revert),on&&dirty);
    EnableWindow(item(SavePad),on&&!pads.empty()); EnableWindow(item(LoadPad),on&&!pads.empty());
    EnableWindow(item(ResetSection),on&&active_page!=0);
    if (!on) EnableWindow(item(InstallUpdate), FALSE);
}
HWND control(const wchar_t* cls, const wchar_t* label, int id, int x, int y, int w, int h, DWORD style = 0) {
    const bool combo = std::wstring(cls)==L"COMBOBOX";
    const bool button = std::wstring(cls)==L"BUTTON";
    if (combo) style |= CBS_OWNERDRAWFIXED | CBS_HASSTRINGS;
    if (button && (style & BS_TYPEMASK) != BS_AUTOCHECKBOX) style = (style & ~BS_TYPEMASK) | BS_OWNERDRAW;
    const auto translated=ui::translate(label?label:L"");
    HWND c = CreateWindowExW(cls == std::wstring(L"EDIT") ? WS_EX_CLIENTEDGE : 0, cls, translated.c_str(),
        WS_CHILD | WS_VISIBLE | style, px(std::max(x, 0)), px(y), px(w > 0 ? w : 100), px(h), parent ? parent : window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    captions[c]=label?label:L"";
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    if (button || combo) SetWindowTheme(c,L"",L"");
    if (button) SetWindowSubclass(c,ui::interactive,1,0);
    if (combo) { SetWindowSubclass(c,ui::combo_frame,2,0); SendMessageW(c,CB_SETITEMHEIGHT,static_cast<WPARAM>(-1),px(28)); SendMessageW(c,CB_SETITEMHEIGHT,0,px(28)); }
    if (creating_page >= 0) placements.push_back({c, creating_page, x, y, w, h, false});
    return c;
}
void hint(HWND h) { small_text.push_back(h); SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(small_font), TRUE); }
void heading(HWND h) { headings.push_back(h); SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(heading_font), TRUE); }
void right(HWND) { placements.back().right_half = true; }
void fonts() {
    const HFONT old[] = {font, title_font, heading_font, small_font};
    auto make = [](int height, int weight) { return CreateFontW(-px(height), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI"); };
    font = make(16, FW_NORMAL); title_font = make(30, FW_SEMIBOLD);
    heading_font = make(18, FW_SEMIBOLD); small_font = make(14, FW_NORMAL);
    EnumChildWindows(window, [](HWND h, LPARAM) -> BOOL { SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE); return TRUE; }, 0);
    for (HWND h : headings) SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(heading_font), TRUE);
    for (HWND h : small_text) SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(small_font), TRUE);
    SendMessageW(item(Title), WM_SETFONT, reinterpret_cast<WPARAM>(title_font), TRUE);
    for (HFONT f : old) if (f) DeleteObject(f);
}
void move(HWND h, int x, int y, int w, int height) { MoveWindow(h, px(x), px(y), px(w), px(height), TRUE); }
int logical(int v) { return MulDiv(v, 96, dpi); }
int page_content_height(int p) { const int heights[]={504,700,350,390,322,330,560,230,454}; return heights[p]; }
void page_layout(int p) {
    RECT r{}; GetClientRect(pages[p], &r);
    const int width = logical(r.right), height = logical(r.bottom);
    SCROLLINFO si{sizeof si, SIF_RANGE | SIF_PAGE | SIF_POS};
    si.nMax = page_content_height(p) - 1; si.nPage = std::max(1, height); si.nPos = scroll_offset[p];
    SetScrollInfo(pages[p], SB_VERT, &si, TRUE);
    si.fMask = SIF_POS; GetScrollInfo(pages[p], SB_VERT, &si); scroll_offset[p] = si.nPos;
    GetClientRect(pages[p], &r);
    const int available = logical(r.right) - 8;
    for (const auto& c : placements) if (c.page == p) {
        const int half = (available - 24) / 2;
        const int x = c.right_half ? half + 24 : c.x == -1 ? available - c.width : c.x;
        const int w = c.width == -1 ? available - x : c.width == -2 ? half : c.width == -3 ? available - 140 : c.width;
        move(c.handle, x, c.y - scroll_offset[p], w, c.height);
    }
    InvalidateRect(pages[p], nullptr, TRUE);
}
void layout() {
    if (!item(Tabs) || !pages[2]) return;
    RECT r{}; GetClientRect(window, &r);
    const int w = logical(r.right), h = logical(r.bottom);
    const int side = w < 850 ? 162 : 204;
    move(item(Brand),18,26,side-32,26); move(item(BrandSub),18,56,side-32,48);
    move(item(BuildLabel),18,h-58,side-28,42);
    const int nav_step=std::min(44,std::max(29,(h-260)/page_count));
    for (int i=0;i<page_count;++i) move(item(NavBase+nav_order[i]),12,122+i*nav_step,side-24,nav_step-3);
    move(item(Title),side+32,24,w-side-64,42);
    move(item(Subtitle),side+32,74,w-side-64,40);
    move(item(ResetSection),18,h-104,side-36,34);
    for (int p=0;p<page_count;++p) {
        move(pages[p],side+32,132,w-side-64,std::max(80,h-312)); page_layout(p);
    }
    move(item(StatusTitle),side+32,h-158,w-side-64,24);
    move(item(Status),side+32,h-128,w-side-64,42);
    move(item(Progress),side+32,h-80,w-side-64,3);
    move(item(Dirty),side+32,h-60,std::max(100,w-470-side-44),20);
    move(item(OpenLog),side+32,h-34,116,26);
    move(item(Revert),w-470,h-58,90,36);
    move(item(IDCANCEL),w-370,h-58,90,36);
    move(item(Save),w-270,h-58,90,36);
    move(item(SavePlay),w-170,h-58,138,36);
    InvalidateRect(window,nullptr,FALSE);
}
void select_page(int p) {
    active_page = std::clamp(p, 0, page_count - 1); TabCtrl_SetCurSel(item(Tabs), active_page);
    SetWindowTextW(item(Title),page_names[active_page]); SetWindowTextW(item(Subtitle),page_descriptions[active_page]);
    for (int i=0;i<page_count;++i) InvalidateRect(item(NavBase+i),nullptr,TRUE);
    for (int i = 0; i < page_count; ++i) ShowWindow(pages[i], i == active_page ? SW_SHOW : SW_HIDE);
    SetWindowPos(pages[active_page], HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    page_layout(active_page);
    EnableWindow(item(ResetSection),active_page!=0&&!preparation);
}
void scroll_page(int p, int value) {
    scroll_offset[p] = std::max(0, value); page_layout(p);
}
LRESULT CALLBACK page_proc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    const int p = static_cast<int>(GetWindowLongPtrW(h, GWLP_USERDATA));
    switch (msg) {
        case WM_COMMAND: return SendMessageW(window, msg, w, l);
        case WM_CTLCOLORSTATIC: case WM_CTLCOLOREDIT: case WM_CTLCOLORLISTBOX: case WM_CTLCOLORBTN:
        case WM_DRAWITEM: case WM_MEASUREITEM: return SendMessageW(window,msg,w,l);
        case WM_ERASEBKGND: { RECT r{}; GetClientRect(h,&r); FillRect(reinterpret_cast<HDC>(w),&r,ui::background); return 1; }
        case WM_VSCROLL: {
            SCROLLINFO si{sizeof si, SIF_ALL}; GetScrollInfo(h, SB_VERT, &si);
            int v = si.nPos;
            switch (LOWORD(w)) {
                case SB_LINEUP: v -= 24; break; case SB_LINEDOWN: v += 24; break;
                case SB_PAGEUP: v -= si.nPage; break; case SB_PAGEDOWN: v += si.nPage; break;
                case SB_THUMBTRACK: v = si.nTrackPos; break;
            }
            scroll_page(p, v); return 0;
        }
        case WM_MOUSEWHEEL:
            scroll_page(p, scroll_offset[p] - GET_WHEEL_DELTA_WPARAM(w) / WHEEL_DELTA * 48); return 0;
    }
    return DefWindowProcW(h, msg, w, l);
}
void reconstruction_help() {
    SetWindowTextW(item(UpscalerHelp), selection(Upscaler) == 1
        ? L"Bilinear enlarges each frame separately. Use it to compare against temporal reconstruction."
        : L"Smooths 3D edges using detail from successive frames. Built in; no extra downloads needed. Restart the game after changing this.");
}
void recommended() {
    choose(item(Mode), 0); choose(item(Upscaler), 0);
    for (size_t i = 0; i < sizes.size(); ++i) if (sizes[i] == std::pair{1280, 720}) choose(item(Resolution), static_cast<int>(i));
    SendMessageW(item(Vsync), BM_SETCHECK, BST_CHECKED, 0); choose(item(Fps60), 0); choose(item(GameOutput), 0); choose(item(RenderRatio), 0);
    EnableWindow(item(Resolution), TRUE); reconstruction_help();
    status(L"Recommended graphics selected. Use Save to apply them; your game folder, language and startup preference are kept.");
}
void refresh_languages() {
    const int old = selection(Lang);
    const std::string preferred = old >= 0 && static_cast<size_t>(old) < langs.size() ? langs[old].value : settings.language;
    const int v = selection(Versions); langs.clear(); SendMessageW(item(Lang), CB_RESETCONTENT, 0, 0);
    if (v >= 0 && static_cast<size_t>(v) < versions.size()) langs = s::available_languages(versions[v]);
    int selected = 0;
    for (size_t i = 0; i < langs.size(); ++i) { add(item(Lang), langs[i].label); if (preferred == langs[i].value) selected = static_cast<int>(i); }
    if (!langs.empty()) choose(item(Lang), selected);
    EnableWindow(item(Lang), !langs.empty());
    EnableWindow(item(Save), !preparation && !versions.empty() && !langs.empty());
    EnableWindow(item(SavePlay), !preparation && !versions.empty() && !langs.empty());
}
void refresh_folder(bool force = false) {
    const std::wstring folder = text(item(Folder));
    if (!force && folder_scanned && folder == last_folder) return;
    folder_scanned = true; last_folder = folder;
    versions.clear(); SendMessageW(item(Versions), CB_RESETCONTENT, 0, 0);
    const auto found = folder.empty() ? std::vector<fs::path>{} : s::candidates(fs::path(folder)); std::string last;
    for (const auto& p : found) {
        const auto valid = s::validate_source(p, false);
        if (valid.ok) { versions.push_back(p); add(item(Versions), p.filename().wstring() + L" — supported build DB93B012"); }
        else last = valid.error;
    }
    if (versions.empty()) {
        state(L"Choose your game folder");
        status(last.empty() ? L"Select the extracted game folder containing exefs and romfs, or its parent update folder." : s::wide(last));
    } else {
        choose(item(Versions), 0);
        state(settings.completed ? L"Local copy recognized" : L"Ready to prepare");
        status(L"Version recognized. Files will be verified before saving.");
    }
    EnableWindow(item(Versions), !versions.empty()); refresh_languages();
}
void browse() {
    IFileOpenDialog* dialog{};
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) { error("Cannot open the folder picker."); return; }
    DWORD options{}; dialog->GetOptions(&options); dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dialog->SetTitle(L"Select your extracted Fire Emblem copy");
    const fs::path current(text(item(Folder)));
    if (fs::is_directory(current)) {
        IShellItem* initial{};
        if (SUCCEEDED(SHCreateItemFromParsingName(current.c_str(), nullptr, IID_PPV_ARGS(&initial)))) { dialog->SetFolder(initial); initial->Release(); }
    }
    if (SUCCEEDED(dialog->Show(window))) {
        IShellItem* chosen{};
        if (SUCCEEDED(dialog->GetResult(&chosen))) {
            PWSTR path{};
            if (SUCCEEDED(chosen->GetDisplayName(SIGDN_FILESYSPATH, &path))) { SetWindowTextW(item(Folder), path); CoTaskMemFree(path); refresh_folder(true); }
            chosen->Release();
        }
    }
    dialog->Release();
}
void launch_game() {
    const auto exe = game_dir / L"fefw.exe";
    std::wstring args = s::quote_arg(exe.wstring()) + L" --skip-setup --game " + s::quote_arg(game_dir.wstring());
    STARTUPINFOW si{sizeof si}; PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), args.data(), nullptr, nullptr, FALSE, 0, nullptr, game_dir.c_str(), &si, &pi)) throw std::runtime_error("Cannot start fefw.exe.");
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
}
void finish_save() {
    s::save(game_dir, pending); result = 0;
    if (start_game) launch_game();
    DestroyWindow(window);
}
void prepare() {
    const auto script = game_dir / L"tools/Prepare-Game.ps1";
    if (!fs::is_regular_file(script)) throw std::runtime_error("Local preparation tools are missing. Keep the tools folder next to the setup tool.");
    wchar_t system[MAX_PATH]{}; GetSystemDirectoryW(system, MAX_PATH);
    const fs::path ps = fs::path(system) / L"WindowsPowerShell/v1.0/powershell.exe";
    std::wstring args = s::quote_arg(ps.wstring()) + L" -NoProfile -ExecutionPolicy Bypass -File " + s::quote_arg(script.wstring()) +
        L" -GameDirectory " + s::quote_arg(game_dir.wstring()) + L" -Source " + s::quote_arg(pending.source.wstring());
    STARTUPINFOW si{sizeof si}; si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    job = CreateJobObjectW(nullptr, nullptr);
    if (!job) throw std::runtime_error("Cannot create the preparation process.");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION ji{}; ji.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &ji, sizeof ji)) { CloseHandle(job); job = nullptr; throw std::runtime_error("Cannot manage local preparation."); }
    if (!CreateProcessW(ps.c_str(), args.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, game_dir.c_str(), &si, &pi)) {
        CloseHandle(job); job = nullptr; throw std::runtime_error("Cannot start local preparation.");
    }
    if (!AssignProcessToJobObject(job, pi.hProcess)) { TerminateProcess(pi.hProcess, 3); CloseHandle(pi.hThread); CloseHandle(pi.hProcess); CloseHandle(job); job = nullptr; throw std::runtime_error("Could not isolate the preparation process."); }
    preparation = pi.hProcess; ResumeThread(pi.hThread); CloseHandle(pi.hThread);
    EnableWindow(item(OpenLog), TRUE);
    enabled(false); ShowWindow(item(Progress), SW_SHOW); SendMessageW(item(Progress), PBM_SETMARQUEE, TRUE, 80);
    state(L"Preparing your local copy"); SetWindowTextW(item(IDCANCEL), L"Stop preparation");
    status(L"First preparation may take around 30–40 minutes. Completed build work is kept if you stop and resume later.");
    SetTimer(window, 1, 500, nullptr);
}
void apply(bool play) {
    if (preparation) return;
    if (s::game_running(game_dir)) throw std::runtime_error("Close the game before changing its settings.");
    const int v = selection(Versions), l = selection(Lang), r = selection(Resolution);
    if (v < 0 || l < 0 || r < 0) throw std::runtime_error("Select a valid folder, resolution and language.");
    pending = read_form(); pending.source = versions.at(v); pending.romfs = pending.source / L"romfs";
    pending.completed = true; start_game = play;
    state(L"Verifying your local copy");
    status(L"Checking original files and the prepared copy before saving…"); UpdateWindow(window);
    const auto original = s::validate_source(pending.source, true);
    if (!original.ok) throw std::runtime_error(original.error);
    if (s::validate_ready(game_dir, pending, false).ok) finish_save(); else prepare();
}
void refresh_update() {
    const s::Ini status_file(game_dir / L"update.status");
    const auto message = status_file.get("update", "message", "No update check has completed yet.");
    SetWindowTextW(item(UpdateText), s::wide(message).c_str());
    const bool busy = update_process && WaitForSingleObject(update_process, 0) != WAIT_OBJECT_0;
    EnableWindow(item(InstallUpdate), !busy && !preparation && !s::game_running(game_dir) && status_file.get("update", "state") == "available");
    EnableWindow(item(CheckUpdates), !busy && !preparation);
    if (!busy) {
        std::ifstream in(game_dir / L"update-notes.txt", std::ios::binary);
        std::string notes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (notes.size() > 128 * 1024) notes.resize(128 * 1024);
        SetWindowTextW(item(UpdateNotes), s::wide(notes).c_str());
    }
}
void update_tools(bool install) {
    if (preparation || (update_process && WaitForSingleObject(update_process, 0) != WAIT_OBJECT_0)) return;
    if (install && dirty && MessageBoxW(window,L"Install the update and discard unsaved settings?",L"Unsaved settings",MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)!=IDYES) return;
    if (install && s::game_running(game_dir)) throw std::runtime_error("Close the game before installing updates.");
    fs::path script = game_dir / L"project/tools/Update-Release.ps1";
    if (!fs::is_regular_file(script)) script = game_dir / L"tools/Update-Release.ps1";
    if (!fs::is_regular_file(script)) throw std::runtime_error("Update tools are missing. Extract the full public package first.");
    wchar_t system[MAX_PATH]{}; GetSystemDirectoryW(system, MAX_PATH);
    const auto ps = fs::path(system) / L"WindowsPowerShell/v1.0/powershell.exe";
    std::wstring args = s::quote_arg(ps.wstring()) + L" -NoProfile -ExecutionPolicy Bypass -File " + s::quote_arg(script.wstring()) +
        L" -GameDirectory " + s::quote_arg(game_dir.wstring()) + L" -Current " + tools_version + (install ? L" -Action Install -ParentId " + std::to_wstring(GetCurrentProcessId()) : L" -Action Check");
    STARTUPINFOW si{sizeof si}; si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(ps.c_str(), args.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, game_dir.c_str(), &si, &pi)) throw std::runtime_error("Cannot start update tools.");
    if (update_process) CloseHandle(update_process);
    update_process = pi.hProcess; CloseHandle(pi.hThread);
    EnableWindow(item(CheckUpdates), FALSE); EnableWindow(item(InstallUpdate), FALSE);
    SetWindowTextW(item(UpdateText), L"Checking the project's GitHub releases…");
    if (install) { result = 0; DestroyWindow(window); }
    else SetTimer(window, 2, 400, nullptr);
}
const int fields[]={Folder,Versions,Lang,Mode,Resolution,Upscaler,Fps60,GameOutput,RenderRatio,Skip,Vsync,AutoCheck,Volume,Mute,Deadzone,RightDeadzone,UiLanguage};
bool checkbox(int id) { return id==Skip||id==Vsync||id==AutoCheck||id==Mute; }
SavedField field_value(int id) {
    const int index=selection(id); auto label=text(item(id));
    auto i=combo_labels.find(item(id)); if(i!=combo_labels.end()&&index>=0&&static_cast<size_t>(index)<i->second.size()&&label==ui::translate(i->second[index])) label=i->second[index];
    return {label,index,SendMessageW(item(id),BM_GETCHECK,0,0)};
}
void remember_form() { for(int id:fields) original_fields[id]=field_value(id); }
void preview() {
    if(!item(RenderPreview)||!item(FrameHelp)) return;
    std::wstring message=selection(RenderRatio)==0?L"The game chooses its original output and internal resolution.":L"Original game output; 3D scale: "+text(item(RenderRatio))+L" per dimension.";
    if(text(item(GameOutput))!=ui::translate(L"Original game output")) {
        int w=0,h=0;
        wchar_t tail=0;
        if(swscanf_s(text(item(GameOutput)).c_str(),L"%dx%d%lc",&w,&h,&tail,1)==2 && w>=640 && h>=360 && w<=3840 && h<=2160 && !(w&1) && !(h&1)) {
            double ratio=selection(RenderRatio)==0?0.5:std::wcstod(text(item(RenderRatio)).c_str(),nullptr)/100;
            if(w*h>2560*1440) ratio=std::min(ratio,0.5);
            const int rw=static_cast<int>(std::lround(w*ratio/2))*2,rh=static_cast<int>(std::lround(h*ratio/2))*2;
            message=L"3D input: "+std::to_wstring(rw)+L" × "+std::to_wstring(rh)+L"  →  Output: "+std::to_wstring(w)+L" × "+std::to_wstring(h);
        } else message=L"Enter even WxH dimensions between 640x360 and 3840x2160.";
    }
    SetWindowTextW(item(RenderPreview),message.c_str());
    SetWindowTextW(item(FrameHelp),selection(Fps60)==2?L"120 FPS target with measured-time correction. Menus keep their 60 FPS target. Requires a fast CPU and GPU; sustained 120 FPS is not guaranteed.":selection(Fps60)==1?L"60 FPS target using the checked timing patch. Performance and compatibility depend on your PC and the scene.":L"Recommended for original timing. The game selects its own frame rate (typically 30 FPS in gameplay and 60 FPS in menus).");
}
void changed() {
    if(!form_ready) return;
    dirty=false;
    for(int id:fields) {
        auto now=field_value(id); const auto& old=original_fields.at(id);
        if(checkbox(id)?now.checked!=old.checked:now.value!=old.value) dirty=true;
    }
    SetWindowTextW(item(Dirty),dirty?L"Unsaved changes":L"No pending changes");
    EnableWindow(item(Revert),dirty&&!preparation); preview();
}
void revert_form() {
    if(preparation) return;
    form_ready=false;
    SetWindowTextW(item(Folder),original_fields.at(Folder).value.c_str()); refresh_folder(true);
    for(int id:fields) {
        const auto& old=original_fields.at(id);
        if(checkbox(id)) SendMessageW(item(id),BM_SETCHECK,old.checked,0);
        else if(id!=Folder) { choose(item(id),old.choice); if(old.choice<0) SetWindowTextW(item(id),old.value.c_str()); }
    }
    EnableWindow(item(Resolution),selection(Mode)==0); reconstruction_help();
    form_ready=true; localize(); changed(); status(L"Pending changes reverted. Saved settings were not modified.");
}
s::Settings read_form() {
    auto value=settings;
    auto checked=[](int id){return SendMessageW(item(id),BM_GETCHECK,0,0)==BST_CHECKED;};
    if(selection(Lang)>=0) value.language=langs.at(selection(Lang)).value;
    if(selection(Resolution)>=0) {value.width=sizes.at(selection(Resolution)).first; value.height=sizes.at(selection(Resolution)).second;}
    value.window_mode=selection(Mode)==2?"exclusive":selection(Mode)==1?"borderless":"windowed";
    value.output=text(item(GameOutput))==ui::translate(L"Original game output")?"console":s::utf8(text(item(GameOutput)));
    value.render_ratio=selection(RenderRatio)==0?0:std::wcstod(text(item(RenderRatio)).c_str(),nullptr)/100;
    value.upscaler=selection(Upscaler)==1?"bilinear":"taau"; value.fps60=selection(Fps60)==1; value.fps120=selection(Fps60)==2;
    value.skip=checked(Skip); value.vsync=checked(Vsync); value.check_updates=checked(AutoCheck); value.mute_unfocused=checked(Mute);
    value.volume=std::wcstod(text(item(Volume)).c_str(),nullptr)/100;
    value.deadzone=std::wcstod(text(item(Deadzone)).c_str(),nullptr)/100;
    value.right_deadzone=std::wcstod(text(item(RightDeadzone)).c_str(),nullptr)/100;
    value.ui_language=selection(UiLanguage)==1?"es":"en"; return value;
}
void localize() {
    const bool was_ready=form_ready; form_ready=false;
    std::map<HWND,SavedField> current_combos;
    for(const auto& [h,labels]:combo_labels) {
        auto current=text(h); int index=static_cast<int>(SendMessageW(h,CB_GETCURSEL,0,0));
        if(index>=0&&static_cast<size_t>(index)<labels.size()&&current!=ui::translate(labels[index])) index=-1;
        current_combos[h]={current,index,0};
    }
    ui::spanish=selection(UiLanguage)==1;
    // Copy: caption() records source text; translations never become source keys.
    const auto labels=captions;
    for(const auto& [h,label]:labels) if(IsWindow(h)) {
        wchar_t cls[40]{}; GetClassNameW(h,cls,40);
        if(std::wstring(cls)!=L"EDIT"&&std::wstring(cls)!=L"COMBOBOX") SetWindowTextW(h,label.c_str());
    }
    for(const auto& [h,labels]:combo_labels) {
        const auto& current=current_combos.at(h).value; const int index=current_combos.at(h).choice;
        SendMessageW(h,CB_RESETCONTENT,0,0);
        for(const auto& label:labels) {const auto t=ui::translate(label); SendMessageW(h,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(t.c_str()));}
        choose(h,index); if(index<0) SetWindowTextW(h,current.c_str());
    }
    reconstruction_help(); select_page(active_page); form_ready=was_ready; changed();
}
void percent(int id,double value) {
    wchar_t label[32]{}; std::swprintf(label,32,L"%.4f%%",value*100);
    int best=-1; const int count=static_cast<int>(SendMessageW(item(id),CB_GETCOUNT,0,0));
    for(int i=0;i<count;++i) {wchar_t v[64]{}; SendMessageW(item(id),CB_GETLBTEXT,i,reinterpret_cast<LPARAM>(v)); if(std::abs(std::wcstod(v,nullptr)/100-value)<0.00001) best=i;}
    if(best<0) {add(item(id),label); best=count;} choose(item(id),best);
}
void put_form(const s::Settings& v) {
    const bool ready=form_ready; form_ready=false;
    choose(item(Mode),v.window_mode=="exclusive"?2:v.window_mode=="borderless"?1:0);
    auto size=std::pair{v.width,v.height}; auto pos=std::find(sizes.begin(),sizes.end(),size);
    if(pos==sizes.end()) {sizes.push_back(size); add(item(Resolution),std::to_wstring(v.width)+L" × "+std::to_wstring(v.height)); pos=sizes.end()-1;}
    choose(item(Resolution),static_cast<int>(pos-sizes.begin()));
    if(v.output=="console") choose(item(GameOutput),0); else SetWindowTextW(item(GameOutput),s::wide(v.output).c_str());
    if(v.render_ratio==0) choose(item(RenderRatio),0); else percent(RenderRatio,v.render_ratio);
    choose(item(Upscaler),v.upscaler=="bilinear"?1:0); choose(item(Fps60),v.fps120?2:v.fps60?1:0);
    for(auto [id,checked]:{std::pair{Skip,v.skip},{Vsync,v.vsync},{AutoCheck,v.check_updates},{Mute,v.mute_unfocused}}) SendMessageW(item(id),BM_SETCHECK,checked?BST_CHECKED:BST_UNCHECKED,0);
    percent(Volume,v.volume); percent(Deadzone,v.deadzone); percent(RightDeadzone,v.right_deadzone); choose(item(UiLanguage),v.ui_language=="es"?1:0);
    EnableWindow(item(Resolution),selection(Mode)==0); form_ready=ready; localize(); changed();
}
void scan_controllers(bool force=false) {
    auto found=controllers->scan(); bool same=found.size()==pads.size();
    if(same) for(size_t i=0;i<found.size();++i) if(found[i].id!=pads[i].id) same=false;
    if(same&&!force) return;
    SDL_JoystickID old=selection(Pads)>=0&&static_cast<size_t>(selection(Pads))<pads.size()?pads[selection(Pads)].id:0;
    pads=std::move(found); SendMessageW(item(Pads),CB_RESETCONTENT,0,0); combo_labels[item(Pads)].clear(); int selected=0;
    for(size_t i=0;i<pads.size();++i) {add(item(Pads),s::wide(pads[i].name)); if(pads[i].id==old) selected=static_cast<int>(i);}
    if(!pads.empty()) choose(item(Pads),selected); controllers->select(pads.empty()?0:pads[selected].id);
    EnableWindow(item(Pads),!pads.empty()); EnableWindow(item(SavePad),!pads.empty()&&!preparation); EnableWindow(item(LoadPad),!pads.empty()&&!preparation);
    SetWindowTextW(item(PadState),pads.empty()?L"No controller detected":s::wide(pads[selected].name).c_str());
    SetWindowTextW(item(PadValues),controllers->connected()?s::wide(controllers->sample()).c_str():L"—");
}
fs::path pick_profile(bool save,bool diagnostic=false) {
    IFileDialog* dialog{};
    HRESULT hr=save?CoCreateInstance(CLSID_FileSaveDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)):
        CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog));
    if(FAILED(hr)) throw std::runtime_error("Cannot open the file picker.");
    COMDLG_FILTERSPEC filter{L"INI profile (*.ini)",L"*.ini"}; dialog->SetFileTypes(1,&filter); dialog->SetDefaultExtension(L"ini");
    if(save) dialog->SetFileName(diagnostic?L"fefw-diagnostics.ini":L"fefw-settings.ini");
    fs::path path;
    if(SUCCEEDED(dialog->Show(window))) {IShellItem* item{}; if(SUCCEEDED(dialog->GetResult(&item))) {PWSTR p{}; if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&p))) {path=p; CoTaskMemFree(p);} item->Release();}}
    dialog->Release();
    if(!path.empty()) {
        // Export must never replace runtime configuration, game files or saves.
        auto name=path.filename().wstring(); std::transform(name.begin(),name.end(),name.begin(),towlower);
        if(name==L"fefw.ini"||name==L"controls.ini"||path.extension()!=L".ini") throw std::runtime_error("Choose a separate .ini profile file.");
        const auto normalized=fs::weakly_canonical(path).wstring();
        auto inside=[&](const fs::path& root) {
            if(root.empty()) return false;
            auto prefix=fs::weakly_canonical(root).wstring(); if(prefix.back()!=L'\\') prefix+=L'\\';
            return normalized.size()>=prefix.size()&&CompareStringOrdinal(normalized.c_str(),static_cast<int>(prefix.size()),prefix.c_str(),static_cast<int>(prefix.size()),TRUE)==CSTR_EQUAL;
        };
        if(inside(settings.source)) throw std::runtime_error("Keep exported profiles outside the original game folder.");
        s::Ini ini(game_dir/L"fefw.ini"); fs::path saves=s::wide(ini.get("fs","save_dir","save")); if(saves.is_relative()) saves=game_dir/saves;
        for(const auto& root:{saves,settings.exefs,settings.shaders,game_dir/L"work",game_dir/L"controller-profiles"}) if(!root.empty()) {
            if(inside(root)) throw std::runtime_error("Choose a profile location outside game data, saves and build files.");
        }
    }
    return path;
}
void reset_section() {
    if(preparation||active_page==0) return;
    auto v=read_form(); s::Settings defaults;
    switch(active_page) {
        case 1:v.output=defaults.output;v.render_ratio=0;v.upscaler=defaults.upscaler;break;
        case 2:v.skip=true;break; case 3:v.check_updates=false;break; case 4:v.fps60=v.fps120=false;break;
        case 5:v.width=1280;v.height=720;v.window_mode="windowed";v.vsync=true;break;
        case 6:v.deadzone=v.right_deadzone=0.10;break;case 7:v.volume=1;v.mute_unfocused=false;break;case 8:v.ui_language="en";break;
    }
    put_form(v); status(L"Section reset. Save to apply the changes.");
}
void preparation_stage(int current) {
    if(current<1||current>9) return;
    const wchar_t* labels[]={L"Tools",L"Game verification",L"Libraries",L"Native code",L"Shader extraction",L"Shader translation",L"Shader correction",L"Local package",L"Save settings"};
    const wchar_t* spanish[]={L"Herramientas",L"Verificación del juego",L"Bibliotecas",L"Código nativo",L"Extracción de shaders",L"Traducción de shaders",L"Corrección de shaders",L"Paquete local",L"Guardar ajustes"};
    std::wstring stages=(ui::spanish?L"Completadas o reutilizadas: ":L"Completed or reused: ")+std::to_wstring(current-1)+L" / 9\n"+
        (ui::spanish?L"En curso: ":L"Current: ")+std::to_wstring(current)+L". "+(ui::spanish?spanish[current-1]:labels[current-1])+L"\n"+
        (ui::spanish?L"Pendientes después de esta etapa: ":L"Remaining after this stage: ")+std::to_wstring(9-current);
    SetWindowTextW(item(Stages),stages.c_str());
    state((ui::spanish?L"Etapa ":L"Stage ")+std::to_wstring(current)+L" / 9 — "+(ui::spanish?L"se reutiliza el trabajo completado":L"completed work is reused"));
}
void create_controls() {
    fonts();
    control(L"STATIC", L"Fire Emblem Recomp", Title, 24, 18, 760, 34);
    SendMessageW(item(Title), WM_SETFONT, reinterpret_cast<WPARAM>(title_font), TRUE);
    control(L"STATIC", L"Settings for your local copy. A controller is required to play.", Subtitle, 24, 58, 760, 24);
    control(WC_TABCONTROLW, L"", Tabs, 24, 98, 760, 330, WS_TABSTOP | WS_CLIPSIBLINGS);
    ShowWindow(item(Tabs),SW_HIDE);
    const auto& names=page_names;
    for (int p = 0; p < page_count; ++p) {
        TCITEMW tab{}; tab.mask = TCIF_TEXT; tab.pszText = const_cast<wchar_t*>(names[p]);
        TabCtrl_InsertItem(item(Tabs), p, &tab);
        pages[p] = CreateWindowExW(WS_EX_CONTROLPARENT, L"FEFW_SettingsPage", names[p],
            WS_CHILD | WS_VSCROLL | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0, 100, 100,
            window, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!pages[p]) throw std::runtime_error("Cannot create the settings pages.");
        SetWindowLongPtrW(pages[p], GWLP_USERDATA, p);
    }

    control(L"STATIC",L"FIRE EMBLEM",Brand,18,26,170,26);
    hint(control(L"STATIC",L"Fortune’s Weave\nPC configuration",BrandSub,18,56,170,48));
    hint(control(L"STATIC",tools_version,BuildLabel,18,700,170,40));
    for(int i=0;i<page_count;++i) control(L"BUTTON",page_names[i],NavBase+i,12,140,180,42,WS_TABSTOP);
    creating_page = 0; parent = pages[0];
    heading(control(L"STATIC", L"Game files", 0, 0, 0, -1, 26));
    control(L"STATIC", L"Choose the folder containing exefs and romfs, or the parent folder containing the supported update. Original game files are preserved.", 0, 0, 34, -1, 44);
    control(L"STATIC", L"Game &folder", 0, 0, 88, -1, 22);
    control(L"EDIT", settings.source.c_str(), Folder, 0, 114, -3, 30, WS_TABSTOP | ES_AUTOHSCROLL);
    folder_tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, window, nullptr, GetModuleHandleW(nullptr), nullptr);
    TOOLINFOW tool{sizeof tool}; tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS; tool.hwnd = window;
    tool.uId = reinterpret_cast<UINT_PTR>(item(Folder)); tool.lpszText = LPSTR_TEXTCALLBACKW;
    SendMessageW(folder_tip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
    SendMessageW(folder_tip, TTM_SETMAXTIPWIDTH, 0, px(700));
    control(L"BUTTON", L"&Browse…", Browse, -1, 113, 128, 32, WS_TABSTOP | BS_PUSHBUTTON);
    control(L"STATIC", L"Game &version", 0, 0, 170, -2, 22);
    control(L"COMBOBOX", L"", Versions, 0, 196, -2, 180, WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL);
    right(control(L"STATIC", L"Game lan&guage", 0, 0, 170, -2, 22));
    right(control(L"COMBOBOX", L"", Lang, 0, 196, -2, 230, WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL));
    hint(control(L"STATIC", L"First preparation can take around 30–40 minutes, depending on your PC and existing caches. Later launches reuse the prepared files. Open log shows preparation details.", 0, 0, 252, -1, 64));

    creating_page = 1; parent = pages[1];
    heading(control(L"STATIC",L"Resolution",0,0,0,-1,28));
    hint(control(L"STATIC",L"Output includes the interface. Internal scale changes 3D detail before reconstruction.",0,0,38,-1,40));
    control(L"STATIC",L"Game &output (WxH)",0,0,94,-2,22);
    control(L"COMBOBOX",L"",GameOutput,0,122,-2,180,WS_TABSTOP|CBS_DROPDOWN|ES_AUTOHSCROLL);
    add(item(GameOutput),L"Original game output");
    for (auto* size : {L"1280x720",L"1600x900",L"1920x1080",L"2560x1440",L"3840x2160"}) add(item(GameOutput),size);
    if (settings.output=="console") choose(item(GameOutput),0); else SetWindowTextW(item(GameOutput),s::wide(settings.output).c_str());
    right(control(L"STATIC",L"Internal &scale",0,0,94,-2,22));
    right(control(L"COMBOBOX",L"",RenderRatio,0,122,-2,180,WS_TABSTOP|CBS_DROPDOWNLIST));
    add(item(RenderRatio),L"Original (usually 50%)");
    int ratio_index=0; const double ratios[]={0.5,2.0/3.0,0.75,1.0};
    for(int i=0;i<4;++i) {
        add(item(RenderRatio),i==1?L"66.67%":std::to_wstring(static_cast<int>(ratios[i]*100))+L"%");
        if(std::abs(settings.render_ratio-ratios[i])<0.006) ratio_index=i+1;
    }
    if(settings.render_ratio>0&&!ratio_index) { add(item(RenderRatio),std::to_wstring(settings.render_ratio*100)+L"%"); ratio_index=5; }
    choose(item(RenderRatio),ratio_index);
    hint(control(L"STATIC",L"",RenderPreview,0,166,-1,48));
    heading(control(L"STATIC",L"Reconstruction",0,0,238,-1,28));
    control(L"STATIC",L"3D &reconstruction",0,0,282,-1,22);
    control(L"COMBOBOX",L"",Upscaler,0,310,-1,160,WS_TABSTOP|CBS_DROPDOWNLIST);
    add(item(Upscaler),L"Built-in temporal (TAAU) — recommended"); add(item(Upscaler),L"Bilinear — no temporal history");
    choose(item(Upscaler),settings.upscaler=="bilinear"?1:0);
    hint(control(L"STATIC",L"",UpscalerHelp,0,356,-1,48));
    hint(control(L"STATIC",L"Higher resolutions use more GPU memory. Above 1440p output, internal rendering is capped at 50%. Changes apply after restarting the game.",0,0,418,-1,48));
    control(L"STATIC",L"Graphics preset",0,0,484,-1,22);
    control(L"COMBOBOX",L"",Preset,0,512,-3,150,WS_TABSTOP|CBS_DROPDOWNLIST);
    for(auto* label:{L"Performance — 720p / 50%",L"Balanced — 1080p / 66.67%",L"Quality — 1440p / 100%"}) add(item(Preset),label);
    choose(item(Preset),1); control(L"BUTTON",L"Apply preset",ApplyPreset,-1,512,128,32,WS_TABSTOP);
    hint(control(L"STATIC",L"Presets use temporal reconstruction and original timing. They are starting points, not performance guarantees.",0,0,558,-1,54));
    control(L"BUTTON",L"Use recommended &graphics",Recommended,0,656,254,32,WS_TABSTOP);
    reconstruction_help();

    creating_page = 4; parent = pages[4];
    heading(control(L"STATIC",L"Gameplay frame rate",0,0,0,-1,28));
    hint(control(L"STATIC",L"Choose the original timing or an experimental higher target. The runtime adjusts game timing with the target.",0,0,40,-1,48));
    control(L"STATIC",L"Frame-rate &target",0,0,110,-1,22);
    control(L"COMBOBOX",L"",Fps60,0,138,-1,180,WS_TABSTOP|CBS_DROPDOWNLIST);
    for(auto* label:{L"Original (30 / 60 FPS)",L"60 FPS (experimental)",L"120 FPS (experimental)"}) add(item(Fps60),label);
    choose(item(Fps60),settings.fps120?2:settings.fps60?1:0);
    hint(control(L"STATIC",L"",FrameHelp,0,188,-1,80));
    hint(control(L"STATIC",L"Restart after changing the target. Movies retain their encoded frame rate.",0,0,280,-1,40));

    creating_page = 5; parent = pages[5];
    heading(control(L"STATIC",L"Presentation",0,0,0,-1,28));
    control(L"STATIC",L"Display &mode",0,0,54,-1,22);
    control(L"COMBOBOX",L"",Mode,0,82,-1,150,WS_TABSTOP|CBS_DROPDOWNLIST);
    for(auto* label:{L"Windowed",L"Borderless (desktop)",L"Fullscreen (exclusive)"}) add(item(Mode),label);
    choose(item(Mode),settings.window_mode=="exclusive"?2:settings.window_mode=="borderless"?1:0);
    control(L"STATIC",L"&Window resolution",0,0,142,-1,22);
    control(L"COMBOBOX",L"",Resolution,0,170,-1,180,WS_TABSTOP|CBS_DROPDOWNLIST);
    std::set<std::pair<int, int>> choices{{1280,720}, {settings.width,settings.height}};
    DEVMODEW dm{}; dm.dmSize = sizeof dm; DWORD maxw = 1280, maxh = 720;
    for (DWORD i = 0; EnumDisplaySettingsW(nullptr, i, &dm); ++i) { maxw = std::max(maxw, dm.dmPelsWidth); maxh = std::max(maxh, dm.dmPelsHeight); }
    for (const auto size : {std::pair{960,540}, {1280,720}, {1600,900}, {1920,1080}, {2560,1440}, {3840,2160}})
        if (static_cast<DWORD>(size.first) <= maxw && static_cast<DWORD>(size.second) <= maxh) choices.insert(size);
    int index = 0;
    for (const auto& size : choices) {
        if (size.first < 640 || size.second < 360) continue;
        sizes.push_back(size); add(item(Resolution), std::to_wstring(size.first) + L" × " + std::to_wstring(size.second));
        if (size.first == settings.width && size.second == settings.height) index = static_cast<int>(sizes.size() - 1);
    }
    choose(item(Resolution), index); EnableWindow(item(Resolution), settings.window_mode == "windowed");
    hint(control(L"STATIC",L"Window size controls presentation, independently of 3D detail. Borderless and fullscreen use the desktop.",0,0,218,-1,48));
    control(L"BUTTON",L"&Vertical synchronization",Vsync,0,288,-1,28,WS_TABSTOP|BS_AUTOCHECKBOX|BS_NOTIFY);
    SendMessageW(item(Vsync),BM_SETCHECK,settings.vsync?BST_CHECKED:BST_UNCHECKED,0);

    creating_page = 2; parent = pages[2];
    heading(control(L"STATIC", L"Launch behavior", 0, 0, 0, -1, 26));
    control(L"BUTTON", L"S&kip setup on startup", Skip, 0, 42, -1, 28, WS_TABSTOP | BS_AUTOCHECKBOX | BS_NOTIFY);
    SendMessageW(item(Skip), BM_SETCHECK, settings.skip ? BST_CHECKED : BST_UNCHECKED, 0);
    control(L"STATIC", L"After preparation, fefw.exe opens the game directly. You can always return here through fefw_setup.exe. Missing resources reopen setup automatically.", 0, 0, 82, -1, 64);
    heading(control(L"STATIC", L"Controller", 0, 0, 164, -1, 26));
    control(L"STATIC", L"Connect your controller before playing. To reconnect during a game, open F1 → Settings → Controls → Gamepad → Rescan controllers.", 0, 0, 200, -1, 54);
    hint(control(L"STATIC", L"Your settings and saves stay with this installation.", 0, 0, 266, -1, 24));
    control(L"BUTTON", L"Open installation &folder", OpenInstall, 0, 298, 254, 32, WS_TABSTOP);

    creating_page = 3; parent = pages[3];
    heading(control(L"STATIC", (std::wstring(L"Project tools ") + tools_version).c_str(), 0, 0, 0, -1, 26));
    hint(control(L"STATIC", L"Updates come from this project. Verified downloads preserve your game files, builds, settings, saves and caches. Review the changes before installing.", 0, 0, 38, -1, 54));
    control(L"BUTTON", L"Check for updates when setup &opens", AutoCheck, 0, 100, -1, 28, WS_TABSTOP | BS_AUTOCHECKBOX);
    SendMessageW(item(AutoCheck), BM_SETCHECK, settings.check_updates ? BST_CHECKED : BST_UNCHECKED, 0);
    control(L"BUTTON", L"&Check now", CheckUpdates, 0, 144, 145, 32, WS_TABSTOP);
    control(L"BUTTON", L"Install update and &restart setup", InstallUpdate, 160, 144, 310, 32, WS_TABSTOP);
    EnableWindow(item(InstallUpdate), FALSE);
    control(L"STATIC", L"Check for a newer release to see its changes. Installing closes setup; save pending settings first.", UpdateText, 0, 190, -1, 40);
    control(L"EDIT", L"", UpdateNotes, 0, 242, -1, 86, ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_TABSTOP);

    creating_page=6; parent=pages[6];
    control(L"STATIC",L"Connected controller",0,0,0,-1,22);
    control(L"COMBOBOX",L"",Pads,0,28,-3,160,WS_TABSTOP|CBS_DROPDOWNLIST);
    control(L"BUTTON",L"Rescan",Rescan,-1,28,128,32,WS_TABSTOP);
    hint(control(L"STATIC",L"No controller detected",PadState,0,74,-1,24));
    hint(control(L"STATIC",L"Connect a controller by USB or Bluetooth. Detection refreshes automatically.",0,0,106,-1,40));
    heading(control(L"STATIC",L"Live input",0,0,164,-1,28));
    hint(control(L"STATIC",L"Move sticks and press buttons to test. Values below are raw SDL input, before the game's deadzone.",0,0,200,-1,44));
    control(L"STATIC",L"—",PadValues,0,254,-1,76);
    control(L"STATIC",L"Left stick deadzone",0,0,348,-2,22);
    control(L"COMBOBOX",L"",Deadzone,0,376,-2,200,WS_TABSTOP|CBS_DROPDOWNLIST);
    right(control(L"STATIC",L"Right stick deadzone",0,0,348,-2,22));
    right(control(L"COMBOBOX",L"",RightDeadzone,0,376,-2,200,WS_TABSTOP|CBS_DROPDOWNLIST));
    for(int id:{Deadzone,RightDeadzone}) for(int i=0;i<=50;i+=5) add(item(id),std::to_wstring(i)+L"%");
    percent(Deadzone,settings.deadzone); percent(RightDeadzone,settings.right_deadzone);
    control(L"BUTTON",L"Save controller profile",SavePad,0,432,-2,34,WS_TABSTOP);
    right(control(L"BUTTON",L"Load controller profile",LoadPad,0,432,-2,34,WS_TABSTOP));
    hint(control(L"STATIC",L"Profiles are stored per SDL hardware GUID. Load the connected model's profile, then Save to apply it to the game. Identical models share a profile.",0,0,486,-1,64));

    creating_page=7; parent=pages[7];
    heading(control(L"STATIC",L"Master volume",0,0,0,-1,28));
    control(L"COMBOBOX",L"",Volume,0,46,-1,210,WS_TABSTOP|CBS_DROPDOWNLIST);
    for(int i=0;i<=100;i+=5) add(item(Volume),std::to_wstring(i)+L"%"); percent(Volume,settings.volume);
    control(L"BUTTON",L"Mute when the game loses focus",Mute,0,110,-1,28,WS_TABSTOP|BS_AUTOCHECKBOX);
    SendMessageW(item(Mute),BM_SETCHECK,settings.mute_unfocused?BST_CHECKED:BST_UNCHECKED,0);
    hint(control(L"STATIC",L"Audio changes apply the next time you start the game. Your audio device and other runtime settings are preserved.",0,0,166,-1,58));

    creating_page=8; parent=pages[8];
    control(L"STATIC",L"Interface language",0,0,0,-1,22);
    control(L"COMBOBOX",L"",UiLanguage,0,28,-1,130,WS_TABSTOP|CBS_DROPDOWNLIST);
    add(item(UiLanguage),L"English"); add(item(UiLanguage),L"Español"); choose(item(UiLanguage),settings.ui_language=="es"?1:0);
    hint(control(L"STATIC",L"Independent of the game's dialogue language.",0,0,70,-1,26));
    heading(control(L"STATIC",L"Portable settings",0,0,120,-1,28));
    control(L"BUTTON",L"Export settings",Export,0,164,-2,34,WS_TABSTOP);
    right(control(L"BUTTON",L"Import settings",Import,0,164,-2,34,WS_TABSTOP));
    hint(control(L"STATIC",L"Profiles include display, audio and deadzones. Game folders, game language, saves and controller bindings are excluded. Imports remain pending until Save.",0,0,214,-1,64));
    heading(control(L"STATIC",L"Diagnostics",0,0,302,-1,28));
    control(L"BUTTON",L"Export diagnostics",Diagnostics,0,346,250,34,WS_TABSTOP);
    hint(control(L"STATIC",L"Exports tool version, readiness, graphics choices and hardware IDs. No game files, saves, logs, personal paths or controller serial numbers. Review the report before sharing it.",0,0,396,-1,64));

    creating_page=0; parent=pages[0];
    heading(control(L"STATIC",L"Preparation stages",0,0,344,-1,28));
    hint(control(L"STATIC",L"No preparation running. Existing completed build work is reused on the next attempt.",Stages,0,384,-1,110));

    creating_page = -1; parent = nullptr;
    control(L"BUTTON",L"Reset section",ResetSection,18,600,168,34,WS_TABSTOP);
    heading(control(L"STATIC", L"Choose your game folder", StatusTitle, 24, 500, 760, 24));
    hint(control(L"STATIC", L"", Status, 24, 530, 760, 39));
    control(PROGRESS_CLASSW, L"", Progress, 24, 570, 760, 4, PBS_MARQUEE);
    ShowWindow(item(Progress), SW_HIDE);
    control(L"BUTTON", L"Open &log", OpenLog, 24, 594, 112, 34, WS_TABSTOP);
    EnableWindow(item(OpenLog), fs::is_regular_file(game_dir / L"preparation.log"));
    control(L"BUTTON", L"Cancel", IDCANCEL, 390, 594, 112, 34, WS_TABSTOP);
    control(L"BUTTON", L"&Save", Save, 514, 594, 112, 34, WS_TABSTOP);
    control(L"BUTTON", L"Save and &play", SavePlay, 638, 594, 148, 34, WS_TABSTOP | BS_DEFPUSHBUTTON);
    control(L"STATIC",L"No pending changes",Dirty,0,0,200,20);
    hint(item(Dirty)); control(L"BUTTON",L"Revert",Revert,0,0,90,36,WS_TABSTOP);
    layout(); select_page(0); refresh_folder();
    remember_form(); form_ready=true; localize(); scan_controllers(true); SetTimer(window,3,50,nullptr); changed();
}

bool capture_ui(const wchar_t* name) {
    RECT client{}; GetClientRect(window, &client);
    HDC dc = GetDC(window), mem = CreateCompatibleDC(dc);
    HBITMAP bitmap = CreateCompatibleBitmap(dc, client.right, client.bottom);
    HGDIOBJ previous = SelectObject(mem, bitmap);
    RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    const bool printed = PrintWindow(window, mem, PW_CLIENTONLY | 0x00000002 /* PW_RENDERFULLCONTENT */) != FALSE;
    SelectObject(mem, previous);
    BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); bi.bmiHeader.biWidth = client.right;
    bi.bmiHeader.biHeight = -client.bottom; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    std::vector<unsigned char> pixels(static_cast<size_t>(client.right) * client.bottom * 4);
    bool ok = printed && GetDIBits(mem, bitmap, 0, client.bottom, pixels.data(), &bi, DIB_RGB_COLORS) != 0;
    if (ok) {
        BITMAPFILEHEADER bf{}; bf.bfType = 0x4d42; bf.bfOffBits = sizeof bf + sizeof bi.bmiHeader; bf.bfSize = bf.bfOffBits + static_cast<DWORD>(pixels.size());
        std::ofstream bmp(game_dir / name, std::ios::binary);
        bmp.write(reinterpret_cast<const char*>(&bf), sizeof bf); bmp.write(reinterpret_cast<const char*>(&bi.bmiHeader), sizeof bi.bmiHeader);
        bmp.write(reinterpret_cast<const char*>(pixels.data()), pixels.size()); ok = bmp.good();
    }
    DeleteObject(bitmap); DeleteDC(mem); ReleaseDC(window, dc); return ok;
}
bool check_ui() {
    bool ok = true; int checks = 0;
    auto check = [&](bool v, const char* name) { ++checks; if (!v) { ok = false; std::printf("UI FAIL: %s\n", name); } };
    ShowWindow(window, SW_SHOWNOACTIVATE); UpdateWindow(window);
    check(!versions.empty() && !langs.empty() && selection(Versions) >= 0 && selection(Lang) >= 0, "folder and language choices");
    check(selection(Mode) >= 0 && selection(Resolution) >= 0 && IsWindowEnabled(item(Save)), "initial graphics and save state");
    check(selection(Upscaler) == (settings.upscaler == "bilinear" ? 1 : 0), "stored reconstruction mode");
    check(folder_tip && SendMessageW(folder_tip, TTM_GETTOOLCOUNT, 0, 0) == 1, "long folder path tooltip registered");
    check(LOWORD(SendMessageW(window, DM_GETDEFID, 0, 0)) == SavePlay, "keyboard default action");
    check(SendMessageW(item(Skip), BM_GETCHECK, 0, 0) == (settings.skip ? BST_CHECKED : BST_UNCHECKED), "stored startup preference");
    check(selection(Fps60) == (settings.fps120 ? 2 : settings.fps60 ? 1 : 0), "stored FPS preference");
    check(!dirty&&!IsWindowEnabled(item(Revert)),"initial form has no pending changes");
    const auto original=read_form(); const auto original_folder=text(item(Folder)); const int original_language=selection(Lang);
    const double test_volume=original.volume==0.35?0.40:0.35;
    percent(Volume,test_volume); SendMessageW(item(Mute),BM_SETCHECK,BST_CHECKED,0); percent(Deadzone,0.20); percent(RightDeadzone,0.25); changed();
    check(dirty&&read_form().volume==test_volume&&read_form().mute_unfocused&&read_form().deadzone==0.20&&read_form().right_deadzone==0.25,"audio and controller controls feed runtime settings");
    revert_form(); check(!dirty&&read_form().volume==original.volume&&read_form().deadzone==original.deadzone,"revert restores audio and deadzones");
    SetWindowTextW(item(GameOutput),L"2048x1152"); choose(item(UiLanguage),1); localize();
    check(ui::spanish&&text(item(NavBase+6))==L"Mandos"&&text(item(SavePlay))==L"Guardar y &jugar","Spanish interface is applied to native navigation and actions");
    check(text(item(Folder))==original_folder&&text(item(GameOutput))==L"2048x1152"&&selection(Lang)==original_language,"language switch preserves custom output, folder and game language");
    select_page(8); check(capture_ui(L"setup-ui-spanish.bmp"),"capture Spanish tools page");
    revert_form(); check(read_form().ui_language==original.ui_language&&!dirty,"revert restores interface language without false dirty state");
    select_page(7); percent(Volume,0.35); reset_section();
    check(read_form().volume==1&&read_form().output==original.output&&read_form().language==original.language,"reset audio section preserves unrelated settings");
    revert_form();
    select_page(1); choose(item(Preset),1); SendMessageW(window,WM_COMMAND,MAKEWPARAM(ApplyPreset,BN_CLICKED),reinterpret_cast<LPARAM>(item(ApplyPreset)));
    check(read_form().output=="1920x1080"&&text(item(RenderPreview)).find(L"1280")!=std::wstring::npos&&!read_form().fps120,"balanced preset exposes real internal resolution and original timing");
    revert_form();
    SDL_VirtualJoystickDesc controller{};SDL_INIT_INTERFACE(&controller);controller.type=SDL_JOYSTICK_TYPE_GAMEPAD;controller.naxes=6;controller.nbuttons=SDL_GAMEPAD_BUTTON_COUNT;
    controller.axis_mask=63;controller.button_mask=(1u<<SDL_GAMEPAD_BUTTON_COUNT)-1;controller.name="FEFW UI test controller";
    auto virtual_id=SDL_AttachVirtualJoystick(&controller);scan_controllers(true);
    check(std::any_of(pads.begin(),pads.end(),[&](const auto& p){return p.id==virtual_id;})&&IsWindowEnabled(item(Pads)),"controller panel discovers hotplug while already open");
    SDL_DetachVirtualJoystick(virtual_id);scan_controllers(true);
    check(std::none_of(pads.begin(),pads.end(),[&](const auto& p){return p.id==virtual_id;}),"controller panel removes disconnected devices");
    select_page(0);
    const int before_fps=selection(Fps60);
    choose(item(Fps60),(before_fps+1)%3); changed();
    check(dirty&&IsWindowEnabled(item(Revert)),"editing a setting exposes pending changes");
    revert_form(); check(!dirty&&selection(Fps60)==before_fps,"revert restores saved form without writing");
    SetWindowTextW(item(GameOutput),L"1920x1080"); choose(item(RenderRatio),1);
    SendMessageW(window,WM_COMMAND,MAKEWPARAM(GameOutput,CBN_EDITCHANGE),reinterpret_cast<LPARAM>(item(GameOutput)));
    check(dirty&&text(item(RenderPreview)).find(L"960")!=std::wstring::npos,"custom resolution edits update the preview and dirty state");
    revert_form();
    const int language = selection(Lang), mode = selection(Mode), resolution = selection(Resolution), upscale = selection(Upscaler);
    const LRESULT sync = SendMessageW(item(Vsync), BM_GETCHECK, 0, 0), fps = selection(Fps60);
    auto tab = [&](int p) {
        TabCtrl_SetCurSel(item(Tabs), p); NMHDR n{item(Tabs), Tabs, TCN_SELCHANGE};
        SendMessageW(window, WM_NOTIFY, Tabs, reinterpret_cast<LPARAM>(&n));
        for (int i = 0; i < page_count; ++i) check((IsWindowVisible(pages[i]) != FALSE) == (i == p), "only selected page is visible");
        RECT r{}; GetWindowRect(pages[p], &r); POINT origin{r.left + px(8), r.top + px(8)};
        MapWindowPoints(nullptr, window, &origin, 1);
        check(ChildWindowFromPoint(window, origin) == pages[p], "selected page is above tab background");
    };
    const wchar_t* names[] = {L"setup-ui-game.bmp", L"setup-ui-graphics.bmp", L"setup-ui-startup.bmp", L"setup-ui-updates.bmp",L"setup-ui-performance.bmp",L"setup-ui-window.bmp",L"setup-ui-controllers.bmp",L"setup-ui-audio.bmp",L"setup-ui-tools.bmp"};
    for (int p = 0; p < page_count; ++p) {
        tab(p);
        RECT client{}, view{}; GetClientRect(window, &client); GetClientRect(pages[p], &view);
        SCROLLINFO scroll{sizeof scroll, SIF_RANGE | SIF_PAGE}; GetScrollInfo(pages[p], SB_VERT, &scroll);
        check(logical(view.bottom) >= page_content_height(p) || scroll.nMax >= page_content_height(p) - 1, "page content fits or remains scrollable");
        check(capture_ui(names[p]), "capture settings page");
    }
    check(selection(Lang) == language && selection(Upscaler) == upscale, "tab changes preserve edits");
    tab(0);
    if (langs.size() > 1) {
        choose(item(Lang), (language + 1) % static_cast<int>(langs.size()));
        SendMessageW(window, WM_COMMAND, MAKEWPARAM(Folder, EN_KILLFOCUS), reinterpret_cast<LPARAM>(item(Folder)));
        check(selection(Lang) == (language + 1) % static_cast<int>(langs.size()), "unchanged folder preserves unsaved language");
        choose(item(Lang), language);
    }
    check(GetNextDlgTabItem(window, item(Browse), FALSE) == item(Versions), "keyboard order follows game fields");
    tab(1); choose(item(Mode), 1);
    SendMessageW(window, WM_COMMAND, MAKEWPARAM(Mode, CBN_SELCHANGE), reinterpret_cast<LPARAM>(item(Mode)));
    check(!IsWindowEnabled(item(Resolution)), "fullscreen disables window resolution");
    choose(item(Upscaler), 1); choose(item(Fps60), 1);
    SendMessageW(item(Vsync), BM_SETCHECK, BST_UNCHECKED, 0);
    SendMessageW(window, WM_COMMAND, MAKEWPARAM(Recommended, BN_CLICKED), reinterpret_cast<LPARAM>(item(Recommended)));
    check(selection(Mode) == 0 && selection(Upscaler) == 0 && sizes.at(selection(Resolution)) == std::pair{1280,720}, "recommended graphics selections");
    check(IsWindowEnabled(item(Resolution)) && SendMessageW(item(Vsync), BM_GETCHECK, 0, 0) == BST_CHECKED && selection(Fps60) == 0, "recommended sync and frame limit");
    check(selection(Lang) == language && SendMessageW(item(Skip), BM_GETCHECK, 0, 0) == (settings.skip ? BST_CHECKED : BST_UNCHECKED), "recommendation preserves unrelated choices");
    choose(item(Mode), mode); choose(item(Resolution), resolution); choose(item(Upscaler), upscale);
    SendMessageW(item(Vsync), BM_SETCHECK, sync, 0); choose(item(Fps60), static_cast<int>(fps));
    EnableWindow(item(Resolution), mode == 0); reconstruction_help();
    const std::wstring folder = text(item(Folder));
    tab(0); SetWindowTextW(item(Folder), L""); refresh_folder();
    check(!IsWindowEnabled(item(Save)) && !IsWindowEnabled(item(SavePlay)) && !IsWindowEnabled(item(Lang)), "empty folder disables invalid actions");
    check(capture_ui(L"setup-ui-empty.bmp"), "capture empty folder state");
    SetWindowTextW(item(Folder), folder.c_str()); refresh_folder();
    RECT normal{}; GetWindowRect(window, &normal);
    SetWindowPos(window, nullptr, 0, 0, px(820), px(560), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    tab(1); scroll_page(1, page_content_height(1));
    SCROLLINFO si{sizeof si, SIF_ALL}; GetScrollInfo(pages[1], SB_VERT, &si);
    check(si.nMax >= static_cast<int>(si.nPage) && si.nPos > 0, "small window has usable scrolling");
    RECT button{}, view{}; GetWindowRect(item(Recommended), &button); GetClientRect(pages[1], &view);
    MapWindowPoints(nullptr, pages[1], reinterpret_cast<POINT*>(&button), 2);
    check(button.top >= 0 && button.bottom <= view.bottom, "last graphics action stays reachable");
    check(capture_ui(L"setup-ui-compact.bmp"), "capture compact settings");
    const int old_interface=selection(UiLanguage);choose(item(UiLanguage),1);localize();tab(8);
    check(capture_ui(L"setup-ui-compact-spanish.bmp"),"capture compact Spanish settings");
    choose(item(UiLanguage),old_interface);localize();tab(1);
    SetWindowPos(window, nullptr, normal.left, normal.top, normal.right - normal.left, normal.bottom - normal.top, SWP_NOZORDER | SWP_NOACTIVATE);
    scroll_page(1, 0);
    const int old_dpi = dpi;
    RECT scaled{normal.left, normal.top, normal.left + MulDiv(normal.right - normal.left, 144, old_dpi), normal.top + MulDiv(normal.bottom - normal.top, 144, old_dpi)};
    SendMessageW(window, WM_DPICHANGED, MAKEWPARAM(144, 144), reinterpret_cast<LPARAM>(&scaled));
    check(dpi == 144 && capture_ui(L"setup-ui-dpi144.bmp"), "DPI change relayout and capture");
    SendMessageW(window, WM_DPICHANGED, MAKEWPARAM(old_dpi, old_dpi), reinterpret_cast<LPARAM>(&normal));
    enabled(false); state(L"Preparing your local copy");
    tab(0); preparation_stage(4);
    check(text(item(Stages)).find(L"3 / 9")!=std::wstring::npos&&text(item(Stages)).find(L"5")!=std::wstring::npos,"preparation shows completed and remaining stages without a time percentage");
    status(L"First preparation may take around 30–40 minutes. Open log shows the current stage.");
    ShowWindow(item(Progress), SW_SHOW);
    check(!IsWindowEnabled(item(Save)) && !IsWindowEnabled(item(Recommended)) && IsWindowEnabled(item(IDCANCEL)), "preparation blocks edits and allows stopping");
    check(capture_ui(L"setup-ui-preparing.bmp"), "capture preparing state fixture");
    enabled(true); ShowWindow(item(Progress), SW_HIDE); refresh_folder(true); tab(0);
    check(capture_ui(L"setup-ui.bmp"), "capture main settings compatibility filename");
    std::printf("ui_checks=%d ui_valid=%d versions=%zu languages=%zu\n", checks, ok, versions.size(), langs.size());
    return ok;
}

LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    try {
        switch (msg) {
            case WM_CREATE: {
                window=h; const BOOL dark=!ui::contrast;
                DwmSetWindowAttribute(h,20,&dark,sizeof dark); create_controls(); return 0;
            }
            case WM_ERASEBKGND: {
                HDC dc=reinterpret_cast<HDC>(w); RECT r{}; GetClientRect(h,&r); FillRect(dc,&r,ui::background);
                const int side=logical(r.right)<850?162:204; RECT nav=r; nav.right=px(side); FillRect(dc,&nav,ui::sidebar);
                return 1;
            }
            case WM_MEASUREITEM: reinterpret_cast<MEASUREITEMSTRUCT*>(l)->itemHeight=px(28); return TRUE;
            case WM_DRAWITEM: {
                auto& d=*reinterpret_cast<DRAWITEMSTRUCT*>(l);
                if(d.CtlType==ODT_COMBOBOX) ui::combo(d,font);
                else ui::button(d,font,d.CtlID==SavePlay,d.CtlID>=NavBase,d.CtlID==NavBase+active_page);
                return TRUE;
            }
            case DM_GETDEFID: return MAKELONG(SavePlay, DC_HASDEFID);
            case DM_SETDEFID: return TRUE;
            case WM_SIZE: layout(); return 0;
            case WM_GETMINMAXINFO: {
                RECT work{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
                auto* info = reinterpret_cast<MINMAXINFO*>(l);
                info->ptMinTrackSize.x = std::min<int>(px(820), work.right - work.left);
                info->ptMinTrackSize.y = std::min<int>(px(560), work.bottom - work.top);
                return 0;
            }
            case WM_DPICHANGED: {
                dpi = HIWORD(w); fonts();
                const auto* r = reinterpret_cast<const RECT*>(l);
                SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
                layout(); return 0;
            }
            case WM_NOTIFY:
                if (reinterpret_cast<NMHDR*>(l)->code == TTN_GETDISPINFOW) {
                    tip_text = text(item(Folder));
                    reinterpret_cast<NMTTDISPINFOW*>(l)->lpszText = tip_text.data(); return 0;
                }
                if (reinterpret_cast<NMHDR*>(l)->idFrom == Tabs && reinterpret_cast<NMHDR*>(l)->code == TCN_SELCHANGE) {
                    select_page(TabCtrl_GetCurSel(item(Tabs))); return 0;
                }
                break;
            case WM_COMMAND:
                if (HWND field = reinterpret_cast<HWND>(l); field && GetParent(field) == pages[active_page]) {
                    RECT r{}; GetWindowRect(field, &r); MapWindowPoints(nullptr, pages[active_page], reinterpret_cast<POINT*>(&r), 2);
                    RECT view{}; GetClientRect(pages[active_page], &view);
                    const int height = std::min<int>(r.bottom - r.top, px(32)); // a combo's list may be much taller than its closed field
                    if (r.top < 0) scroll_page(active_page, scroll_offset[active_page] + logical(r.top) - 8);
                    else if (r.top + height > view.bottom) scroll_page(active_page, scroll_offset[active_page] + logical(r.top + height - view.bottom) + 8);
                }
                if(LOWORD(w)>=NavBase&&LOWORD(w)<NavBase+page_count) { select_page(LOWORD(w)-NavBase); return 0; }
                if(form_ready && (HIWORD(w)==EN_CHANGE || HIWORD(w)==CBN_SELCHANGE || HIWORD(w)==CBN_EDITCHANGE || HIWORD(w)==BN_CLICKED)) changed();
                switch (LOWORD(w)) {
                    case UiLanguage: if(HIWORD(w)==CBN_SELCHANGE) localize(); return 0;
                    case ResetSection: reset_section(); return 0;
                    case Rescan: scan_controllers(true); return 0;
                    case Pads: if(HIWORD(w)==CBN_SELCHANGE&&selection(Pads)>=0) {
                        controllers->select(pads.at(selection(Pads)).id); SetWindowTextW(item(PadState),s::wide(pads.at(selection(Pads)).name).c_str());
                    } return 0;
                    case SavePad: case LoadPad: {
                        if(preparation) return 0;
                        if(selection(Pads)<0) throw std::runtime_error("Select a connected controller.");
                        auto value=read_form(); s::controller_profile(game_dir,pads.at(selection(Pads)).guid,value,LOWORD(w)==SavePad);
                        if(LOWORD(w)==LoadPad) put_form(value);
                        status(LOWORD(w)==SavePad?L"Controller profile saved for this hardware model.":L"Controller profile loaded. Save to apply its deadzones."); return 0;
                    }
                    case ApplyPreset: { if(preparation) return 0; auto value=read_form(); s::graphics_preset(value,selection(Preset)); put_form(value);
                        status(L"Graphics preset selected. Review the resolution summary, then Save."); return 0; }
                    case Export: case Import: case Diagnostics: {
                        if(preparation) return 0;
                        const auto path=pick_profile(LOWORD(w)!=Import,LOWORD(w)==Diagnostics); if(path.empty()) return 0;
                        auto value=read_form();
                        if(LOWORD(w)==Import) {put_form(s::import_preferences(path,value)); status(L"Settings imported. Review the choices and Save to apply them.");}
                        else if(LOWORD(w)==Export) {s::export_preferences(path,value);status(L"Settings exported without game folders or saves.");}
                        else {s::export_diagnostics(path,game_dir,value,static_cast<int>(pads.size()));status(L"Diagnostics exported. Review the report before sharing.");} return 0;
                    }
                    case Browse: browse(); return 0;
                    case Folder: if (HIWORD(w) == EN_KILLFOCUS) refresh_folder(); return 0;
                    case Versions: if (HIWORD(w) == CBN_SELCHANGE) refresh_languages(); return 0;
                    case Mode: if (HIWORD(w) == CBN_SELCHANGE) EnableWindow(item(Resolution), selection(Mode) == 0); return 0;
                    case Upscaler: if (HIWORD(w) == CBN_SELCHANGE) reconstruction_help(); return 0;
                    case Recommended: recommended(); changed(); return 0;
                    case Revert: revert_form(); return 0;
                    case CheckUpdates: update_tools(false); return 0;
                    case InstallUpdate: update_tools(true); return 0;
                    case OpenInstall:
                        if (reinterpret_cast<INT_PTR>(ShellExecuteW(h, L"open", game_dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
                            throw std::runtime_error("Cannot open the installation folder.");
                        return 0;
                    case Save: apply(false); return 0;
                    case SavePlay: apply(true); return 0;
                    case OpenLog: {
                        const auto log = game_dir / L"preparation.log";
                        if (reinterpret_cast<INT_PTR>(ShellExecuteW(h, L"open", log.c_str(), nullptr, game_dir.c_str(), SW_SHOWNORMAL)) <= 32)
                            throw std::runtime_error("Cannot open preparation.log. Open it from the setup folder.");
                        return 0;
                    }
                    case IDCANCEL: SendMessageW(h, WM_CLOSE, 0, 0); return 0;
                } break;
            case WM_TIMER:
                if(w==3) {
                    static int ticks=0; if(++ticks%20==0) scan_controllers();
                    if(active_page==6) SetWindowTextW(item(PadValues),controllers->connected()?s::wide(controllers->sample()).c_str():L"—");
                    return 0;
                }
                if (w == 2) {
                    refresh_update();
                    if (update_process && WaitForSingleObject(update_process, 0) == WAIT_OBJECT_0) {
                        CloseHandle(update_process); update_process = nullptr; KillTimer(h, 2);
                    }
                    return 0;
                }
                if (preparation) {
                    if (WaitForSingleObject(preparation, 0) == WAIT_OBJECT_0) {
                        DWORD code = 1; GetExitCodeProcess(preparation, &code); CloseHandle(preparation); preparation = nullptr;
                        CloseHandle(job); job = nullptr; KillTimer(h, 1); SendMessageW(item(Progress), PBM_SETMARQUEE, FALSE, 0); enabled(true);
                        SetWindowTextW(item(IDCANCEL), L"Cancel"); ShowWindow(item(Progress), SW_HIDE);
                        if (!code) { finish_save(); return 0; }
                        SendMessageW(item(Progress), PBM_SETPOS, 0, 0);
                        state(L"Preparation needs attention"); status(L"Use Open log to review the error, then retry. Your original files and saves are preserved.");
                        std::ifstream in(game_dir / L"preparation.status", std::ios::binary);
                        std::string detail; std::getline(in, detail);
                        error("Preparation failed.\n\n" + detail.substr(0, 4000) + "\n\nUse Open log for details, then retry.");
                    } else {
                        std::ifstream in(game_dir / L"preparation.status", std::ios::binary);
                        std::string line; if (std::getline(in, line) && !line.empty()) status(s::wide(line));
                        s::Ini progress(game_dir/L"preparation.progress"); int current=0;
                        try {current=std::stoi(progress.get("preparation","stage","0"));} catch(...) {}
                        preparation_stage(current);
                    }
                } return 0;
            case WM_CLOSE:
                if (preparation && MessageBoxW(h, L"Stop preparing this copy?\n\nCompleted build work is kept. Reopen setup and save to resume later.",
                        L"Stop preparation", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) return 0;
                if(!preparation&&dirty&&!test_ui&&MessageBoxW(h,L"Discard unsaved changes?",L"Unsaved settings",MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)!=IDYES) return 0;
                DestroyWindow(h); return 0;
            case WM_CTLCOLORSTATIC: case WM_CTLCOLOREDIT: case WM_CTLCOLORLISTBOX: case WM_CTLCOLORBTN:
                if(GetDlgCtrlID(reinterpret_cast<HWND>(l))==Brand || GetDlgCtrlID(reinterpret_cast<HWND>(l))==BrandSub || GetDlgCtrlID(reinterpret_cast<HWND>(l))==BuildLabel) {
                    SetTextColor(reinterpret_cast<HDC>(w),GetDlgCtrlID(reinterpret_cast<HWND>(l))==Brand?ui::fg:ui::muted);
                    SetBkColor(reinterpret_cast<HDC>(w),ui::inset); return reinterpret_cast<LRESULT>(ui::sidebar);
                }
                return ui::colors(msg,w,l,small_font);
            case WM_DESTROY:
                controllers.reset();
                if (job) { CloseHandle(job); job = nullptr; }
                if (preparation) { CloseHandle(preparation); preparation = nullptr; }
                for (HFONT f : {font, title_font, heading_font, small_font}) if (f) DeleteObject(f);
                font = title_font = heading_font = small_font = nullptr;
                PostQuitMessage(result); return 0;
        }
    } catch (const std::exception& e) { enabled(true); state(L"Settings need attention"); status(s::wide(e.what())); error(e.what()); }
    return DefWindowProcW(h, msg, w, l);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    try {
        const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
        if ((!out || out == INVALID_HANDLE_VALUE) && AttachConsole(ATTACH_PARENT_PROCESS)) { std::freopen("CONOUT$", "w", stdout); std::freopen("CONOUT$", "w", stderr); }
        int argc = 0; LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        game_dir = s::executable_dir(); fs::path source;
        bool validate = false, apply_cli = false, status_cli = false, probe=false, controller_test=false;
        int width = 0, height = 0; std::string language, upscaler; int skip = -1, vsync = -1, fps60 = -1, fps = -1;
        std::string window_mode, output; double ratio = -1;
        std::string interface_language;std::optional<double> volume,left_deadzone,right_deadzone;int mute=-1;
        for (int i = 1; i < argc; ++i) {
            const std::wstring arg(argv[i]);
            auto next = [&]() -> std::wstring { if (i + 1 >= argc) throw std::runtime_error("Missing argument"); return argv[++i]; };
            if (arg == L"--game") game_dir = fs::absolute(next());
            else if (arg == L"--source") source = fs::absolute(next());
            else if (arg == L"--launch") start_game = true;
            else if (arg == L"--validate") { validate = true; source = fs::absolute(next()); }
            else if (arg == L"--status") status_cli = true;
            else if (arg == L"--apply") apply_cli = true;
            else if (arg == L"--width") width = std::stoi(next());
            else if (arg == L"--height") height = std::stoi(next());
            else if (arg == L"--language") language = s::utf8(next());
            else if (arg == L"--skip") skip = std::stoi(next());
            else if (arg == L"--vsync") vsync = std::stoi(next());
            else if (arg == L"--upscaler") upscaler = s::utf8(next());
            else if (arg == L"--fps60") fps60 = std::stoi(next());
            else if (arg == L"--fps") fps = std::stoi(next());
            else if (arg == L"--window-mode") window_mode = s::utf8(next());
            else if (arg == L"--output") output = s::utf8(next());
            else if (arg == L"--render-ratio") ratio = std::stod(next());
            else if (arg == L"--ui-language") interface_language=s::utf8(next());
            else if (arg == L"--volume") volume=std::stod(next());
            else if (arg == L"--deadzone") left_deadzone=std::stod(next());
            else if (arg == L"--right-deadzone") right_deadzone=std::stod(next());
            else if (arg == L"--mute-unfocused") {mute=std::stoi(next());if(mute!=0&&mute!=1) throw std::runtime_error("Mute must be 0 or 1.");}
            else if (arg == L"--test-ui") test_ui = true;
            else if (arg == L"--probe-controllers") probe=true;
            else if (arg == L"--test-controllers") controller_test=true;
            else throw std::runtime_error("Unknown argument: " + s::utf8(arg));
        }
        LocalFree(argv);
        if(probe||controller_test) {
            s::Controllers device; auto found=device.scan();
            for(const auto& pad:found) {device.select(pad.id); std::printf("controller=%s guid=%s connected=%d\n%s\n",pad.name.c_str(),pad.guid.c_str(),device.connected(),device.sample().c_str());}
            std::printf("physical_controllers=%zu\n",found.size());
            if(!controller_test) return device.error.empty()?0:2;
            SDL_VirtualJoystickDesc description{}; SDL_INIT_INTERFACE(&description); description.type=SDL_JOYSTICK_TYPE_GAMEPAD;
            description.naxes=SDL_GAMEPAD_AXIS_COUNT; description.nbuttons=SDL_GAMEPAD_BUTTON_COUNT;
            description.axis_mask=(1u<<SDL_GAMEPAD_AXIS_COUNT)-1; description.button_mask=(1u<<SDL_GAMEPAD_BUTTON_COUNT)-1; description.name="FEFW test controller";
            auto id=SDL_AttachVirtualJoystick(&description); auto virtual_pad=SDL_OpenJoystick(id);
            auto list=device.scan(); bool detected=std::any_of(list.begin(),list.end(),[&](const auto& p){return p.id==id;});
            bool opened=device.select(id); SDL_SetJoystickVirtualAxis(virtual_pad,0,16384); SDL_SetJoystickVirtualButton(virtual_pad,SDL_GAMEPAD_BUTTON_SOUTH,true); SDL_UpdateJoysticks();
            auto sample=device.sample(); bool input=sample.find("+0.50")!=std::string::npos&&sample.find(SDL_GetGamepadStringForButton(SDL_GAMEPAD_BUTTON_SOUTH))!=std::string::npos;
            SDL_DetachVirtualJoystick(id); device.scan(); bool disconnected=!device.connected(); SDL_CloseJoystick(virtual_pad);
            id=SDL_AttachVirtualJoystick(&description); list=device.scan(); bool reconnected=device.select(id)&&device.connected();
            SDL_DetachVirtualJoystick(id); device.scan(); device.select(0);
            bool ok=detected&&opened&&input&&disconnected&&reconnected;
            std::printf("detected=%d opened=%d input=%d disconnected=%d reconnected=%d controller_valid=%d\n",detected,opened,input,disconnected,reconnected,ok); return ok?0:2;
        }
        if (validate) { const auto v = s::validate_source(source, true); std::printf("%s\n", v.ok ? "VALID" : v.error.c_str()); return v.ok ? 0 : 2; }
        settings = s::load(game_dir); if (!source.empty()) { settings.source = source; settings.romfs = source / L"romfs"; }
        if (width) settings.width = width; if (height) settings.height = height;
        if (!language.empty()) settings.language = language; if (skip >= 0) settings.skip = skip != 0;
        if (vsync >= 0) settings.vsync = vsync != 0;
        if (!upscaler.empty()) settings.upscaler = upscaler;
        if (fps60 >= 0) { settings.fps60 = fps60 != 0; settings.fps120 = false; }
        if (fps >= 0) {
            if (fps != 0 && fps != 60 && fps != 120) throw std::runtime_error("FPS target must be 0, 60 or 120.");
            settings.fps60 = fps == 60; settings.fps120 = fps == 120;
        }
        if (!window_mode.empty()) settings.window_mode = window_mode;
        if (!output.empty()) settings.output = output;
        if (ratio >= 0) settings.render_ratio = ratio;
        if(!interface_language.empty()) settings.ui_language=interface_language;
        if(volume) settings.volume=*volume; if(left_deadzone) settings.deadzone=*left_deadzone; if(right_deadzone) settings.right_deadzone=*right_deadzone;
        if(mute>=0) settings.mute_unfocused=mute==1;
        if (status_cli) {
            const auto v = s::validate_ready(game_dir, settings);
            std::printf("ready=%d skip=%d show_setup=%d width=%d height=%d language=%s fps60=%d\n%s\n", v.ok, settings.skip, s::needs_setup(game_dir), settings.width, settings.height, settings.language.c_str(), settings.fps60, v.error.c_str());
            std::printf("fps120=%d window_mode=%s output=%s render_ratio=%.4f\n", settings.fps120, settings.window_mode.c_str(), settings.output.c_str(), settings.render_ratio); return v.ok ? 0 : 2;
        }
        if (apply_cli) { settings.completed = true; s::save(game_dir, settings); std::puts("SAVED"); return 0; }
        const HANDLE lock = CreateMutexW(nullptr, FALSE, (s::game_mutex_name(game_dir) + L"_setup").c_str());
        if (lock && GetLastError() == ERROR_ALREADY_EXISTS) return 0;
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        controllers=std::make_unique<s::Controllers>();
        INITCOMMONCONTROLSEX cc{sizeof cc, ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS | ICC_TAB_CLASSES}; InitCommonControlsEx(&cc);
        dpi = static_cast<int>(GetDpiForSystem()); ui::init();
        WNDCLASSEXW wc{sizeof wc}; wc.hInstance = instance; wc.lpfnWndProc = proc; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION); wc.hbrBackground = nullptr; wc.lpszClassName = L"FEFW_Configuration";
        RegisterClassExW(&wc);
        WNDCLASSEXW page_class = wc; page_class.lpfnWndProc = page_proc; page_class.lpszClassName = L"FEFW_SettingsPage";
        RegisterClassExW(&page_class);
        RECT rect{0, 0, px(1120), px(860)}, work_area{};
        const DWORD style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
        AdjustWindowRectEx(&rect, style, FALSE, WS_EX_CONTROLPARENT);
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work_area, 0);
        const int initial_width = std::min(rect.right - rect.left, work_area.right - work_area.left);
        const int initial_height = std::min(rect.bottom - rect.top, work_area.bottom - work_area.top);
        HWND h = CreateWindowExW(WS_EX_CONTROLPARENT, wc.lpszClassName, L"Fire Emblem Recomp — Setup", style,
            work_area.left + (work_area.right - work_area.left - initial_width) / 2,
            work_area.top + (work_area.bottom - work_area.top - initial_height) / 2,
            initial_width, initial_height, nullptr, nullptr, instance, nullptr);
        if (!h) throw std::runtime_error("Cannot open the setup tool");
        if (test_ui) {
            result = check_ui() ? 0 : 2; SendMessageW(h, WM_COMMAND, IDCANCEL, 0);
        } else { ShowWindow(h, show); refresh_update(); if (settings.check_updates) update_tools(false); }
        MSG msg{}; while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            if (msg.message == WM_KEYDOWN && msg.wParam == VK_TAB && GetKeyState(VK_CONTROL) < 0) {
                int index=0; while(nav_order[index]!=active_page) ++index;
                select_page(nav_order[(index+(GetKeyState(VK_SHIFT)<0?page_count-1:1))%page_count]);
                SetFocus(item(NavBase+active_page)); continue;
            }
            if (!IsDialogMessageW(h, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        }
        if (lock) CloseHandle(lock); CoUninitialize(); return result;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        // CLI validation/preparation is unattended and communicates through its exit status.
        const std::wstring cmd(GetCommandLineW());
        const bool cli = cmd.find(L"--validate") != std::wstring::npos || cmd.find(L"--apply") != std::wstring::npos || cmd.find(L"--status") != std::wstring::npos || cmd.find(L"--test-ui") != std::wstring::npos || cmd.find(L"--probe-controllers")!=std::wstring::npos || cmd.find(L"--test-controllers")!=std::wstring::npos;
        if (!cli) MessageBoxW(nullptr, s::wide(e.what()).c_str(), L"Fire Emblem Recomp", MB_OK | MB_ICONERROR);
        return 2;
    }
}
