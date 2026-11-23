// SPDX-License-Identifier: GPL-3.0-or-later
// The only embedded binary is the byte-identical audited public ZIP.
#include "fefw_config.h"
#include "fefw_bootstrap_payload.h"
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <bcrypt.h>
#include <fstream>
#include <vector>
#include <stdexcept>
#include <cstdio>
namespace fs=std::filesystem;
namespace s=fefw::setup;
namespace {
bool quiet=false;
std::string sha(const void* data,size_t bytes) {
    BCRYPT_ALG_HANDLE alg{};BCRYPT_HASH_HANDLE hash{};DWORD size{},got{};
    if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("Cannot verify the embedded package.");
    BCryptGetProperty(alg,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&size),sizeof size,&got,0);
    std::vector<unsigned char> object(size),digest(32);
    const bool ok=BCryptCreateHash(alg,&hash,object.data(),size,nullptr,0,0)>=0&&BCryptHashData(hash,(PUCHAR)data,static_cast<ULONG>(bytes),0)>=0&&BCryptFinishHash(hash,digest.data(),32,0)>=0;
    if(hash)BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(alg,0);
    if(!ok)throw std::runtime_error("Embedded package verification failed.");
    std::string out;for(auto ch:digest){out+="0123456789abcdef"[ch>>4];out+="0123456789abcdef"[ch&15];}return out;
}
std::wstring ps_string(const std::wstring& value){std::wstring out=L"'";for(wchar_t ch:value){out+=ch;if(ch==L'\'')out+=ch;}return out+L"'";}
void process(const fs::path& executable,std::wstring args,const fs::path& cwd,bool wait) {
    STARTUPINFOW si{sizeof si};si.dwFlags=STARTF_USESHOWWINDOW;si.wShowWindow=wait?SW_HIDE:SW_SHOWNORMAL;
    PROCESS_INFORMATION pi{};
    if(!CreateProcessW(executable.c_str(),args.data(),nullptr,nullptr,FALSE,wait?CREATE_NO_WINDOW:0,nullptr,cwd.c_str(),&si,&pi))throw std::runtime_error("Cannot open local preparation.");
    CloseHandle(pi.hThread);
    if(wait){WaitForSingleObject(pi.hProcess,INFINITE);DWORD code=1;GetExitCodeProcess(pi.hProcess,&code);CloseHandle(pi.hProcess);if(code)throw std::runtime_error("Package installation failed. Existing game files and saves were preserved. Close any open setup window and retry.");}
    else CloseHandle(pi.hProcess);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int) {
    try {
        int count=0;LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&count);
        fs::path installation;bool extract_only=false;
        for(int i=1;i<count;++i){if(!wcscmp(argv[i],L"--game")&&i+1<count)installation=fs::absolute(argv[++i]);else if(!wcscmp(argv[i],L"--extract-only")){extract_only=true;quiet=true;}else throw std::runtime_error("Usage: FEFW-Recomp.exe [--game FOLDER] [--extract-only]");}
        LocalFree(argv);
        if(installation.empty()){PWSTR path{};if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&path)))throw std::runtime_error("Cannot locate the local application folder.");installation=fs::path(path)/L"FEFW-Recomp";CoTaskMemFree(path);}
        const auto resource=FindResourceW(instance,MAKEINTRESOURCEW(201),RT_RCDATA);
        const auto bytes=resource?SizeofResource(instance,resource):0;
        const auto loaded=resource?LoadResource(instance,resource):nullptr;
        const void* data=loaded?LockResource(loaded):nullptr;
        if(!data||!bytes||sha(data,bytes)!=FEFW_PAYLOAD_SHA256)throw std::runtime_error("The launcher package is incomplete or damaged. Download it again from the project release.");
        fs::create_directories(installation);
        std::ifstream current(installation/L"bootstrap.version");std::string version;std::getline(current,version);
        const auto setup=installation/L"fefw_setup.exe";
        if(version!=FEFW_PAYLOAD_SHA256||!fs::is_regular_file(setup)) {
            GUID guid{};CoCreateGuid(&guid);wchar_t id[40]{};StringFromGUID2(guid,id,40);
            const auto stage=installation/L"work/bootstrap"/id;fs::create_directories(stage);
            const auto zip=stage/L"payload.zip",tree=stage/L"package";
            std::ofstream file(zip,std::ios::binary);file.write(static_cast<const char*>(data),bytes);file.close();if(!file)throw std::runtime_error("Not enough writable space for the package.");
            wchar_t system[MAX_PATH]{};GetSystemDirectoryW(system,MAX_PATH);
            const auto powershell=fs::path(system)/L"WindowsPowerShell/v1.0/powershell.exe";
            const auto script=L"$ErrorActionPreference='Stop'; Expand-Archive -LiteralPath "+ps_string(zip.wstring())+L" -DestinationPath "+ps_string(tree.wstring())+L"; & "+ps_string((tree/L"project/tools/Install-Bootstrap.ps1").wstring())+L" -Stage "+ps_string(tree.wstring())+L" -GameDirectory "+ps_string(installation.wstring())+L" -PayloadHash "+ps_string(s::wide(FEFW_PAYLOAD_SHA256));
            process(powershell,s::quote_arg(powershell.wstring())+L" -NoProfile -ExecutionPolicy Bypass -Command "+s::quote_arg(script),stage,true);
        }
        if(!extract_only)process(setup,s::quote_arg(setup.wstring())+L" --game "+s::quote_arg(installation.wstring()),installation,false);
        return 0;
    }catch(const std::exception& e){if(quiet)std::fprintf(stderr,"%s\n",e.what());else MessageBoxW(nullptr,s::wide(e.what()).c_str(),L"FEFW Recomp",MB_OK|MB_ICONERROR);return 1;}
}
