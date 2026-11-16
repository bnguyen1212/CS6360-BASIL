// ours/lookup.h -- the full lookup path over Sample EB-PLA (the two-level index).
//
//   level 2: SegmentDirectory  key -> segment (binary search over segment first keys)
//   level 1: segment line      key -> predicted position p (clamped; see sample_eb_pla.h)
//   last mile: binary search   keys[max(0, p - e) .. min(n - 1, p + e)] -> lower_bound
//
// e = model.error_bound(): eps for the (default) tightened model, eps + I - 1 for the
// untightened experiment. Correctness for every key x in the array: the model
// guarantees |p - pos(x)| <= e, so pos(x) lies inside the window. Every key in the
// window before pos(x) is < x and keys[pos(x)] == x, so lower_bound over the window
// returns exactly pos(x).
// For keys NOT in the array, the window is not guaranteed to contain the answer
// (as in BASIL/SOSD, which only use positive lookups).
//
// The key array is referenced, never copied: the caller keeps it alive.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "sample_eb_pla.h"

namespace ours {

struct SearchWindow {
  size_t lo;  // first index to search
  size_t hi;  // one past the last index to search (exclusive, like BASIL's SearchBound.stop)
};

class SampleEBPLAIndex {
 public:
  SampleEBPLAIndex(const uint64_t* keys, size_t n, size_t eps, size_t I,
                   SampleEBPLA::Bound bound = SampleEBPLA::Bound::kTightened)
      : keys_(keys), n_(n), model_(keys, n, eps, I, bound), e_(model_.error_bound()) {}

  // [max(0, p - e), min(n - 1, p + e)], as a half-open window.
  SearchWindow window(uint64_t x) const {
    const size_t p = static_cast<size_t>(model_.predict(x));  // p <= n - 1 by the clamp
    const size_t lo = p > e_ ? p - e_ : 0;
    const size_t hi = std::min(n_ - 1, p + e_) + 1;
    return {lo, hi};
  }

  // lower_bound position of x (exact for every key present in the array).
  size_t lookup(uint64_t x) const {
    const SearchWindow w = window(x);
    return static_cast<size_t>(std::lower_bound(keys_ + w.lo, keys_ + w.hi, x) - keys_);
  }

  const SampleEBPLA& model() const { return model_; }
  size_t search_radius() const { return e_; }
  size_t size_in_bytes() const { return model_.size_in_bytes(); }  // excludes the key array

 private:
  const uint64_t* keys_;
  size_t n_;
  SampleEBPLA model_;
  size_t e_;
};

}  // namespace ours
