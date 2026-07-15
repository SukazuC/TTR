#pragma once
#include "explorer_session.h"
#include <string>
namespace ttr::host { bool ExportDiagnostics(HWND,const ExplorerInfo*,std::uint64_t,const std::wstring&,std::wstring&)noexcept; }
