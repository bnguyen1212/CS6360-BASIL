# NOTES

Setup facts and harness notes for our BASIL fork. "(unverified)" means it comes
from reading the code and hasn't been run yet.

## Machine (2026-11-15)
- WSL2 on an Intel i3-1115G4 (4 threads), x86_64. `.wslconfig` sets `memory=16GB`;
  `free` shows 15 GiB RAM and 4 GiB swap. About 938 GB of free disk.
- Installed: g++, make, wget, md5sum, python3.
- Missing at first look: clang, cmake, zstd, Boost headers (installed since).

## 1. Build (Release)
Prerequisites (Boost is needed because `dtl/` headers include it; zstd and m4 are
in the upstream README; zstd is only used by the 800M downloads):
```bash
sudo apt -y update
sudo apt -y install clang cmake zstd m4 libboost-all-dev libomp-dev
pip3 install -r requirements.txt   # only needed for the plotting scripts
```
Build with clang (`scripts/prepare.sh` does the same thing but uses the default
compiler, which is g++ here, and `make -j 16`):
```bash
mkdir -p build && cd build
rm -rf CMakeCache.txt CMakeFiles/          # needed whenever you change -D macros
CC=clang CXX=clang++ cmake -DCMAKE_BUILD_TYPE=Release -DUSER=ON ..
make -j4 benchmark generate
cd ..
```
- Release adds `-O3 -DNDEBUG`. CMakeLists also always adds
  `-ffast-math -march=native -Wall -Wfatal-errors`, plus `-fopenmp` if the
  compiler accepts it. clang needs `libomp-dev` for that; without it OpenMP is
  silently turned off.
- `-DUSER=ON` compiles only the `#ifdef USER` configurations. The other macros
  (`SPEEDUP`, `DESIGNSPACE`, `PARETO_*`, ...) compile hundreds of template
  instantiations and make the build very slow.
- The repo root is on the include path through `dtl` (`target_include_directories(dtl PUBLIC ..)`),
  so `#include "ours/..."` will resolve.
- The upstream README names scripts that don't exist. The real ones are
  `scripts/download.sh` and `scripts/execute_*.sh`.

## 2. Datasets
- `scripts/download.sh` downloads **all eleven** 200M datasets from
  `https://www.cse.cuhk.edu.hk/mlsys/gre/<name>` into `data/<name>_200M_uint64`
  (`wget -nc`, no checksum). It then also downloads `books_800M` and `osm_800M`
  from Dropbox (zstd-compressed, md5-checked). **Don't run it.**
- Download only `history`:
  ```bash
  mkdir -p data
  wget --no-check-certificate -O data/history_200M_uint64 https://www.cse.cuhk.edu.hk/mlsys/gre/history
  ```
  The download has no checksum, and `download.sh` uses `-nc`, which keeps a partial
  file. Verify the result: size should be 1,600,000,008 bytes, and
  `od -An -tu8 -N8 data/history_200M_uint64` should print `200000000`.
- **Downloaded (2026-11-15):** `data/history_200M_uint64`, SOSD format: 8-byte uint64
  count header (`200000000`) followed by 200M uint64 keys. File size
  1,600,000,008 bytes (verified with `stat` and `od`).
  **The downloaded file was NOT sorted.** When `generate` ran (19:37), it printed
  "keys have to be sorted … Overwrite sorted keys" and rewrote the file sorted, in
  place. The local copy is now sorted. Anyone who re-downloads must run `generate`
  (or sort the file) before benchmarking.
- Format (`util::load_data`): a uint64 count `N` as the header, then `N` raw
  little-endian uint64 keys. No values are stored. The key type comes from the
  filename suffix (`_uint64`). The harness requires sorted keys and allows
  duplicates (it prints "data contains duplicates").
- Queries:
  ```bash
  mkdir -p results
  cd build && ./generate ../data/history_200M_uint64 10000000 && cd ..
  ```
  This writes `data/history_200M_uint64_equality_lookups_10M`: a uint64 count, then
  `{uint64 key, uint64 result}` pairs. All lookups are positive by default, drawn
  from unique keys with at most 100 duplicates each. `result` is the **sum of the
  positions** of all rows with that key, because the value of row `i` is `i`.
  It is not the lower_bound position.
  `generate` also **re-sorts and overwrites the data file if it isn't sorted**, and
  appends to `results/dataset_hardness.csv` (path relative to `build/`).

## 3. Running one benchmark
```bash
./build/benchmark ./data/history_200M_uint64 ./data/history_200M_uint64_equality_lookups_10M \
    --only sPGM --csv --suffix run1
```
- **Index:** `--only <tag>`. The tags are listed in `execute_64_bit()` in `benchmark.cc`
  (`BS, ART, BTree, IBTree, sRT, sRMI_CPP, sPGM, sRS, sCHT, ms*`). Without `--only`,
  every compiled index runs.
- **Error bound and sampling interval:** compile-time template arguments in the
  `#ifdef USER` block of `benchmarks/benchmark_<index>.h`, for example
  `sPGM<uint64_t, eps_bottom, I_bottom, eps_upper, I_upper>` (USER default
  `<16,1,4,1>`) and `sCHT<uint64_t, bins, eps, I>`. Changing them means rebuilding.
- **Dataset:** the first positional argument. Paths must contain `data/`, because
  the harness parses the dataset name from it.
- **Query count:** whichever lookups file you pass (generate makes 10M or 1M).
- **Repeats:** `-r N` repeats only the lookups and reports the **median**, not the
  raw runs. To get ≥3 raw rows, run the binary 3 times with different `--suffix`
  values (or run them all with the same suffix, since rows are appended).
- **Output:** a `RESULT: name,variant,size_bytes,build_ns,ns_per_lookup` line on
  stdout. With `--csv`, a row is appended to
  `./results/<dataset>_results_table[_<suffix>].csv` (columns
  `index,options,space,build,average,dataset`). The `_build`, `_tail`, `_sep` and
  `_perf` variants go with `-b`, `-l`, `--sep` and `-p`. Run from the repo root.
- **Correctness:** if any lookup's checksum doesn't match, the latency field
  becomes `fail`. Nothing else is checked: the harness never checks per-key
  error, so our exhaustive test has to do that. `--perf` uses `perf_event_open`,
  which may not work in WSL2 (unverified).

## 4. Interface for a new index
Template on `wrappers/wrapper_binary_search.h` and derive from `Competitor`
(`wrappers/base.h`):
```cpp
uint64_t Build(std::vector<KeyValue<KeyType>>& data,           // sorted (key, pos)
               std::vector<EqualLookup<KeyType>>& lookups);    // returns build ns (use util::timing)
SearchBound EqualityLookup(const KeyType key) const;           // {start, stop}: stop is EXCLUSIVE
std::string name() const;   std::string variant() const;   std::size_t size() const; // bytes
bool applicable(bool unique, const std::string& data_filename) const;  // optional
```
After `EqualityLookup`, the harness runs `std::lower_bound(data+start, data+stop)`.
So for us, return `{max(0,p-eps), min(n, p+eps+1)}`.

Registration takes the minimum number of harness edits (log them here when made):
1. A new `benchmarks/benchmark_ours.h` that defines
   `template<...> void benchmark_64_ours(sosd::Benchmark<uint64_t,Searcher>&)` with
   `Run<OurIndex<...>>()` calls, followed by `INSTANTIATE_TEMPLATES(benchmark_64_ours, uint64_t);`.
2. In `benchmark.cc`, add `#include "benchmarks/benchmark_ours.h"` and
   `check_only("Ours", benchmark_64_ours(benchmark));`.
3. No CMake change is needed for headers in `ours/`, because `benchmarks/*.h` is
   globbed and the root is on the include path. Unit tests will need their own
   `add_executable`.

## 5. Off-limits (authors' sampled-index code), identified by name and includes only
- `indexes/sPGM-index/`: sampled PGM, i.e. Sample EB-PLA
  (`include/pgm/pgm_index_sampling.hpp`, `pgm_index_sampling_meta.hpp`,
  `piecewise_linear_model_sampling.hpp`)
- `indexes/sCHT/`: sampled CHT, i.e. Sample EB-Histogram
- `indexes/srs/`: sampled RadixSpline
- `indexes/analysis-srmi/`: sampled RMI
- `wrappers/wrapper_spgm_index.h`, `wrapper_scht.h`, `wrapper_srs.h`,
  `wrapper_srmi_cpp_robust.h`, `wrappers/meta/*`: glue code that takes `interval`
  parameters and may do the sampling itself
- `wrappers/wrapper_srt.h` (sampled radix table, implemented inline),
  `wrapper_art_primary_lb.h`, `wrapper_ibtree.h`, `wrapper_stx_btree.h`: sampled
  traditional indexes, with the sampling done in the wrapper
- `utils/pgm_metric.h`: not the authors' code, but it contains a PGM-style
  `OptimalPiecewiseLinearModel` (from GRE). Keep it off-limits so our PLA stays
  independent. Using it as a black box through `generate` is fine.

Allowed to read: `benchmark.h`, `benchmark.cc`, `util.h`, `generate.cc`, `config.h`,
`wrappers/base.h`, `wrappers/wrapper_binary_search.h`, `searches/`, `scripts/`,
`benchmarks/*.h` (configuration lists only), `dtl/`, `indexes/stx-btree-0.9/` (the plain library).

## 6. Memory (>16 GB risks)
- **`books_800M` and `osm_800M`** are 6.4 GB raw each and would need about 30 GB in
  `benchmark`. `download.sh` fetches both. Never use them.
- **`benchmark` on one 200M dataset:** it holds `keys` (1.6 GB), `data_` Rows
  (3.2 GB) and `index_data_` KeyValues (3.2 GB, filled with `push_back` and no
  `reserve`). Its peak is about **11 GB** while the constructor runs (the last
  vector growth briefly holds 2.1 + 4.3 GB), then it settles to about 7.6 GB
  before the index is built. That is an estimate, not a measurement. Any index
  that copies its data adds to that. It fits in 16 GB, but only one process at a
  time with nothing else large running. Record the real peak with
  `/usr/bin/time -v` (G0).
- **`generate` on 200M:** about 6.5 GB (keys, unique copy, Rows) plus the hardness
  computation. That's fine, but don't run it at the same time as `benchmark`.
- **Builds:** `-DUSER=ON` is light. The reproduce macros (`SPEEDUP`,
  `DESIGNSPACE`) instantiate hundreds of templates in a single translation unit,
  so expect high compiler RAM and use `-j` ≤ 4.
- **Peak memory, history 200M (G0, measured with `/usr/bin/time -v`):**
  `benchmark` uses **12.2 GiB (12,517 MB) max RSS** for every index and config
  (BS and sPGM eps=256 at I=1/16/64/256, 3 runs each, spread under 1 MB). The
  harness's own copies of the data dominate; the sPGM index is only 124–137 KB.
  That is 78% of the 15.6 GiB VM, leaving about 3.4 GiB of headroom. `generate`
  uses 6.4 GiB. There were no major page faults and no swap use.

## Build/environment fixes and harness edits (log every one)
Toolchain: Ubuntu clang 18.1.3, cmake 3.28.3.
1. `dtl/thread.hpp`: added `#include <functional>`. It uses `std::function`, which
   newer libstdc++ no longer pulls in transitively (`fatal error: no template named 'function'`).
2. Environment: clang accepts `-fopenmp`, but `omp.h` is missing until you install
   `libomp-dev`. `indexes/sPGM-index/.../piecewise_linear_model_sampling.hpp` includes it.
   Fix: `sudo apt -y install libomp-dev` (add it to the prerequisites in §1).
3. Config (not a build fix): the `#ifdef USER` block of `benchmarks/benchmark_spgm.h` was
   changed from `sPGM<uint64_t,16,1,4,1>` to four runs at eps_b=256 with
   I_b = 1, 16, 64, 256. The upper level stays at the USER default (eps_u=4, I_u=1).
   For G0, `results/g0/run_g0.sh` builds one binary per I (`build/g0bin/`) by temporarily
   leaving a single line in that block, then restores the 4-line version.

## G0 results (2026-11-15), in `results/g0/`
- Configuration: clang 18 Release, `-DUSER=ON`, history 200M, 10M positive lookups,
  `--only BS|sPGM`, 1 lookup repeat per process, 3 processes per config.
  `summary.csv` has one row per process. The `*_runN.log` files hold the raw stdout
  and `time -v` output. `driver.log` holds the build and run output.
- All 15 runs passed the harness checksum (none reported `fail`).
- **These timings are not usable as performance numbers.** Binary search measured
  2.0–10.0 µs per lookup and sPGM 0.66–2.25 µs. The sPGM build time for a single
  config varied by up to 15x (for example, I=256 ranged from 0.65 s to 10.2 s).
  The 200M-key load took anywhere from 1.1 s to 19.8 s, and sys time was often
  higher than user time. The likely cause is host/VM memory pressure: a 12.2 GiB
  process in a 16 GB WSL2 VM, competing with Windows. This is not yet verified.
  Before timing seriously, close other Windows apps, check for CPU throttling (a
  laptop i3), and consider more runs. The memory and size numbers are stable.

## G1: our code in `ours/` (2026-11-15)
- Build and test (a standalone CMake project; the harness build is untouched):
  ```bash
  CC=clang CXX=clang++ cmake -S ours -B build/ours -DCMAKE_BUILD_TYPE=Release
  cmake --build build/ours -j4
  ./build/ours/test_sample_eb_pla                                  # synthetic, ~10 s
  ./build/ours/test_sample_eb_pla data/history_200M_uint64 10000000 # + first 10M history keys
  ```
- Files: `sampler.h` (systematic sampler), `sample_eb_pla.h` (Sample EB-PLA plus the
  correctness argument in its header comment), `error.h` (exhaustive max error over
  every key), `sosd_io.h` (our single-copy loader), and `tests/test_sample_eb_pla.cc`.
- **PLA used: greedy shrinking cone.** Each segment passes exactly through its first
  point, and slopes are restricted to >= 0. This is not the optimal PLA, so segment
  counts will be higher than BASIL's sPGM at the same eps and I.
- Two design decisions that the proof depends on (see `sample_eb_pla.h`):
  1. **Duplicates.** Each distinct sampled key v gets the point (v, lower_bound(v)). If
     v's duplicate run started before its sample index, it ALSO gets (v+1, H(v)),
     where H(v) is the last sample index holding v. On unique data (history is
     unique) this reduces to plain (key[j*I], j*I).
  2. **Boundary gap.** A key between the last point of segment g and the first point
     of g+1 is predicted by extrapolating segment g. The prediction is clamped to the
     y of g+1's first point.
- Ablation (done in the scratchpad, not committed): removing the clamp makes the
  tests fail with errors up to 918 at eps=1, and on plain uniform data too.
  Removing the (v+1, H(v)) point makes the duplicate tests fail with errors up to 2000.
- Results: 7,000,121 synthetic checks, 0 failures. On the history slice (first 10M
  keys) max error <= eps in every config, and the process peaked at 446 MB RSS:

  | eps | I | segments | max err | mean err |
  |----:|----:|-------:|----:|------:|
  | 256 | 1   | 140    | 256 | 90.60 |
  | 256 | 16  | 146    | 251 | 86.27 |
  | 256 | 64  | 204    | 241 | 70.27 |
  | 256 | 256 | 18,472 | 218 | 7.94  |
  | 16  | 1   | 14,646 | 16  | 5.79  |
  | 16  | 17  | 293,867| 16  | 1.72  |
  At I close to eps+1, the tightened delta (1 or 0) forces many more segments. This
  is the size cost of sampling.

## G1 results: Sample EB-PLA on history, eps = 256 (2026-11-15), in `results/g1/`
- `./build/ours/pla_bench data/history_200M_uint64 <10000000|all> 256 results/g1/pla.csv 1 16 64 256`
  (clang 18, `-O3 -DNDEBUG`). The keys are loaded once with `ours::load_sosd_keys`,
  not the harness loader. There are 3 timed builds per I. Build time covers sampling,
  anchor points and the cone fit; it excludes loading. The bench checks that all 3
  builds are identical, then computes the max error over every key once. Raw rows
  are in `pla.csv`; stdout and `time -v` output are in `pla_10M.log` / `pla_200M.log`.
- **0 failures.** Max error is <= 256 over every key in all 8 configs.

| keys | I | build ms (3 runs) | median | vs I=1 | segments | bytes | max err |
|---|---:|---|---:|---:|---:|---:|---:|
| 10M | 1 | 310.4 / 191.1 / 221.9 | 221.9 | 1.0x | 140 | 5,600 | 256 |
| 10M | 16 | 21.7 / 25.6 / 25.9 | 25.6 | 8.7x | 146 | 5,840 | 251 |
| 10M | 64 | 10.0 / 8.7 / 11.2 | 10.0 | 22.1x | 204 | 8,160 | 241 |
| 10M | 256 | 4.7 / 4.0 / 3.9 | 4.0 | 55.1x | 18,472 | 738,880 | 218 |
| 200M | 1 | 9066 / 11655 / 6144 | 9066 | 1.0x | 10,609 | 424,360 | 256 |
| 200M | 16 | 599 / 956 / 554 | 599 | 15.1x | 11,144 | 445,760 | 255 |
| 200M | 64 | 255 / 269 / 215 | 255 | 35.6x | 13,676 | 547,040 | 252 |
| 200M | 256 | 111 / 96 / 93 | 96 | 94.2x | 373,805 | 14,952,200 | 253 |

- **Peak RSS on 200M is 7.45 GiB, set by I=1.** The build materializes the sample
  vector and the anchor-point vector at 16 B per key each (~6.4 GB) on top of the
  1.6 GB of keys. That much allocation probably also explains why the I=1 build
  times are noisy (6.1–11.7 s). Fitting in a streaming pass would cut memory to
  O(segments), and the I=1 baseline time would then stop including the allocation
  of two 3.2 GB vectors. We should decide this before comparing build times against BASIL.
- The model costs 40 B per segment (a 32 B `Segment` plus an 8 B first key).

## G2 prep: black-box check of the authors' sPGM (2026-11-15), in `results/g2/`
`ours/blackbox/spgm_probe.cc` includes `wrappers/wrapper_spgm_index.h` and uses only
the harness contract: `Build` returns ns, and `EqualityLookup` returns `[start, stop)`.
**We did not read the authors' index or wrapper code.** Template parameters are as
documented in README §3.4. The probe is compiled with the harness flags
(`-ffast-math -fopenmp -march=native -O3`), and the keys are loaded once, straight
into the `KeyValue` vector that `Build` needs (3.2 GB; peak RSS 3.0 GiB).

1. **Each config really used its I.** At runtime, `variant()` prints `256/1/4/1`,
   `256/16/4/1`, `256/64/4/1` and `256/256/4/1`, the same strings as in the G0 logs, and
   the sizes match G0 to the byte. In this low-noise process the median build time
   falls with I: 5.05 s, 0.51 s, 0.23 s, 0.070 s for I = 1, 16, 64, 256 (raw data in
   `spgm_probe_200M.csv`). The G0 timings were too noisy to show this.
2. **Their error at eps=256, I=256 is not <= 256.** We queried every one of the 200M keys:

   | I | bound width (unclipped) | keys outside bound | max error (from bound centre) | size B |
   |---:|---:|---:|---:|---:|
   | 1   | 514  = 2(256+1)   | 0 | 256 | 137,256 |
   | 16  | 544  = 2(256+16)  | 0 | 270 | 135,704 |
   | 64  | 640  = 2(256+64)  | 0 | 316 | 132,264 |
   | 256 | 1024 = 2(256+256) | 0 | **482** | 124,352 |

   The returned bound is `prediction ± (eps + I)`, roughly. So the `eps` template
   argument behaves like the PLA's delta on the samples, with NO tightening, and the
   search window is widened by I to stay correct. Lookups are always correct (0 keys
   outside their bound), but the guaranteed error is about eps + I - 1 = 511 (482
   measured), not eps. The "max error from bound centre" assumes the bound is
   symmetric around the prediction, which the constant widths support; it is
   accurate to about ±1.
3. **Ours with no tightening** (`pla_bench --untightened`, delta = eps, which
   guarantees eps + I - 1), 200M keys:

   | I | segments | size B | max err | bound | build ms (median) |
   |---:|---:|---:|---:|---:|---:|
   | 16  | 10,459 | 418,360 | 271 | 271 | 629 |
   | 64  | 10,197 | 407,880 | 316 | 319 | 202 |
   | 256 | 9,624  | 384,960 | 496 | 511 | 81  |

   Untightened, our size also falls slowly with I, as theirs does. Theirs grows
   instead of shrinking only if you hold the true error fixed. But we are still
   **3.1x larger than their 124 KB** at I=256. Likely causes, neither verified: our
   40 B per segment (their bytes per segment are unknown), and greedy-cone vs
   optimal PLA giving more segments.
4. **Sample points use the array index j*I.** The sampler steps `j += I` over array
   indices and stores that index, never the ordinal. The new test "sample points
   use index j*I (not j)" checks that every sample index is j*I (or n-1). On unique
   data it also checks that every anchor point is exactly (key[j*I], j*I). It passes
   on synthetic data and on the 10M history slice.
- **Consequence for comparisons:** "eps" means different things. Our eps is the true
   max error; their eps is about the true error minus (I - 1). To compare at equal
   true error, run ours at eps_ours = eps_theirs + I - 1, or ours untightened.

## G2: two-level index + lookup (2026-11-15), in `results/g2/lookup*.{csv,log}`
- Code: `ours/segment_directory.h` is **level 2**: a binary search over the segments'
  first keys, kept separate so a recursive PLA can replace it. `ours/sample_eb_pla.h`
  is **level 1**: the segments, with clamped prediction. `ours/lookup.h` holds
  `SampleEBPLAIndex::lookup`: predict p, then `std::lower_bound` over
  `[max(0,p-e), min(n-1,p+e)]` with e = `error_bound()`. That is eps for the tightened
  model (all G2 numbers below) and eps+I-1 for the untightened one. The key array is
  referenced, never copied. Lookups are guaranteed only for keys present in the array.
- Unit tests now check `lookup(key) == lower_bound` for EVERY key of every synthetic
  array, tightened and untightened, plus the 10M history slice: 7,797,616 checks,
  0 failures. Ablation (scratchpad only): shrinking the window by 1 makes 10 test
  groups fail.
- Benchmark: `./build/ours/lookup_bench data/history_200M_uint64 <10000000|all> 256 results/g2/lookup.csv 10000000 <all|random> 1 16 64 256`.
  The keys are loaded once. The 10M random existing keys use seed 20261115 and are
  drawn uniformly over positions; the same set is used for every config. The
  reference answers come from `std::lower_bound` over the whole array. There are 3
  timed runs per config, and every timed run's checksum is verified.
- **Correctness: 0 wrong answers.** On the 10M slice all 10,000,000 keys were checked;
  on 200M, 10,000,000 random existing keys. That holds for every I in {1, 16, 64, 256}.

| keys | index | I | segments | model bytes | avg lookup ns (3 runs) |
|---|---|---:|---:|---:|---|
| 10M  | binary search | – | – | 0 | 620.8 / 561.6 / 558.3 |
| 10M  | ours eps=256 | 1   | 140     | 5,600      | 355.7 / 346.2 / 333.5 |
| 10M  | ours eps=256 | 16  | 146     | 5,840      | 360.0 / 366.7 / 355.5 |
| 10M  | ours eps=256 | 64  | 204     | 8,160      | 335.5 / 390.8 / 365.4 |
| 10M  | ours eps=256 | 256 | 18,472  | 738,880    | 454.8 / 411.5 / 424.2 |
| 200M | binary search | – | – | 0 | 1532.8 / 1538.3 / 1057.7 |
| 200M | ours eps=256 | 1   | 10,609  | 424,360    | 628.5 / 628.4 / 553.0 |
| 200M | ours eps=256 | 16  | 11,144  | 445,760    | 487.7 / 510.1 / 500.8 |
| 200M | ours eps=256 | 64  | 13,676  | 547,040    | 484.7 / 492.2 / 488.3 |
| 200M | ours eps=256 | 256 | 373,805 | 14,952,200 | 637.4 / 827.5 / 653.3 |

- Peak RSS on 200M was 7.6 GiB: 1.6 GB of keys, 160 MB of queries and answers, and
  the transient ~6.4 GB of the I=1 build. There were no major faults.
- Reading the numbers:
  * At I=256 the 15 MB model is larger than the 6 MB L3, so the level-2 search
    misses cache. A recursive level 2 is the fix to try.
  * At 200M, I=1 is about 25% slower than I=16 although their models are nearly
    identical. **Unexplained.** It could be measurement order: I=1 is timed right
    after freeing the ~6.4 GB build vectors. Rerun with a different config order
    before drawing conclusions.
  * Run-to-run noise is up to about 30% (binary search 1058–1538 ns). Same WSL2 caveat as G0.
  * These numbers are not comparable to BASIL's latencies. The harness searches
    16 B `Row`s and sums the values of duplicate rows; we search the 8 B key array.
    A fair comparison needs our index inside the harness (BASIL wrapper, next step),
    at equal guaranteed error (see CLAUDE.md).
