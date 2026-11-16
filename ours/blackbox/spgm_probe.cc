// Black-box probe of the AUTHORS' sPGM (BASIL wrapper), used without reading its code.
//
// We only use the wrapper's public interface (the harness contract in NOTES.md §4):
//   uint64_t Build(std::vector<KeyValue<K>>&, std::vector<EqualLookup<K>>&)  -> build ns
//   SearchBound EqualityLookup(K) const   -> [start, stop), stop exclusive
//   size(), variant(), name()
// Template parameters as documented in README.md §3.4:
//   sPGM<K, eps_bottom, I_bottom, eps_upper, I_upper>
//
// Keys are read ONCE, directly into the KeyValue vector the wrapper's Build needs
// ({key, position}, as the harness does). No separate uint64 key array is kept.
//
// For EVERY key, with pos = lower_bound position:
//   contained  : start <= pos < stop (otherwise a harness lookup could fail)
//   width      : stop - start
//   center err : |pos - (start + stop - 1)/2|, only for bounds not clipped at 0 or n.
//                If the bound is symmetric around the index's prediction p (the
//                usual [p - e, p + e]), this is exactly |p - pos|, the model error.
//   edge dist  : max(pos - start, stop - 1 - pos). This is the bound the search
//                actually relies on.
//
// Usage: spgm_probe <sosd_file> <num_keys|all> <csv_out>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "util.h"
#include "wrappers/wrapper_spgm_index.h"

using K = uint64_t;

static std::vector<KeyValue<K>> load_kv(const std::string& path, size_t max_keys) {
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) { std::perror(path.c_str()); std::exit(1); }
  uint64_t count = 0;
  if (std::fread(&count, 8, 1, f) != 1) { std::fprintf(stderr, "bad header\n"); std::exit(1); }
  const size_t n = std::min<uint64_t>(count, max_keys);
  std::vector<KeyValue<K>> kv(n);
  std::vector<K> buf(1 << 20);
  for (size_t done = 0; done < n;) {
    const size_t want = std::min(buf.size(), n - done);
    if (std::fread(buf.data(), 8, want, f) != want) { std::fprintf(stderr, "short read\n"); std::exit(1); }
    for (size_t j = 0; j < want; ++j) kv[done + j] = {buf[j], done + j};
    done += want;
  }
  std::fclose(f);
  for (size_t i = 1; i < n; ++i)
    if (kv[i].key < kv[i - 1].key) { std::fprintf(stderr, "not sorted\n"); std::exit(1); }
  return kv;
}

template <size_t EPS, size_t I>
static void probe(std::vector<KeyValue<K>>& kv, std::FILE* out) {
  const size_t n = kv.size();
  std::vector<EqualLookup<K>> no_lookups;
  uint64_t ns[3];
  for (int r = 0; r < 3; ++r) {
    sPGM<K, EPS, I, 4, 1> idx;
    ns[r] = idx.Build(kv, no_lookups);
    if (r > 0) {
      std::fprintf(out, "%zu,%zu,%zu,%d,%llu,%zu,%s,,,,,,\n", n, EPS, I, r + 1,
                   (unsigned long long)ns[r], idx.size(), idx.variant().c_str());
      continue;
    }
    size_t not_contained = 0, min_w = SIZE_MAX, max_w = 0, max_center = 0, max_edge = 0;
    size_t first_bad = SIZE_MAX, pos = 0;
    for (size_t i = 0; i < n; ++i) {
      if (i == 0 || kv[i].key != kv[i - 1].key) pos = i;
      const SearchBound b = idx.EqualityLookup(kv[i].key);
      if (!(b.start <= pos && pos < b.stop)) {
        if (first_bad == SIZE_MAX) first_bad = i;
        ++not_contained;
      }
      const size_t w = b.stop - b.start;
      min_w = std::min(min_w, w);
      max_w = std::max(max_w, w);
      if (b.stop > pos) max_edge = std::max(max_edge, b.stop - 1 - pos);
      if (pos >= b.start) max_edge = std::max(max_edge, pos - b.start);
      if (b.start > 0 && b.stop < n) {  // unclipped: center = prediction if symmetric
        const long long twice_c = (long long)b.start + (long long)b.stop - 1;
        const long long d = 2 * (long long)pos - twice_c;
        max_center = std::max<size_t>(max_center, (size_t)((d < 0 ? -d : d) / 2));
      }
    }
    std::printf("sPGM variant=%s name=%s n=%zu eps=%zu I=%zu build_ms=%.1f size=%zu "
                "width=[%zu,%zu] not_contained=%zu max_center_err=%zu max_edge_dist=%zu\n",
                idx.variant().c_str(), idx.name().c_str(), n, EPS, I, ns[0] / 1e6, idx.size(),
                min_w, max_w, not_contained, max_center, max_edge);
    if (not_contained)
      std::printf("  first uncontained key at i=%zu key=%llu\n", first_bad,
                  (unsigned long long)kv[first_bad].key);
    std::fprintf(out, "%zu,%zu,%zu,1,%llu,%zu,%s,%zu,%zu,%zu,%zu,%zu,\n", n, EPS, I,
                 (unsigned long long)ns[0], idx.size(), idx.variant().c_str(), min_w, max_w,
                 not_contained, max_center, max_edge);
    std::fflush(out);
  }
  std::printf("  build_ms runs: %.1f %.1f %.1f\n", ns[0] / 1e6, ns[1] / 1e6, ns[2] / 1e6);
}

int main(int argc, char** argv) {
  if (argc < 4) { std::fprintf(stderr, "usage: %s <sosd_file> <num_keys|all> <csv_out>\n", argv[0]); return 1; }
  const std::string nk = argv[2];
  auto kv = load_kv(argv[1], nk == "all" ? SIZE_MAX : std::strtoull(argv[2], nullptr, 10));
  std::printf("loaded %zu keys into KeyValue vector (%.0f MB)\n", kv.size(), kv.size() * 16.0 / 1e6);
  std::FILE* out = std::fopen(argv[3], "a");
  std::fseek(out, 0, SEEK_END);
  if (std::ftell(out) == 0)
    std::fprintf(out, "n,eps,I,run,build_ns,size_bytes,variant,min_width,max_width,not_contained,max_center_err,max_edge_dist,\n");
  probe<256, 1>(kv, out);
  probe<256, 16>(kv, out);
  probe<256, 64>(kv, out);
  probe<256, 256>(kv, out);
  std::fclose(out);
  return 0;
}
