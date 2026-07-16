#include "pe_identity.h"
#include "sha256.h"

#include <Windows.h>

#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string_view>
#include <vector>

namespace
{

std::wstring EscapeJson(const std::wstring_view value)
{
  std::wstring result;
  result.reserve(value.size());
  for (const wchar_t character : value)
  {
    if (character == L'\\' || character == L'"')
      result.push_back(L'\\');
    result.push_back(character);
  }
  return result;
}

std::wstring Wide(const std::string_view value)
{
  if (value.empty())
    return {};
  const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                       static_cast<int>(value.size()), nullptr, 0);
  if (size <= 0)
    return {};
  std::wstring result(static_cast<std::size_t>(size), L'\0');
  if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                           static_cast<int>(value.size()), result.data(), size))
  {
    return {};
  }
  return result;
}

std::wstring Canonical(const std::wstring& path)
{
  std::error_code error;
  auto canonical = std::filesystem::weakly_canonical(path, error);
  return error ? path : canonical.wstring();
}

void PrintGeneric(const std::wstring& path, const ttr::PeImage& image, const ttr::Sha256& hash)
{
  std::wcout << L"  {\"path\":\"" << EscapeJson(Canonical(path)) << L"\",\"name\":\""
             << EscapeJson(Wide(image.baseName)) << L"\",\"sha256\":\"" << ttr::Sha256Hex(hash)
             << L"\",\"machine\":\"x64\",\"optional_header\":\"PE32+\"" << L",\"timestamp\":"
             << image.timeDateStamp << L",\"size_of_image\":" << image.sizeOfImage
             << L",\"debug_entries\":[";
  for (std::size_t index = 0; index < image.debugEntries.size(); ++index)
  {
    if (index)
      std::wcout << L',';
    const auto& entry = image.debugEntries[index];
    std::wcout << L"{\"type\":" << entry.type << L",\"size\":" << entry.sizeOfData << L",\"rva\":"
               << entry.addressOfRawData << L",\"file_offset\":" << entry.pointerToRawData << L'}';
  }
  std::wcout << L"],\"codeview\":";
  if (image.codeView)
  {
    std::wcout << L"{\"format\":\"RSDS\",\"pdb_guid\":\"" << ttr::GuidString(image.codeView->guid)
               << L"\",\"pdb_age\":" << image.codeView->age << L",\"pdb_path\":\""
               << EscapeJson(Wide(image.codeView->pdbPath)) << L"\"}";
  }
  else
  {
    std::wcout << L"null";
  }
  std::wcout << L",\"sections\":[";
  for (std::size_t index = 0; index < image.sections.size(); ++index)
  {
    if (index)
      std::wcout << L',';
    const auto& section = image.sections[index];
    std::wcout << L"{\"rva\":" << section.virtualAddress << L",\"virtual_size\":"
               << section.virtualSize << L",\"raw_size\":" << section.rawDataSize
               << L",\"raw_offset\":" << section.rawDataPointer << L",\"characteristics\":"
               << section.characteristics << L'}';
  }
  std::wcout << L"]}";
}

} // namespace

int wmain(const int argc, wchar_t** argv)
{
  bool inspectOnly = false;
  int firstPath = 1;
  if (argc > 1 && _wcsicmp(argv[1], L"--inspect") == 0)
  {
    inspectOnly = true;
    firstPath = 2;
  }
  std::vector<std::wstring> paths;
  if (argc > firstPath)
  {
    for (int index = firstPath; index < argc; ++index)
      paths.emplace_back(argv[index]);
  }
  else if (firstPath == 1)
  {
    wchar_t windows[MAX_PATH]{};
    const UINT length = GetWindowsDirectoryW(windows, MAX_PATH);
    if (!length || length >= MAX_PATH)
    {
      std::cerr << "moduleid: unable to locate the Windows directory\n";
      return 1;
    }
    paths.push_back(std::filesystem::path(windows) / L"explorer.exe");
    const auto taskbar = std::filesystem::path(windows) / L"System32" / L"taskbar.dll";
    if (std::filesystem::exists(taskbar))
      paths.push_back(taskbar);
  }
  else
  {
    std::cerr << "usage: moduleid [--inspect] <image> [image...]\n";
    return 2;
  }

  bool first = true;
  int result = 0;
  std::wcout << L"[\n";
  for (const auto& path : paths)
  {
    ttr::PeImage image;
    ttr::Sha256 hash{};
    std::string error;
    if (!ttr::InspectPeImage(path, image, error) || !ttr::Sha256File(path, hash))
    {
      std::wcerr << L"moduleid: " << path << L": " << Wide(error) << L'\n';
      result = 1;
      continue;
    }
    if (!inspectOnly && !image.codeView)
    {
      std::wcerr << L"moduleid: " << path
                 << L": RSDS CodeView identity not found (the image is a valid x64 PE)\n";
      result = 1;
      continue;
    }
    if (!first)
      std::wcout << L",\n";
    first = false;
    PrintGeneric(path, image, hash);
  }
  std::wcout << L"\n]\n";
  return result;
}
