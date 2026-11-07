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

namespace s = fefw::setup;
namespace fs = std::filesystem;
namespace {
enum { Folder = 100, Browse, Versions, Resolution, Mode, Lang, Skip, Save, SavePlay, Status, Progress, Vsync, Fps60, OpenLog };
fs::path game_dir;
s::Settings settings, pending;
HWND window{};
HFONT font{};
int dpi = 96, result = 3;
bool start_game = false, test_ui = false;
std::vector<fs::path> versions;
std::vector<s::Language> langs;
std::vector<std::pair<int, int>> sizes;
HANDLE preparation{}, job{};

int px(int v) { return MulDiv(v, dpi, 96); }
HWND item(int id) { return GetDlgItem(window, id); }
std::wstring text(HWND h) {
    const int n = GetWindowTextLengthW(h); std::wstring v(n + 1, L'\0');
    GetWindowTextW(h, v.data(), n + 1); v.resize(n); return v;
}
void status(const std::wstring& v) { SetWindowTextW(item(Status), v.c_str()); }
void error(const std::string& v) { MessageBoxW(window, s::wide(v).c_str(), L"Fire Emblem Recomp", MB_OK | MB_ICONERROR); }
void choose(HWND h, int n) { SendMessageW(h, CB_SETCURSEL, n, 0); }
int selection(int id) { return static_cast<int>(SendMessageW(item(id), CB_GETCURSEL, 0, 0)); }
void add(HWND h, const std::wstring& v) { SendMessageW(h, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(v.c_str())); }
void enabled(bool on) {
    for (int id : {Folder, Browse, Versions, Resolution, Mode, Lang, Skip, Save, SavePlay, Vsync, Fps60}) EnableWindow(item(id), on);
    if (on) EnableWindow(item(Resolution), selection(Mode) == 0);
}
HWND control(const wchar_t* cls, const wchar_t* label, int id, int x, int y, int w, int h, DWORD style = 0) {
    HWND c = CreateWindowExW(cls == std::wstring(L"EDIT") ? WS_EX_CLIENTEDGE : 0, cls, label,
        WS_CHILD | WS_VISIBLE | style, px(x), px(y), px(w), px(h), window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE); return c;
}
void refresh_languages() {
    const int v = selection(Versions); langs.clear(); SendMessageW(item(Lang), CB_RESETCONTENT, 0, 0);
    if (v >= 0 && static_cast<size_t>(v) < versions.size()) langs = s::available_languages(versions[v]);
    int selected = 0;
    for (size_t i = 0; i < langs.size(); ++i) { add(item(Lang), langs[i].label); if (settings.language == langs[i].value) selected = static_cast<int>(i); }
    if (!langs.empty()) choose(item(Lang), selected);
}
void refresh_folder() {
    versions.clear(); SendMessageW(item(Versions), CB_RESETCONTENT, 0, 0);
    const auto found = s::candidates(fs::path(text(item(Folder)))); std::string last;
    for (const auto& p : found) {
        const auto valid = s::validate_source(p, false);
        if (valid.ok) { versions.push_back(p); add(item(Versions), p.filename().wstring() + L" — supported build DB93B012"); }
        else last = valid.error;
    }
    if (versions.empty()) {
        status(last.empty() ? L"Select the extracted game folder containing exefs and romfs, or its parent update folder." : s::wide(last));
    } else {
        choose(item(Versions), 0);
        status(L"Version recognized. Files will be verified before saving.");
    }
    refresh_languages(); EnableWindow(item(Save), !versions.empty()); EnableWindow(item(SavePlay), !versions.empty());
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
            if (SUCCEEDED(chosen->GetDisplayName(SIGDN_FILESYSPATH, &path))) { SetWindowTextW(item(Folder), path); CoTaskMemFree(path); refresh_folder(); }
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
    enabled(false); SendMessageW(item(Progress), PBM_SETMARQUEE, TRUE, 80);
    status(L"Preparing your local copy. The first build and shaders may take time. You can cancel and resume later.");
    SetTimer(window, 1, 500, nullptr);
}
void apply(bool play) {
    if (preparation) return;
    if (s::game_running(game_dir)) throw std::runtime_error("Close the game before changing its settings.");
    const int v = selection(Versions), l = selection(Lang), r = selection(Resolution);
    if (v < 0 || l < 0 || r < 0) throw std::runtime_error("Select a valid folder, resolution and language.");
    pending = settings; pending.source = versions.at(v); pending.romfs = pending.source / L"romfs";
    pending.language = langs.at(l).value; pending.width = sizes.at(r).first; pending.height = sizes.at(r).second;
    pending.fullscreen = selection(Mode) == 1; pending.skip = SendMessageW(item(Skip), BM_GETCHECK, 0, 0) == BST_CHECKED;
    pending.vsync = SendMessageW(item(Vsync), BM_GETCHECK, 0, 0) == BST_CHECKED;
    pending.fps60 = SendMessageW(item(Fps60), BM_GETCHECK, 0, 0) == BST_CHECKED;
    pending.completed = true; start_game = play;
    status(L"Verifying original files and the prepared copy…"); UpdateWindow(window);
    const auto original = s::validate_source(pending.source, true);
    if (!original.ok) throw std::runtime_error(original.error);
    if (s::validate_ready(game_dir, pending, false).ok) finish_save(); else prepare();
}
void create_controls() {
    font = CreateFontW(-px(16), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    control(L"STATIC", L"Fire Emblem Recomp", 0, 24, 20, 620, 28);
    control(L"STATIC", L"Set up your local copy. A controller is required to play.", 0, 24, 53, 620, 30);
    control(L"STATIC", L"Game folder", 0, 24, 96, 240, 22);
    control(L"EDIT", settings.source.c_str(), Folder, 24, 123, 498, 28, WS_TABSTOP | ES_AUTOHSCROLL);
    control(L"BUTTON", L"Browse…", Browse, 534, 122, 112, 30, WS_TABSTOP | BS_PUSHBUTTON);
    control(L"STATIC", L"Game version", 0, 24, 164, 250, 22);
    control(L"COMBOBOX", L"", Versions, 24, 189, 622, 180, WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL);
    control(L"STATIC", L"Display mode", 0, 24, 235, 290, 22);
    control(L"COMBOBOX", L"", Mode, 24, 262, 296, 130, WS_TABSTOP | CBS_DROPDOWNLIST);
    add(item(Mode), L"Windowed"); add(item(Mode), L"Fullscreen (desktop)"); choose(item(Mode), settings.fullscreen ? 1 : 0);
    control(L"STATIC", L"Window resolution", 0, 344, 235, 290, 22);
    control(L"COMBOBOX", L"", Resolution, 344, 262, 302, 180, WS_TABSTOP | CBS_DROPDOWNLIST);
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
    choose(item(Resolution), index); EnableWindow(item(Resolution), !settings.fullscreen);
    control(L"STATIC", L"Fullscreen uses your desktop. Internal rendering keeps the game’s original resolution.", 0, 24, 303, 622, 40);
    control(L"STATIC", L"Game language", 0, 24, 352, 300, 22);
    control(L"COMBOBOX", L"", Lang, 24, 379, 622, 230, WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL);
    control(L"BUTTON", L"Vertical synchronization", Vsync, 24, 425, 245, 25, WS_TABSTOP | BS_AUTOCHECKBOX);
    SendMessageW(item(Vsync), BM_SETCHECK, settings.vsync ? BST_CHECKED : BST_UNCHECKED, 0);
    control(L"BUTTON", L"Skip setup on startup", Skip, 24, 489, 622, 25, WS_TABSTOP | BS_AUTOCHECKBOX);
    SendMessageW(item(Skip), BM_SETCHECK, settings.skip ? BST_CHECKED : BST_UNCHECKED, 0);
    control(L"BUTTON", L"60 FPS (Kompas9, experimental)", Fps60, 280, 425, 366, 25, WS_TABSTOP | BS_AUTOCHECKBOX);
    SendMessageW(item(Fps60), BM_SETCHECK, settings.fps60 ? BST_CHECKED : BST_UNCHECKED, 0);
    control(L"STATIC", L"60 FPS changes the limit and timing. Restart required; performance depends on your PC.", 0, 24, 456, 622, 24);
    control(L"STATIC", L"", Status, 24, 523, 622, 39);
    control(PROGRESS_CLASSW, L"", Progress, 24, 568, 622, 9, PBS_MARQUEE);
    control(L"BUTTON", L"Cancel", IDCANCEL, 24, 588, 112, 34, WS_TABSTOP);
    control(L"BUTTON", L"Open log", OpenLog, 148, 588, 150, 34, WS_TABSTOP);
    EnableWindow(item(OpenLog), fs::is_regular_file(game_dir / L"preparation.log"));
    control(L"BUTTON", L"Save", Save, 366, 588, 128, 34, WS_TABSTOP);
    control(L"BUTTON", L"Save and play", SavePlay, 506, 588, 140, 34, WS_TABSTOP | BS_DEFPUSHBUTTON);
    refresh_folder();
}
LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    try {
        switch (msg) {
            case WM_CREATE: window = h; create_controls(); return 0;
            case WM_COMMAND:
                switch (LOWORD(w)) {
                    case Browse: browse(); return 0;
                    case Folder: if (HIWORD(w) == EN_KILLFOCUS) refresh_folder(); return 0;
                    case Versions: if (HIWORD(w) == CBN_SELCHANGE) refresh_languages(); return 0;
                    case Mode: if (HIWORD(w) == CBN_SELCHANGE) EnableWindow(item(Resolution), selection(Mode) == 0); return 0;
                    case Save: apply(false); return 0;
                    case SavePlay: apply(true); return 0;
                    case OpenLog: {
                        const auto log = game_dir / L"preparation.log";
                        if (reinterpret_cast<INT_PTR>(ShellExecuteW(h, L"open", log.c_str(), nullptr, game_dir.c_str(), SW_SHOWNORMAL)) <= 32)
                            throw std::runtime_error("Cannot open preparation.log. Open it from the setup folder.");
                        return 0;
                    }
                    case IDCANCEL: DestroyWindow(h); return 0;
                } break;
            case WM_TIMER:
                if (preparation) {
                    if (WaitForSingleObject(preparation, 0) == WAIT_OBJECT_0) {
                        DWORD code = 1; GetExitCodeProcess(preparation, &code); CloseHandle(preparation); preparation = nullptr;
                        CloseHandle(job); job = nullptr; KillTimer(h, 1); SendMessageW(item(Progress), PBM_SETMARQUEE, FALSE, 0); enabled(true);
                        if (!code) { finish_save(); return 0; }
                        SendMessageW(item(Progress), PBM_SETPOS, 0, 0);
                        status(L"Preparation failed. Use Open log for details, then retry.");
                        std::ifstream in(game_dir / L"preparation.status", std::ios::binary);
                        std::string detail; std::getline(in, detail);
                        error("Preparation failed.\n\n" + detail.substr(0, 4000) + "\n\nUse Open log for details, then retry.");
                    } else {
                        std::ifstream in(game_dir / L"preparation.status", std::ios::binary);
                        std::string line; if (std::getline(in, line) && !line.empty()) status(s::wide(line));
                    }
                } return 0;
            case WM_CLOSE: DestroyWindow(h); return 0;
            case WM_CTLCOLORSTATIC:
                SetTextColor(reinterpret_cast<HDC>(w), GetSysColor(COLOR_WINDOWTEXT));
                SetBkMode(reinterpret_cast<HDC>(w), TRANSPARENT);
                return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
            case WM_DESTROY:
                if (job) { CloseHandle(job); job = nullptr; }
                if (preparation) { CloseHandle(preparation); preparation = nullptr; }
                if (font) { DeleteObject(font); font = nullptr; }
                PostQuitMessage(result); return 0;
        }
    } catch (const std::exception& e) { enabled(true); status(s::wide(e.what())); error(e.what()); }
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
        int width = 0, height = 0; std::string language; int skip = -1, vsync = -1, fps60 = -1;
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
            else if (arg == L"--fps60") fps60 = std::stoi(next());
            else if (arg == L"--test-ui") test_ui = true;
            else throw std::runtime_error("Unknown argument: " + s::utf8(arg));
        }
        LocalFree(argv);
        if (validate) { const auto v = s::validate_source(source, true); std::printf("%s\n", v.ok ? "VALID" : v.error.c_str()); return v.ok ? 0 : 2; }
        settings = s::load(game_dir); if (!source.empty()) { settings.source = source; settings.romfs = source / L"romfs"; }
        if (width) settings.width = width; if (height) settings.height = height;
        if (!language.empty()) settings.language = language; if (skip >= 0) settings.skip = skip != 0;
        if (vsync >= 0) settings.vsync = vsync != 0;
        if (fps60 >= 0) settings.fps60 = fps60 != 0;
        if (status_cli) {
            const auto v = s::validate_ready(game_dir, settings);
            std::printf("ready=%d skip=%d show_setup=%d width=%d height=%d language=%s fps60=%d\n%s\n", v.ok, settings.skip, s::needs_setup(game_dir), settings.width, settings.height, settings.language.c_str(), settings.fps60, v.error.c_str()); return v.ok ? 0 : 2;
        }
        if (apply_cli) { settings.completed = true; s::save(game_dir, settings); std::puts("SAVED"); return 0; }
        const HANDLE lock = CreateMutexW(nullptr, FALSE, (s::game_mutex_name(game_dir) + L"_setup").c_str());
        if (lock && GetLastError() == ERROR_ALREADY_EXISTS) return 0;
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        INITCOMMONCONTROLSEX cc{sizeof cc, ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS}; InitCommonControlsEx(&cc);
        dpi = static_cast<int>(GetDpiForSystem());
        WNDCLASSEXW wc{sizeof wc}; wc.hInstance = instance; wc.lpfnWndProc = proc; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION); wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1); wc.lpszClassName = L"FEFW_Configuration";
        RegisterClassExW(&wc);
        RECT rect{0, 0, px(670), px(646)};
        const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        AdjustWindowRectEx(&rect, style, FALSE, WS_EX_CONTROLPARENT);
        HWND h = CreateWindowExW(WS_EX_CONTROLPARENT, wc.lpszClassName, L"Fire Emblem Recomp — Setup", style,
            CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, instance, nullptr);
        if (!h) throw std::runtime_error("Cannot open the setup tool");
        if (test_ui) {
            const bool ok = !versions.empty() && !langs.empty() && selection(Versions) >= 0 && selection(Lang) >= 0 && selection(Mode) >= 0 && selection(Resolution) >= 0 &&
                IsWindowEnabled(item(Save)) && SendMessageW(item(Skip), BM_GETCHECK, 0, 0) == (settings.skip ? BST_CHECKED : BST_UNCHECKED) &&
                SendMessageW(item(Fps60), BM_GETCHECK, 0, 0) == (settings.fps60 ? BST_CHECKED : BST_UNCHECKED);
            std::printf("ui_valid=%d versions=%zu languages=%zu\n", ok, versions.size(), langs.size());
            // Capture only our own configuration form for visual QA, then
            // exercise the real Cancel handler without saving anything.
            RECT client{}; GetClientRect(h, &client);
            HDC dc = GetDC(h), mem = CreateCompatibleDC(dc);
            HBITMAP bitmap = CreateCompatibleBitmap(dc, client.right, client.bottom);
            HGDIOBJ previous = SelectObject(mem, bitmap);
            ShowWindow(h, SW_SHOWNOACTIVATE); UpdateWindow(h);
            PrintWindow(h, mem, PW_CLIENTONLY | 0x00000002 /* PW_RENDERFULLCONTENT */);
            SelectObject(mem, previous);
            BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); bi.bmiHeader.biWidth = client.right;
            bi.bmiHeader.biHeight = -client.bottom; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
            std::vector<unsigned char> pixels(static_cast<size_t>(client.right) * client.bottom * 4);
            if (GetDIBits(mem, bitmap, 0, client.bottom, pixels.data(), &bi, DIB_RGB_COLORS)) {
                BITMAPFILEHEADER bf{}; bf.bfType = 0x4d42; bf.bfOffBits = sizeof bf + sizeof bi.bmiHeader; bf.bfSize = bf.bfOffBits + static_cast<DWORD>(pixels.size());
                std::ofstream bmp(game_dir / L"setup-ui.bmp", std::ios::binary);
                bmp.write(reinterpret_cast<const char*>(&bf), sizeof bf); bmp.write(reinterpret_cast<const char*>(&bi.bmiHeader), sizeof bi.bmiHeader);
                bmp.write(reinterpret_cast<const char*>(pixels.data()), pixels.size());
            }
            DeleteObject(bitmap); DeleteDC(mem); ReleaseDC(h, dc);
            result = ok ? 0 : 2; SendMessageW(h, WM_COMMAND, IDCANCEL, 0);
        } else ShowWindow(h, show);
        MSG msg{}; while (GetMessageW(&msg, nullptr, 0, 0) > 0) if (!IsDialogMessageW(h, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
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
