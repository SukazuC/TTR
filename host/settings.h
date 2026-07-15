#pragma once
#include <Windows.h>
namespace ttr::host { struct Settings { bool enabled=true; bool checkManifestUpdates=true; ULONGLONG lastManifestCheckUtc=0; }; Settings LoadSettings() noexcept; bool SaveEnabled(bool) noexcept; }
