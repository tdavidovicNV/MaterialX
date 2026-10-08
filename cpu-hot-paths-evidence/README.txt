MaterialX CPU hot paths: standalone reproduction

All four candidates remain on upstream main d07921060b8f44cd2eac963b08c608d7fdb9fec6
(MaterialX 1.39.6 development), fetched on 2026-10-08.
The original 1.39.5 consumer percentages are not used as benchmark savings.

Production changes are on perf/cpu-hot-paths, combined commit
3806b06b7a7813f257945d563869b9dbc27bbddd. Four independent commits:
  68f45c29: set lookup for reserved shader names
  b5f70493: single-target fast path and early intersection exit
  b53edbd2: omit duplicate unqualified candidate queries
  3806b06b: ordinary non-inherited input/output enumeration fast paths

This evidence branch is for reproduction only. It is not part of the upstream
code PR or its normal test suite. No Falcor2 dependency, GPU or Python is needed.

Environment and experiment contract

Windows 11, Intel Core Ultra 9 285K, 24 cores / 24 logical processors.
Visual Studio 2022, MSVC 19.38.33145, x64 Release static libraries.
C++ compiler flags: /O2 /Ob2 /DNDEBUG /Z7
Executable linker flags: /DEBUG:FULL
All five shader generators enabled; renderer, Python and benchmarks tests disabled.
Native correctness tests enabled. GPU execution and shader compilation are not
part of this CPU study.

Changed factor: one source optimization at a time against exactly d0792106.
The unchanged harness and toolchain are linked separately with each variant.
No cross-material caches, shader semantic changes or validation changes.

Timing: steady_clock wall nanoseconds per operation, single benchmark thread,
three untimed warmup operations per workload, eight paired samples per variant.
Baseline/candidate order alternates AB/BA; candidate order rotates per round.
Each sample uses a fresh process, 100000 iterations per microbenchmark and 150
iterations per material-generation case. No CPU affinity or clock locking.
System activity, frequency scaling and OS file cache are uncontrolled; source
files are warm in practice after warmup. Samples are reported individually in
timings.csv; summary.csv gives medians and the full range of paired reductions.
Positive reduction means less time: 100 * (1 - candidate / paired baseline).
Small generation changes with ranges crossing zero are treated as inconclusive.

Microbenchmarks include ordinary/reserved GLSL names; single target hit/miss;
a three-target list; standard_surface node-definition and implementation lookup;
16 non-inherited inputs and one non-inherited output. They measure complete API
calls, including returned containers. The harness consumes results in a checksum.
They are not estimates of consumer application speedups.

Generation repeatedly constructs a fresh document, a standard_surface or
open_pbr_surface node and a material node, then generates vertex and pixel GLSL.
The generator is reused, but GenContext is new on every request. The context's
implementation cache is therefore fresh. Standard-library loading is outside
timing, as is generator construction. Timed work includes document/node/context
construction and destruction, type registration, generation, shader source reads,
and result disposal. Libraries use an attached read-only data library. Three
warmups initialize library lookup tables. No cache-flush or cold-cache claim.

Correctness

The focused checks pass on baseline, every isolated variant and the combined
version: 207 assertions in six test cases. They cover reserved/ordinary/invalid
names; a 12x12 target matrix (wildcards, duplicates, empty token lists, separator
edges, tabs/newlines); qualified lookup precedence/fallback including already
qualified names; versions, output types, exact/rough inputs; target inheritance,
generic fallback and linked graph resolution; inherited input/output overrides,
ordering, missing/wrong-category/empty inheritance, cycles, and library-first
document deduplication. A non-document element with an attached library is also
covered. Existing native tests cover the broader APIs.

Combined full configured suite: 97 cases, 26844 assertions, all passed.
The suite includes GLSL, Slang, OSL, MDL and MSL generation; renderer disabled.
Both GLSL stages of both standalone materials are byte-identical to baseline
for every isolated candidate and the combined build. shader-hashes.csv records
the SHA256 values. No numeric tolerance or source normalization was used.

Semantic decisions

Reserved words: use std::set::find without changing name sanitization or suffixes.
Targets: only empty raw strings are wildcards. The actual separator set is comma
and space. Raw equality is used only when neither nonempty string has separators.
For lists, retain one membership set and stop at the first match; separator-only
strings remain empty lists. This avoids a quadratic nested token comparison.
Lookup: compute the qualified key once. Append plain candidates only if the keys
differ. Selection order and all matching logic remain unchanged.
Interfaces: only non-document elements without an inherit attribute return their
ordinary children directly. All documents retain merging because attached library
enumeration can contain duplicate names. Explicit inheritance (even empty or
invalid) retains existing traversal and cycle behavior. No cached mutable state.

Reproduction (PowerShell)

Use a clean, isolated checkout of the combined production commit and a separate
checkout of the evidence branch. From a clone containing the fork refs:

  git fetch origin perf/cpu-hot-paths perf/cpu-hot-paths-evidence
  git worktree add ../mx-cpu-code 3806b06b7a7813f257945d563869b9dbc27bbddd
  git worktree add ../mx-cpu-evidence origin/perf/cpu-hot-paths-evidence
  $source = (Resolve-Path ../mx-cpu-code).Path
  $evidence = (Resolve-Path ../mx-cpu-evidence/cpu-hot-paths-evidence).Path
  powershell -NoProfile -ExecutionPolicy Bypass -File "$evidence/build-variants.ps1" -Source $source -CMake cmake
  powershell -NoProfile -ExecutionPolicy Bypass -File "$evidence/measure.ps1" -Source $source
  powershell -NoProfile -ExecutionPolicy Bypass -File "$evidence/summarize.ps1"
  Push-Location $source
  ./build/bin/Release/MaterialXTest.exe
  Pop-Location

Pass an absolute CMake executable path if it is not on PATH. CMake >=3.26 and
Visual Studio 2022 C++ tools are required. Run configure/build and native tests
outside the sandbox. The build script temporarily replaces only the five source
files in the clean task checkout to form baseline and isolated variants, then
restores the combined source in a finally block. All regression tests remain
present in every variant. Binary snapshots and source patches are saved next to
the evidence scripts. Do not edit this checkout concurrently.

To configure manually (the script performs these same steps):
  cmake -S $source -B "$source/build" -G "Visual Studio 17 2022" -A x64 `
    -DMATERIALX_BUILD_TESTS=ON -DMATERIALX_BUILD_RENDER=OFF `
    "-DCMAKE_CXX_FLAGS_RELEASE=/O2 /Ob2 /DNDEBUG /Z7" `
    "-DCMAKE_EXE_LINKER_FLAGS_RELEASE=/DEBUG:FULL" `
    "-DCMAKE_PROJECT_MaterialX_INCLUDE=$evidence/harness.cmake"
  cmake --build "$source/build" --config Release --parallel 12 --target CpuHotPaths MaterialXTest

Individual runs:
  & "$evidence/baseline/CpuHotPaths.exe" $source micro 100000
  & "$evidence/baseline/CpuHotPaths.exe" $source generate 150
  & "$evidence/baseline/CpuHotPaths.exe" $source dump "$evidence/baseline/shaders"

VTune 2026.4.0 (build 632893), software sampling with user call stacks:
  vtune -collect hotspots -knob sampling-mode=sw -result-dir "$evidence/vtune-baseline" -- "$evidence/baseline/CpuHotPaths.exe" $source generate 1500
  vtune -report top-down -r "$evidence/vtune-baseline" -limit 2000 -format csv -report-output "$evidence/vtune-baseline-stacks.csv"

The same profile is collected separately for the combined variant. These runs
are excluded from the timing table. The baseline call tree contains all four
reported hot paths. Fresh-context generation also spends substantial CPU in
source-file I/O; the profile is not an estimate of consumer-workload savings.
System DLL symbols and hardware performance counters were unavailable, but the
MaterialX executable symbols and software-sampled stacks were resolved.

Review grouping

One upstream PR with four independent commits keeps this small, related CPU
cleanup together while allowing any commit to be reviewed or dropped separately.
Each commit has focused correctness coverage, and every performance variant uses
the same upstream baseline. The evidence branch holds the optional harness and
raw results so normal CI does not acquire an expensive benchmark suite.
