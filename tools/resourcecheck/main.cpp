#include "crypto.h"
#include "manifest_selection.h"
#include "pe_identity.h"
#include "ttr_version.h"

#include <Windows.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <vector>

namespace
{

std::vector<std::byte> Read(const std::wstring& path)
{
  std::ifstream file(std::filesystem::path(path), std::ios::binary);
  if (!file)
    return {};
  file.seekg(0, std::ios::end);
  const auto size = file.tellg();
  file.seekg(0);
  if (size <= 0)
    return {};
  std::vector<std::byte> result(static_cast<std::size_t>(size));
  file.read(reinterpret_cast<char*>(result.data()), size);
  return file ? result : std::vector<std::byte>{};
}

bool Resource(HMODULE module, const int id, const wchar_t* type, std::span<const std::byte>& bytes)
{
  const auto resource = FindResourceW(module, MAKEINTRESOURCEW(id), type);
  const auto loaded = resource ? LoadResource(module, resource) : nullptr;
  const auto* data = loaded ? static_cast<const std::byte*>(LockResource(loaded)) : nullptr;
  const DWORD size = resource ? SizeofResource(module, resource) : 0;
  if (!data || !size)
    return false;
  bytes = {data, size};
  return true;
}

bool VersionMatches(const std::wstring& path)
{
  DWORD ignored = 0;
  const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
  if (!size)
    return false;
  std::vector<std::byte> bytes(size);
  if (!GetFileVersionInfoW(path.c_str(), 0, size, bytes.data()))
    return false;
  VS_FIXEDFILEINFO* info = nullptr;
  UINT infoSize = 0;
  if (!VerQueryValueW(bytes.data(), L"\\", reinterpret_cast<void**>(&info), &infoSize) || !info ||
      infoSize < sizeof(*info))
  {
    return false;
  }
  return HIWORD(info->dwFileVersionMS) == TTR_VERSION_MAJOR &&
         LOWORD(info->dwFileVersionMS) == TTR_VERSION_MINOR &&
         HIWORD(info->dwFileVersionLS) == TTR_VERSION_PATCH;
}

} // namespace

int wmain(const int argc, wchar_t** argv)
{
  if (argc != 6)
  {
    std::cerr
        << "usage: resourcecheck <host.exe> <payload.dll> <manifest> <signature> <public-key>\n";
    return 2;
  }
  ttr::PeImage host;
  std::string error;
  if (!ttr::InspectPeImage(argv[1], host, error))
  {
    std::cerr << "resourcecheck: host: " << error << '\n';
    return 1;
  }
  const HMODULE module = LoadLibraryExW(
      argv[1], nullptr, LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
  if (!module)
  {
    std::cerr << "resourcecheck: unable to load host resources\n";
    return 1;
  }
  std::span<const std::byte> embeddedPayload;
  std::span<const std::byte> publicKey;
  std::span<const std::byte> embeddedManifest;
  std::span<const std::byte> embeddedSignature;
  ttr::PeImage payloadImage;
  const auto builtPayload = Read(argv[2]);
  const bool payloadValid =
      Resource(module, 101, RT_RCDATA, embeddedPayload) &&
      std::ranges::equal(embeddedPayload, builtPayload) &&
      ttr::InspectPeImageBytes(embeddedPayload, "ttrhook64.dll", payloadImage, error);
  const bool keyValid = Resource(module, 102, RT_RCDATA, publicKey) &&
                        ttr::ValidateEcdsaP256PublicKey(publicKey, error) &&
                        std::ranges::equal(publicKey, Read(argv[5]));
  ttr::ManifestSelectionResult selection;
  const bool baselineValid =
      Resource(module, 103, RT_RCDATA, embeddedManifest) &&
      Resource(module, 104, RT_RCDATA, embeddedSignature) &&
      std::ranges::equal(embeddedManifest, Read(argv[3])) &&
      std::ranges::equal(embeddedSignature, Read(argv[4])) && keyValid &&
      ttr::SelectSignedManifest(publicKey, {embeddedManifest, embeddedSignature}, {}, selection,
                                error) &&
      selection.embeddedSignatureValid;
  const bool iconsValid = FindResourceW(module, MAKEINTRESOURCEW(201), RT_GROUP_ICON) &&
                          FindResourceW(module, MAKEINTRESOURCEW(202), RT_GROUP_ICON) &&
                          FindResourceW(module, MAKEINTRESOURCEW(203), RT_GROUP_ICON);
  const bool versionValid = VersionMatches(argv[1]);
  FreeLibrary(module);
  if (!payloadValid || !keyValid || !baselineValid || !iconsValid || !versionValid)
  {
    std::cerr << "resourcecheck: payload=" << payloadValid << " key=" << keyValid
              << " baseline=" << baselineValid << " icons=" << iconsValid
              << " version=" << versionValid << " detail=" << error << '\n';
    return 1;
  }
  std::cout << "resourcecheck: host x64; embedded payload exact/x64; public key and signed "
               "baseline exact/valid; icons present; version matches\n";
  return 0;
}
