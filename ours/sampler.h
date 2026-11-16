// ours/sampler.h -- systematic sampling of a sorted key array.
//
// Systematic sampling with interval I takes every I-th key:
//     sample indices 0, I, 2I, ..., m*I   (m*I <= n-1)
// and ALWAYS adds the last index n-1 if it is not already a multiple of I.
// Including n-1 guarantees that every key lies between two sample indices that
// are at most I apart: no key sits past the final sample point.
//
// Each sample is (key, index): the key and the index it was taken from. The index is
// NOT necessarily the key's true position when there are duplicates (true position
// = lower_bound). sample_eb_pla.h turns these raw samples into the points it fits.
#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace ours {

struct SamplePoint {
  uint64_t key;
  size_t index;  // index in the key array this sample was taken from
};

// keys[0..n-1] must be sorted ascending (duplicates allowed). Requires I >= 1.
// Returns samples in increasing index order (hence non-decreasing key order).
inline std::vector<SamplePoint> systematic_sample(const uint64_t* keys, size_t n,
                                                  size_t I) {
  if (I == 0) throw std::invalid_argument("sampling interval I must be >= 1");
  std::vector<SamplePoint> out;
  if (n == 0) return out;
  out.reserve(n / I + 2);
  size_t j = 0;
  while (true) {
    out.push_back({keys[j], j});
    if (n - 1 - j < I) break;  // next multiple of I would be past n-1 (no overflow)
    j += I;
  }
  if (out.back().index != n - 1) out.push_back({keys[n - 1], n - 1});
  return out;
}

}  // namespace ours
