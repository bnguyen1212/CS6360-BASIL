// ours/sosd_io.h -- our own SOSD-format loader (NOT the harness's util::load_data).
// Format: uint64 count N, then N uint64 keys (little-endian). Loads exactly one
// copy: at most `max_keys` keys are read straight into the returned vector.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace ours {

inline std::vector<uint64_t> load_sosd_keys(const std::string& path,
                                            size_t max_keys = SIZE_MAX) {
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) throw std::runtime_error("cannot open " + path);
  uint64_t count = 0;
  if (std::fread(&count, sizeof(count), 1, f) != 1) {
    std::fclose(f);
    throw std::runtime_error("cannot read header of " + path);
  }
  const size_t n = static_cast<size_t>(std::min<uint64_t>(count, max_keys));
  std::vector<uint64_t> keys(n);
  const size_t got = std::fread(keys.data(), sizeof(uint64_t), n, f);
  std::fclose(f);
  if (got != n) throw std::runtime_error("short read from " + path);
  if (!std::is_sorted(keys.begin(), keys.end()))
    throw std::runtime_error(path + " is not sorted");
  return keys;
}

}  // namespace ours
