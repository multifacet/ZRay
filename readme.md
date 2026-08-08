# ZRay

**Portable compiler-assisted memory traffic characterization.**

This is part of the research artifact for the IISWC 2026 paper *ZRay: Portable Compiler-Assisted
Memory Traffic Characterization*.

---

## Requirements

| | |
|---|---|
| OS | Linux |
| LLVM | **15** — `llvm-15-dev` and `clang-15`. A stock upstream LLVM is sufficient. |
| Build | `make`, a C++14 host compiler |
| Optional | `perf`/PMU access (`-DUSE_HW_PERF_COUNTERS`) |

On Debian/Ubuntu:

```sh
sudo apt install llvm-15-dev clang-15
```
---

## Build

```sh
git clone https://github.com/multifacet/ZRay/ zray && cd zray
./setup.sh                       # or: LLVM_BIN=/path/to/llvm/bin ./setup.sh
```

`setup.sh` locates LLVM, sources `setupEnv.sh`, and builds into `./bin`:

| Artifact | |
|---|---|
| `bin/libzray.so` | the ZRay LLVM IR pass |
| `bin/zray_runtime.ll` | the ZRay runtime, linked into instrumented programs |
| `bin/zray_post_process` | offline reader for host-mode logs |
| `bin/libzray_noinline.so` | helper pass (marks ROI functions `noinline`) |

To build by hand instead:

```sh
export LLVM_BIN=/usr/lib/llvm-15/bin
. ./setupEnv.sh
make zray
```

---

## Quick start

Mark a region of interest (ROI), then build it through the four-step pipeline:

```c
// kernel.cc
#include "zray.h"
#include <cstdlib>

void kernel(double *a, double *b, int n) {
    ZRAY_BEGIN(15); // Will create ROI with label 15.
    for (int i = 0; i < n; i++)
        a[i] = b[i] * 2.0;
    ZRAY_END(15);
}

int main() {
    const int n = 100000;
    double *a = (double *)malloc(n * sizeof(double));
    double *b = (double *)malloc(n * sizeof(double));
    for (int i = 0; i < n; i++) b[i] = i;
    kernel(a, b, n);
    return a[n - 1] == 0.0;
}
```

```sh
. ./setupEnv.sh
export ZRAY_LOGFILE=$PWD/run.zlog        # required: where the pass writes region metadata

# 1. source -> LLVM IR
$CUSTOM_CC -o tmp.ll kernel.cc -std=c++14 -O2 -S -emit-llvm \
    -Xclang -disable-O0-optnone -I./include

# 2. run the ZRay pass
$CUSTOM_OPT -enable-new-pm=0 -O2 -mem2reg -load ./bin/libzray.so -zray -S \
    < tmp.ll > instrumented.ll

# 3. link the ZRay runtime in
$CUSTOM_LINK -o linked.ll instrumented.ll ./bin/zray_runtime.ll

# 4. assemble
$CUSTOM_CC -o kernel linked.ll -std=c++14
```

Run it, and ZRay writes `zray_application_stats.csv` next to the binary:

```sh
./kernel
```

The CSV has two `Region` rows here. At `-O2` clang inlines `kernel` into `main`, so the
work is reported under **`main`**:

```
Function            Loads    Stores   Read Bytes   Written Bytes
main                99992    49996    1199904      799936
_Z6kernelPdS_i          1         1          8            8
```

`Written Bytes` matches the expected 100000 × 8 = 800 KB. `Stores` is about half the
iteration count because the loop vectorized, two doubles per store instruction.

A worked example lives in `examples/` — `make -C examples` builds `bin/mem`.

---

## Marking regions

Include `zray.h` and wrap the code you care about:

```c
ZRAY_BEGIN(group_id);
  ...
ZRAY_END(group_id);
```

Both macros expand to a volatile inline asm comment (`#ZRAY_ROI_BEGIN <id>`).
the IR pass scans for.

The **group ID** labels a region. Regions may share an ID to form a group.

---

## Configuration

### Environment variables

| Variable | | |
|---|---|---|
| `ZRAY_LOGFILE` | **required** | Path where the pass writes region metadata and the runtime reads it back. |
| `ZRAY_SAMPLE_RATE` | default `1` | Instrument every *N*th region execution. `1` = every execution; `100` = every hundredth; `0` = disable. Higher values cut overhead; see the paper's sampling study. |
| `ZRAY_BIN_PATH` | set by `setupEnv.sh` | Output directory for build artifacts. |
| `LLVM_BIN` | default `/usr/lib/llvm-15/bin` | Which LLVM install to build against. |

`ZRAY_INST` and `ZRAY_PATCH_ID` are read by the runtime but are leftovers from an earlier
XRay-based design; the handlers they configured are commented out. Treat them as
non-functional.

### Pass options

Passed to `opt` after `-zray`:

| Option | Default | |
|---|---|---|
| `--postdomset` | `true` | Share one counter across blocks that provably execute equally often (post-dominator sets). |
| `--loophoist` | `true` | Hoist a counter out of a loop and multiply by its trip count instead of counting each iteration. |
| `--functionclone` | `true` | Clone functions called from a region so their traffic is attributed to that region. |
| `--full-scan` | `false` | Instrument every function, ignoring region markers. |
| `--hostmonitor` | `false` | Enable host-thread monitoring. |

---

## Output

`zray_application_stats.csv`, one row per region entry per thread iteration:

| Columns | |
|---|---|
| `App name`, `Thread Iter`, `Entry type`, `Region`, `Group ID`, `Counter Index`, `Function` | identity of the row |
| `Time Elapsed (ns)` | wall time in the region |
| `Total Insns`, `Counter Insns`, `ZRay Counters Incremented` | instruction totals and instrumentation cost |
| `Estimated Load BW (MB/s)`, `Estimated Store BW (MB/s)` | achieved data rates |
| `Read Bytes`, `Written Bytes` | traffic volume |
| `Loads`, `Stores`, `Int Insns`, `FP Insns`, `Cast Inst` | instruction mix |
| `Global/Stack/Heap Read`, `Global/Stack/Heap Write` | traffic split by memory class |
| `Intrinsic Load`, `Instrinsic Store` | traffic from intrinsics (`memcpy`, vector ops) |

`Entry type` distinguishes two kinds of row: `Basic Block` rows give per-block detail, and
`Region` rows give the summary for a whole region. Start with the `Region` rows.

`NA` appears where a quantity was not measured — for example timing on a per-basic-block
row, where only whole-region timing is meaningful.

---

## How it works

```
source (ZRAY_BEGIN/END)
   │  clang -S -emit-llvm
   ▼
LLVM IR ── opt -zray ──▶ instrumented IR ── llvm-link zray_runtime.ll ──▶ binary
   │                                                                          │
   │  writes region metadata                                        runtime counters
   ▼                                                                          ▼
$ZRAY_LOGFILE ─────────────────── combined at exit ────────────▶ stats CSV
```

The pass finds region markers, builds a post-dominator tree over each region's blocks, and
places the smallest set of counters that still lets every block's execution count be
recovered. For each block it records the static instruction mix. At exit, the runtime
multiplies recorded counts by that mix and writes the CSV.

Source layout:

| | |
|---|---|
| `src/zray_pass.cc` | pass entry, region discovery, instrumentation driver |
| `src/zray_parse.cc` | IR parsing, per-block static instruction mix |
| `src/zray_loop.cc` | loop analysis, trip counts, hoisting |
| `src/zray_codegen.cc` | emits counter increments and timing events |
| `src/zray_dyn.cc` | runtime: counters, timing, sampling, CSV output |
| `src/post_process.cc` | offline log reader |
| `include/zray.h` | `ZRAY_BEGIN` / `ZRAY_END` |

---

## Repository layout

| | |
|---|---|
| `src/`, `include/` | the pass and runtime |
| `examples/` | small programs demonstrating region marking |
| `scripts/`, `util/` | experiment and analysis helpers |
| `spec-cfg/` | SPEC CPU 2017 config (SPEC itself is not redistributable) |

---

## Citation

If you use ZRay, please cite the IISWC 2026 paper. See `CITATION.cff`.
