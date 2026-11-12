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
