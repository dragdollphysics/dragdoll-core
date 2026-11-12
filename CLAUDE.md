# DragDoll — Instructions for Claude

Open-source (MIT) active-ragdoll library in C++20 on Jolt Physics. **First goal: a GTA San Andreas mod where DragDoll handles pedestrians hit by vehicles** (see `docs/workplan.md` §0).

## Start of every session
1. Read the last entry of `docs/devlog.md` — it says what's next.
2. Check `docs/workplan.md` for the current phase and its checklist.
3. Decisions live in `docs/decisions/` (ADR-0001 … 0017). Don't re-litigate them; propose a new ADR instead.

## End of every session
Append to `docs/devlog.md`: **Changed**, **Next** (one concrete task), **Open questions**.

## Working rhythm (the user works in hyperfocus bursts)
* Estimate in effort hours, never calendar dates.
* Keep tasks small (≈ 1–6 h) and finish whole units: builds on x86 + x64, tests green, devlog updated. Never leave the build broken at a pause.
* New ideas that aren't the current task → one line in `docs/ideas.md`, then continue.
* Each phase ends with a short clip in `docs/progress/`.

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
* Format with `.clang-format` (Allman, 4 spaces, 120 columns).

## Clean-room rules (ADR-0014) — non-negotiable
* Nothing derived from Euphoria, endorphin, RAGE, or any proprietary engine or mod: no code, data, parameters, recordings, leaked material. No decompiling.
* Never use the words "Euphoria"/"NaturalMotion" in code, APIs or file names.
* Every major technique cites its published source in an ADR.
* `private-reference` data stays in `/private/` (git-ignored) and is never an optimisation target.

## Build & test
_Filled in during Phase 1 (CMake presets for x86/x64, test command, workbench run command)._

## Commits
DCO sign-off on every commit (`git commit -s`). Commit only when the user asks.
