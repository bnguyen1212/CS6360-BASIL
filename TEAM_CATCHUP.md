# BASIL Reproduction: Team Catch-up

As of 2026-11-15 · Bao Nguyen

## Where we are

G0, G1 and the first half of G2 are done, and every correctness check has passed. BASIL builds and runs on `history`. Our Sample EB-PLA stays within eps on every one of the 200M keys, and our two-level index returns the exact position for every lookup we tested. The BASIL wrapper and the side-by-side run against their index are not started.

**The one thing everyone must know:** BASIL's sPGM `eps` is not a guaranteed error. Measured as a black box, its search window is about prediction ± (eps + I), and at eps=256, I=256 its real max error is 482. Ours tightens the fit so the true max error is ≤ eps. Any comparison must be at equal guaranteed error (see Rules and gotchas).

Full details, commands and every number are in `NOTES.md`, and the raw data is under `results/`.

## Get set up

All numbers so far come from one laptop: an Intel i3-1115G4 (2 cores, 4 threads, 6 MiB L3) running Ubuntu 24.04.3 on WSL2 with 16 GB for the VM, and clang 18.1.3.

1. Install prerequisites. `libomp-dev` is easy to miss, and without it neither target builds under clang:
    ```
    sudo apt -y install clang cmake zstd m4 libboost-all-dev libomp-dev
    ```
2. Build the harness in Release. Clear the CMake cache whenever you change a `-D` macro:
    ```
    mkdir -p build && cd build && rm -rf CMakeCache.txt CMakeFiles/
    CC=clang CXX=clang++ cmake -DCMAKE_BUILD_TYPE=Release -DUSER=ON ..
    make -j4 benchmark generate && cd ..
    ```
3. Build our code. It is a separate CMake project in `ours/` and doesn't touch the harness build:
    ```
    CC=clang CXX=clang++ cmake -S ours -B build/ours -DCMAKE_BUILD_TYPE=Release
    cmake --build build/ours -j4
    ./build/ours/test_sample_eb_pla data/history_200M_uint64 10000000
    ```
    The last line runs all unit tests plus the 10M history slice in about 30 s. It should end with `0 failures`.
4. Get the dataset. Download only `history` (1.6 GB); **do not run `scripts/download.sh`**, which fetches all 11 datasets plus two 800M ones that won't fit in memory:
    ```
    wget --no-check-certificate -O data/history_200M_uint64 https://www.cse.cuhk.edu.hk/mlsys/gre/history
    ```
    The file is SOSD format: an 8-byte count (200,000,000), then the uint64 keys, 1,600,000,008 bytes in total. **The downloaded file is not sorted.** Run `cd build && ./generate ../data/history_200M_uint64 10000000` once. It sorts the file in place and writes the 10M query file the harness needs.

The upstream README names scripts that don't exist; the real ones are `scripts/download.sh` and `scripts/execute_*.sh`.

## What's done

Every check passed: 0 wrong lookups and every max error within its bound.

| Step | Result | Files |
| --- | --- | --- |
| G0: BASIL on `history` | Builds after one code fix and one missing package. Binary search plus their sPGM at eps=256 and I=1/16/64/256, 3 runs each, all lookups correct. Peak memory 12.2 GiB in every run. | `results/g0/` |
| G1: sampler + Sample EB-PLA + tests | Greedy shrinking-cone fit with tightened delta = eps − I + 1. Max error ≤ 256 over all 200M keys for every I. | `ours/sampler.h`, `ours/sample_eb_pla.h`, `ours/error.h`, `results/g1/` |
| G2 prep: black-box check of their sPGM | Their search window is 2(eps + I) wide, and their max error reaches 482 at I=256. | `ours/blackbox/spgm_probe.cc`, `results/g2/spgm_probe_*` |
| G2: two-level index + lookup | Lookup is exact for all 10M keys of the slice, and for 10M random keys out of 200M, for every I. | `ours/segment_directory.h`, `ours/lookup.h`, `results/g2/lookup*` |

Our tightened index on all 200M keys, eps=256. Each mean ± SD is over 3 runs in a standalone process with one copy of the keys loaded:

| I | Max error | Segments | Size (bytes) | Build (ms) | Lookup (ns) |
| --- | --- | --- | --- | --- | --- |
| 1 | 256 | 10,609 | 424,360 | 8955 ± 2757 | 603 ± 44 |
| 16 | 255 | 11,144 | 445,760 | 703 ± 220 | 500 ± 11 |
| 64 | 252 | 13,676 | 547,040 | 246 ± 28 | 488 ± 4 |
| 256 | 253 | 373,805 | 14,952,200 | 100 ± 10 | 706 ± 105 |

For reference, plain binary search over the same 200M keys took 1376 ± 276 ns. Sampling cuts build time about 90x from I=1 to I=256. The cost is size: at I=256 the tightened delta is 1, so the fit is almost exact and segments jump 35x.

## How our index works

The build fits lines to every I-th key, and the lookup predicts a position and then searches a window of ±eps around it. The full proof that every key stays within eps is in the header comment of `ours/sample_eb_pla.h`. Read it before changing anything there.

**Build**

1. Sample: take keys at array positions 0, I, 2I, … and always the last key (`sampler.h`). Samples use the array position j·I, not j.
2. Make fit points: one point (key, lower_bound position) per distinct sampled key. On duplicate-free data such as `history`, this is just (key[j·I], j·I).
3. Fit: a greedy shrinking cone with tolerance delta = eps − I + 1 and slopes kept ≥ 0. Each segment passes exactly through its first point. I > eps + 1 is rejected.

**Lookup**

1. Level 2 (`segment_directory.h`): binary search over the segments' first keys finds the segment.
2. Level 1 (`sample_eb_pla.h`): evaluate that segment's line, round, and cap at the next segment's starting position.
3. Last mile (`lookup.h`): `lower_bound` over positions [max(0, p − eps), min(n − 1, p + eps)].

Two decisions the error guarantee depends on. Removing either one makes the tests fail, which we checked by compiling broken copies:

- **Cap at segment boundaries.** Keys just after a segment's last sample are predicted by extending that segment's line, which can overshoot wildly. Capping the prediction at the next segment's first position fixes this and can never move a prediction away from the right answer. Without it, errors reach 918 at eps=1, even on uniform random data.
- **Extra point for duplicate runs.** If a sampled key's run of duplicates starts before its sample, keys just after the run would be predicted far too low. Fitting a second point (key + 1, last sample position in the run) fixes it. Without it, errors reach 2000. This never triggers on `history`, which has no duplicates.

The fit is greedy, not optimal like the PGM-index's, so expect more segments than an optimal fit would give.

## Rules and gotchas

The first rule is about academic independence; the rest will cost you hours if you miss them.

- **Don't open the authors' sampled-index code.** That means `indexes/sPGM-index/`, `indexes/sCHT/`, `indexes/srs/` and `indexes/analysis-srmi/`, plus the sampled wrappers in `wrappers/` and `utils/pgm_metric.h`. The full list is in `CLAUDE.md`. You may read the harness, and you may run their indexes as black boxes (as `ours/blackbox/spgm_probe.cc` does). For a new index, use `wrappers/base.h` and `wrappers/wrapper_binary_search.h` as the template.
- **Compare at equal guaranteed error.** Their sPGM at (eps, I) guarantees about eps + I − 1. Run ours tightened at eps' = eps + I − 1 against theirs at eps, or ours untightened (`SampleEBPLA::Bound::kUntightened`) against theirs at the same eps and I. Even untightened, ours is 3.1x larger than theirs at I=256 (385 KB vs 124 KB). Our 40 bytes per segment and greedy fit are the likely causes, but neither is confirmed.
- **Memory: one 200M dataset at a time, one heavy process at a time.** The harness holds about 12.2 GiB for 200M keys, 78% of the 16 GB VM. Our standalone programs load exactly one copy of the keys (1.6 GB) with our own loader, never the harness's. Our I=1 build still peaks near 7.5 GiB, because it keeps the samples and fitted points in memory.
- **Timing on this laptop is noisy.** In G0, the same config's build time varied up to 15x inside the harness. Standalone runs are steadier, but still vary up to 30%. Always do at least 3 runs, write raw rows to CSV under `results/`, and close other Windows apps.
- **Latencies aren't comparable to BASIL's yet.** The harness searches 16-byte rows, and our benchmark searches the plain 8-byte key array. We need our index inside the harness first.
- **Harness edits** so far: one `#include` added to `dtl/thread.hpp`, and the USER configs in `benchmarks/benchmark_spgm.h`. Log any new one in `NOTES.md`, and keep the tests passing on every commit.

## What's next

The next milestone is our index running inside the BASIL harness, side by side with theirs. The items below are in priority order and have no owners yet.

- [ ] **BASIL wrapper for our index** (finishes G2). Add `benchmarks/benchmark_ours.h` and two lines in `benchmark.cc`; `NOTES.md` section 4 has the exact recipe. `EqualityLookup` must return `{max(0, p − eps), min(n, p + eps + 1)}`, since `stop` is exclusive.
- [ ] **Side-by-side run at equal guaranteed error.** Run ours tightened at eps' = eps + I − 1 against their sPGM at eps=256, plus ours untightened against theirs. Use I = 1, 16, 64, 256, 3 runs each. Keep the harness at 12.2 GiB with nothing else running.
- [ ] **Explain the slow I=1 lookups.** At 200M, I=1 is about 20% slower than I=16 although the models are nearly the same size. Rerun `lookup_bench` with the configs in a different order to test whether freeing the 6.4 GB of build memory causes it.
- [ ] **Streaming build.** Fit while sampling instead of storing every sample and fitted point. Memory drops from about 6.4 GB to O(segments) at I=1, and the I=1 baseline timing stops including two 3.2 GB allocations. Do this before quoting build speedups.
- [ ] **Recursive level 2.** At I=256 the 15 MB model is larger than the 6 MB L3, and lookups slow down. Replace the binary search in `segment_directory.h` with a PLA over first keys, as in PGM.
- [ ] **Close the size gap with sPGM.** Try a smaller segment record (we use 40 B per segment) and consider an optimal PLA. Record which fit each result used.
- [ ] **Status report tonight.** The numbers are in `NOTES.md` and the tables above.

Sample EB-Histogram / CHT is out of scope today.
