// ours/segment_directory.h -- LEVEL 2 of the two-level index.
//
// Level 1 is the list of PLA segments (sample_eb_pla.h). Level 2 maps a key x to the
// segment that covers it: the last segment whose first_key <= x. Today it is a binary
// search over the contiguous array of segment first keys (8 B per segment, so it
// stays small: ~85 KB for 10.6k segments on history at eps=256, I=1). The planned
// upgrade is a recursive PLA over these first keys, as in the PGM-index. Only this
// file would change.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ours {

class SegmentDirectory {
 public:
  SegmentDirectory() = default;
  explicit SegmentDirectory(std::vector<uint64_t> first_keys) : first_keys_(std::move(first_keys)) {}

  // Index of the last segment with first_key <= x. Keys below first_keys_[0]
  // (smaller than every key in the array) map to segment 0.
  size_t find(uint64_t x) const {
    const size_t g = static_cast<size_t>(
        std::upper_bound(first_keys_.begin(), first_keys_.end(), x) - first_keys_.begin());
    return g == 0 ? 0 : g - 1;
  }

  size_t size() const { return first_keys_.size(); }
  size_t size_in_bytes() const { return first_keys_.size() * sizeof(uint64_t); }

 private:
  std::vector<uint64_t> first_keys_;  // strictly increasing
};

}  // namespace ours
