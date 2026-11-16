// ours/error.h -- exhaustive error measurement over EVERY key of a sorted array.
//
// For each index i, the true position of keys[i] is pos = lower_bound(keys[i]),
// i.e. the first index of keys[i]'s duplicate run. Because the array is sorted,
// that is computed in one linear pass (no per-key binary search):
//     pos = i if i == 0 or keys[i] != keys[i-1], else the previous pos.
// The model is any type with `uint64_t predict(uint64_t key) const`.
#pragma once

#include <cstddef>
#include <cstdint>

namespace ours {

struct ErrorStats {
  uint64_t max_error = 0;   // max |predict(key) - pos(key)| over all n keys
  size_t argmax = 0;        // an index i attaining max_error
  double mean_error = 0.0;  // mean over all n keys (duplicates counted each time)
};

template <class Model>
ErrorStats max_error(const Model& model, const uint64_t* keys, size_t n) {
  ErrorStats st;
  if (n == 0) return st;
  size_t pos = 0;
  long double sum = 0;
  for (size_t i = 0; i < n; ++i) {
    if (i == 0 || keys[i] != keys[i - 1]) pos = i;
    const uint64_t p = model.predict(keys[i]);
    const uint64_t err = p > pos ? p - pos : pos - p;
    sum += err;
    if (err > st.max_error) {
      st.max_error = err;
      st.argmax = i;
    }
  }
  st.mean_error = static_cast<double>(sum / n);
  return st;
}

}  // namespace ours
