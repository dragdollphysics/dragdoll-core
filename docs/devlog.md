# Devlog

Newest entry at the bottom. Every session ends with **Changed / Next / Open questions**.

---

## 2026-11-12 — Phase 0 kickoff
**Changed**
* Created `dragdoll-core` repo skeleton (layout per ADR-0013), MIT license, `.gitignore`, `.gitattributes`, `.editorconfig`, `.clang-format`.
* Wrote ADR-0001 … 0017, `CLAUDE.md`, README with manifesto. Moved the workplan into `docs/`.

**Next**
* Then Phase 1, task 1: CMake skeleton + FetchContent Jolt (pinned) + `dragdoll_jolt_config` + x86/x64 presets.

**Open questions**
* Final behaviour vocabulary for the car-hit chain (own ADR; vogue touches welcome).
* GitHub organization created: https://github.com/dragdollphysics; `dragdoll-core` pushed over SSH.
* Toolchain found: Visual Studio Build Tools 2026 (MSVC 14.51, x86 + x64 targets) with bundled CMake and Ninja — not on PATH; Phase 1 presets must use a VS developer environment.

---

## 2026-11-12 — Phase 0 complete
**Changed**
* ADR-0018 (any-game workflow mission) and ADR-0019 (community governance) added.
* GitHub org `dragdollphysics` created; `dragdoll-core` pushed via SSH (ed25519 key, Windows ssh-agent).

**Next**
* Phase 1, task 1 (~3 h): `CMakeLists.txt` + `CMakePresets.json` (x86/x64, Debug/Release, Ninja, VS dev env), FetchContent Jolt at a pinned tag, `dragdoll_jolt_config` interface target, empty `dragdoll` library that builds on both architectures.

**Open questions**
* Final behaviour vocabulary for the car-hit chain (own ADR).
* SA install (founder's original DVD) at `C:\Games\GTA San Andreas`: `gta_sa.exe` 14,383,616 bytes, dated 2005-06-07, SHA-1 `185b73fbceaa05d66452691fc0d15c8d61b92a7e` — size matches the 1.0 US layout the modding ecosystem targets; confirm with plugin-sdk's version check in the Phase 1 spike. Clean install (no ASI loader / CLEO / modloader yet); launches fine on Windows 11. Pristine backup at `C:\Games\GTA San Andreas (clean)`.

---

## 2026-11-12 — Pre-Phase-1 retrospective
**Changed** (local, not committed)
* Six SA-readiness refinements folded into Phases 1–6 (tagged **[SA]**): activation from motion, mock 32-bone skeleton test, C-API-boundary recording with host-session replay, host-rate jitter test, guest-process budgets (64 MB, 2–4 threads, x86 perf), `failed` state with fallback.
* ADR-0015 and ADR-0017 amended; ADR-0020 (C++ conventions) added.

**Next**
* Phase 1, task 1 (~3 h): `CMakeLists.txt` + `CMakePresets.json` (x86/x64, Debug/Release, Ninja, VS dev env), FetchContent Jolt at a pinned tag, `dragdoll_jolt_config`, empty `dragdoll` library building on both architectures.

**Open questions**
* Behaviour vocabulary ADR (any time before Phase 4).

---

## 2026-11-12 — Phase 1, task 1: build system
**Changed** (local, not committed)
* Root `CMakeLists.txt`, `CMakePresets.json` (x64/x86 × debug/release + `x86-ship`), `cmake/DragDollJolt.cmake` (Jolt v5.6.0 pinned, `dragdoll_jolt_config`), `cmake/DragDollCompilerOptions.cmake` (ADR-0020 flags), `scripts/dd.ps1` (loads VS env per architecture).
* `capi/include/dragdoll/dragdoll.h`: versioning, `DdResult`, `dragdoll_init/shutdown/get_version/get_build_info/last_error_message` (`__cdecl`, noexcept boundary).
* `core/src/capi_library.cpp`: Jolt factory/type registration behind the C ABI. `dragdoll.dll` (Jolt static inside, static CRT).
* `tests/smoke/smoke.c`: plain-C smoke test (version check, mismatch rejection, double init/shutdown).
* Result: all 5 presets build and pass. x64 = avx2/avx/sse4.2/fma; x86 = sse4.2. `x86-ship` `dragdoll.dll` depends only on KERNEL32.dll (670 KB).
* Fixed: with Ninja, Jolt silently built for plain SSE2 (needs `CMAKE_VS_PLATFORM_NAME`). ADR-0012 amended.

**Next**
* Phase 1, task 2 (~3 h): GitHub Actions CI — build + test `x64-debug`, `x64-release`, `x86-debug`, `x86-release` on `windows-latest`, cache the Jolt build.

**Open questions**
* Install Ninja-independent ccache later? (not installed; optional speed-up.)

---

## 2026-11-12 — Phase 1, task 2: CI
**Changed** (local, not committed)
* `.github/workflows/ci.yml`: matrix `x64-debug`, `x64-release`, `x86-debug`, `x86-release` on `windows-latest`; MSVC env via `ilammy/msvc-dev-cmd`; Jolt `_deps` cached per preset (key = hash of `cmake/DragDollJolt.cmake` + `CMakePresets.json`); concurrency cancel; read-only token. Actions pinned to major versions (checkout v7, cache v6, msvc-dev-cmd v1).
* README CI badge.
* First run on GitHub: all 4 presets green, 68–85 s each (cold cache). Pushed as `4da6283`.

**Next**
* Then Phase 1, task 3 (~4 h, **Opus** — design-heavy): fixed-step accumulator + `DdWorld` in the C API with proxy colliders and the up-axis setting.

**Open questions**
* If the Jolt cache proves flaky (stale `_deps`), switch to ccache.

---

## 2026-11-12 — Phase 1, task 3: world, fixed-step clock, proxy colliders
**Changed** (local, not committed)
* `dragdoll.h`: `DdVec3/DdQuat/DdTransform`; `DdWorldDesc` (`struct_size`, up axis, gravity, step_hz, max_steps_per_call, worker_threads, max_bodies); world create/destroy/step/step_fixed/alpha/step count/state hash; proxy colliders (`DdColliderDesc`: sphere/box/capsule/mesh × static/kinematic/dynamic; bulk add/remove; kinematic targets; interpolated transform query).
* `core/src/world.{hpp,cpp}`: Jolt PhysicsSystem per world (2 object layers), single-threaded or thread-pool job system, 16 MB temp allocator; double accumulator with spiral-of-death guard; kinematic targets spread evenly over the batch of fixed steps (smooth velocities), velocity zeroed when the host sends none; capsule axis follows the world up; LinearCast CCD on non-static bodies (ADR-0016); all-or-nothing batch add; FNV-1a state hash in collider-id order.
* `core/src/capi_guard.hpp` + `capi_world.cpp`: shared error/guard helpers; exceptions → `DdResult`.
* Tests: doctest v2.5.3; 12 cases / 2,793 assertions — validation, accumulator, up axis (±Y, Z, X), capsule axis, **host-rate jitter determinism (Z-up, 25–30 fps)**, **thread-count determinism (0/1/4/8)**, bulk streaming (1,000 boxes), all-or-nothing batch, triangle mesh floor, kinematic targets, **car at 120 km/h vs a 5 cm "shin" pole (no tunnelling)**, interpolation.
* All 5 presets build and pass.

**Next**
* Phase 1, task 4 (~4 h, **Opus**): rig JSON + 16-body ragdoll (`RagdollSettings`, joint limits, de Leva masses, CCD), `DdCharacter` in the C API, `drop_from_1m` scenario.

**Open questions**
* Kinematic targets are reached at the end of the next batch of fixed steps; a host running faster than 120 Hz may call step with 0 steps, so the target waits for the next batch. Fine for SA (~30 fps); revisit for high-rate hosts (the founder's 144 Hz monitor can serve as a real test case for the workbench later).
* Memory accounting (Jolt allocator hooks, 64 MB budget) still open in Phase 1.
