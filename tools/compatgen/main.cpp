#include "pe_identity.h"
#include <Windows.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <dia2.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <vector>

namespace
{
template <class T> class Com
{
public:
  ~Com()
  {
    if (value_)
      value_->Release();
  }
  T** put()
  {
    return &value_;
  }
  T* operator->() const
  {
    return value_;
  }
  operator bool() const
  {
    return value_ != nullptr;
  }

private:
  T* value_{};
};
struct Spec
{
  std::string id, kind;
  bool optional{};
  std::vector<std::string> names;
};
std::string Trim(std::string s)
{
  auto non = [](unsigned char c) { return !std::isspace(c); };
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), non));
  s.erase(std::find_if(s.rbegin(), s.rend(), non).base(), s.end());
  return s;
}
std::wstring Wide(std::string const& s)
{
  int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.c_str(), static_cast<int>(s.size()),
                              nullptr, 0);
  std::wstring w(n, L'\0');
  if (n)
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.c_str(), static_cast<int>(s.size()),
                        w.data(), n);
  return w;
}
std::string Narrow(std::wstring const& s)
{
  int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()),
                              nullptr, 0, nullptr, nullptr);
  std::string r(n, '\0');
  if (n)
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()),
                        r.data(), n, nullptr, nullptr);
  return r;
}
bool ReadSpec(char const* path, std::string const& module, std::vector<Spec>& out,
              std::string& error)
{
  std::ifstream f(path);
  if (!f)
  {
    error = "unable to open symbol specification";
    return false;
  }
  std::string line, currentModule;
  Spec current;
  bool have{};
  auto finish = [&] {
    if (have && _stricmp(currentModule.c_str(), module.c_str()) == 0)
      out.push_back(current);
    current = {};
    have = false;
  };
  while (std::getline(f, line))
  {
    auto text = Trim(line);
    if (text.empty() || text[0] == '#' || text == "modules:" || text.rfind("version:", 0) == 0 ||
        text.rfind("source:", 0) == 0)
      continue;
    if (line.rfind("  ", 0) == 0 && line.rfind("    ", 0) != 0 && text.back() == ':')
    {
      finish();
      currentModule = text.substr(0, text.size() - 1);
      continue;
    }
    if (text.rfind("- id:", 0) == 0)
    {
      finish();
      current.id = Trim(text.substr(5));
      have = true;
      continue;
    }
    if (!have)
      continue;
    if (text.rfind("kind:", 0) == 0)
      current.kind = Trim(text.substr(5));
    else if (text.rfind("optional:", 0) == 0)
      current.optional = Trim(text.substr(9)) == "true";
    else if (text.rfind("names:", 0) == 0)
    {
      try
      {
        current.names = nlohmann::json::parse(Trim(text.substr(6))).get<std::vector<std::string>>();
      }
      catch (...)
      {
        error = "names must be a JSON-compatible YAML array";
        return false;
      }
    }
  }
  finish();
  if (out.empty())
  {
    error = "module has no entries in symbol specification";
    return false;
  }
  for (auto const& s : out)
    if (s.id.empty() || s.names.empty() || (s.kind != "function" && s.kind != "data"))
    {
      error = "incomplete symbol specification entry";
      return false;
    }
  return true;
}
bool SameGuid(ttr::GuidBytes const& a, GUID const& b)
{
  return std::memcmp(a.value, &b, 16) == 0;
}
HRESULT CreateDiaSource(IDiaDataSource** source, HMODULE& module)
{
  HRESULT hr = CoCreateInstance(CLSID_DiaSource, nullptr, CLSCTX_INPROC_SERVER,
                                __uuidof(IDiaDataSource), reinterpret_cast<void**>(source));
  if (SUCCEEDED(hr))
    return hr;
  const HMODULE loaded = LoadLibraryExW(
      TTR_DIA_DLL_PATH_W, nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
  if (!loaded)
    return HRESULT_FROM_WIN32(GetLastError());
  using GetClassObject = HRESULT(WINAPI*)(REFCLSID, REFIID, LPVOID*);
  auto getClassObject =
      reinterpret_cast<GetClassObject>(GetProcAddress(loaded, "DllGetClassObject"));
  if (!getClassObject)
    return HRESULT_FROM_WIN32(GetLastError());
  Com<IClassFactory> factory;
  hr = getClassObject(CLSID_DiaSource, IID_IClassFactory, reinterpret_cast<void**>(factory.put()));
  if (SUCCEEDED(hr))
    hr = factory->CreateInstance(nullptr, __uuidof(IDiaDataSource),
                                 reinterpret_cast<void**>(source));
  module = nullptr;
  return hr;
}
} // namespace

int wmain(int argc, wchar_t** argv)
{
  if (argc != 5)
  {
    std::wcerr << L"usage: compatgen <module.dll> <module.pdb> <symbol-spec.yaml> <output.json>\n";
    return 2;
  }
  ttr::PeIdentity image;
  std::string error;
  if (!ttr::ReadPeIdentity(argv[1], image, error))
  {
    std::cerr << "compatgen: " << error << "\n";
    return 1;
  }
  std::vector<Spec> specs;
  auto moduleName = std::string(image.identity.baseName);
  if (!ReadSpec(Narrow(argv[3]).c_str(), moduleName, specs, error))
  {
    std::cerr << "compatgen: " << error << "\n";
    return 1;
  }
  if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)))
  {
    std::cerr << "compatgen: COM initialization failed\n";
    return 1;
  }
  HMODULE diaModule{};
  Com<IDiaDataSource> source;
  Com<IDiaSession> session;
  Com<IDiaSymbol> global;
  HRESULT hr = CreateDiaSource(source.put(), diaModule);
  if (SUCCEEDED(hr))
    hr = source->loadDataFromPdb(argv[2]);
  if (SUCCEEDED(hr))
    hr = source->openSession(session.put());
  if (SUCCEEDED(hr))
    hr = session->get_globalScope(global.put());
  if (FAILED(hr))
  {
    std::wcerr << L"compatgen: DIA could not open the PDB (HRESULT 0x" << std::hex << hr << L")\n";
    CoUninitialize();
    if (diaModule)
      FreeLibrary(diaModule);
    return 1;
  }
  GUID pdbGuid{};
  DWORD pdbAge{};
  if (FAILED(global->get_guid(&pdbGuid)) || FAILED(global->get_age(&pdbAge)) ||
      !SameGuid(image.identity.pdbGuid, pdbGuid) || image.identity.pdbAge != pdbAge)
  {
    std::cerr << "compatgen: PDB identity does not match the module CodeView record\n";
    CoUninitialize();
    if (diaModule)
      FreeLibrary(diaModule);
    return 1;
  }
  nlohmann::json output;
  output["module"] = {{"name", moduleName},
                      {"timestamp", image.identity.timeDateStamp},
                      {"size_of_image", image.identity.sizeOfImage},
                      {"pdb_guid", Narrow(ttr::GuidString(image.identity.pdbGuid))},
                      {"pdb_age", image.identity.pdbAge}};
  output["symbols"] = nlohmann::json::array();
  for (auto const& spec : specs)
  {
    std::set<DWORD> matches;
    for (auto const& candidate : spec.names)
    {
      Com<IDiaEnumSymbols> symbols;
      auto name = Wide(candidate);
      if (FAILED(global->findChildren(SymTagNull, name.c_str(), nsfCaseSensitive, symbols.put())) ||
          !symbols)
        continue;
      for (;;)
      {
        IDiaSymbol* raw{};
        ULONG fetched{};
        if (symbols->Next(1, &raw, &fetched) != S_OK || !fetched)
          break;
        Com<IDiaSymbol> symbol;
        *symbol.put() = raw;
        DWORD rva{};
        if (SUCCEEDED(symbol->get_relativeVirtualAddress(&rva)))
          matches.insert(rva);
      }
    }
    if (matches.empty() && spec.optional)
      continue;
    if (matches.size() != 1)
    {
      std::cerr << "compatgen: symbol " << spec.id
                << (!matches.empty() ? " is ambiguous" : " was not found") << "\n";
      CoUninitialize();
      if (diaModule)
        FreeLibrary(diaModule);
      return 1;
    }
    const DWORD foundRva = *matches.begin();
    auto kind = spec.kind == "function" ? ttr::SymbolKind::Function : ttr::SymbolKind::ReadOnlyData;
    if (!ttr::ValidateSymbolRva(image, foundRva, kind))
    {
      std::cerr << "compatgen: symbol " << spec.id << " has the wrong PE section permissions\n";
      CoUninitialize();
      if (diaModule)
        FreeLibrary(diaModule);
      return 1;
    }
    output["symbols"].push_back(
        {{"id", spec.id}, {"rva", foundRva}, {"kind", spec.kind}, {"required", !spec.optional}});
  }
  std::ofstream file(std::filesystem::path(argv[4]), std::ios::binary | std::ios::trunc);
  file << output.dump(2) << '\n';
  CoUninitialize();
  if (diaModule)
    FreeLibrary(diaModule);
  if (!file)
  {
    std::cerr << "compatgen: unable to write output\n";
    return 1;
  }
  return 0;
}
