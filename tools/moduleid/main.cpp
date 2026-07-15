#include "pe_identity.h"
#include <Windows.h>
#include <filesystem>
#include <iostream>
#include <string_view>
#include <vector>

namespace {
std::wstring EscapeJson(std::wstring_view value) {
  std::wstring result;
  result.reserve(value.size());
  for (const wchar_t character : value) {
    if (character == L'\\' || character == L'"') result.push_back(L'\\');
    result.push_back(character);
  }
  return result;
}
}

int wmain(int argc, wchar_t** argv) {
  std::vector<std::wstring> paths;
  if (argc > 1) {
    for (int i = 1; i < argc; ++i) paths.emplace_back(argv[i]);
  } else {
    wchar_t windows[MAX_PATH]{};
    const UINT length = GetWindowsDirectoryW(windows, MAX_PATH);
    if (!length || length >= MAX_PATH) {
      std::cerr << "moduleid: unable to locate the Windows directory\n";
      return 1;
    }
    paths.push_back(std::filesystem::path(windows) / L"explorer.exe");
    paths.push_back(std::filesystem::path(windows) / L"System32" / L"taskbar.dll");
  }

  bool first = true;
  int result = 0;
  std::wcout << L"[\n";
  for (const auto& path : paths) {
    ttr::PeIdentity pe;
    std::string error;
    if (!ttr::ReadPeIdentity(path, pe, error)) {
      std::wcerr << L"moduleid: " << path << L": "
                 << std::wstring(error.begin(), error.end()) << L'\n';
      result = 1;
      continue;
    }
    wchar_t name[40]{};
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, pe.identity.baseName, -1,
                             name, static_cast<int>(std::size(name)))) {
      std::wcerr << L"moduleid: " << path << L": invalid module name\n";
      result = 1;
      continue;
    }
    if (!first) std::wcout << L",\n";
    first = false;
    std::wcout << L"  {\"path\":\"" << EscapeJson(path) << L"\",\"name\":\""
               << EscapeJson(name) << L"\",\"timestamp\":" << pe.identity.timeDateStamp
               << L",\"size_of_image\":" << pe.identity.sizeOfImage << L",\"pdb_guid\":\""
               << ttr::GuidString(pe.identity.pdbGuid) << L"\",\"pdb_age\":"
               << pe.identity.pdbAge << L"}";
  }
  std::wcout << L"\n]\n";
  return result;
}
