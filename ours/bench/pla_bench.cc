// Sample EB-PLA build benchmark + exhaustive validation on one dataset.
//
// Usage: pla_bench <sosd_file> <num_keys|all> <eps> <csv_out> <I> [I ...]
//
// Loads ONE copy of the keys with ours::load_sosd_keys (never the harness loader).
// For each I: builds the model 3 times (timing only the build: sampling + anchor
// points + cone fit), checks that all 3 builds produce identical segments, then
// computes the max error over EVERY key once. Appends one CSV row per run.
// Exits with status 2 as soon as any max error > eps (so a failure stops the sweep).
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "../error.h"
#include "../sample_eb_pla.h"
#include "../sosd_io.h"

using namespace ours;

int main(int argc, char** argv) {
  if (argc < 6) {
    std::fprintf(stderr, "usage: %s <sosd_file> <num_keys|all> <eps> <csv_out> <I> [I ...]\n", argv[0]);
    return 1;
  }
  const std::string path = argv[1];
  const std::string nk = argv[2];
  const size_t max_keys = nk == "all" ? SIZE_MAX : std::strtoull(nk.c_str(), nullptr, 10);
  const size_t eps = std::strtoull(argv[3], nullptr, 10);
  const std::string csv = argv[4];
  constexpr int kRuns = 3;

  const auto t0 = std::chrono::steady_clock::now();
  const std::vector<uint64_t> keys = load_sosd_keys(path, max_keys);
  const double load_s =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  const size_t n = keys.size();
  std::printf("loaded %zu keys in %.1f s (one copy, %.0f MB)\n", n, load_s, n * 8.0 / 1e6);

  std::FILE* out = std::fopen(csv.c_str(), "a");
  if (!out) { std::perror(csv.c_str()); return 1; }
  std::fseek(out, 0, SEEK_END);
  if (std::ftell(out) == 0)
    std::fprintf(out, "n,eps,I,run,build_ns,num_samples,num_segments,size_bytes,max_error,mean_error,status\n");

  int status = 0;
  for (int a = 5; a < argc && status == 0; ++a) {
    const size_t I = std::strtoull(argv[a], nullptr, 10);
    std::vector<uint64_t> build_ns(kRuns);
    std::vector<SampleEBPLA::Segment> first_segs;
    size_t samples = 0, segs = 0, bytes = 0;
    ErrorStats st;
    bool identical = true;
    for (int r = 0; r < kRuns; ++r) {
      const auto b0 = std::chrono::steady_clock::now();
      SampleEBPLA m(keys.data(), n, eps, I);
      const auto b1 = std::chrono::steady_clock::now();
      build_ns[r] = std::chrono::duration_cast<std::chrono::nanoseconds>(b1 - b0).count();
      samples = m.num_samples(); segs = m.num_segments(); bytes = m.size_in_bytes();
      if (r == 0) {
        first_segs = m.segments();
        st = max_error(m, keys.data(), n);  // exhaustive, every key; deterministic build
      } else {
        const auto& s = m.segments();
        identical = identical && s.size() == first_segs.size();
        for (size_t g = 0; identical && g < s.size(); ++g)
          identical = s[g].first_key == first_segs[g].first_key && s[g].y0 == first_segs[g].y0 &&
                      s[g].slope == first_segs[g].slope && s[g].clamp_hi == first_segs[g].clamp_hi;
      }
    }
    const bool ok = st.max_error <= eps && identical;
    for (int r = 0; r < kRuns; ++r)
      std::fprintf(out, "%zu,%zu,%zu,%d,%llu,%zu,%zu,%zu,%llu,%.4f,%s\n", n, eps, I, r + 1,
                   (unsigned long long)build_ns[r], samples, segs, bytes,
                   (unsigned long long)st.max_error, st.mean_error, ok ? "ok" : "FAIL");
    std::fflush(out);
    std::printf("n=%zu eps=%zu I=%-4zu build_ms=[%.1f %.1f %.1f] segs=%zu bytes=%zu max_err=%llu mean_err=%.2f %s\n",
                n, eps, I, build_ns[0] / 1e6, build_ns[1] / 1e6, build_ns[2] / 1e6, segs, bytes,
                (unsigned long long)st.max_error, st.mean_error, ok ? "ok" : "FAIL");
    if (!ok) {
      std::printf("FAILURE: %s at i=%zu key=%llu (max_error=%llu > eps=%zu)\n",
                  identical ? "error bound violated" : "non-deterministic build", st.argmax,
                  (unsigned long long)keys[st.argmax], (unsigned long long)st.max_error, eps);
      status = 2;
    }
  }
  std::fclose(out);
  return status;
}
