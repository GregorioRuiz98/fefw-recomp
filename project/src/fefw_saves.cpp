// SPDX-License-Identifier: GPL-3.0-or-later
#include "fefw_saves.h"
#include <windows.h>
#include <objbase.h>
#include <algorithm>
#include <array>
#include <cwctype>
#include <fstream>
#include <stdexcept>
namespace fefw::setup {
namespace {
fs::path io_path(const fs::path& value) {
    const auto path=fs::absolute(value).lexically_normal().wstring();
    if(path.starts_with(L"\\\\?\\"))return path;
    if(path.starts_with(L"\\\\"))return L"\\\\?\\UNC\\"+path.substr(2);
    return L"\\\\?\\"+path;
}
fs::path plain_path(const fs::path& value) {
    const auto path=value.wstring();
    if(path.starts_with(L"\\\\?\\UNC\\"))return L"\\\\"+path.substr(8);
    if(path.starts_with(L"\\\\?\\"))return path.substr(4);
    return value;
}
fs::path active_saves(const fs::path& game) {
    fs::path path=wide(Ini(game/L"fefw.ini").get("fs","save_dir","save"));
    return plain_path(fs::weakly_canonical(io_path(path.is_absolute()?path:game/path)));
}
void regular_tree(const fs::path& folder) {
    const auto root=io_path(folder);
    auto linked=[](const fs::path& p){const auto attr=GetFileAttributesW(p.c_str());return attr==INVALID_FILE_ATTRIBUTES||(attr&FILE_ATTRIBUTE_REPARSE_POINT);};
    if(linked(root)) throw std::runtime_error("Choose a regular saved-game folder; linked folders cannot be imported.");
    for(const auto& p:fs::recursive_directory_iterator(root))
        if(linked(p.path())||(!p.is_directory()&&!p.is_regular_file())) throw std::runtime_error("The saved-game folder contains an unsupported file or link.");
}
bool inside(const fs::path& path,const fs::path& root) {
    auto lower=[](std::wstring value){std::transform(value.begin(),value.end(),value.begin(),[](wchar_t c){return std::towlower(c);});return value;};
    auto base=lower(fs::weakly_canonical(io_path(root)).wstring());auto child=lower(fs::weakly_canonical(io_path(path)).wstring());
    return child==base||child.starts_with(base+L"\\");
}
bool same_bytes(const fs::path& left,const fs::path& right) {
    if(fs::file_size(left)!=fs::file_size(right))return false;
    std::ifstream a(left,std::ios::binary),b(right,std::ios::binary);
    if(!a||!b)return false;
    std::array<char,65536> x{},y{};
    do {a.read(x.data(),x.size());b.read(y.data(),y.size());if(a.gcount()!=b.gcount()||!std::equal(x.begin(),x.begin()+a.gcount(),y.begin()))return false;}while(a&&b);
    return a.eof()&&b.eof()&&!a.bad()&&!b.bad();
}
void copy_tree(const fs::path& from,const fs::path& to) {
    const auto source=io_path(from),target=io_path(to);
    if(inside(target,source))throw std::runtime_error("The saved-game source cannot contain the import or backup destination.");
    regular_tree(source);fs::create_directories(target);
    for(const auto& entry:fs::recursive_directory_iterator(source)) {
        const auto out=target/entry.path().lexically_relative(source);
        if(entry.is_directory())fs::create_directories(out);
        else {fs::create_directories(out.parent_path());fs::copy_file(entry.path(),out);if(!same_bytes(entry.path(),out))throw std::runtime_error("Saved-game copy verification failed. Active saves were preserved.");}
    }
}
}
SavedGame inspect_saved_game(const fs::path& selected) {
    const auto folder=plain_path(fs::canonical(io_path(selected)));
    fs::path root=folder,installation;
    if(fs::is_regular_file(folder/L"fefw.ini")) {installation=folder;root=active_saves(folder);}
    else if(fs::is_directory(folder/L"save/account")) {installation=folder;root=folder/L"save";}
    else if(fs::is_regular_file(folder.parent_path()/L"fefw.ini")&&active_saves(folder.parent_path())==folder)installation=folder.parent_path();
    root=io_path(root);
    if(!fs::is_directory(root/L"account"))throw std::runtime_error("No complete saved game found. Select the other build's folder or its save folder.");
    regular_tree(root);
    bool complete=false;size_t count=0;
    for(const auto& user:fs::directory_iterator(root/L"account")) {
        if(!user.is_directory()||!fs::is_regular_file(root/L"account"/(user.path().filename().wstring()+L".meta")))continue;
        for(const auto& slot:fs::directory_iterator(user.path())) {
            if(!slot.is_directory()||!fs::is_regular_file(slot.path()/L"Global")||fs::file_size(slot.path()/L"Global")==0)continue;
            for(const auto& file:fs::directory_iterator(slot.path())) {
                const auto name=file.path().filename().wstring();
                if(file.is_regular_file()&&(name==L"Auto"||name.starts_with(L"Chapter_"))&&fs::file_size(file.path())>0)complete=true;
            }
        }
    }
    if(!complete)throw std::runtime_error("The selected save is incomplete: account metadata, Global and an Auto or Chapter save are required.");
    for(const auto& p:fs::recursive_directory_iterator(root))if(p.is_regular_file())++count;
    return {plain_path(fs::canonical(root)),installation,count};
}
SaveImport import_saved_game(const fs::path& game,const fs::path& selected) {
    fs::create_directories(game);
    const auto installation=fs::canonical(game);
    if(game_running(installation))throw std::runtime_error("Close the game before importing a saved game.");
    const auto source=inspect_saved_game(selected);
    if(!source.installation.empty()&&game_running(source.installation))throw std::runtime_error("Close the game in the other build before importing its saved game.");
    if(source.root==active_saves(installation))throw std::runtime_error("This saved game is already active in this installation.");
    GUID guid{};if(FAILED(CoCreateGuid(&guid)))throw std::runtime_error("Cannot create a saved-game import folder.");
    wchar_t id[40]{};StringFromGUID2(guid,id,40);
    SaveImport result{installation/L"save-imports"/id,installation/L"save-backups"/id,source.files};
    const auto current=active_saves(installation);
    fs::create_directories(io_path(result.backup));
    if(fs::is_regular_file(installation/L"fefw.ini"))fs::copy_file(io_path(installation/L"fefw.ini"),io_path(result.backup/L"fefw.ini"));
    if(fs::is_directory(current))copy_tree(current,result.backup/L"save");
    copy_tree(source.root,result.destination);
    const auto imported=inspect_saved_game(result.destination);
    if(imported.files!=source.files)throw std::runtime_error("Saved-game import is incomplete. Active saves were preserved.");
    // The original folders are never modified. Switch only after the complete
    // copy and backup succeed; Ini::save replaces the config atomically.
    Ini ini(installation/L"fefw.ini");ini.set("fs","save_dir",path_text(result.destination));ini.save(installation/L"fefw.ini");
    return result;
}
}
