// ours/sample_eb_pla.h -- Sample EB-PLA: an error-bounded piecewise linear model
// fitted to a systematic sample, with a guaranteed max error of eps over EVERY key.
//
// Independent implementation for CS 6360 (see CLAUDE.md). PLA fitting is the
// simple GREEDY SHRINKING CONE (not the optimal PLA of the PGM-index), so expect
// somewhat more segments than an optimal-PLA implementation at the same eps.
//
// ---------------------------------------------------------------------------
// Notation
//   keys[0..n-1] sorted ascending, duplicates allowed.
//   pos(x) = lower_bound(x) = first index i with keys[i] >= x  (the "true position").
//   eps    = error bound the user asks for:  |predict(x) - pos(x)| <= eps  for every key x.
//   I      = sampling interval (1 <= I <= eps + 1).
//   delta  = eps - I + 1 >= 0  = the tightened bound the PLA fits the sample points with.
//
// Step 1: sample.  S = sample indices {0, I, 2I, ..., n-1} (sampler.h). Consecutive
//   elements of S are at most I apart.
//
// Step 2: turn samples into the points the PLA must fit ("anchor points").
//   For each DISTINCT sampled key v (in increasing order) let
//     L(v) = pos(v)                              (its true position)
//     H(v) = largest sample index whose key is v (H(v) >= L(v))
//   Emit the point (v, L(v)). If H(v) > L(v) (v's run of duplicates started
//   before the sample index, or several samples hit the run) AND there is room
//   for an unsampled key after v (v + 1 < next distinct sampled key), ALSO emit
//   (v + 1, H(v)). On duplicate-free data L(v) = H(v) = the sample index, so this
//   is exactly "fit the points (keys[j*I], j*I)".
//   Both coordinates strictly increase along the point list: x because
//   v < v+1 < next key, and y because the next key's run starts after H(v).
//   Computing L(v) touches O(log I) extra keys per sample, using a binary search
//   between the previous sample index and this one.
//
// Step 3: greedy shrinking cone with bound delta, slopes restricted to >= 0.
//   A segment starts at a point p0 and passes through it exactly. For each next
//   point p, the slopes m with |p0.y + m*(p.x - p0.x) - p.y| <= delta form an
//   interval. Intersect it with the running interval [lo, hi], where lo starts at
//   0 and hi at +inf. If the result is empty, p starts a new segment. The
//   segment's slope is the midpoint of [lo, hi]. Since p.y > p0.y, the first
//   point after p0 always fits, so the cone never forces a negative slope.
//   Result: every anchor point is predicted within delta by the segment that
//   contains it, and every slope is >= 0 (the line is non-decreasing).
//
// Step 4: prediction. Segment g covers keys [first_key_g, first_key_{g+1}). Its
//   first_key is the x of its first anchor point. predict(x) evaluates segment g's
//   line at x, rounds to the nearest integer, then CLAMPS to at most clamp_hi_g,
//   the y of the next segment's first anchor point (n-1 for the last segment).
//
// ---------------------------------------------------------------------------
// Why every key stays within eps, including the SEGMENT-BOUNDARY GAP
//
// (a) The clamp never hurts. Let P = (px, py) be the next segment's first anchor
//     point. Every key x covered by segment g has x < px, and pos(x) <= py:
//       - if P = (b, L(b)), then x < b, so pos(x) < pos(b) = py;
//       - if P = (v+1, H(v)), then x <= v, so pos(x) <= L(v) <= H(v) = py.
//     Lowering a prediction that is above py >= pos(x) down to py only moves it
//     closer to pos(x). So the clamp can never increase any error.
//
// (b) Sampled keys. If x is a sampled key, its point (x, L(x)) is fitted within
//     delta <= eps. Rounding keeps that, because pos +- delta are integers. By (a),
//     so does the clamp.
//
// (c) Unsampled keys. Let x be a key that was not sampled, with i = pos(x). Let s
//     be the largest sample index <= i, and t the next sample index (t exists
//     because n-1 is in S). keys[s] != x, otherwise s would equal i and x would
//     be sampled. So a = keys[s] < x. Similarly b = keys[t] > x. No sample index
//     lies strictly between s and t, so:
//       H(a) = s,     s + 1 <= i <= t - 1,     i < L(b) <= t <= s + I.
//     Let A be a's LAST anchor point: (a+1, s) if it was emitted, otherwise
//     (a, L(a)) with L(a) = H(a) = s. Either way A = (A.x, s) with A.x <= x. The
//     next anchor point after A is B = (b, L(b)).
//
//     Lower side. A lies in some segment g, and so does x: A.x <= x < b = B.x,
//     and no segment can start strictly between consecutive anchor points. The
//     line's slope is >= 0, so line_g(x) >= line_g(A.x) >= s - delta. The clamp
//     value is >= pos(x) = i > s (see (a)). So prediction >= s - delta, and
//         i - prediction <= (t - 1) - (s - delta) <= (I - 1) + delta = eps.
//
//     Upper side. There are two cases.
//       * B is in the same segment g as A. Then line_g(x) <= line_g(b) <= L(b) + delta.
//       * B starts segment g+1. This is the BOUNDARY GAP: x is predicted by
//         EXTRAPOLATING segment g past its last point A, and a steep line could
//         shoot far past L(b). The clamp caps the prediction at clamp_hi_g = L(b).
//     In both cases prediction <= L(b) + delta <= t + delta, so
//         prediction - i <= (t + delta) - (s + 1) <= (I - 1) + delta = eps.
//
//     Rounding to the nearest integer keeps both bounds, because i +- eps are
//     integers. Without the clamp the boundary gap is a real bug: see the
//     "dense_then_jump" test. Without the (a+1, H(a)) point, duplicate runs
//     that start before a sample break the lower side: see "dup_run_then_close_keys".
//
// Floating point: each slope is computed in double. A rounding error of ~1e-16
// relative is far below the 0.5 margin that rounding to an integer leaves, and
// the exhaustive tests check the final integer predictions.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "sampler.h"

namespace ours {

struct AnchorPoint {
  uint64_t x;  // key
  uint64_t y;  // position the model must hit within delta
};

// Step 2 above. `samples` must come from systematic_sample(keys, n, I).
inline std::vector<AnchorPoint> anchor_points(const uint64_t* keys, size_t n,
                                              const std::vector<SamplePoint>& samples) {
  (void)n;
  std::vector<AnchorPoint> pts;
  pts.reserve(samples.size());
  size_t g = 0;
  while (g < samples.size()) {
    const uint64_t v = samples[g].key;
    // L(v): the previous sample (if any) has a smaller key, so pos(v) is in
    // (samples[g-1].index, samples[g].index].
    const size_t lo = (g == 0) ? 0 : samples[g - 1].index + 1;
    const size_t L = static_cast<size_t>(
        std::lower_bound(keys + lo, keys + samples[g].index + 1, v) - keys);
    // H(v): skip over further samples that hit the same duplicate run.
    size_t h = g;
    while (h + 1 < samples.size() && samples[h + 1].key == v) ++h;
    const size_t H = samples[h].index;

    pts.push_back({v, L});
    const bool has_next = h + 1 < samples.size();
    // v < next key here, so v + 1 cannot overflow.
    if (H > L && has_next && v + 1 < samples[h + 1].key) pts.push_back({v + 1, H});
    g = h + 1;
  }
  return pts;
}

class SampleEBPLA {
 public:
  struct Segment {
    uint64_t first_key;  // x of the first anchor point; segment covers [first_key, next first_key)
    uint64_t y0;         // y of the first anchor point (the line passes through it)
    double slope;        // >= 0
    uint64_t clamp_hi;   // y of the next segment's first anchor point (n-1 for the last)
  };

  // kTightened (the default and the real algorithm): delta = eps - I + 1, which
  //   guarantees max error <= eps over every key.
  // kUntightened (an experiment only): delta = eps, so the samples are fitted with
  //   the full eps. The same proof (with delta = eps) then gives only
  //   max error <= eps + I - 1. Use it to compare against indexes that do not tighten.
  enum class Bound { kTightened, kUntightened };

  // Builds the model. keys[0..n-1] sorted ascending; the array is NOT copied.
  // Throws std::invalid_argument unless n >= 1, I >= 1, and (if tightened) I <= eps + 1.
  SampleEBPLA(const uint64_t* keys, size_t n, size_t eps, size_t I,
              Bound bound = Bound::kTightened)
      : n_(n), eps_(eps), I_(I) {
    if (n == 0) throw std::invalid_argument("SampleEBPLA: empty key array");
    if (I == 0) throw std::invalid_argument("SampleEBPLA: I must be >= 1");
    if (bound == Bound::kTightened && I > eps + 1)
      throw std::invalid_argument("SampleEBPLA: need I <= eps + 1 (got I=" +
                                  std::to_string(I) + ", eps=" + std::to_string(eps) + ")");
    delta_ = bound == Bound::kTightened ? eps - I + 1 : eps;

    const std::vector<SamplePoint> samples = systematic_sample(keys, n, I);
    const std::vector<AnchorPoint> pts = anchor_points(keys, n, samples);
    num_samples_ = samples.size();
    num_points_ = pts.size();
    fit_shrinking_cone(pts);

    first_keys_.reserve(segments_.size());
    for (size_t g = 0; g < segments_.size(); ++g) {
      first_keys_.push_back(segments_[g].first_key);
      segments_[g].clamp_hi = (g + 1 < segments_.size()) ? segments_[g + 1].y0 : n - 1;
    }
  }

  // Predicted position of key x (meaningful for keys in the array; any x is safe).
  uint64_t predict(uint64_t x) const {
    // Segment g = last segment with first_key <= x. Keys below the first key go to
    // segment 0 (its y0 is 0). Binary search over first keys for now; the
    // two-level index (G2) will replace this.
    size_t g = static_cast<size_t>(
        std::upper_bound(first_keys_.begin(), first_keys_.end(), x) - first_keys_.begin());
    if (g == 0) return segments_[0].y0;
    const Segment& s = segments_[g - 1];
    const double p = static_cast<double>(s.y0) +
                     s.slope * static_cast<double>(x - s.first_key);
    const double cap = static_cast<double>(s.clamp_hi);
    if (!(p < cap)) return s.clamp_hi;  // also catches inf
    const uint64_t r = static_cast<uint64_t>(p + 0.5);  // round to nearest (p >= 0)
    return std::min(r, s.clamp_hi);
  }

  size_t num_segments() const { return segments_.size(); }
  size_t num_samples() const { return num_samples_; }
  size_t num_points() const { return num_points_; }
  size_t eps() const { return eps_; }
  size_t interval() const { return I_; }
  size_t delta() const { return delta_; }
  size_t n() const { return n_; }
  const std::vector<Segment>& segments() const { return segments_; }
  size_t size_in_bytes() const {
    return segments_.size() * sizeof(Segment) + first_keys_.size() * sizeof(uint64_t);
  }

 private:
  // Step 3 above.
  void fit_shrinking_cone(const std::vector<AnchorPoint>& pts) {
    const double d = static_cast<double>(delta_);
    size_t i = 0;
    while (i < pts.size()) {
      const AnchorPoint p0 = pts[i];
      double lo = 0.0;                                     // slope >= 0
      double hi = std::numeric_limits<double>::infinity();
      size_t j = i + 1;
      for (; j < pts.size(); ++j) {
        const double dx = static_cast<double>(pts[j].x - p0.x);  // > 0
        const double dy = static_cast<double>(pts[j].y - p0.y);  // > 0
        const double nlo = std::max(lo, (dy - d) / dx);
        const double nhi = std::min(hi, (dy + d) / dx);
        if (nlo > nhi) break;  // p_j cannot join: it starts the next segment
        lo = nlo;
        hi = nhi;
      }
      const double slope = std::isinf(hi) ? 0.0 : 0.5 * (lo + hi);  // 1-point segment: flat
      segments_.push_back({p0.x, p0.y, slope, 0});
      i = j;
    }
  }

  size_t n_, eps_, I_, delta_ = 0;
  size_t num_samples_ = 0, num_points_ = 0;
  std::vector<Segment> segments_;
  std::vector<uint64_t> first_keys_;  // segments_[g].first_key, contiguous for search
};

}  // namespace ours
