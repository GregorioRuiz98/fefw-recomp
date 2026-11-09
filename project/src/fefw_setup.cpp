// SPDX-License-Identifier: GPL-3.0-or-later
#include "fefw_config.h"
#include <windows.h>
#include <commctrl.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <set>
#include <cmath>

namespace s = fefw::setup;
namespace fs = std::filesystem;
namespace {
enum { Folder = 100, Browse, Versions, Resolution, Mode, Lang, Skip, Save, SavePlay, Status, Progress, Vsync, Fps60, OpenLog, Upscaler,
    Tabs, Title, Subtitle, StatusTitle, UpscalerHelp, Recommended, OpenInstall, GameOutput, RenderRatio, CheckUpdates, InstallUpdate, UpdateNotes, UpdateText, AutoCheck };
fs::path game_dir;
s::Settings settings, pending;
HWND window{};
HFONT font{}, title_font{}, heading_font{}, small_font{};
constexpr int page_count = 4;
constexpr const wchar_t* tools_version = L"v0.1.0-preview.6";
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
void status(const std::wstring& v) { SetWindowTextW(item(Status), v.c_str()); }
void state(const wchar_t* v) { SetWindowTextW(item(StatusTitle), v); }
void error(const std::string& v) { MessageBoxW(window, s::wide(v).c_str(), L"Fire Emblem Recomp", MB_OK | MB_ICONERROR); }
void choose(HWND h, int n) { SendMessageW(h, CB_SETCURSEL, n, 0); }
int selection(int id) { return static_cast<int>(SendMessageW(item(id), CB_GETCURSEL, 0, 0)); }
void add(HWND h, const std::wstring& v) { SendMessageW(h, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(v.c_str())); }
void enabled(bool on) {
    for (int id : {Folder, Browse, Versions, Resolution, Mode, Lang, Skip, Save, SavePlay, Vsync, Fps60, Upscaler, GameOutput, RenderRatio, AutoCheck, CheckUpdates}) EnableWindow(item(id), on);
    if (on) EnableWindow(item(Resolution), selection(Mode) == 0);
    EnableWindow(item(Save), on && !versions.empty() && !langs.empty());
    EnableWindow(item(SavePlay), on && !versions.empty() && !langs.empty());
    EnableWindow(item(Versions), on && !versions.empty());
    EnableWindow(item(Lang), on && !langs.empty());
    EnableWindow(item(Recommended), on);
    if (!on) EnableWindow(item(InstallUpdate), FALSE);
}
HWND control(const wchar_t* cls, const wchar_t* label, int id, int x, int y, int w, int h, DWORD style = 0) {
    HWND c = CreateWindowExW(cls == std::wstring(L"EDIT") ? WS_EX_CLIENTEDGE : 0, cls, label,
        WS_CHILD | WS_VISIBLE | style, px(std::max(x, 0)), px(y), px(w > 0 ? w : 100), px(h), parent ? parent : window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
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
    font = make(16, FW_NORMAL); title_font = make(25, FW_SEMIBOLD);
    heading_font = make(18, FW_SEMIBOLD); small_font = make(14, FW_NORMAL);
    EnumChildWindows(window, [](HWND h, LPARAM) -> BOOL { SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE); return TRUE; }, 0);
    for (HWND h : headings) SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(heading_font), TRUE);
    for (HWND h : small_text) SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(small_font), TRUE);
    SendMessageW(item(Title), WM_SETFONT, reinterpret_cast<WPARAM>(title_font), TRUE);
    for (HFONT f : old) if (f) DeleteObject(f);
}
void move(HWND h, int x, int y, int w, int height) { MoveWindow(h, px(x), px(y), px(w), px(height), TRUE); }
int logical(int v) { return MulDiv(v, 96, dpi); }
int page_content_height(int p) { return p == 1 ? 522 : p == 2 ? 338 : 328; }
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
    move(item(Title), 24, 18, w - 48, 34);
    move(item(Subtitle), 24, 58, w - 48, 24);
    move(item(Tabs), 24, 98, w - 48, h - 254);
    RECT content{}; GetClientRect(item(Tabs), &content); TabCtrl_AdjustRect(item(Tabs), FALSE, &content);
    POINT origin{content.left, content.top}; MapWindowPoints(item(Tabs), window, &origin, 1);
    for (int p = 0; p < page_count; ++p) {
        MoveWindow(pages[p], origin.x + px(20), origin.y + px(20),
            std::max<int>(px(100), content.right - content.left - px(40)),
            std::max<int>(px(80), content.bottom - content.top - px(28)), TRUE);
        page_layout(p);
    }
    move(item(StatusTitle), 24, h - 140, w - 48, 24);
    move(item(Status), 24, h - 111, w - 48, 39);
    move(item(Progress), 24, h - 64, w - 48, 4);
    move(item(OpenLog), 24, h - 48, 112, 34);
    move(item(IDCANCEL), w - 420, h - 48, 112, 34);
    move(item(Save), w - 296, h - 48, 112, 34);
    move(item(SavePlay), w - 172, h - 48, 148, 34);
}
void select_page(int p) {
    active_page = std::clamp(p, 0, page_count - 1); TabCtrl_SetCurSel(item(Tabs), active_page);
    for (int i = 0; i < page_count; ++i) ShowWindow(pages[i], i == active_page ? SW_SHOW : SW_HIDE);
    SetWindowPos(pages[active_page], HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    page_layout(active_page);
}
void scroll_page(int p, int value) {
    scroll_offset[p] = std::max(0, value); page_layout(p);
}
LRESULT CALLBACK page_proc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    const int p = static_cast<int>(GetWindowLongPtrW(h, GWLP_USERDATA));
    switch (msg) {
        case WM_COMMAND: return SendMessageW(window, msg, w, l);
        case WM_CTLCOLORSTATIC: return SendMessageW(window, msg, w, l);
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
        : L"Recommended. Combines frames using motion and depth to smooth 3D edges. Restart the game after changing this.");
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
    pending = settings; pending.source = versions.at(v); pending.romfs = pending.source / L"romfs";
    pending.language = langs.at(l).value; pending.width = sizes.at(r).first; pending.height = sizes.at(r).second;
    pending.window_mode = selection(Mode) == 2 ? "exclusive" : selection(Mode) == 1 ? "borderless" : "windowed"; pending.skip = SendMessageW(item(Skip), BM_GETCHECK, 0, 0) == BST_CHECKED;
    pending.vsync = SendMessageW(item(Vsync), BM_GETCHECK, 0, 0) == BST_CHECKED;
    pending.upscaler = selection(Upscaler) == 1 ? "bilinear" : "taau";
    pending.check_updates = SendMessageW(item(AutoCheck), BM_GETCHECK, 0, 0) == BST_CHECKED;
    pending.fps60 = selection(Fps60) == 1; pending.fps120 = selection(Fps60) == 2;
    pending.output = selection(GameOutput) == 0 ? "console" : s::utf8(text(item(GameOutput)));
    pending.render_ratio = selection(RenderRatio) == 0 ? 0 : std::stod(s::utf8(text(item(RenderRatio)))) / 100.0;
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
void create_controls() {
    fonts();
    control(L"STATIC", L"Fire Emblem Recomp", Title, 24, 18, 760, 34);
    SendMessageW(item(Title), WM_SETFONT, reinterpret_cast<WPARAM>(title_font), TRUE);
    control(L"STATIC", L"Settings for your local copy. A controller is required to play.", Subtitle, 24, 58, 760, 24);
    control(WC_TABCONTROLW, L"", Tabs, 24, 98, 760, 330, WS_TABSTOP | WS_CLIPSIBLINGS);
    const wchar_t* names[] = {L"Game", L"Graphics", L"Startup", L"Updates"};
    for (int p = 0; p < page_count; ++p) {
        TCITEMW tab{}; tab.mask = TCIF_TEXT; tab.pszText = const_cast<wchar_t*>(names[p]);
        TabCtrl_InsertItem(item(Tabs), p, &tab);
        pages[p] = CreateWindowExW(WS_EX_CONTROLPARENT, L"FEFW_SettingsPage", names[p],
            WS_CHILD | WS_VSCROLL | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0, 100, 100,
            window, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!pages[p]) throw std::runtime_error("Cannot create the settings pages.");
        SetWindowLongPtrW(pages[p], GWLP_USERDATA, p);
    }

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
    control(L"STATIC", L"Display &mode", 0, 0, 0, -2, 22);
    control(L"COMBOBOX", L"", Mode, 0, 26, -2, 130, WS_TABSTOP | CBS_DROPDOWNLIST);
    add(item(Mode), L"Windowed"); add(item(Mode), L"Borderless (desktop)"); add(item(Mode), L"Fullscreen (exclusive)");
    choose(item(Mode), settings.window_mode == "exclusive" ? 2 : settings.window_mode == "borderless" ? 1 : 0);
    right(control(L"STATIC", L"&Window resolution", 0, 0, 0, -2, 22));
    right(control(L"COMBOBOX", L"", Resolution, 0, 26, -2, 180, WS_TABSTOP | CBS_DROPDOWNLIST));
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
    hint(control(L"STATIC", L"Window size controls presentation. Borderless and fullscreen use the desktop. Game output and internal scale below control 3D detail.", 0, 0, 66, -1, 36));
    control(L"STATIC", L"3D &reconstruction", 0, 0, 112, -1, 22);
    control(L"COMBOBOX", L"", Upscaler, 0, 138, -1, 130, WS_TABSTOP | CBS_DROPDOWNLIST);
    add(item(Upscaler), L"Temporal anti-aliasing (TAAU) — recommended");
    add(item(Upscaler), L"Bilinear — comparison mode");
    choose(item(Upscaler), settings.upscaler == "bilinear" ? 1 : 0);
    hint(control(L"STATIC", L"", UpscalerHelp, 0, 176, -1, 42));
    control(L"STATIC", L"Frame-rate &target", 0, 0, 232, -2, 22);
    control(L"COMBOBOX", L"", Fps60, 0, 258, -2, 160, WS_TABSTOP | CBS_DROPDOWNLIST);
    for (const auto* label : {L"Original (30 / 60 FPS)", L"60 FPS (experimental)", L"120 FPS (experimental)"}) add(item(Fps60), label);
    choose(item(Fps60), settings.fps120 ? 2 : settings.fps60 ? 1 : 0);
    right(control(L"BUTTON", L"&Vertical synchronization", Vsync, 0, 258, -2, 28, WS_TABSTOP | BS_AUTOCHECKBOX | BS_NOTIFY));
    SendMessageW(item(Vsync), BM_SETCHECK, settings.vsync ? BST_CHECKED : BST_UNCHECKED, 0);
    hint(control(L"STATIC", L"Higher targets also adjust game timing. Restart required. Actual FPS depends on your PC and scene; movies keep their encoded frame rate.", 0, 0, 298, -1, 40));
    control(L"STATIC", L"Game &output (WxH)", 0, 0, 354, -2, 22);
    control(L"COMBOBOX", L"", GameOutput, 0, 380, -2, 180, WS_TABSTOP | CBS_DROPDOWN | ES_AUTOHSCROLL);
    add(item(GameOutput), L"Original game output");
    for (const auto* size : {L"1280x720", L"1600x900", L"1920x1080", L"2560x1440", L"3840x2160"}) add(item(GameOutput), size);
    if (settings.output == "console") choose(item(GameOutput), 0); else SetWindowTextW(item(GameOutput), s::wide(settings.output).c_str());
    right(control(L"STATIC", L"Internal &scale per dimension", 0, 0, 354, -2, 22));
    right(control(L"COMBOBOX", L"", RenderRatio, 0, 380, -2, 180, WS_TABSTOP | CBS_DROPDOWNLIST));
    add(item(RenderRatio), L"Original (usually 50%)");
    int ratio_index = 0;
    const double ratios[] = {0.5, 2.0/3.0, 0.75, 1.0};
    for (int i = 0; i < 4; ++i) {
        add(item(RenderRatio), i == 1 ? L"66.67%" : std::to_wstring(static_cast<int>(ratios[i] * 100)) + L"%");
        if (std::abs(settings.render_ratio - ratios[i]) < 0.006) ratio_index = i + 1;
    }
    if (settings.render_ratio > 0 && !ratio_index) { add(item(RenderRatio), std::to_wstring(settings.render_ratio * 100) + L"%"); ratio_index = 5; }
    choose(item(RenderRatio), ratio_index);
    hint(control(L"STATIC", L"TAAU reconstructs 3D to game output. Larger sizes cost GPU memory. Above 1440p output, internal rendering is capped at 50% to protect the game's target pool.", 0, 0, 420, -1, 48));
    control(L"BUTTON", L"Use recommended &graphics", Recommended, 0, 480, 254, 32, WS_TABSTOP);
    reconstruction_help();

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
    hint(control(L"STATIC", L"Updates come from GregorioRuiz98/fefw-recomp. SHA256 and the package manifest are verified before installation. Game files, builds, saves and caches are preserved.", 0, 0, 38, -1, 54));
    control(L"BUTTON", L"Check for updates when setup &opens", AutoCheck, 0, 100, -1, 28, WS_TABSTOP | BS_AUTOCHECKBOX);
    SendMessageW(item(AutoCheck), BM_SETCHECK, settings.check_updates ? BST_CHECKED : BST_UNCHECKED, 0);
    control(L"BUTTON", L"&Check now", CheckUpdates, 0, 144, 145, 32, WS_TABSTOP);
    control(L"BUTTON", L"Install update and &restart setup", InstallUpdate, 160, 144, 310, 32, WS_TABSTOP);
    EnableWindow(item(InstallUpdate), FALSE);
    control(L"STATIC", L"Check for a newer release to see its changes. Installing closes setup; save pending settings first.", UpdateText, 0, 190, -1, 40);
    control(L"EDIT", L"", UpdateNotes, 0, 242, -1, 86, ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_TABSTOP);

    creating_page = -1; parent = nullptr;
    heading(control(L"STATIC", L"Choose your game folder", StatusTitle, 24, 500, 760, 24));
    hint(control(L"STATIC", L"", Status, 24, 530, 760, 39));
    control(PROGRESS_CLASSW, L"", Progress, 24, 570, 760, 4, PBS_MARQUEE);
    ShowWindow(item(Progress), SW_HIDE);
    control(L"BUTTON", L"Open &log", OpenLog, 24, 594, 112, 34, WS_TABSTOP);
    EnableWindow(item(OpenLog), fs::is_regular_file(game_dir / L"preparation.log"));
    control(L"BUTTON", L"Cancel", IDCANCEL, 390, 594, 112, 34, WS_TABSTOP);
    control(L"BUTTON", L"&Save", Save, 514, 594, 112, 34, WS_TABSTOP);
    control(L"BUTTON", L"Save and &play", SavePlay, 638, 594, 148, 34, WS_TABSTOP | BS_DEFPUSHBUTTON);
    layout(); select_page(0); refresh_folder();
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
    const wchar_t* names[] = {L"setup-ui-game.bmp", L"setup-ui-graphics.bmp", L"setup-ui-startup.bmp", L"setup-ui-updates.bmp"};
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
    SetWindowPos(window, nullptr, 0, 0, px(700), px(460), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    tab(1); scroll_page(1, page_content_height(1));
    SCROLLINFO si{sizeof si, SIF_ALL}; GetScrollInfo(pages[1], SB_VERT, &si);
    check(si.nMax >= static_cast<int>(si.nPage) && si.nPos > 0, "small window has usable scrolling");
    RECT button{}, view{}; GetWindowRect(item(Recommended), &button); GetClientRect(pages[1], &view);
    MapWindowPoints(nullptr, pages[1], reinterpret_cast<POINT*>(&button), 2);
    check(button.top >= 0 && button.bottom <= view.bottom, "last graphics action stays reachable");
    check(capture_ui(L"setup-ui-compact.bmp"), "capture compact settings");
    SetWindowPos(window, nullptr, normal.left, normal.top, normal.right - normal.left, normal.bottom - normal.top, SWP_NOZORDER | SWP_NOACTIVATE);
    scroll_page(1, 0);
    const int old_dpi = dpi;
    RECT scaled{normal.left, normal.top, normal.left + MulDiv(normal.right - normal.left, 144, old_dpi), normal.top + MulDiv(normal.bottom - normal.top, 144, old_dpi)};
    SendMessageW(window, WM_DPICHANGED, MAKEWPARAM(144, 144), reinterpret_cast<LPARAM>(&scaled));
    check(dpi == 144 && capture_ui(L"setup-ui-dpi144.bmp"), "DPI change relayout and capture");
    SendMessageW(window, WM_DPICHANGED, MAKEWPARAM(old_dpi, old_dpi), reinterpret_cast<LPARAM>(&normal));
    enabled(false); state(L"Preparing your local copy");
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
            case WM_CREATE: window = h; create_controls(); return 0;
            case DM_GETDEFID: return MAKELONG(SavePlay, DC_HASDEFID);
            case DM_SETDEFID: return TRUE;
            case WM_SIZE: layout(); return 0;
            case WM_GETMINMAXINFO: {
                RECT work{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
                auto* info = reinterpret_cast<MINMAXINFO*>(l);
                info->ptMinTrackSize.x = std::min<int>(px(700), work.right - work.left);
                info->ptMinTrackSize.y = std::min<int>(px(460), work.bottom - work.top);
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
                switch (LOWORD(w)) {
                    case Browse: browse(); return 0;
                    case Folder: if (HIWORD(w) == EN_KILLFOCUS) refresh_folder(); return 0;
                    case Versions: if (HIWORD(w) == CBN_SELCHANGE) refresh_languages(); return 0;
                    case Mode: if (HIWORD(w) == CBN_SELCHANGE) EnableWindow(item(Resolution), selection(Mode) == 0); return 0;
                    case Upscaler: if (HIWORD(w) == CBN_SELCHANGE) reconstruction_help(); return 0;
                    case Recommended: recommended(); return 0;
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
                    }
                } return 0;
            case WM_CLOSE:
                if (preparation && MessageBoxW(h, L"Stop preparing this copy?\n\nCompleted build work is kept. Reopen setup and save to resume later.",
                        L"Stop preparation", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) return 0;
                DestroyWindow(h); return 0;
            case WM_CTLCOLORSTATIC:
                SetTextColor(reinterpret_cast<HDC>(w), GetSysColor(COLOR_WINDOWTEXT));
                SetBkMode(reinterpret_cast<HDC>(w), TRANSPARENT);
                return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
            case WM_DESTROY:
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
        bool validate = false, apply_cli = false, status_cli = false;
        int width = 0, height = 0; std::string language, upscaler; int skip = -1, vsync = -1, fps60 = -1, fps = -1;
        std::string window_mode, output; double ratio = -1;
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
            else if (arg == L"--test-ui") test_ui = true;
            else throw std::runtime_error("Unknown argument: " + s::utf8(arg));
        }
        LocalFree(argv);
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
        if (status_cli) {
            const auto v = s::validate_ready(game_dir, settings);
            std::printf("ready=%d skip=%d show_setup=%d width=%d height=%d language=%s fps60=%d\n%s\n", v.ok, settings.skip, s::needs_setup(game_dir), settings.width, settings.height, settings.language.c_str(), settings.fps60, v.error.c_str());
            std::printf("fps120=%d window_mode=%s output=%s render_ratio=%.4f\n", settings.fps120, settings.window_mode.c_str(), settings.output.c_str(), settings.render_ratio); return v.ok ? 0 : 2;
        }
        if (apply_cli) { settings.completed = true; s::save(game_dir, settings); std::puts("SAVED"); return 0; }
        const HANDLE lock = CreateMutexW(nullptr, FALSE, (s::game_mutex_name(game_dir) + L"_setup").c_str());
        if (lock && GetLastError() == ERROR_ALREADY_EXISTS) return 0;
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        INITCOMMONCONTROLSEX cc{sizeof cc, ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS | ICC_TAB_CLASSES}; InitCommonControlsEx(&cc);
        dpi = static_cast<int>(GetDpiForSystem());
        WNDCLASSEXW wc{sizeof wc}; wc.hInstance = instance; wc.lpfnWndProc = proc; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION); wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1); wc.lpszClassName = L"FEFW_Configuration";
        RegisterClassExW(&wc);
        WNDCLASSEXW page_class = wc; page_class.lpfnWndProc = page_proc; page_class.lpszClassName = L"FEFW_SettingsPage";
        RegisterClassExW(&page_class);
        RECT rect{0, 0, px(820), px(850)}, work_area{};
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
                select_page((active_page + (GetKeyState(VK_SHIFT) < 0 ? page_count - 1 : 1)) % page_count);
                SetFocus(item(Tabs)); continue;
            }
            if (!IsDialogMessageW(h, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        }
        if (lock) CloseHandle(lock); CoUninitialize(); return result;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        // CLI validation/preparation is unattended and communicates through its exit status.
        const std::wstring cmd(GetCommandLineW());
        const bool cli = cmd.find(L"--validate") != std::wstring::npos || cmd.find(L"--apply") != std::wstring::npos || cmd.find(L"--status") != std::wstring::npos || cmd.find(L"--test-ui") != std::wstring::npos;
        if (!cli) MessageBoxW(nullptr, s::wide(e.what()).c_str(), L"Fire Emblem Recomp", MB_OK | MB_ICONERROR);
        return 2;
    }
}
