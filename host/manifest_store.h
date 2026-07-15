#pragma once
#include "ttr_manifest.h"
#include "pe_identity.h"
#include <span>
#include <string>
#include <vector>
namespace ttr::host {class ManifestStore{public:bool Load(std::string&)noexcept;const ManifestView&view()const noexcept{return view_;}std::span<const std::byte>SelectedRecordBlob(const std::vector<PeIdentity>&,std::uint64_t&)const noexcept;private:std::vector<std::byte>bytes_;ManifestView view_{};};}
