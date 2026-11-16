// Unit tests for ours/sampler.h, ours/sample_eb_pla.h, ours/error.h.
//
// The key property is checked exhaustively: for EVERY key,
// |predict(key) - lower_bound(key)| <= eps.
//
// Usage: test_sample_eb_pla [path/to/history_200M_uint64 [num_keys]]
//   With a path, it also runs on the first num_keys keys of that file (default 10M),
//   loaded once with ours::load_sosd_keys.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
#include <random>
#include <string>
#include <vector>

#include "../error.h"
#include "../sample_eb_pla.h"
#include "../sampler.h"
#include "../sosd_io.h"

using namespace ours;
using Keys = std::vector<uint64_t>;

// ---------------------------------------------------------------- mini framework
static int g_checks = 0, g_failures = 0;
#define CHECK(cond, ...)                                              \
  do {                                                                \
    ++g_checks;                                                       \
    if (!(cond)) {                                                    \
      ++g_failures;                                                   \
      std::printf("  FAIL %s:%d: %s | ", __FILE__, __LINE__, #cond);  \
      std::printf(__VA_ARGS__);                                       \
      std::printf("\n");                                              \
    }                                                                 \
  } while (0)

static void run_test(const char* name, const std::function<void()>& fn) {
  const int before = g_failures;
  fn();
  std::printf("[%s] %s\n", g_failures == before ? " OK " : "FAIL", name);
}

// ---------------------------------------------------------------- helpers
static std::vector<std::pair<size_t, size_t>> configs(size_t eps) {
  // (eps, I) pairs covering I = 1, small I, I = eps, and the extreme I = eps + 1.
  std::vector<size_t> Is = {1, 2, eps / 2 + 1, eps, eps + 1};
  std::sort(Is.begin(), Is.end());
  Is.erase(std::unique(Is.begin(), Is.end()), Is.end());
  std::vector<std::pair<size_t, size_t>> out;
  for (size_t I : Is)
    if (I >= 1 && I <= eps + 1) out.push_back({eps, I});
  return out;
}

// Builds the model and checks every invariant plus the exhaustive error bound.
static void check_model(const Keys& keys, size_t eps, size_t I, const char* what,
                        bool verbose = false) {
  SampleEBPLA m(keys.data(), keys.size(), eps, I);
  const ErrorStats st = max_error(m, keys.data(), keys.size());
  CHECK(st.max_error <= eps, "%s n=%zu eps=%zu I=%zu: max_error=%llu at i=%zu key=%llu",
        what, keys.size(), eps, I, (unsigned long long)st.max_error, st.argmax,
        (unsigned long long)keys[st.argmax]);
  CHECK(m.delta() == eps - I + 1, "%s delta", what);
  const auto& segs = m.segments();
  CHECK(!segs.empty(), "%s no segments", what);
  CHECK(segs[0].first_key == keys[0] && segs[0].y0 == 0, "%s first segment", what);
  for (size_t g = 0; g < segs.size(); ++g) {
    CHECK(segs[g].slope >= 0.0, "%s seg %zu slope %g < 0", what, g, segs[g].slope);
    if (g + 1 < segs.size())
      CHECK(segs[g].first_key < segs[g + 1].first_key && segs[g].y0 < segs[g + 1].y0,
            "%s seg %zu not increasing", what, g);
  }
  // Predictions must be monotone in the key (needed for the two-level index later).
  uint64_t prev = 0;
  for (size_t i = 0; i < keys.size(); ++i) {
    const uint64_t p = m.predict(keys[i]);
    if (p < prev) {
      CHECK(false, "%s prediction not monotone at i=%zu", what, i);
      break;
    }
    prev = p;
  }
  if (verbose)
    std::printf("    %-22s n=%-9zu eps=%-4zu I=%-4zu segs=%-7zu max_err=%-4llu mean_err=%.2f\n",
                what, keys.size(), eps, I, m.num_segments(),
                (unsigned long long)st.max_error, st.mean_error);
}

static void check_all_configs(const Keys& keys, const char* what,
                              std::vector<size_t> eps_list = {0, 1, 4, 16, 64, 256},
                              bool verbose = false) {
  for (size_t eps : eps_list)
    for (auto [e, I] : configs(eps)) check_model(keys, e, I, what, verbose);
}

static Keys sorted(Keys k) {
  std::sort(k.begin(), k.end());
  return k;
}

// ---------------------------------------------------------------- data generators
static Keys uniform_keys(size_t n, uint64_t seed) {
  std::mt19937_64 rng(seed);
  Keys k(n);
  for (auto& x : k) x = rng();
  return sorted(k);
}
static Keys skewed_keys(size_t n, uint64_t seed) {  // lognormal: dense head, long tail
  std::mt19937_64 rng(seed);
  std::lognormal_distribution<double> d(0.0, 2.0);
  Keys k(n);
  for (auto& x : k) x = static_cast<uint64_t>(d(rng) * 1e9);
  return sorted(k);
}
static Keys heavy_dup_keys(size_t n, uint64_t seed, uint64_t distinct) {
  std::mt19937_64 rng(seed);
  // Zipf-ish run lengths: few values, some with very long runs.
  std::uniform_real_distribution<double> u(0.0, 1.0);
  Keys k(n);
  for (auto& x : k) x = static_cast<uint64_t>(std::pow(u(rng), 3.0) * distinct) * 1000;
  return sorted(k);
}
// Random mixture of dense integer stretches, big jumps, and duplicate runs:
// stresses segment boundaries.
static Keys mixed_keys(size_t n, uint64_t seed) {
  std::mt19937_64 rng(seed);
  Keys k;
  k.reserve(n);
  uint64_t cur = rng() % 1000;
  while (k.size() < n) {
    const int kind = rng() % 4;
    const size_t len = 1 + rng() % 300;
    for (size_t j = 0; j < len && k.size() < n; ++j) {
      if (kind == 0) cur += 1;                          // dense
      else if (kind == 1) cur += 1 + rng() % 1000000;   // sparse
      else if (kind == 2) { /* duplicate run */ }
      else cur += 1 + rng() % 3;
      k.push_back(cur);
    }
    if (rng() % 5 == 0) cur += rng() % (1ull << 40);  // big jump
  }
  return k;
}

// ---------------------------------------------------------------- tests
static void test_sampler() {
  const Keys k = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100};  // n = 10
  auto idx = [&](size_t I) {
    std::vector<size_t> v;
    for (auto& s : systematic_sample(k.data(), k.size(), I)) {
      CHECK(s.key == k[s.index], "key/index mismatch");
      v.push_back(s.index);
    }
    return v;
  };
  CHECK((idx(1) == std::vector<size_t>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9}), "I=1");
  CHECK((idx(3) == std::vector<size_t>{0, 3, 6, 9}), "I=3 (n-1 is a multiple)");
  CHECK((idx(4) == std::vector<size_t>{0, 4, 8, 9}), "I=4 (n not divisible): last key added");
  CHECK((idx(9) == std::vector<size_t>{0, 9}), "I=9");
  CHECK((idx(100) == std::vector<size_t>{0, 9}), "I > n");
  CHECK((idx(SIZE_MAX) == std::vector<size_t>{0, 9}), "huge I must not overflow");
  const Keys one = {5};
  CHECK(systematic_sample(one.data(), 1, 7).size() == 1, "n=1");
  CHECK(systematic_sample(one.data(), 0, 7).empty(), "n=0");
  bool threw = false;
  try { systematic_sample(k.data(), k.size(), 0); } catch (const std::invalid_argument&) { threw = true; }
  CHECK(threw, "I=0 must throw");
  // Consecutive sample indices are at most I apart (needed by the proof).
  for (size_t n : {1, 2, 7, 100, 1003})
    for (size_t I : {1, 2, 3, 10, 64}) {
      Keys kk(n);
      for (size_t i = 0; i < n; ++i) kk[i] = i;
      auto s = systematic_sample(kk.data(), n, I);
      CHECK(s.front().index == 0 && s.back().index == n - 1, "ends n=%zu I=%zu", n, I);
      for (size_t j = 1; j < s.size(); ++j)
        CHECK(s[j].index > s[j - 1].index && s[j].index - s[j - 1].index <= I,
              "gap n=%zu I=%zu", n, I);
    }
}

static void test_rejects_bad_params() {
  const Keys k = {1, 2, 3, 4, 5};
  auto throws = [&](size_t n, size_t eps, size_t I) {
    try { SampleEBPLA m(k.data(), n, eps, I); } catch (const std::invalid_argument&) { return true; }
    return false;
  };
  CHECK(throws(5, 16, 18), "I = eps + 2 must be rejected");
  CHECK(throws(5, 0, 2), "eps=0, I=2 must be rejected");
  CHECK(throws(5, 16, 0), "I = 0 must be rejected");
  CHECK(throws(0, 16, 1), "n = 0 must be rejected");
  CHECK(!throws(5, 16, 17), "I = eps + 1 is allowed");
  CHECK(!throws(5, 0, 1), "eps = 0, I = 1 is allowed");
}

// The error function itself, against a brute-force std::lower_bound reference.
static void test_max_error_function() {
  struct Fake {  // predicts a fixed table
    const Keys* keys;
    std::vector<uint64_t> table;
    uint64_t predict(uint64_t x) const {
      return table[std::lower_bound(keys->begin(), keys->end(), x) - keys->begin()];
    }
  };
  std::mt19937_64 rng(7);
  for (int trial = 0; trial < 200; ++trial) {
    const size_t n = 1 + rng() % 50;
    Keys k(n);
    for (auto& x : k) x = rng() % 20;  // many duplicates
    k = sorted(k);
    Fake f{&k, std::vector<uint64_t>(n)};
    for (auto& t : f.table) t = rng() % (n + 5);
    uint64_t ref = 0;
    for (size_t i = 0; i < n; ++i) {
      const uint64_t pos = std::lower_bound(k.begin(), k.end(), k[i]) - k.begin();
      const uint64_t p = f.predict(k[i]);
      ref = std::max<uint64_t>(ref, p > pos ? p - pos : pos - p);
    }
    CHECK(max_error(f, k.data(), n).max_error == ref, "trial %d", trial);
  }
}

static void test_tiny_arrays() {
  // Every n in 1..12, several key shapes, every valid I for small eps.
  std::mt19937_64 rng(1);
  for (size_t n = 1; n <= 12; ++n)
    for (int shape = 0; shape < 4; ++shape) {
      Keys k(n);
      for (size_t i = 0; i < n; ++i)
        k[i] = shape == 0 ? i : shape == 1 ? rng() % 5 : shape == 2 ? rng() : 42;
      k = sorted(k);
      for (size_t eps : {0, 1, 2, 3, 8})
        for (size_t I = 1; I <= eps + 1; ++I) check_model(k, eps, I, "tiny");
    }
}

static void test_uniform() { check_all_configs(uniform_keys(1000000, 11), "uniform", {0, 1, 4, 16, 64, 256}, true); }
static void test_skewed() { check_all_configs(skewed_keys(1000000, 12), "skewed(lognormal)", {0, 1, 4, 16, 64, 256}, true); }

static void test_heavy_duplicates() {
  check_all_configs(heavy_dup_keys(500000, 13, 100), "dups(100 distinct)", {0, 1, 4, 16, 64, 256}, true);
  check_all_configs(heavy_dup_keys(500000, 14, 5000), "dups(5000 distinct)");
  Keys same(10007, 123456789);  // a single value
  check_all_configs(same, "all equal");
  // Runs of length exactly around I, so runs straddle sample indices.
  for (size_t run : {15, 16, 17, 63, 64, 65, 255, 256, 257}) {
    Keys k;
    for (uint64_t v = 0; k.size() < 50000; ++v)
      for (size_t r = 0; r < run; ++r) k.push_back(v * 10);
    check_all_configs(k, "runs≈I", {16, 64, 256});
  }
}

static void test_n_not_divisible() {
  for (size_t n : {1001, 1023, 1025, 9999, 100003}) {
    check_all_configs(uniform_keys(n, n), "n%I!=0 uniform", {16, 64, 256});
    check_all_configs(heavy_dup_keys(n, n, 37), "n%I!=0 dups", {16, 64, 256});
  }
}

static void test_extreme_I() {
  // I = 1 (no sampling, delta = eps) and I = eps + 1 (delta = 0: exact fit of samples).
  for (size_t eps : {0, 1, 7, 64, 256}) {
    for (size_t I : {size_t(1), eps + 1}) {
      check_model(uniform_keys(200000, 21), eps, I, "I extreme uniform", true);
      check_model(heavy_dup_keys(200000, 22, 300), eps, I, "I extreme dups");
      check_model(mixed_keys(200000, 23), eps, I, "I extreme mixed");
    }
  }
}

// Adversarial case 1 (the boundary gap): a dense block makes a steep segment, then
// a huge jump. The keys right after the jump are predicted by extrapolating the
// steep segment; without the clamp they would get predictions near 1e12.
static void test_dense_then_jump() {
  for (size_t eps : {4, 16, 64, 256})
    for (auto [e, I] : configs(eps)) {
      Keys k;
      for (uint64_t i = 0; i < 5000; ++i) k.push_back(i);                     // slope 1
      for (uint64_t i = 0; i < 5000; ++i) k.push_back(1000000000000ull + i * 7);
      for (uint64_t i = 0; i < 5000; ++i) k.push_back(2000000000000ull + i);   // steep again
      for (uint64_t i = 0; i < 5000; ++i) k.push_back(9000000000000000000ull + i * 1000003);
      check_model(k, e, I, "dense_then_jump");
    }
}

// Adversarial case 2 (duplicates): a long run of one key, sampled several times,
// followed by keys only slightly larger. Without the (v+1, H(v)) anchor point, the
// line from (v, pos(v)) would predict ~pos(v) for them, which is far below their
// true positions (just after the run).
static void test_dup_run_then_close_keys() {
  for (size_t eps : {1, 4, 16, 64, 256})
    for (auto [e, I] : configs(eps)) {
      Keys k;
      for (int i = 0; i < 3; ++i) k.push_back(i);
      for (int i = 0; i < 2000; ++i) k.push_back(100);        // long run
      for (uint64_t i = 0; i < 2000; ++i) k.push_back(101 + i);  // adjacent keys
      for (uint64_t i = 0; i < 2000; ++i) k.push_back(1000000000ull + i * 999983);
      check_model(k, e, I, "dup_run_then_close");
    }
}

static void test_mixed_fuzz() {
  for (uint64_t seed = 100; seed < 140; ++seed) {
    const size_t n = 1000 + (seed * 7919) % 60000;
    check_all_configs(mixed_keys(n, seed), "mixed fuzz", {0, 3, 16, 64, 256});
  }
}

static void test_key_extremes() {
  const uint64_t M = std::numeric_limits<uint64_t>::max();
  Keys k = {0, 0, 1, 2, M - 3, M - 2, M - 1, M - 1, M, M, M};
  check_all_configs(k, "near UINT64_MAX", {0, 1, 2, 4, 16});
  Keys big;
  for (int i = 0; i < 1000; ++i) big.push_back(M - 1000 + i);
  for (int i = 0; i < 50; ++i) big.push_back(M);
  check_all_configs(big, "dense at UINT64_MAX", {0, 4, 64});
}

static void test_history_slice(const std::string& path, size_t num_keys) {
  const Keys k = load_sosd_keys(path, num_keys);
  std::printf("    loaded %zu keys from %s (one copy, %.0f MB)\n", k.size(), path.c_str(),
              k.size() * 8.0 / 1e6);
  for (size_t I : {1, 16, 64, 256}) check_model(k, 256, I, "history slice", true);
  check_model(k, 16, 1, "history slice", true);
  check_model(k, 16, 17, "history slice", true);
}

int main(int argc, char** argv) {
  run_test("sampler: indices, last key, n%I!=0, I>n, I=0", test_sampler);
  run_test("EB-PLA rejects I > eps+1, I=0, n=0", test_rejects_bad_params);
  run_test("max_error matches brute-force lower_bound", test_max_error_function);
  run_test("tiny arrays n=1..12, all valid I", test_tiny_arrays);
  run_test("uniform 1M", test_uniform);
  run_test("skewed (lognormal) 1M", test_skewed);
  run_test("heavy duplicates", test_heavy_duplicates);
  run_test("n not divisible by I", test_n_not_divisible);
  run_test("I = 1 and I = eps + 1", test_extreme_I);
  run_test("boundary gap: dense_then_jump", test_dense_then_jump);
  run_test("duplicates: dup_run_then_close_keys", test_dup_run_then_close_keys);
  run_test("mixed fuzz (40 seeds)", test_mixed_fuzz);
  run_test("keys near UINT64_MAX", test_key_extremes);
  if (argc >= 2) {
    const size_t num = argc >= 3 ? std::strtoull(argv[2], nullptr, 10) : 10000000;
    run_test("history slice", [&] { test_history_slice(argv[1], num); });
  }
  std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
