# Phase 1 retrospective — Core foundation + SA spike

Date: 2026-11-12. Planned ~60 h; the founder's burst sessions made it a single long day.

## What shipped
* Build system (CMake presets x64/x86 × debug/release + `x86-ship`), Jolt v5.6.0 pinned, CI (4 jobs, ~70-135 s, green).
* C API: world + fixed 120 Hz clock + interpolation, proxy colliders (bulk), time-stamped kinematic targets, characters from rigs, impulses, debug draw, memory stats.
* `humanoid16` rig (de Leva masses, anatomical limits) → limp ragdoll, human-reviewed.
* Live viewer (raylib) + shared scenario library → the human-validation loop works.
* **SA spike: DragDoll runs inside GTA San Andreas (HoodLum 1.0 US)** — load, read skeleton, drop, shove; ~15 min across 3 sessions without a crash.
* Memory accounting (25 MB peak for 8 active ragdolls + level data, budget 64) and a perf baseline (~70 µs per ragdoll per step).

## What went well
* Doing the 32-bit build + SA spike in Phase 1 retired the biggest risk early; nothing in the core had to change for it except small API additions (`DD_DRAW_SKIP_STATIC`).
* Shared scenarios (tests check numbers, viewer shows looks) and the human-eye loop found real defects tests missed (arm through chest, floor drawn over the game).
* Determinism tests across thread counts and host frame rates (25-200 fps) caught the kinematic-vehicle frame-rate flaw before it mattered.

## What went badly / surprises
* Environment friction (windowed mode, ASI loader, dxwrapper, mouse look) cost more time than any core work. **Mouse look in the windowed setup is still broken** (known issue).
* The benchmark showed 8 *awake* ragdolls cost ~2.2 ms/frame at 30 fps — over the 1 ms criterion. Not a blocker; tiers + budget manager are planned (Phase 6).
* Several plan details turned out to be wrong or incomplete when met in practice (up-axis consistency in the viewer, `EnableD3d9Wrapper`, Jolt penetration slop 2 cm vs 4 cm hands).

## Decisions taken since the plan (ADRs)
* ADR-0018 mission, 0019 governance, 0020 C++ conventions (before Phase 1).
* **ADR-0021 (new): the workbench starts on raylib** (+ Dear ImGui via rlImGui when panels are needed); Dawn/WebGPU is deferred to the web playground (post-v1). See `docs/decisions/0021-workbench-raylib.md`.

## Plan changes for Phase 2
* Renderer: raylib (ADR-0021) instead of Dawn.
* Order: recording/replay first (everything else builds on it), then inspect + annotations, ImGui panels, hot reload, SA session replay, vehicle pack, Tracy, MCP bridge.
* SA mouse-look issue and the 1 ms frame budget are tracked, not blocking.
