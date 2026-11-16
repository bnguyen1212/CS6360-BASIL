# CS 6360 project: sampling-based learned indexes (BASIL reproduction)

## What we're building
An independent C++17 implementation of the sampling schemes from Choi et al.,
"Can Learned Indexes be Built Efficiently? A Deep Dive into Sampling Trade-offs"
(SIGMOD 2024), added to our fork of the BASIL benchmark harness (built on SOSD).

Today's scope (status report due tonight): G0, G1, and part of G2.
- G0: BASIL builds and runs on the `history` dataset; peak memory recorded.
- G1: systematic sampler + Sample EB-PLA + unit tests.
- G2 (partial): lookup path, two-level index, exhaustive validation on all 200M keys,
  then (if time) a BASIL index wrapper and a side-by-side run against BASIL's own index.
Sample EB-Histogram / CHT is NOT in scope today.

## Algorithm notes
- Position of a key = its lower_bound index in the sorted array (datasets may contain
  duplicate keys; always measure error against the lower_bound position).
- Sampler: every I-th key, i.e. sample points (key[j*I], j*I). Always include the
  last key so no keys sit past the final sample point.
- Sample EB-PLA: fit an error-bounded piecewise linear model to the sample points with
  the tightened bound delta = eps - I + 1. Requires 1 <= I <= eps + 1 (reject otherwise).
  Why it works: for a non-negative-slope segment, a key between two consecutive sample
  points gets a prediction between theirs, so its error is at most (I - 1) + delta = eps.
- WATCH the segment-boundary gap: keys that fall between the last sample point of one
  segment and the first sample point of the next. Decide which segment predicts them and
  prove (and test) that they still stay within eps. Write the argument in a comment.
- PLA fitting: start with a greedy shrinking-cone fit (simple, monotone). An optimal
  PLA (as in the PGM-index) gives fewer segments; only switch if there is time, and note
  which one we used, since segment counts vs. BASIL depend on it.
- Lookup: predict p, then binary-search [max(0, p - eps), min(n - 1, p + eps)].
- Two-level index: level 1 = our segments; level 2 = a small structure over segment
  first-keys (binary search is fine for today; recursive PLA as in PGM is the upgrade).
- COMPARING WITH BASIL's sPGM: their `eps` template argument is NOT the guaranteed error.
  Measured as a black box, their search bound is about prediction ± (eps + I), and the
  true error reaches about eps + I - 1 (NOTES.md, G2 prep). Always compare at EQUAL
  guaranteed error: ours tightened at eps' = eps + I - 1 vs. theirs at eps, OR ours
  untightened (`Bound::kUntightened`) vs. theirs at the same eps and I.

## Hard rules
- INDEPENDENCE: we write the sampling and model-building logic ourselves.
  Do NOT open, read, or copy the authors' sampled-index implementations:
  - `indexes/sPGM-index/` (sampled PGM / Sample EB-PLA)
  - `indexes/sCHT/` (sampled CHT / Sample EB-Histogram)
  - `indexes/srs/` (sampled RadixSpline)
  - `indexes/analysis-srmi/` (sampled RMI)
  - `wrappers/wrapper_spgm_index.h`, `wrappers/wrapper_scht.h`, `wrappers/wrapper_srs.h`,
    `wrappers/wrapper_srmi_cpp_robust.h`, `wrappers/meta/*` (glue; may contain sampling)
  - `wrappers/wrapper_srt.h`, `wrappers/wrapper_art_primary_lb.h`, `wrappers/wrapper_ibtree.h`,
    `wrappers/wrapper_stx_btree.h` (sampled traditional indexes; sampling done in the wrapper)
  - `utils/pgm_metric.h` (PGM-style optimal PLA from GRE; keep our PLA independent)
  Use `wrappers/base.h` + `wrappers/wrapper_binary_search.h` as the interface template.
  You MAY read and use the harness: dataset loading, query generation, timing,
  baseline indexes, and the interface a new index must implement. You MAY run the
  authors' indexes as black boxes to compare results.
- Our code lives in `ours/` (headers + tests). Don't modify harness files except the
  minimum needed to register our index; list any such edits in NOTES.md.
- Every change keeps the tests passing. The key test: for EVERY key,
  |predicted - true position| <= eps, and every lookup returns the correct position.
  Test synthetic data (uniform, skewed, many duplicates, tiny arrays, n not divisible
  by I) and the history slices.
- Comment the algorithm clearly. Every team member must be able to explain this code.
- Commit after each working step with a clear message. Do NOT add `Co-Authored-By`
  (or any other AI attribution) lines to commit messages or PR descriptions; commits are
  authored by the team member alone. This is intentional.
- Team-facing status lives in `TEAM_CATCHUP.md` (repo root). Update it when a milestone,
  key number, or the next-steps list changes; `NOTES.md` stays the detailed log.

## Workflow
- Iterate on a small slice (first 10M keys of `history`); use all 200M keys only for
  validation and timing.
- ~16 GB RAM: never load more than one 200M-key dataset at once. Our standalone
  benchmark/validation loads exactly ONE copy of the keys (~1.6 GB for 200M) with our own
  loader (`ours/`), never the harness's `util::load_data`/`sosd::Benchmark` (which hold
  ~12 GB). Our code takes `const uint64_t* keys, size_t n` and never copies the key array. Build with Release
  flags (-O3, NDEBUG) for any timing run.
- Timing results: at least 3 runs per configuration, raw rows to CSV under `results/`.
- Record setup facts (machine, build commands, dataset paths, peak memory) in NOTES.md.
- Ubuntu (native or WSL2), clang, CMake, Boost, zstd.
