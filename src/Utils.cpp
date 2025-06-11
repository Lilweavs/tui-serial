#define NOMINMAX 1
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>

#include <filesystem>
#include <iostream>

std::filesystem::path getApplicationFolderDirectory() {
    std::filesystem::path path = "";
    PWSTR path_tmp;

    auto get_folder_path_ret = SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &path_tmp);
    
    if (get_folder_path_ret != S_OK) {
      path = "";
    } else {
      path = path_tmp;
    }
    CoTaskMemFree(path_tmp);

    return path;
}
