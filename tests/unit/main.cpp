#include "ttr_array_move.h"
#include "ttr_manifest.h"
#include "ttr_protocol.h"
#include <cstring>
#include <iostream>
#include <vector>

namespace
{
int failures{};
void Check(bool value, const char* name)
{
  if (!value)
  {
    std::cerr << "FAIL: " << name << "\n";
    ++failures;
  }
}
std::vector<std::byte> Empty()
{
  std::vector<std::byte> b(sizeof(ttr::ManifestHeaderV1));
  auto* h = reinterpret_cast<ttr::ManifestHeaderV1*>(b.data());
  std::memcpy(h->magic, ttr::kManifestMagic, 8);
  h->formatVersion = ttr::kManifestVersion;
  h->headerSize = sizeof(*h);
  h->totalSize = static_cast<std::uint32_t>(b.size());
  h->recordTableOffset = sizeof(*h);
  return b;
}
} // namespace
int main()
{
  Check(ttr::CheckedRange(0, 0, 0), "empty range");
  Check(!ttr::CheckedRange(SIZE_MAX, 2, SIZE_MAX), "overflow range");
  auto good = Empty();
  ttr::ManifestView view;
  std::string error;
  Check(ttr::ParseManifest(good, view, error), "valid empty manifest");
  auto bad = good;
  reinterpret_cast<ttr::ManifestHeaderV1*>(bad.data())->magic[0] = 'X';
  Check(!ttr::ParseManifest(bad, view, error), "bad magic");
  bad = good;
  reinterpret_cast<ttr::ManifestHeaderV1*>(bad.data())->totalSize++;
  Check(!ttr::ParseManifest(bad, view, error), "bad total size");
  bad = good;
  auto* h = reinterpret_cast<ttr::ManifestHeaderV1*>(bad.data());
  h->recordCount = UINT32_MAX;
  Check(!ttr::ParseManifest(bad, view, error), "excessive record count");
  void *a = reinterpret_cast<void*>(1), *b = reinterpret_cast<void*>(2),
       *c = reinterpret_cast<void*>(3);
  void* items[] = {a, b, c};
  Check(ttr::MovePointer(items, 0, 2) && items[0] == b && items[1] == c && items[2] == a,
        "forward array move");
  Check(ttr::MovePointer(items, 2, 0) && items[0] == a && items[1] == b && items[2] == c,
        "backward array move");
  Check(!ttr::MovePointer(items, 3, 0), "invalid array move");
  Check(sizeof(ttr::TtrSessionControlV1) == 88, "protocol layout");
  return failures ? 1 : 0;
}
