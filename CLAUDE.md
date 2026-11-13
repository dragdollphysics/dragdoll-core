# DragDoll — Instructions for Claude

Open-source (MIT), community-owned (ADR-0019) active-ragdoll library in C++20 on Jolt Physics, plus a **workflow to replace canned reaction animations in any game** (ADR-0018). **First goal: a GTA San Andreas mod where DragDoll handles pedestrians hit by vehicles** (see `docs/workplan.md` §0).

## Start of every session
1. Read the last entry of `docs/devlog.md` — it says what's next.
2. Check `docs/workplan.md` for the current phase and its checklist.
3. Decisions live in `docs/decisions/` (ADR-0001 … 0022). Don't re-litigate them; propose a new ADR instead.

## End of every session
Append to `docs/devlog.md`: **Changed**, **Next** (one concrete task), **Open questions**.

## Working rhythm (the user works in hyperfocus bursts)
* Estimate in effort hours, never calendar dates.
* Keep tasks small (≈ 1–6 h) and finish whole units: builds on x86 + x64, tests green, devlog updated. Never leave the build broken at a pause.
* New ideas that aren't the current task → one line in `docs/ideas.md`, then continue.
* No progress clips: the founder reviews in the live viewer (decided 2026-11-13).

## Layout (ADR-0013)
```
core/        Layer 0: Jolt wrapper, rig, motors, sensors, pose I/O, recording
capi/        dragdoll.h — C ABI (opaque handles, POD structs, versioned)
behaviours/  Layer 1: may include ONLY behaviour_sdk.h
packs/       Layer 2: JSON data (rigs/, vehicles/, presets)
workbench/   native WebGPU (Dawn) + ImGui hot-reload host
lab/         dragdoll-lab headless optimisation loop
tools/       inspect/ (recording queries), mcp/ (workbench bridge)
tests/ scenarios/ docs/
```

## Conventions
* SI units; right-handed; quaternions `(x, y, z, w)`; **up axis is a world setting** — never hardcode Y or Z (ADR-0004).
* Jolt math types in core, C POD at the ABI, no GLM (ADR-0005).
* Fixed 120 Hz step, interpolated output (ADR-0006). Deterministic: seeded RNG only, no wall-clock in simulation (ADR-0007).
* Stiffness = frequency (Hz) + damping ratio (ADR-0009). Internal torques only; assist is explicit and 0 in tests (ADR-0003).
* Jolt defines only in the `dragdoll_jolt_config` CMake target (ADR-0012).
* Must build and pass tests on **Win32 x86 and x64** (ADR-0015). CCD on limbs and vehicles (ADR-0016).
* C ABI naming: functions `dragdoll_*`, types `Dd*`, macros `DD_*`. C++ namespace `dragdoll::`.
* Format with `.clang-format` (Allman, 4 spaces, 120 columns). Code rules (errors, exceptions, memory, threads, naming): ADR-0020.

## Clean-room rules (ADR-0014) — non-negotiable
* Nothing derived from Euphoria, endorphin, RAGE, or any proprietary engine or mod: no code, data, parameters, recordings, leaked material. No decompiling.
* Never use the words "Euphoria"/"NaturalMotion" in code, APIs or file names.
* Every major technique cites its published source in an ADR.
* `private-reference` data stays in `/private/` (git-ignored) and is never an optimisation target.

## Build & test
Toolchain: Visual Studio Build Tools 2026 (MSVC 14.51) with bundled CMake + Ninja — not on PATH, so always go through the helper, which loads the VS environment for the preset's architecture:
```
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/dd.ps1 <preset> [-Step configure|build|test]
```
Presets: `x64-debug`, `x64-release`, `x86-debug`, `x86-release`, `x86-ship` (SA plugin: no dev tools), or `all` (the four dev presets). Output: `build/<preset>/bin/` (`dragdoll.dll`, `dragdoll_smoke.exe`, `dragdoll_tests.exe` — doctest; run it directly for details, e.g. `-tc="determinism*"`).
* Jolt is pinned in `cmake/DragDollJolt.cmake` (`DRAGDOLL_JOLT_TAG`); all Jolt options live only there (ADR-0012).
* x86 presets set `DRAGDOLL_PORTABLE_CPU=ON` (SSE4.2, no AVX/FMA) for players' older machines; x64 uses AVX2+FMA.

## Live viewer (human validation loop)
`powershell -NoProfile -ExecutionPolicy Bypass -File scripts/watch.ps1 <scenario> [-Build] [-Fps 30|60|144]` opens `dragdoll_view` detached (x64-release, no focus steal; closes any previous viewer first). `-List` prints scenario names. Scenarios live in `scenarios/` (C API only) and are shared with the unit tests. After building something visual, launch the relevant scenario and ask the user for a verdict ("looks fine" / "looks odd" = a real defect). Don't take screenshots to judge (costs their usage). To drive a running viewer and read exact numbers: `build/x64-release/bin/dragdoll_ctl.exe <command>` (`help` lists them; `state` = per-body positions/speeds as JSON).

## Commits
DCO sign-off on every commit (`git commit -s`). Commit only when the user asks.
