// Lookup correctness + latency for the two-level Sample EB-PLA index (tightened).
//
// Usage: lookup_bench <sosd_file> <num_keys|all> <eps> <csv_out> <num_queries>
//                     <check: all|random> <I> [I ...]
//
// Loads ONE copy of the keys (ours::load_sosd_keys). Then:
//   correctness: check=all    -> lookup(keys[i]) == lower_bound position, for EVERY i
//                check=random -> the same, for num_queries random existing keys
//                The reference is std::lower_bound over the whole array, computed
//                independently of our index.
//   latency:     num_queries random existing keys (uniform over positions, fixed seed,
//                same set for every config), 3 timed runs per config. Average ns per
//                lookup = wall time / num_queries. The returned positions are summed
//                and compared with the reference sum, so the loop cannot be optimized
//                away and every timed answer is also verified.
//   reference:   the same timed loop with plain std::lower_bound over all n keys.
// Exits with status 2 on the first wrong answer.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

#include "../lookup.h"
#include "../sosd_io.h"

using namespace ours;
using Clock = std::chrono::steady_clock;

int main(int argc, char** argv) {
  if (argc < 8) {
    std::fprintf(stderr, "usage: %s <sosd_file> <num_keys|all> <eps> <csv_out> <num_queries> <all|random> <I> [I ...]\n", argv[0]);
    return 1;
  }
  const std::string nk = argv[2];
  const size_t eps = std::strtoull(argv[3], nullptr, 10);
  const std::string csv = argv[4];
  const size_t nq = std::strtoull(argv[5], nullptr, 10);
  const std::string check = argv[6];
  if (check != "all" && check != "random") { std::fprintf(stderr, "check must be all|random\n"); return 1; }

  const std::vector<uint64_t> keys =
      load_sosd_keys(argv[1], nk == "all" ? SIZE_MAX : std::strtoull(nk.c_str(), nullptr, 10));
  const size_t n = keys.size();
  const uint64_t* K = keys.data();
  std::printf("loaded %zu keys (one copy, %.0f MB)\n", n, n * 8.0 / 1e6);

  // Random existing keys + independent reference answers.
  std::vector<uint64_t> q(nq);
  std::vector<uint64_t> ref(nq);
  std::mt19937_64 rng(20261115);
  uint64_t ref_sum = 0;
  for (size_t j = 0; j < nq; ++j) {
    q[j] = K[rng() % n];
    ref[j] = static_cast<uint64_t>(std::lower_bound(K, K + n, q[j]) - K);
    ref_sum += ref[j];
  }

  std::FILE* out = std::fopen(csv.c_str(), "a");
  if (!out) { std::perror(csv.c_str()); return 1; }
  std::fseek(out, 0, SEEK_END);
  if (std::ftell(out) == 0)
    std::fprintf(out, "n,index,eps,I,search_radius,segments,size_bytes,check,checked,wrong,run,num_queries,avg_lookup_ns,checksum_ok\n");

  auto timed = [&](auto&& lookup_fn, double* ns_out, bool* ok_out) {
    for (int r = 0; r < 3; ++r) {
      uint64_t sum = 0;
      const auto t0 = Clock::now();
      for (size_t j = 0; j < nq; ++j) sum += lookup_fn(q[j]);
      const auto t1 = Clock::now();
      ns_out[r] = std::chrono::duration<double, std::nano>(t1 - t0).count() / nq;
      ok_out[r] = sum == ref_sum;
    }
  };

  // Reference: plain binary search over the whole array.
  {
    double ns[3]; bool ok[3];
    timed([&](uint64_t x) { return (uint64_t)(std::lower_bound(K, K + n, x) - K); }, ns, ok);
    for (int r = 0; r < 3; ++r)
      std::fprintf(out, "%zu,binary_search,,,,,0,,,,%d,%zu,%.2f,%s\n", n, r + 1, nq, ns[r], ok[r] ? "ok" : "FAIL");
    std::printf("binary_search                      avg_ns=[%.1f %.1f %.1f]\n", ns[0], ns[1], ns[2]);
  }

  int status = 0;
  for (int a = 7; a < argc && status == 0; ++a) {
    const size_t I = std::strtoull(argv[a], nullptr, 10);
    SampleEBPLAIndex idx(K, n, eps, I);  // tightened: guaranteed error = eps

    // Correctness.
    size_t checked = 0, wrong = 0, first_bad = SIZE_MAX;
    if (check == "all") {
      size_t pos = 0;
      for (size_t i = 0; i < n; ++i) {
        if (i == 0 || K[i] != K[i - 1]) pos = i;
        if (idx.lookup(K[i]) != pos && wrong++ == 0) first_bad = i;
      }
      checked = n;
      // Cross-check the incremental `pos` reference: on a stride, compare lookups
      // directly with std::lower_bound over the whole array.
      for (size_t i = 0; i < n; i += 9973)
        if (idx.lookup(K[i]) != (size_t)(std::lower_bound(K, K + n, K[i]) - K) && wrong++ == 0)
          first_bad = i;
    } else {
      for (size_t j = 0; j < nq; ++j)
        if (idx.lookup(q[j]) != ref[j] && wrong++ == 0) first_bad = j;
      checked = nq;
    }

    double ns[3]; bool ok[3];
    timed([&](uint64_t x) { return (uint64_t)idx.lookup(x); }, ns, ok);
    const bool all_ok = wrong == 0 && ok[0] && ok[1] && ok[2];
    for (int r = 0; r < 3; ++r)
      std::fprintf(out, "%zu,ours_tightened,%zu,%zu,%zu,%zu,%zu,%s,%zu,%zu,%d,%zu,%.2f,%s\n", n, eps, I,
                   idx.search_radius(), idx.model().num_segments(), idx.size_in_bytes(),
                   check.c_str(), checked, wrong, r + 1, nq, ns[r], ok[r] ? "ok" : "FAIL");
    std::fflush(out);
    std::printf("ours eps=%zu I=%-4zu segs=%-7zu bytes=%-9zu check=%s checked=%zu wrong=%zu avg_ns=[%.1f %.1f %.1f] %s\n",
                eps, I, idx.model().num_segments(), idx.size_in_bytes(), check.c_str(), checked, wrong,
                ns[0], ns[1], ns[2], all_ok ? "ok" : "FAIL");
    if (!all_ok) {
      if (first_bad != SIZE_MAX) std::printf("FAILURE: first wrong answer at %s index %zu\n", check == "all" ? "key" : "query", first_bad);
      else std::printf("FAILURE: timed checksum mismatch\n");
      status = 2;
    }
  }
  std::fclose(out);
  return status;
}
