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
* ~~Kinematic targets wait a batch at > 120 Hz~~ → solved in task 3b.
* Memory accounting (Jolt allocator hooks, 64 MB budget) still open in Phase 1.

---

## 2026-11-12 — Phase 1, task 3b: frame-rate independent vehicles
**Why:** 60 fps mods are common in the SA community. With "reach the target at the end of this frame's batch", vehicle speed per fixed step varied with the number of steps a frame got: ±25 % at 30 fps (3–5 steps), up to ±50 % at 60 fps (1–3 steps), worse at 144 fps (0–1 steps). Impact impulses would have been noise.

**Changed** (local, not committed)
* Kinematic targets are now **time-stamped** with host time (defined as simulated time + accumulator, so hitches drop time from both clocks). Each collider keeps a short trajectory (≤ 16 samples); every fixed step samples it at its exact end time (lerp/slerp), and `MoveKinematic` derives the velocity. ≤ 1 fixed step (8.3 ms) latency. No new targets = holds last pose.
* `dragdoll.h` documents the semantics, including "teleport = remove + re-add".
* Tests: host-rate determinism now covers SA vanilla 25–30, 60 fps (55–65), 144 Hz (138–150) and unlocked 30–200 fps; new test: vehicle at 50 km/h has the same speed (±1 %) on every fixed step for 30 / 60 / 144 / unlocked hosts; hold-last-pose test. 14 cases / 13,035 assertions, all 5 presets green.
* Workplan Phase 6: feed the game's real frame time; test vanilla / SilentPatch 60 fps / unlocked.

**Next**
* Phase 1, task 4 (~4 h, **Opus**): rig JSON + 16-body ragdoll, `DdCharacter`, `drop_from_1m`.

**Open questions**
* Teleport API (explicit `dragdoll_world_teleport_colliders`) if remove + re-add proves awkward for the SA plugin.

---

## 2026-11-12 — Phase 1, task 3c: live viewer + shared scenarios
**Why:** the user wants to watch scenarios live and act as the human-eye validation layer.

**Changed** (local, not committed)
* C API: `dragdoll_world_debug_draw` (line/triangle callbacks over Jolt's DebugRenderer; dev builds only, ship builds return `DD_ERROR_UNSUPPORTED`); new `DD_ERROR_UNSUPPORTED`.
* `scenarios/` library (C API only): `pile_zup`, `ball_drop_yup/zup`, `car_vs_pole_50kmh/120kmh`, `car_speed_50kmh`; `Scenario` interface (configure/build/hostFrame/hud/rateIndependent) + `Instance` helper.
* `workbench/` → `dragdoll_view` (raylib 6.2, x64 dev builds): orbit camera with arbitrary up axis, flat-shaded debug geometry, grid, HUD (step, sim time, host frame dt, fixed steps taken, alpha, state hash, **steady-120 Hz reference-run match** for rate-independent scenarios, scenario readouts). Keys: Space pause, N step, Up/Down time scale, **F cycle host rate (real / 30 / 60 / 144 / random 30-200)**, R restart, Tab next scenario, H help.
* `scripts/watch.ps1`; CI configures with `-DDRAGDOLL_BUILD_VIEWER=OFF`.
* Tests: `test_scenarios.cpp` — every scenario runs; rate-independent scenarios identical at SA/60/144/unlocked hosts; ball rest height (Y/Z up); car-vs-pole outcome (hit, not tunnelled, same at 30/60/144 fps within 0.75 m); debug-draw. 19 cases / 22.8k assertions, all presets green.
* Scenario fix found by tests: the original car had 5 cm ground clearance and pinched the pole; now 20 cm, pole is 1 m tall.

**Known limits**
* Debug draw shows the latest simulated state (no interpolation yet): at 144 Hz vs 120 Hz physics expect a little judder — expected, to be fixed by interpolating drawn bodies.

**Next**
* Human review of the viewer by the founder (verdicts feed the next fixes).
* Then Phase 1, task 4 (~4 h, **Opus**): rig JSON + 16-body ragdoll, `DdCharacter`, `drop_from_1m`.

---

## 2026-11-12 — Phase 1, task 4: the 16-body ragdoll
**Changed** (local, not committed — tasks 3b, 3c and 4 are all still uncommitted)
* `packs/rigs/humanoid16.json`: 1.75 m / 75 kg adult; de Leva (1996) segment mass fractions; rig space +Y up, +Z forward, +X = character's left, origin between the feet. Boxes for pelvis/spine/hands/feet, capsules for head and limbs. Swing-twist joints (spine, neck, shoulders, wrists, hips, ankles) with asymmetric ranges via a tilted cone axis (shoulder 41 deg forward/out, hip 40 deg forward); hinges for elbows (-3..145) and knees (-3..150).
* Core: `rig.{hpp,cpp}` (nlohmann/json v3.12.0, strict validation with precise messages), `character.{hpp,cpp}` (Jolt RagdollSettings: bone-aligned capsule frames, world-space constraints, DisableParentChildCollisions with rest-pose matrices, Stabilize, LinearCast, 16 velocity / 8 position solver iterations), built-in rig generated from the JSON by CMake.
* World: characters (id-ordered), capture for interpolation, included in the state hash, removed before the physics system on teardown.
* C API: `dragdoll_get_builtin_rig`, `DdCharacterDesc`, character create/destroy/body count/name/find/mass/transforms/apply_impulse; `DdDebugDrawCallbacks.flags` (`DD_DRAW_SHAPES / CONSTRAINTS / CONSTRAINT_LIMITS`).
* Scenarios: `drop_from_1m`, `fall_forward`, `shove_mid_fall` (Z-up), HUD shows mass, pelvis height, knee/elbow flex. `metrics::hingeBendDegrees` shared by tests and HUD. Viewer: **L** toggles the joint-limit overlay.
* Tests (24 cases / ~41.6k assertions, all 5 presets): rig loads (16 bodies, 75 kg, rest pose sane, left = +X), invalid rigs rejected with precise messages, the three scenarios never NaN / explode / sink, knees and elbows never bend the wrong way by more than ~1.4 deg past the -3 deg limit (measured worst: -4.4 deg), bodies come to rest, 0 vs 4 threads identical (two ragdolls piling up), impulses, destroy mid-simulation. Rate-independence test now also covers the ragdoll drops.
* Found & fixed while testing: wrong-way elbow bend reached -8.2 deg on x86-debug with 12/4 solver iterations; 16/8 brings all platforms to about -4.4 deg.

**Next**
* Founder's visual review of `drop_from_1m`, `fall_forward`, `shove_mid_fall` (and the L overlay).
* Then Phase 1, task 5: memory accounting (Jolt allocator hooks, 64 MB budget) + perf baseline; or the SA spike.

**Human review (founder):** proportions, falls and joint directions look right. Defect found: in `shove_mid_fall` the character lands on its arm and the upper arm sinks into the chest box.

**Fixes after review**
* Rig option `"collide_with_parent": true` (upper arms vs chest); chest box narrowed to 0.30 m so the hanging arm has 17 mm clearance at rest. Measured deepest arm-in-chest: 1.8 mm (was visibly deep). New test asserts < 2 cm.
* While testing it, a hand reached 2 cm below the floor: Jolt's default penetration slop (2 cm) is larger than a hand's half thickness. World now uses 5 mm. Lowest hand centre 19.2 mm (half thickness 20 mm); feet sink at most ~8 mm for one frame on landing.
* L overlay: limit gizmos 12 cm (were 1 m), drawn 50 % transparent in a separate pass. Ground slab hidden when the camera is below the floor (grid stays).

---

## 2026-11-12 — Pause: SA test environment ready (usage limit)
**Changed**
* Founder's SA install prepared (details in Claude memory "sa-test-environment"): HOODLUM 1.0 US exe, Ultimate ASI Loader as `dinput8.dll`, dxwrapper windowed mode (`EnableD3d9Wrapper=1` was the key), intros renamed to `.bak`.
* Founder said **"go"** for the SA test: OK to write `dragdoll_sa.asi`, the x86 `dragdoll.dll` and a log into `C:\Games\GTA San Andreas`, and to create `dragdoll-sa/` locally (next to `dragdoll-core`).

**Next (resume here, Opus)** — Phase 1 SA spike, in order:
1. Check plugin-sdk (DK22Pac): license vs MIT, how to build it for Win32 with our CMake, HOODLUM 1.0 US support. If unsuitable, report before choosing another route.
2. `dragdoll-sa/`: git init, CMake (Win32 only, static CRT), links the x86 `dragdoll.dll` from `dragdoll-core` (x86-release or x86-ship preset).
3. Load test: on load write `dragdoll_sa.log` (build info, detected game version), init/shutdown DragDoll.
4. Read test: every second log player position + RpHAnim bone matrices.
5. Drop test: key press spawns a limp humanoid16 at the player, flat ground proxy at the feet, drawn in-game via debug draw.
6. 10-minute stability run by the founder.

**Open questions**
* Does SA keep running when the window loses focus (dxwrapper `HideWindowFocusChanges=1`)? If not, add a keep-running fix to `dragdoll_sa.asi`.

---

## 2026-11-12 — Phase 1 SA spike: DragDoll runs inside GTA San Andreas
**Changed** (local, not committed)
* New repo `dragdoll-sa/` (sibling of dragdoll-core): Win32-only CMake, plugin-sdk (zlib) pinned at `15f15b6` and built from its sources (mirrors its premake settings), dragdoll-core added as a subdirectory (portable CPU, dev tools on), `dragdoll_sa.asi` delay-loads `dragdoll.dll` from its own folder (`scripts\`, nothing in the game root). `scripts/build.ps1`, `scripts/deploy.ps1` (game folder from `-GameDir` / `DRAGDOLL_SA_GAME_DIR`).
* dragdoll-core: `DD_DRAW_SKIP_STATIC` debug-draw flag.
* Results on the founder's HOODLUM 1.0 US install (3 sessions, ~4, ~7 and ~4 min, 19 drops, all clean shutdowns): load test OK (game version detected), read test OK (32-bone RpHAnim skeleton, world-space bone matrices, head 0.58 m above pelvis), drop test OK (F7 limp ragdoll on a flat floor proxy at the found ground height, drawn with RwIm3D), F8 shove OK.
* Fixed during the spike: render states leaked (now saved/restored); the floor proxy was drawn over the game's ground (now skipped); the game recentred/confined the mouse while in the background (IAT hooks on the game's `SetCursorPos`/`ClipCursor`, active only while a game window is in the foreground); the log now appends across runs.

**Known issues**
* Mouse look doesn't work in the windowed setup (menu cursor does). Independent of `HideWindowFocusChanges` (tested 0 and 1; restored to 1 so the game keeps running in the background). Likely the windowed setup vs the game's DirectInput mouse. Not needed for the spike; investigate before Phase 6.
* One hang on the very first launch (log of that run lost); not reproduced since. Suspect a first-time antivirus scan of the new DLLs.
* Debug colours look pale/over-bright in game (unlit vertex colours); cosmetic.

**Next**
* Commit dragdoll-sa + core change; push dragdoll-core; create `dragdollphysics/dragdoll-sa` on GitHub when the founder wants.
* Remaining Phase 1: memory accounting (64 MB budget) + perf baseline (x86/x64).

---

## 2026-11-12 — Phase 1, task 5: memory accounting + performance baseline
**Changed** (local, not committed)
* `core/src/memory_stats.{hpp,cpp}`: counting allocator (16-byte header with size + alignment, atomics, installed once in `dragdoll_init`); C API `dragdoll_get_memory_stats`, `dragdoll_reset_peak_memory`.
* `tests/unit/test_memory.cpp`: stats validation; 8 ragdolls + 400 static boxes + 7,200-triangle mesh = **25.0 MB peak** (limit 64), everything returned after world destroy.
* `tools/bench` (`dragdoll_bench`): N active ragdolls (staggered drops, neighbours collide, gentle shoves keep them awake), mean/p95/worst step time, per-character cost, heap peak. `bench_budget` ctest in Release builds: budget 100 us (x64) / 120 us (x86) per character per step; CI configures `DRAGDOLL_BENCH_SLACK=3` for noisy shared runners.
* All 5 presets pass (26 unit cases / 41.6k assertions + smoke + bench).

**Measurements** (Ryzen 9 3900X, single thread unless noted, per 120 Hz fixed step)
| ragdolls | x64 | x86 |
|---|---|---|
| 1 | 64 us | 74 us |
| 8 | 556 us (2 threads: 395 us) | 609 us |
| 20 | 1445 us (2 threads: 821 us) | 1625 us |
Per character ~70 us (x64) / ~76-81 us (x86). Heap ~24 MB (16 MB of it is the Jolt temp allocator).

**Finding / decision needed — SA ship criterion**
"≤ 8 simultaneous ragdolls, < 1 ms added CPU per frame" does not hold for 8 *awake* ragdolls at 120 Hz: SA at 30 fps = 4 steps/frame → 8 × 70 us × 4 = 2.2 ms (1.6 ms with 2 threads); at 60 fps (2 steps) 1.1 ms. Caveats: ragdolls go to sleep when they settle (the benchmark keeps them awake on purpose), and a typical car-hit scene has 1-3 awake bodies. Options: (a) restate the criterion per 120 Hz step / "typical scene", (b) allow a 60 Hz mode for SA (ADR-0006 permits 60-240; check stiffness), (c) Phase 5 optimisation pass (solver iterations 16/8 are generous; per-body motion-quality, simpler contact for far ragdolls), (d) LOD: distance-based deactivation. Decide before Phase 6.

**Next**
* Phase 1 is feature-complete: only the founder's review of the spike remains (done) → write the Phase 1 retrospective, then Phase 2 (workbench: ImGui/Dawn decision, hot reload, recordings).
* Optional quick win for the SA plugin: log `dragdoll_get_memory_stats` once per second in `dragdoll_sa.log`.

---

## 2026-11-12 — Idea (founder): outcome-aware ragdoll tiers
Crowd question (car through the pier) → cost is linear in awake ragdolls (~70 us each per step). Founder's optimisation: a ped that is **killed** by the hit needs no muscles (limp, cheap, can settle and freeze), only a ped that **survives** needs protective reactions and a get-up. Added to workplan Phase 6 as "Outcome tiers + ragdoll budget manager"; API gets an outcome hint at activation (D17 already has the `dead` state). To measure: cost of limp vs reduced solver iterations vs full active.

---

## 2026-11-12 — Phase 2, task 2.1: recordings & replay
**Changed** (local, not committed)
* `DdWorldDesc.record_path`: a world records every changing call at the C ABI boundary (step + steps taken + state hash after, step_fixed, add/remove colliders incl. mesh data, kinematic targets, character create (rig JSON) / destroy, impulses, notes) to a `.ddrec` file (`core/src/recording.{hpp,cpp}`; header = magic, format v1, API version, build info; records = type + size + payload; UTF-8 paths).
* Replay (`core/src/capi_replay.cpp`): `dragdoll_replay_open / get_info / advance / get_world / close`. Rebuilds the world, re-applies calls host frame by host frame, checks the state hash every frame, reports the first divergence and keeps going; collider/character ids must match; truncated/corrupt files are rejected. `DdReplayFrame.host_dt` lets viewers play back at the recorded pace.
* `dragdoll_world_record_note` / `is_recording`. Scenario `Instance::make(..., recordPath)`.
* Viewer: `--record <file>` (auto-restart off, F9 adds a mark) and `--replay <file>` (recorded pace, pause / N next frame, R restart, divergence + notes in the HUD). `watch.ps1 -Record / -Replay`.
* Tests: all 9 scenarios recorded with a jittery 25-200 fps host replay bit-identically (every frame), notes arrive with the right frame, a tampered hash is reported at frame 120, junk/truncated files and an uncreatable path fail cleanly. 29 cases, all 5 presets green.

**Known limit**
* Bit-identical only with the same binary (ADR-0007). An SA session recorded by the x86 game build and replayed in the x64 viewer will run but diverge; decide in task 2.5 (options: viewer replays through the x86 DLL via a helper, or `JPH_CROSS_PLATFORM_DETERMINISTIC` in dev builds).

**Next**
* Founder: try record → replay in the viewer.
* Task 2.2 (Sonnet): text notes + screenshots on F9, `dragdoll-inspect` CLI.

---

## 2026-11-12 — Founder review via recording: twisting lower spine → passive joint friction
**Report:** "the yellow box at the torso keeps moving after the rest of the character is almost still" (F9 mark in `shove_mid_fall`, saved as `build/shove_founder_torso.ddrec`).
**Diagnosis with `dragdoll_inspect --motion / --body`:** `lower_spine` spins in place at ~100 deg/s (1.5-1.8 s) and creeps at ~3 deg/s until 2.2 s while its neighbours are below 5 deg/s; it hangs just above the floor between pelvis and chest, and the limp joints had zero friction, so its twist was undamped.
**Fix:** rig joints get optional `"friction"` (N*m, passive tissue resistance) → Jolt `mMaxFrictionTorque` (scaled with mass). humanoid16 values: spine 4, neck 0.8, shoulder 1, elbow 0.4, wrist 0.1, hip 3, knee 1, ankle 0.3 — each well below the gravity torque on its limb, so the body still falls limp.
**New test:** no body spins in place (> 30 deg/s, < 2 cm/s) while the rest is still (< 10 deg/s), 3 ragdoll scenarios x 30/60/144 fps. Before the fix it failed with 79-108 deg/s (`lower_spine`); after: < 7 deg/s everywhere. (A frictionless control test was tried and removed: the frictionless outcome is chaotic per build, so it was flaky.)
**New tools:** `dragdoll_inspect` (`tools/inspect`): summary + notes, `--motion` (fastest bodies around each note and over the last second), `--body NAME` (speed trace). C API: `dragdoll_world_get_character_count / get_character`.
**Viewer:** replays stop on their last frame (A loops), restart clears old notes, clearer HUD wording.

---

## 2026-11-12 — Phase 2, task 2.2: F9 notes with text + screenshot
**Changed** (local, not committed)
* Viewer (record mode): F9 freezes the sim and opens a note box; Enter records `<text> | shot=<file>` as a `.ddrec` note and saves `<recording>_step<N>.png` (scene as annotated, before the box is drawn); Esc cancels; empty text = plain "F9 mark" note. Hotkeys are off while typing; Esc quits the viewer only outside the note box (exit key moved into the loop condition).
* `dragdoll_inspect` shows the note text and the screenshot path (flags a missing file).
* Founder test: note + screenshot saved and read back via inspect. 4 dev presets green.

**Known quirk:** the note's replay time is the end of the host frame it was applied in, up to a few steps after the step in the screenshot name (264 vs step 219 in the test = same host frame batch); the file name carries the exact step.

**Next**
* Task 2.3 (Opus): ImGui panels via rlImGui (scenario list, parameter sliders, body inspector, click-impulse).

**Open questions**
* Commit 44fa192 was pushed without the DCO sign-off CLAUDE.md requires (`git commit -s`); leave it or rewrite with a force-push? Use `-s` from now on.

---

## 2026-11-12 — Phase 2, task 2.3 (first slice): ImGui panels in the viewer
**Changed** (local, not committed)
* Dear ImGui v1.92.9 (MIT) + rlImGui 5c17e70 (zlib), fetched and pinned in `workbench/CMakeLists.txt` as the `dragdoll_imgui` static lib (no imgui.ini written).
* Panels: **Playback** (pause, step, restart, log time-scale slider, host-rate combo, auto-restart / joint limits / help toggles), **Scenarios** (click to load, camera kept), **Bodies** (per-body m/s and deg/s between rendered steps; spin > 30 deg/s while < 2 cm/s shown orange, the founder's lower-spine pattern). Works in replay too.
* Hotkeys and camera ignore input that ImGui captures; F9 screenshots are taken before the panels are drawn.
* Founder: "Looks good". 4 dev presets green.

**Next**
* Task 2.3 second slice (Opus): live parameter sliders (scenario params, rig friction) + click-impulse. Or 2.4 hot-reload host if sliders can wait.

**Open questions**
* DCO sign-off missing on pushed 44fa192 (see previous entry).

---

## 2026-11-13 — Phase 2, task 2.3 (second slice): right-click push
**Changed** (local, not committed)
* Viewer: right-click a body to push it (impulse along the view ray at the hit point; bodies picked as 15 cm spheres at their centres). Playback panel: Impulse slider 5-300 N*s (default 40; the shove scenario uses 70) and a "last push" line. Pushes go through `dragdoll_character_apply_impulse`, so they are recorded and replay exactly, and they wake sleeping bodies.
* Rig/parameter sliders moved to 2.4: they need the character rebuilt, which belongs with live rig reload.
* With auto-restart off, the world keeps running past the scenario's end (was frozen), so the ragdoll can still be pushed (founder report).
* Founder: "feels right" (asked for the 5-300 range). 4 dev presets green.

**Next**
* Task 2.4 (Opus): hot-reload DLL host + replay restore, live rig JSON reload with sliders.

**Open questions**
* DCO sign-off missing on pushed 44fa192.

---

## 2026-11-13 — Phase 2, task 2.4 (first slice): live rig editing
**Changed** (local, not committed)
* Scenarios: `setRigOverride(json)` / `rigJson()` — the ragdoll scenarios use the override when set (viewer only; tests always use the built-in rig).
* Viewer `--rig <file>` (passed by `watch.ps1` = the repo's `packs/rigs/humanoid16.json`): polled every 0.5 s, reloads on save and restarts the scenario (camera kept). Every rig is first built in a scratch world; a broken one is refused with the library's error and the last good rig stays.
* **Rig (live)** panel: per-joint friction sliders (0-10 N*m, applied on release), Save to file (rewrites only the friction numbers, keeps the hand formatting), Revert. nlohmann/json linked into the viewer.
* `watch.ps1 -Rig <file>` edits a copy instead of the repo rig. Save verified on a copy: all 15 friction values rewritten, every other byte of the file unchanged, still valid JSON.
* Founder: "Works well". 4 dev presets green.

**Next**
* Task 2.4 second slice (Opus): hot-reload DLL host (viewer loads dragdoll.dll through a function table, rebuild -> swap without closing the window) + restore by replaying the running session's recording.

**Open questions**
* None new.

---

## 2026-11-13 — Phase 2, task 2.4 (second slice): hot reload of dragdoll.dll
**Changed** (local, not committed)
* `workbench/hot_reload.{hpp,cpp}` (kept apart: `<windows.h>` clashes with raylib): the viewer links with `/DELAYLOAD:dragdoll.dll /DELAY:UNLOAD`; a delay-load hook loads a shadow copy `bin/hot/dragdoll_<pid>_<n>.dll`, so the build can overwrite `bin/dragdoll.dll` while the viewer runs. `changed()` waits until the new time stamp is stable for one 0.5 s poll (linker done).
* Reload: destroy replay/worlds, `dragdoll_shutdown`, `__FUnloadDelayLoadedDLL2`, copy the new build, `dragdoll_init`, restore: scenario re-run with 1/120 s host frames to the same step with the logged right-click pushes re-applied at their steps, or the replay advanced to the same frame. Pause state kept; a recording restarts (noted in the status). Status line in Playback > Library.
* Verified: 4 rebuilds while the viewer ran (copies #1-#4, old copies deleted, memory steady); founder pushed + paused, rebuild -> "Same pose". 4 dev presets green.

**Known limits**
* Viewer code itself (main.cpp, scenarios) is not hot-reloaded: rebuilding it still needs the viewer closed (`watch.ps1 -Build`).
* Restore re-simulates from the start: long sessions take proportionally longer (~120 steps per simulated second).

**Next**
* Task 2.5 (Opus): SA session capture + replay; decide the x86-recording -> x64-viewer determinism question (devlog 2.1).

**Open questions**
* None new.

---

## 2026-11-13 — Phase 2, task 2.5: SA session capture + bit-identical replay (ADR-0022)
**Changed** (local, not committed; dragdoll-core + dragdoll-sa)
* Decision ADR-0022 (option A): recordings replay through the build that made them. Viewer now built on x86 dev presets too (`DRAGDOLL_BUILD_VIEWER` default = dev tools on); `--dll <path>` loads/hot-reloads a given `dragdoll.dll`; `watch.ps1 -Replay` reads the header, picks the x86 viewer for x86 recordings and the game's `scripts\dragdoll.dll` (via `DRAGDOLL_SA_GAME_DIR` or `-Dll`).
* dragdoll-sa: every F7 drop creates a fresh world recording to `scripts\dragdoll_recordings\sa_YYYYMMDD_HHMMSS.ddrec` (previous world destroyed = recording closed); F9 adds a flushed "F9 mark in game at step N" note. F8 now pushes away from CJ (CJ -> chest line) instead of along his facing (founder: the ragdoll went where CJ looked).
* Viewer replays: camera jumps to the first character once it exists, F re-centres; the grid follows the camera target (SA sits ~2.5 km from the origin — founder saw "nothing on the grid").
* **Verified:** founder SA session (958 frames, 35.7 s, many F8 shoves, F9 at step 3682): x86 inspect + game DLL "identical to the original at every frame"; x64 "DIVERGED". Founder: replay "matches" the game. 4 dev presets green.

**Known limits**
* A recording is tied to its DLL; rebuilding the game DLL makes older recordings "another build".
* A killed game leaves a truncated last record, and replay rejects truncated files: consider replaying up to the last complete frame (idea).

**Next**
* Task 2.6 (Sonnet): vehicle shape pack (SA vehicle proxies as JSON).

**Open questions**
* F8 direction change not yet tried in game.

---

## 2026-11-13 — Phase 2, task 2.6: vehicle shape pack
**Changed** (local, not committed)
* `packs/vehicles/{sedan,sports,suv,van,bus,motorbike}.json` (format `dragdoll-vehicle` v1): side profile = length, width, clearance, bumper height, bonnet edge height + setback, hood length + angle, windshield angle, roof height + length, rear height. Own generic class values (bonnet edge: sports 0.60 < sedan 0.72 < SUV 1.00 < van 1.05 < bus flat front).
* `scenarios/vehicles.{hpp,cpp}`: strict parser, profile outline -> ear-clipped sides + perimeter quads = closed outward mesh (windings fixed per face), `Drive` (constant speed then braking). Packs compiled into the scenarios lib (CMake-generated `builtin_vehicles.cpp`, reconfigures when a JSON changes).
* Scenarios `hit_<class>_40kmh` (6): kinematic mesh vehicle hits a standing limp ragdoll side-on at 0.1 s, brakes at 7 m/s^2. Not rate-independent: braking is a curve and kinematic targets are interpolated linearly between host samples (mm differences, as in the game).
* **Core fix:** kinematic mesh colliders asserted in debug builds (Jolt cannot compute a mesh's mass; release builds ran with invalid mass). They now get a nominal 1000 kg box mass/inertia (never used: kinematic). The game's deployed DLL predates this fix (only matters if the SA plugin adds kinematic meshes, Phase 6).
* Debug builds print the failed Jolt assert (expression, message, file:line) to stderr before breaking.
* Tests (4 new): meshes closed + outward + right size, bonnet-edge class order, invalid profiles rejected, braking maths, every hit throws the pedestrian ahead of / onto the vehicle (never through or under). Founder: hits "look fine". 4 dev presets green, 34 test cases.

**Next**
* Task 2.7 (Opus): Tracy profiler zones + workbench command socket / MCP bridge (spawn scenario, step, get state, query recording).

**Open questions**
* None new.

---

## 2026-11-13 — Phase 2, task 2.7 (first slice): command channel + dragdoll_ctl
**Changed** (local, not committed)
* `workbench/command_server.{hpp,cpp}`: non-blocking TCP server on 127.0.0.1 only (default port 47820, `--port`, 0 = off), polled from the frame loop, one line in -> one JSON line out; status in Playback > Library.
* Commands: `help, list, load <scenario>, restart, pause, resume, step [n], timescale <x>, autorestart on|off, state, push <body> <x> <y> <z>, note <text>, shot <file.png>`. `state` = mode, name, step, time, paused, hash, scenario HUD lines, per body: position (mm), speed (m/s), spin (deg/s).
* `tools/ctl` (`dragdoll_ctl`): sends one command, prints the reply; exit 0 ok / 1 viewer error / 2 no viewer.
* Verified by driving the viewer: load hit_suv_40kmh, step 240, state (pelvis 11.95 m ahead after 2 s), push, bad command -> error + exit 1, shot -> PNG saved. 4 dev presets green. CLAUDE.md tells future sessions to use it instead of screenshots.

**Decision:** Tracy deferred to Phase 6 (founder agreed): the bench + budget test already catch regressions; Tracy explains hotspots when optimising for the 1 ms / 8-ragdoll target.

**Next**
* Close Phase 2: short retrospective (`docs/retrospectives/phase-2.md`) + progress clip, then Phase 3 (muscles / active ragdoll).

**Open questions**
* None new.

---

## 2026-11-13 — Phase 2 closed
**Changed** (local, not committed)
* `docs/retrospectives/phase-2.md`: what shipped (2.1-2.7), what went well/badly, ADR-0022, plan notes for Phase 3.

**Next**
* Phase 3, task 3.1 (Opus): pose drive via joint motors (frequency + damping ratio, ADR-0009) with tension profiles `limp` / `relaxed` / `tense` / `stunned` and smooth blending; scenario `hold_pose` (ragdoll on the ground keeps a target pose), live stiffness sliders.

**Decision:** no progress clips at all; the founder reviews in the live viewer (CLAUDE.md updated).

---

## 2026-11-13 — Phase 3, task 3.1: muscles (joint motors + tension profiles)
**Changed** (local, not committed)
* Rig joints: optional `"strength"` (peak torque, N*m); humanoid16: spine 250/200, neck 40, shoulder 70, elbow 60, wrist 12, hip 220, knee 200, ankle 150.
* `core/src/muscles.cpp`: Jolt position motors (swing-twist: swing + twist to the rest orientation in body space; hinge: angle 0) with frequency + damping (ADR-0009) and torque limit = strength x share. Profiles: limp (off), stunned 3 Hz / 0.35 / 30%, relaxed 4 Hz / 1.0 / 40%, tense 6 Hz / 0.9 / 100%. Blends ramp share and stiffness (through a 0.5 Hz spring to/from limp; 0 Hz would be rigid in Jolt). Updated every fixed step before the physics update.
* C API: `dragdoll_character_set_tension / get_tension`, `DdTension`, `dragdoll_character_get_body_parent`; recorded (record type 11) and replayed.
* Scenario `tension_lineup` (4 humanoids, one per profile, rate-independent); `metrics::poseError` (RMS / max joint angle vs a reference pose).
* Viewer: Playback > Muscles (scenario | limp | relaxed | tense | stunned, sticky across restarts — founder report; blend slider); ctl `tension <profile|default> [blend]`; `state` reports tension and pose error. Tension changes are restored after a hot reload.
* **Findings while tuning (via ctl + hot reload):** an active Jolt motor *replaces* the joint's passive friction, and its spring is sized to the two joint bodies, not the load above — so the first soft profiles (1.5 Hz relaxed, 0.7 Hz stunned) held *less* than limp. Stiffer profiles fixed it; blends now ramp stiffness, not only the torque limit (which a falling body never reaches).
* Tests (3 new): API + parents; lineup order at 30/60/144 fps (settled RMS: tense 8.4 < relaxed 22.7 < limp 43.1 deg; stunned folds slower than limp); blending. Founder: "first two look limpy, the later two stiffy". 4 dev presets green, 37 test cases.

**Next**
* Task 3.2 (Opus): host pose input + full output contract (D17): the host sets a target pose per body; motors track it.

**Open questions**
* Stunned ends as crumpled as limp (folds slower, same end state). Acceptable for "dazed"; revisit when behaviours use it.

---

## 2026-11-13 — Phase 3, task 3.2: host pose input + output contract (D17); muscles sized by real inertia
**Changed** (local, not committed)
* C API: `dragdoll_character_set_target_pose` (NULL = rest) / `dragdoll_character_get_pose` (+ root transform), `DdCharacterInfo` via `dragdoll_character_get_info` (state ACTIVE/SETTLED after 0.5 s still; RECOVERING/DEAD/FAILED reserved; facing +1/-1/0; horizontal head direction), `dragdoll_character_get_blend_weights` (all 1). Convention: per body, rotation relative to the parent as an offset from rest, in rig axes (+X left, +Y up, +Z forward); identity = rest. Motor targets: swing-twist `conj(P_parent) d P_child` in body space, hinge = twist angle of d about its rig axis. Target poses recorded (record type 12).
* `core/src/pose.cpp`; scenario `pose_zero_g` (tense, zero gravity, rest -> guard -> arms up -> tuck).
* **Founder review fixes:**
  * "Arms up is arms front": shoulder cones recentred forward-down `[+-0.3, -0.35, 0.89]`, 115 deg (overhead reach, ~45 deg extension). Limp checks (arm-in-chest, hyperextension) still pass.
  * "Tense wobbles, a tense body should lock": Jolt's frequency springs use only the two joint bodies' inertia, so a whole arm at the shoulder swung at ~1.4 Hz under-damped. Muscles now use `StiffnessAndDamping` from each joint's effective inertia (child side x parent side / sum, about the pivot, at rest): k = I w^2, c = 2 zeta I w. Guard pose: reached in ~0.3 s, no overshoot, then 0.0 deg / < 2 deg/s. Founder: "stays still".
  * With honest stiffness the profiles were retuned: tense 8 Hz / 2.0 / 100%, relaxed 1.2 Hz / 1.0 / 15%, stunned 0.6 Hz / 0.3 / 30%. Lineup settled RMS: tense 2.3 < relaxed 6.0 < stunned 42.5 ~ limp 43.1 deg; stunned folds 2x slower than limp.
* Tests (4 new): rest = identity + root; zero-g tracking at 3 headings (0.3 deg worst after 2 s); validation + NULL reset + blend weights; state/facing/head direction. 4 dev presets green, 41 test cases.

**Next**
* Task 3.3 (Opus): activation from motion — start bodies from two consecutive host poses (positions + velocities); `walk_then_activate` without a velocity pop.

**Open questions**
* Founder to re-check `tension_lineup` with the retuned relaxed/stunned profiles.

---

## 2026-11-13 — Phase 3, task 3.3: activation from motion (ADR-0017)
**Changed** (local, not committed)
* C API: `dragdoll_character_activate(character, previous_root, previous_joints, current_root, current_joints, count, dt)`: forward kinematics from root + joint rotations (rest geometry: joint pivot in the parent body frame, body offset from its pivot), bodies teleported to the current pose with linear/angular velocity from previous -> current over dt; no previous = at rest. Also sets the target pose to the current pose (founder: a tense walker snapped straight to the rest pose). Interpolation history reset, state back to active. Recorded (record type 13). `dragdoll_character_get_body_mass` added.
* Scenario `walk_then_activate`: two synthetic walkers puppeted for 1 s via activate each host frame, then limp; LEFT handed over with momentum, RIGHT without (classic freeze-then-drop). Founder: "noticeably keeps the momentum"; with tense it "keeps stride".
* Tests (4 new): FK round trip (get_pose -> activate reproduces bodies; 5 mm because simulated joints stretch a few mm), momentum within 5% at 30/60/144 fps (measured 1.40 -> 1.39 m/s), validation + body mass, muscles hold the handed-over pose. 4 dev presets green, 45 test cases.

**Next**
* Task 3.4 (Opus): mock 32-bone SA ped skeleton (bone IDs/hierarchy as facts, own proportions) -> rig mapping, round-trip error < 0.5 deg.

**Open questions**
* A tense character still falls as a statue: balance is task 3.6.

---

## 2026-11-13 — Phase 3, task 3.4: skeleton maps (SA ped -> humanoid16)
**Changed** (local, not committed)
* Generic skeleton maps (any host, ADR-0018): `packs/skeletons/<name>.json` (format `dragdoll-skeleton-map` v1: model up/forward axes, rig body -> host bone id), compiled in (`dragdoll_get_builtin_skeleton_map("sa_ped")`).
* C API: `DdHostBone` (id, parent index, bind transform in model space), `dragdoll_skeleton_map_create / destroy / to_pose / from_pose`. Math (`core/src/skeleton_map.cpp`): A = model -> rig axes; G_k = A H_k B_k^-1 A^-1 (bone rotation since bind, rig axes); per-body bind correction D_i = shortest arc from the rig's rest bone direction to the host's bind bone direction (handles T-pose vs arms-down binds); joint d_i = (G_kp D_p)^-1 (G_k D_i); unmapped host bones (fingers, toes, clavicles, jaw, a root above the pelvis) follow their parent rigidly. Independent of the host's local bone frames.
* `packs/skeletons/sa_ped.json`: pelvis 1, spine 2/3, head <- neck 4, arms 32-34 / 22-24, legs 41-43 / 51-53; SA model axes +Z up, +Y forward. Bone ids are interoperability facts (plugin-sdk ePedBones); hierarchy to be verified in-game in Phase 6.
* Tests (4 new) with a mock SA ped (25 bones, SA ids/hierarchy, own proportions, T-pose bind, arbitrary bone frames): T-pose -> arms straight out (head 3.6 deg: the rig's head sits 1 cm forward, a proportion difference); rig rest -> host arms hanging down, fingers keep their offset; host -> DragDoll (activate, get_pose) -> host round trip **0.056 deg worst** (criterion 0.5); invalid maps rejected. 4 dev presets green, 49 test cases; dragdoll-sa still builds.

**Next**
* Task 3.5 (Opus): `failed` state — NaN / energy-explosion guards deactivate the character and report it; host fallback test.

**Open questions**
* The SA root-position offset (pelvis bone vs pelvis body centre, ~5 cm) is taken as zero for now; tune when the mod feeds real poses (Phase 6).

---

## 2026-11-13 — Phase 3, task 3.5: the FAILED state (ADR-0017)
**Changed** (local, not committed)
* Guards after every step (`pose.cpp checkHealth`): any body not finite, faster than 100 m/s (Jolt caps speed at 500 m/s, so real blow-ups show as speed or stretch before NaN), or a joint's two ends > 0.25 m apart. On failure: state FAILED + `DdCharacterInfo.failure` (NOT_FINITE / TOO_FAST / TORN_APART), the ragdoll is removed from the physics system at once, the last good pose is kept (the bad step discarded), muscles stop; impulse / tension / target pose / activate return a clear error; get_pose / transforms / info keep working; the host destroys it and falls back to its own animation.
* `dragdoll_debug_corrupt_character` (dev builds only; DD_ERROR_UNSUPPORTED otherwise): injects NaN where a real blow-up would surface.
* **Bug found by the tests:** destroying a world that still held a FAILED character removed its ragdoll from the physics system twice (segfault). World destructor now guards like `destroyCharacter`.
* Tests (3 new): forced NaN -> FAILED / NOT_FINITE, last good pose readable, commands refused, the other character keeps falling, world keeps stepping, destroy works; absurd impulse (1e6 N*s) caught (TOO_FAST); no false alarms in any scenario at 30/60/144 fps. 4 dev presets green, 52 test cases; dragdoll-sa builds.

**Next**
* Task 3.6 (Opus): standing balance — CoM, support polygon, capture point, ankle and hip strategies; `hold_stance` upright 60 s under 5 N*s pokes, `bump_5kmh` absorbed.

**Open questions**
* The SA plugin does not react to FAILED yet (Phase 6: destroy + hand back to the game's animation).

---

## 2026-11-13 — Phase 3, task 3.6: standing balance (ADR-0023) — Phase 3 acceptance met
**Changed** (local, not committed)
* ADR-0023 (sources: Pratt et al. 2006 capture point; Hof et al. 2005 XcoM; Horak & Nashner 1986 ankle/hip strategies; Winter 1995 lateral load/unload).
* `core/src/balance.cpp`, run each step before the muscles: centre of mass + velocity, support centre (feet), capture point `x + v/omega`; error relative to the body's own standing point (rest CoM vs feet centre); only while standing (CoM > 70% of standing height above the feet, trunk within ~45 deg). Offsets on top of the target pose: ankles (forward 120 deg/m, side 120 deg/m), hips forward past 5 cm (400 deg/m), hips sideways load/unload (150 deg/m). While balancing, stance-leg joints (thigh/shin/foot) are sized by the load they carry (parent-side inertia), not the light limb below.
* C API `dragdoll_character_set_balance / get_balance`, `DdCharacterInfo.balance_error`; recorded (type 14). Viewer: Balance checkbox; ctl `balance on|off`; `state.library` (live-reload copy) for scripts.
* Scenarios: `stand_balance`, `hold_stance` (5 N*s random pokes every 2 s, seeded; only while upright), `bump_5kmh` (sedan at 5 km/h, braced pedestrian).
* **Bugs found on the way (tests + founder):** balancing around the feet centre instead of the standing point tipped a still body over; light ankle springs could not hold an inverted pendulum (-> load sizing); the 5 km/h bump was a coin flip above 120 fps host rates (-> lateral hip strategy); balance kept acting on a fallen body (founder: "shakes like a fish"); a 30 ms error filter made the sideways loop waddle (founder: "penguin") and weakened shin-push recovery — removed; hold_stance kept poking a fallen body (founder: "final twitch").
* Tests (4 new): quiet standing (< 2 cm), **60 s under 30 pokes upright** at 30/60 fps (capture point <= 3.2 cm), **bump absorbed at 30-200 fps**, balance needs muscles. Founder: stunned + balance stays on its feet hunched; both problems gone. 4 dev presets green, 56 test cases.

**Phase 3 acceptance:** hold_stance 60 s ✓ | tracking < 5 deg RMS ✓ (0.3 deg) | bump_5kmh ✓ | walk_then_activate momentum within 5% ✓ (0.6%) | mock-skeleton round trip < 0.5 deg ✓ (0.056) | forced NaN -> failed, no crash ✓.

**Next**
* Phase 3 retrospective, then Phase 4 (car-hit vertical slice).

**Open questions**
* Pushes beyond ~20-25 N*s at the chest topple the body: correct without stepping (a later phase).

---

## 2026-11-13 — Bridge task 3.7: live tap + SA live tools (founder-driven session)
**Changed** (local, NOT committed; full multi-preset tests NOT run — founder: no heavy builds/tests unless asked)
* **Live tap** (`core/src/live_tap.*`, dev builds): `dragdoll_world_start/stop_live_tap(port)`; JSON lines over TCP on 127.0.0.1; `scene` (colliders with shapes, characters with rig, names, world id) + `frame` after every host frame (<= 60 Hz: colliders, per character root + joints, state, tension, balance, visible; host camera); frames dropped, never queued. Viewer -> host commands: `show`, `tension`, `balance` (simulation changes go through the C API, so they are recorded).
* **Library additions:** `dragdoll_character_set_name / set_debug_draw`, `dragdoll_character_set_kinematic` (host-animated bodies: no gravity/muscles/balance, not pushed, push ragdolls; dynamic again keeps momentum — the Phase 4 handover; recorded, type 15; guards skip speed/stretch for kinematic), `dragdoll_world_set_host_camera` (tools only). Tests added: live tap (3), kinematic (3) — run individually, all green.
* **Viewer attach mode** (`--attach 47830`, `watch.ps1 -Attach`): mirror world from scene/frames (x64 viewer watches the x86 game), quick reconnect, in-place scene updates, Live characters panel (show in game, tension, balance per character), camera modes free / game camera / follow CJ (C), grid on the floor proxy, two-sided drawing, smooth characters (frames buffered by host time, shown 100 ms behind, blended; host camera blended too), **capture for Claude** (Start/Stop + note -> `build/captures/live_*.jsonl` incl. per-redraw `view` samples).
* **dragdoll-sa:** split into a permanent **loader** (`dragdoll_sa.asi`: hooks, mouse fix) + hot-reloadable **logic** (`dragdoll_sa_logic.dll`) and `dragdoll.dll` run from shadow copies — rebuilds swap in ~1 s without restarting the game (`deploy.ps1` hot-copies while the game runs; loader built with /Brepro). Live tap per world; CJ as a **kinematic puppet** (live skeleton -> sa_ped map -> activate each frame, read in the render callback where bones are current; created at logic start); nearby cars as kinematic collision parts (SA mesh, flipped outward + spheres; no SA car has boxes) with spinning wheel cylinders; host camera.
* **Founder-found defects, all via live view / captures:** grid at height 0 (SA sits at ~23 m); camera Y-up hid the doll as "below the floor"; F7 froze the view (1 s reconnect); car faces inverted / sides missing (SA winds inward; sides are spheres); puppet bobbing (dynamic puppet vs gravity/floor -> kinematic); choppy walk (bones read one frame late: alternating 10-19 vs 2-4 m/s -> now 5.0-5.5 m/s; viewer buffer margin and camera smoothing).

**Next**
* Item 3: pedestrians as kinematic puppets with true bind poses from skin data. Then item 4: real terrain + incremental live updates.
* Before committing: founder-chosen full test run (all presets).

**Open questions**
* Wheel spin is derived from rotation between frames: correct up to ~half a turn per frame (~120 km/h at 30 fps).
* Smooth car hull (Phase 4): SA car collision is 13-25 spheres + 4-12 triangles, bumpy for a sliding body.

---

## 2026-11-14 — Bridge task 3.7 continued: SA live tools complete (founder-driven, captures as the feedback loop)
**Changed** (local, NOT committed in either repo; full multi-preset tests NOT run — founder chooses when)
* **Library:** debug-draw level of detail (`DD_DRAW_CAMERA_LOD`, `DdDebugDrawCallbacks.camera_position`: Jolt was never told the camera, so every sphere drew at full detail); live tap sends the scene once, then `delta` messages (needed for streamed level meshes); viewer -> host messages (`dragdoll_world_poll_live_message`). Tests: live tap 5 (host messages, deltas), run alone, green.
* **Viewer:** smooth playback from one clock (characters, cars, game camera; 120 ms behind; rate-tracking clock — the old "pull 10 % per redraw" clock made speed swing ~22 %, measured by replaying captures); cars blended with the characters (the bike no longer leaves CJ behind); mirror characters kinematic (a pose jump "failed" them: CJ vanished after F7 / on a bike); remote control of CJ (click / Esc, sensitivity, invert Y); settings persist; no grid / no floor hiding when the game streams its own level.
* **dragdoll-sa:** level collision streaming (buildings within 60 m, queued, ~2500 triangles per frame); peds with hysteresis (30/40 m, <= 20, also in vehicles); CJ kept in vehicles; failed puppets rebuilt, non-finite frames skipped; pad hook (loader: `processScriptsEvent` -> `dd_logic_pad`) for remote input; F7 drops into the running world (no per-drop world / `.ddrec`).
* **Measured from the founder's captures:** viewer 19-59 -> ~144 redraws/s; ped flicker 2 short-lived of 41; game frames > 100 ms 0.4-1.6 % (the game's own hitches, up to ~290 ms).

**Next**
* Commit both repos after a founder-approved full test run (all presets).
* Then, in order of reasoning load (see the workplan): capture = session recording with bit-exact replay; Phase 3 retrospective; Phase 4 design (perception + behaviour chain ADR).

**Open questions**
* Remote F7/F8 can retrigger if held across a reconnect (rare now that F7 keeps the world).
* Dynamic map objects (fences, lamp posts) are not level collision yet.

---

## 2026-11-16 — Commit of bridge task 3.7; Phase 4 design (ADR-0024, ADR-0025)
**Changed**
* Committed and pushed dragdoll-core `05a1f08` (bridge task 3.7); dragdoll-sa `06bdc94` (local repo). Targeted tests only (x64-release: live tap, kinematic, replay, recording, determinism, failed — 21 cases green); the four-preset run is still owed.
* **ADR-0024 (Proposed):** senses (contacts, per-body velocity change, self state, threat with time to collision), collider groups, the **front profile probe** (a vehicle's silhouette measured from its own collision geometry: no per-game vehicle tables), the impact report, and the rule that wrap / projection / vault are **measured outcomes of the physics**, with a passive-baseline gate before any behaviour.
* **ADR-0025 (Proposed):** behaviours command muscles only; reaction clock (reflex 0.1 s, voluntary 0.5 s), vitality, awareness; stages Watch -> Anticipate -> Impact -> Carried -> Airborne -> Landing -> Tumble -> Settle -> Handback; reflex layers Guard / Reach / Curl / Startle; behaviour SDK; v1 exclusions (stepping, diving, get-up).
* Sources checked online before citing (bibliographic details and abstracts; full texts not read yet — task 4.2).
* Workplan: Phase 4 split into tasks 4.1-4.10 with estimates and model hints.

**Next**
* ADR-0024 and ADR-0025 accepted by the founder (2026-11-16).
* Then task 4.1 (senses I: contacts and self state) — Opus. Fable tasks: 4.2, 4.4, 4.5, 4.6, 4.7.

**Open questions**
* Values to source in 4.2: body-on-bonnet friction, head-strike times, projection efficiency per class, reflex latency for a whole-body startle.
* humanoid16 has no separate hands: is the forearm end enough for Reach and break-fall?

---

## 2026-11-16 — ADR-0025 amended: rest poses (one animation slot, nothing left behind)
**Changed**
* Founder's design: the chain starts from the host's pose at the hit and ends in the host's own lying pose (face up / face down, read from the game at runtime); the ragdoll is destroyed at handback for every outcome, so crowded scenes leave ordinary game corpses. ADR-0025 §3a; workplan §0 scope, task 4.8, Phase 6 Handback, roadmap (one slot at a time: lying dead, walking, per-part weights).

**Next**
* Task 4.2, reading half: `docs/reference/pedestrian-impact.md` from the open-access sources (Fable).

---

## 2026-11-16 — Task 4.2, reading (first pass): docs/reference/pedestrian-impact.md
**Changed**
* `docs/reference/pedestrian-impact.md`: the literature's numbers for Phase 4, each tagged [read] / [abstract] / [via X] / [ours]. Full texts read: PEDPCREACT final report (CORDIS), Li Q. et al. 2021 (avoidance), Li G. et al. 2021 (head kinematics from 57 real collisions), Simms 2013 (Engineers Journal), Saulić et al. 2020 (throw distance; reports Burg & Moser 2007).
* Key numbers: head strike 50-140 ms after first contact (77 % of cases), wrap-around distance 1.5-2.3 m (mean 1.84), head speed 83 ± 23 % of vehicle speed, bonnet 39 % / windscreen 61 %; flight 0.45-0.7 s at 33-49 km/h; slide 7.2-8.2 m/s²; road friction 0.66-0.79 (Searle); reaction time 501 ms (young) / 721 ms (older); getting clear needs >= 1.6-1.8 s to collision.
* ADR-0024 §5 corrected: the throw corridor applies to flight + slide from the separation point (carry is separate), with the launch height term. ADR-0025: Shield has the two observed shapes (cover / fend); §3b records the timing argument for the reaction clock. Workplan 4.2 split; Phase 4 acceptance uses the sourced numbers.
* Not reachable by automated fetch (repository refuses it, or paywalled): Wood, Simms & Walsh 2005; Bhalla et al. 2002; Ravani 1981; Searle 1983.

**Next**
* Task 4.1 (senses I: collider groups, contacts, per-body velocity change, airborne, ground probe, landing prediction) — Opus.
* Founder, optional: download Wood, Simms & Walsh 2005 (TCD TARA, open access) into `dragdoll-core/private/` for the second reading pass.

**Open questions**
* Body-on-bonnet friction; launch-speed ratio per trajectory type; whole-body startle latencies.

---

## 2026-11-16 — Task 4.4: threat perception and the front profile probe
**Changed** (local, not committed)
* `core/src/perception.cpp`: moving colliders are perceived as objects (a group, or one ungrouped collider); every fixed step each character gets its most imminent threat (time to collision, miss distance, impact line, closing speed). The first time an object threatens a character its front is probed with rays against that object only (horizontal rays for the front face, vertical rays for the top, every 10 cm, median-of-3): bumper line, leading edge, bonnet, windscreen, roof; predicted trajectory class. Read-only: no effect on the simulation (determinism tests unchanged).
* C API: `DdColliderDesc.group`, `DdThreat`, `DdFrontProfile`, `DdTrajectory`, `dragdoll_character_get_threat`, `dragdoll_character_get_threat_silhouette`. Recording format 2 (collider group); live tap sends the group.
* Tests (4, `perception*`): sedan measured exactly (leading edge 0.72 m, bonnet 10 deg, windscreen 29 vs 30 deg); all six packs within tolerance; sedan / sports -> wrap, SUV / van / bus -> forward projection; sphere-built car: leading edge 0.66 m (true 0.72), wrap. Targeted run on x64-release (determinism, replay, recording, live tap, kinematic, colliders, memory, perception): 26 cases green. x86 builds (via dragdoll-sa). Four-preset run still owed.
* Viewer: probed silhouette drawn over the vehicle, threat line at the bottom left. dragdoll-sa: car parts and wheels share a group per vehicle (built and deployed, not yet run in the game).
* Correction to the previous entry: humanoid16 **does** have hands (`hand_l`, `hand_r`); the open question about Reach without hands is void.

**Next**
* Task 4.5 (behaviour SDK + muscles per region) — Fable; or 4.1 (contacts, per-body velocity change, airborne, ground probe) — Opus.

**Open questions**
* A turning vehicle is extrapolated in a straight line (fine at 2 s; revisit if cornering hits look wrong).
* In the live mirror the viewer recomputes threats from mirrored motion (speeds about 20 % off at 144 redraws/s); the tap should stream the host's own threat.
* Forward-projection threshold (leading edge >= 95 % of centre-of-mass height) is ours; the passive baseline (4.3) calibrates it.

---

## 2026-11-16 — Task 4.5a: behaviour SDK, muscles per region, first behaviour
**Changed** (local, not committed)
* **Muscles per region** (`muscles.cpp`): six regions (head-neck, trunk, arms, legs) from the rig's body names; each blends on its own; `set_tension` sets all. Balance now needs both legs tensed (not the whole body).
* **Behaviour SDK** (`behaviours/include/dragdoll/behaviour_sdk.h`): `Percept` (self state, threat, how long it has been seen, awareness, vitality, current tensions), `Command` (tension per region, target pose, balance), `Behaviour`, `ReactionClock`, `Params`. `dragdoll_behaviours` is a static library whose include paths are only the SDK and the public C header (ADR-0013 enforced by the build).
* **Runner** (`behaviour_host.cpp`): after perception, before balance and muscles; also for kinematic characters (they watch). Awareness: automatic (threat within about 100 degrees of where the head faces) or host-set; vitality host-set. Parameters per world, built-in pack compiled in, replaceable live.
* **C API:** `set/get_region_tension`, `get_body_region`, `set/get_behaviour`, `get_behaviour_status`, `set/get_awareness`, `set/get_vitality`, `dragdoll_world_set_behaviour_params`. Recording format 3 (records 16-20).
* **`car_hit` v0** (`behaviours/src/car_hit.cpp`): Watch -> Anticipate when aware and within 1.2 s -> Startle (whole-body tense) after the 0.1 s reflex latency -> Recover (tension back to what it was 0.6 s after the threat is gone).
* **Tests** (5): regions; a tense arm holds while the limp arm swings; startle exactly 12 steps (0.1 s) after the threat enters the window; unaware / told by the host / vitality 0; live parameters; a session with behaviour + parameters + awareness + region tension replays bit-identically frame by frame. Targeted run on x64-release (everything the muscle change touches): 52 cases green. x86 builds (via dragdoll-sa). The full run was not made (founder's rule); four presets still owed.
* Viewer: the threat line shows the behaviour and its stage.

**Next**
* Task 4.5b (pose library + aim helper) — Fable. Then 4.1 (contacts, velocity change, airborne) — Opus — which the Impact stage (4.6) needs.

**Open questions**
* Regions come from body names; a rig with unusual names needs a `region` field in the rig JSON (not added yet).
* While a behaviour is attached it and the host can both set tension; the last call wins (documented in the header). Enough for v1?

---

## 2026-11-17 — Task 4.5b: aim, pose library, Shield; live check in the game
**Changed** (local, not committed)
* **Aim** (`core/src/aim.cpp`): limb = the region's first body (free joint), second (elbow / knee) and last (hand / foot). In the frame the parent body carries: bend the second joint (bisection on the hinge angle, inside the hinge's limits, flexing towards the side with more range) until the limb's end is `min(distance, extension x length)` from the first joint; then turn the first joint by the shortest arc so the end points at the target. Sets targets only. Measured (zero gravity, tense): hand 1.3 cm and 0.2 cm from reachable targets, foot 0.06 cm; elbow 90.5 deg at extension 0.7 (91 expected).
* **Pose library:** `"poses"` in behaviour packs; `"rest"` built in; region mask.
* **Bug found by the tests:** a body at rest is asleep in Jolt and ignored new aim / pose targets (the hands did not move at all). New targets now wake the body, as tension changes already did.
* **`car_hit` Shield (fend):** from 0.5 s after the stimulus, both arms aim at a point 0.55 m towards the vehicle, 0.15 m above the centre of mass, extension 0.85. Test: hands 36 cm ahead of the pelvis 0.3 s before the impact (facing the car).
* Scenario `aware_sedan_40kmh` (covered by the scenario-wide replay / stability / rate tests). Live tap frames carry `ttc`, `expects`, `behaviour`, `stage`; the viewer shows them per character; mirror colliders keep their group.
* **In the game** (hot-reloaded): CJ and 5 pedestrians report `car_hit: watch`; 3 cars arrive as 3 groups. No threat seen yet (the game was idle): needs the founder driving.
* Tests: aim (3), poses (1), Shield (1). Targeted runs on x64-release: 30 cases (scenarios, recording, replay, live tap, perception, behaviour, aim) green; x86 builds via dragdoll-sa. Full and four-preset runs still owed.

**Next**
* Task 4.1 (contacts, per-body velocity change, airborne, ground probe, landing prediction) — Opus. It unblocks Impact (4.6) and the impact report (4.2).

**Open questions**
* Side-on, the far arm cannot reach across the trunk: only the near arm fends. A person would also turn towards the vehicle: for 4.9 (Anticipate), with the "cover" shape.
* The first joint's swivel (where the elbow points) is whatever the shortest arc gives; no explicit control yet.
* The shoulder / hip targets are not clamped to the cone limits: the joint itself stops the limb. Fine so far (the unreachable-target test), revisit if arms fight their limits visibly.

**Founder review (2026-11-17), `aware_sedan_40kmh`:** "the arms look interesting, the rest of the body looks a bit stiff". Expected with this first version (whole body held at TENSE from the startle to the impact); recorded as work for task 4.9 in the workplan.

---

## 2026-11-17 — Perception in the game: first live check, three fixes
**Changed** (local, not committed)
* Founder attached the viewer to the game (yellow silhouettes on cars approaching pedestrians) and saved a capture; I then read the game's tap directly and drove CJ through the viewer -> game input channel (W held) to collect numbers.
* **Found and fixed:**
  * Drivers and passengers (and CJ in his car) were "threatened" by their own vehicle at 0 s, flickering. Position differences of a host-animated character are zero between host frames; perception now takes the centre-of-mass velocity from the bodies' own velocities. And the host says what it knows: `dragdoll_character_set_riding(character, group)` — that object is never a threat (recorded, type 23); dragdoll-sa sets it from the ped's vehicle.
  * A character within 25 cm of a passing car's side counted as "on" it: now only inside its width.
* The tap streams the measurements behind each prediction (`threat`: group, leading edge, bumper line, roof, bonnet length, centre-of-mass height, lateral offset, closing speed).
* **In the game after the fixes** (12 s, CJ walking): six real threat episodes, times to collision counting down (e.g. 1.00 -> 0.53 s at 25 km/h), stages `watch > anticipate > startle > shield` on real pedestrians, standing centre-of-mass heights 0.92-1.06 m, no false threats.
* Tests: rider test added; targeted run 41 cases green (x64-release); x86 builds.

**Next**
* Task 4.1 (contacts, per-body velocity change, airborne, ground probe) — Opus.

**Open questions**
* **Front heights of SA cars read high:** leading edges 0.72-1.12 m and roofs up to 1.77 m, varying with where across the car the line is probed (SA builds cars from fat spheres: about 0.85 m tall bonnet spheres on a floor some 0.3 m above the road). Against a 0.92-1.06 m centre of mass many saloons read as "forward projection". Is the measurement right (SA cars are simply tall blobs to collide with) or is the ground reference low? A log line now prints the player's car geometry once a second while driving (`player car model ...`): needs the founder in a car. If the spheres are the cause, the car needs a better collision shape for DragDoll than the game's own (the "smooth hull" task): SA's own 12-triangle mesh is a clean outline.
* The wheel cylinders sit about 0.35 m higher than a wheel that touches the road should: check with the same log line.
* The first frames of a threat report class "?" (profile not valid yet).

---

## 2026-11-17 — SA car fronts: ground truth, leading edge by the regulation's definition
**Changed** (local, not committed)
* **Ground truth from the game** (player in a Primo, model 547; log line `player car model`): road 0.74 m below the car's origin; wheel hub 0.39 m below, wheel radius 0.35 m -> the wheels touch the road exactly (ground reference and wheel cylinders are right; the earlier suspicion was wrong); collision spheres span 0.23-1.62 m above the road; collision box bottom 0.10 m below the road. SA cars are about 1.6 m tall against pedestrians whose standing centre of mass is 0.90-0.92 m.
* **Leading edge redefined** (`perception.cpp`): the first point a straight edge held 50 deg rearward of vertical touches (UN GTR 9 / EEVC WG17 bonnet leading edge reference line; cited from memory, to verify) = the top-profile point furthest out along that edge's normal. Independent of ray spacing; exact on the packs (sedan 0.72 m) and 0.66 m on the test's sphere car (geometry: 0.64). The previous rule (first place the top is flatter than 20 deg) took nearly the crest of a rounded front.
* **Result in the game:** saloons read leading edge 0.91-0.96 m, bumper line 0.80 m, roof 1.40-1.61 m -> "forward projection" against a 0.91 m centre of mass. This is honest: the game's collision spheres (radius ~0.4 m, centres ~0.63 m above the road) are a high, round front. A ragdoll hit by them would be pushed at pelvis height, not swept at the shins.
* SA's own low-poly collision mesh for the same cars is a realistic outline (bonnet nose about 0.6-0.7 m above the road, rising to 0.8-0.9 m, windscreen 37 deg, roof): it only lacks a front face and sides.
* Tests: perception 5 cases; targeted run 41 cases green (x64-release); x86 builds; hot-reloaded and measured in the game (CJ's car driven 107 m through the viewer -> game input channel).

**Next**
* dragdoll-sa: build each car's DragDoll shape from the game's outline mesh (side profile extruded to a closed mesh, as the workbench vehicles are), keeping the spheres only where there is no mesh. Then re-measure: saloons should read 0.6-0.7 m and "wrap". Needed before any hit is tuned in the game.
* Task 4.1 (contacts, velocity change, airborne) — Opus.

**Open questions**
* Characters riding a vehicle still perceive other vehicles as threats (a crash between cars). Out of scope for v1; harmless while they are host-animated.

---

## 2026-11-17 — dragdoll-sa: car bodies from the game's outline mesh; character height from its own ground
**Changed** (local, not committed)
* **dragdoll-sa `buildOutlineSolid`:** a car's body is one closed mesh built from the game's low-poly collision mesh: stations at every vertex position along the car, height = highest point of the mesh's cross-section there, extruded across the mesh's width over its flat floor, with flat front and back faces. The game's spheres are no longer used for cars whose mesh is such an outline (all 10+ models seen in traffic: noses 0.63-0.90 m, roofs 1.24-1.56 m above their floors); other vehicles keep mesh + spheres. Wheels unchanged.
* **Perception:** the character's centre-of-mass height is measured from the ground under the character (a ray), not under the vehicle's front — on slopes and kerbs the two differ (values of -0.01 and 1.30 m had appeared).
* **Measured in the game** (driving ~1 km through the viewer -> game input channel): saloons read leading edge 0.94-0.96 m, bumper line 0.90 m, roof 1.57-1.59 m, the same at any lateral offset (spheres had given 0.72-1.12 m); standing pedestrians' centre of mass 1.06 m above their own ground (the earlier 0.91 m was against the road, from the kerb); people sitting in vehicles 0.74-0.84 m. Result: **saloons -> wrap (0.95 / 1.06 = 0.90 of the centre-of-mass height), taller vehicles (1.09-1.12 m) -> forward projection.**
* Ground truth (Primo): floor about 0.22 m above the road, nose 0.72 m above the floor = 0.94 m above the road. SA cars are large next to its pedestrians: a saloon's leading edge sits where a real SUV's does, relative to a person.
* The per-second car geometry log is no longer called.
* Tests: targeted run 41 cases green (x64-release); x86 builds; hot-reloaded several times in the running game without a restart.

**Next**
* Task 4.1 (contacts, per-body velocity change, airborne, ground probe as a sense) — Opus.

**Open questions**
* The car body spans the outline mesh's full width at every station (the cabin is as wide as the body): fine for frontal hits; revisit for side impacts.
* Vehicles without an outline mesh (bikes, some specials) still use the game's spheres; at the extreme lateral line they read very low profiles.
* Passengers of one vehicle still perceive other vehicles (crashes between cars are out of scope for v1).

---

## 2026-11-17 — Probe on slopes; task 4.1: contacts and self state
**Changed** (local, not committed)
* **Founder report:** on a slope the yellow silhouette did not go up and down with the car. Cause: the probe used the world's level and vertical, and the silhouette was stored against the ground height at probe time. Now the object has its own frame (travel direction following the slope, its left, and the up that goes with them): extents, probe rays and heights are in that frame, and the silhouette is stored in the local space of the object's first collider. Test: a sedan on a 15 degree ramp reads leading edge 0.72 m, roof 1.45 m, bonnet 10 degrees (as on the flat), and the silhouette climbs with it at a constant height above the road.
* **Task 4.1** (`core/src/senses.{hpp,cpp}`): `ContactLog` (Jolt `ContactListener`; entries appended under a lock from worker threads, sorted before use). After every step, per character: per-body contacts and velocity change; airborne; ground / object body counts; carried by; ground distance and normal (ray); uprightness; angular momentum (orbital + spin with the mean principal inertia); landing prediction (ballistic centre of mass, body turning as the pelvis turns now; lowest body on arrival). Peaks restart at the kinematic -> dynamic handover.
* API: `DdSenses`, `DdBodyContact`, `dragdoll_character_get_senses`, `dragdoll_character_get_contacts`; SDK `Percept::senses / contacts`; live tap `senses` per character; viewer line "senses: ...".
* **Bug found by the tests:** a body lying still falls asleep and Jolt reports no contacts for sleeping bodies, so it felt airborne. A sleeping body keeps the contacts it fell asleep with.
* Meaning of `delta_v` settled: velocity change minus gravity's share = what an accelerometer reports (0 in free fall, 0.082 m/s per step at rest, large when hit).
* Measured: 3 m feet-first drop -> landing predicted 0.66 s ahead (0.67 analytic), on a foot; landing 3.2 m/s (body) / 3.6 m/s (head) in one step; sedan at 40 km/h on a braced pedestrian 2.4 / 2.7 m/s in one step; 30 N*s at the head of a floating body -> 22.8 kg m^2/s, conserved.
* Tests: senses (7), slope (1). Targeted run on x64-release: 57 cases green. Benchmark 79 us per character per step (8 characters, single thread; budget 100; the figure before perception / behaviours / senses was not recorded). x86 builds via dragdoll-sa; deployed to the running game.
* In the game: an F7 drop through the input channel created the ragdoll on real terrain, but the game was then paused (clock stopped), so its senses on real terrain are not checked yet.

**Next**
* Founder: unpause, press F7, watch the ragdoll's "senses" in its status / a capture.
* Then 4.2's code half (impact report: events, wrap-around distance, launch speed, throw distance, measured class) and 4.3 (passive baseline) — both now unblocked. 4.6 (Impact stage) — Fable.

**Open questions**
* Benchmark headroom is 21 %: record a baseline before the Impact stage adds work; the contact log allocates per step (reuse the vector's capacity: it does, but the per-character temporaries could be pooled).
* `delta_v` is per 1/120 s step; injury-style measures need a window (the impact report can integrate).

---

## 2026-11-17 — Two founder-found bugs: ragdoll through the level, outlines on every driver
**Changed** (local, not committed)
* **The F7 ragdoll fell through the level** (it only collided with cars). Capture: it lay on a car with up to 16 bodies in contact, slid off, and fell 755 m until the too-fast guard failed it — while its ground ray kept reporting the road 1.25 m below. Cause: Jolt's simulation ignores the back faces of mesh triangles, and SA winds its collision triangles the opposite way round (every car-mesh triangle had needed flipping; rays hit both sides, so the probes never showed it). Since the level collision task the level meshes had been inside out. `addBuildingColliders` now reverses the winding. The mod also removes an F7 ragdoll that ends up 40 m below the player.
* **Yellow outlines on every car, for its driver** (viewer only): the mirror recomputes threats and did not know who rides what. The tap streams `riding` per character; the viewer sets it on the mirror characters.
* **Verified in the game** (F7 through the input channel, senses read from the tap): airborne 0.08 s, landing predicted 0.49 s ahead on a foot; landed (2 bodies on the ground), folded (7, then 11 bodies), settled with the centre of mass 0.12 m above the ground; hardest head hit 2.8 m/s. First real-terrain run of task 4.1's senses.

**Next**
* 4.2's code half (impact report) and 4.3 (passive baseline) — Opus; 4.6 (Impact stage) — Fable.

**Open questions**
* The level winding fix assumes SA is consistent (car meshes were, 12 of 12 triangles). If a ragdoll ever passes through one piece of level, that piece is wound the other way: make level meshes two-sided then.

---

## 2026-11-17 — Task 4.2 (code half): the impact report
**Changed** (local, not committed)
* `core/src/impact_report.cpp`: per character, the first hit by a moving object since creation / handover, measured from the senses after every step: first contact (lowest touching body; zone and height from the probed silhouette), contact order, head strike (time, zone, wrap-around distance along the contour from the ground, head speed relative to the vehicle), separation (25 ms apart) and launch (centre-of-mass velocity, angle, height), apex, first ground contact after separation, rest (still 0.5 s); carry, flight+slide and throw distances (horizontal); the throw corridor for the measured launch; pitch about the vehicle's left axis until ground contact; the measured class (wrap if head or upper trunk touched bonnet / windscreen / roof, else forward projection; roof vault if over the roof at separation; fender vault at a corner with a sideways launch; somersault = wrap with > 270 deg pitch). Silhouette points now also keep their (behind-the-front, height) values. The report restarts at every kinematic -> dynamic handover.
* API `DdImpactReport`, `DdZone`, `dragdoll_character_get_impact_report`; live tap `impact`; viewer line "impact: ...".
* **First numbers** (limp, side-on, 40 km/h, braking 7 m/s^2 from contact; tests print them):

| Vehicle | Class | First contact | Head strike | Launch | Throw (carry + flight/slide) | Flight/slide vs corridor |
|---|---|---|---|---|---|---|
| sedan | wrap | shin, front, 0.32 m | 442 ms on the bonnet, WAD 1.53 m, 15 km/h | 29 km/h | 10.3 m (6.3 + 4.0) | 4.0 vs 5.6-7.9: short |
| sports | forward projection | shin, front | none | 29 km/h | 10.8 m (8.2 + 2.6) | short |
| SUV | forward projection | shin, front | none | 36 km/h | 10.4 m (4.1 + 6.3) | inside |
| van | forward projection | shin, front | none | 35 km/h | 10.2 m (3.5 + 6.7) | inside |
| bus | forward projection | near hand, front, 0.87 m | 25 ms on the front face | 43 km/h | 9.3 m (2.0 + 7.2) | short |
| motorbike | forward projection | shin, front | none | 37 km/h | 11.9 m (5.9 + 6.0) | inside |

* Against the literature (reference file §2-§3), these are **findings for task 4.3, not report bugs**: the sedan's head strike is three times late (442 ms vs 50-140 ms) and slow (15 vs ~33 km/h), the sports car carries the body 8 m on its bonnet without a head strike; flights + slides of the low cars are short of the corridor. Likely suspects: friction between body and bonnet (the default 0.6 on both sides), the limp body's stiffness at hips and knees, the braking from contact (in real crashes braking often starts earlier).
* Tests: impact report (2). Targeted run on x64-release: 59 cases green. Benchmark 81.5-83.8 us per character per step (79 before the report; budget 100). x86 builds (via dragdoll-sa).

**Next**
* Task 4.3 (passive baseline): the 6 vehicle classes x 10-80 km/h x seeds, headless, table against the reference numbers; fix body / vehicle / friction until the pattern holds — Opus.

**Open questions**
* The braking model in the tests and scenarios (from first contact) decides much of the carry distance: 4.3 should sweep "brakes before / at / after contact".

---

## 2026-11-17 — Task 4.3 started: the passive baseline runner and its first findings
**Changed** (local, not committed)
* `tools/baseline` (`dragdoll_baseline`): limp pedestrian standing host-animated until the vehicle is one step away, then handed over (the literature's cadavers are held and released the same way); vehicle packs x speeds x headings x lateral offsets; per run the impact report -> CSV; summary per vehicle and speed. Switches: `--brake contact|none`, `--car-friction`, `--tension`, `--rig file`, `--trace` (body every 25 ms). 168 runs in 6 s (x64-release, single thread).
* **Found (v0, default rig, braking at contact):**
  * Heading decides the class: side-on (0 / 180 deg) mostly forward projection below 40 km/h, wraps above; facing or back to the car (90 / 270) mostly wraps (the bonnet edge meets the thigh first).
  * Low cars carry the body far (sports car 8-9 m) and 10 of 168 runs never separate: body-on-car friction is 0.69 (Jolt combines body 0.8 and car 0.6 as a geometric mean), the braking car decelerates at 0.71 g, so the body barely slides off. Lowering car friction to 0.3 / 0.12 does not change head strike times.
  * **Sedan head strikes are 2-3 times late** (220-430 ms vs 50-140 ms) while the wrap-around distance is right (1.8-2.1 m vs 1.5-2.3). Ruled out by experiment: braking (constant speed: same), car friction, a larger and lower pelvis (0.82-1.08 m, 34 cm wide), stiffer hips (25 N*m passive). The trace: the trunk rotates as a whole at about 6-9 rad/s (a rigid-body estimate gives ~11), the spine stays straight, the legs whip up to 2.1 m, and the pelvis rides at 1.07-1.4 m - so the head must pass horizontal before reaching a sedan's bonnet (~0.9 m). In real wraps the pelvis sits on the bonnet edge.
  * SUV / van head strikes are close to the literature (50-125 ms at 30-80 km/h); flights + slides mostly fall outside the corridor (both short and long).
* Nothing in the library changed for 4.3 yet.

**Next**
* Compare whole trajectories, not one time: published PMHS pelvis / head (and T1) trajectories relative to the vehicle over time for sedan-type cars at 40 km/h (Kerrigan et al., ESV / Stapp; open ESV proceedings) -> a reference corridor in `docs/reference/`, and a trajectory plot from `--trace`. Then decide what the body lacks (pelvis height on the bonnet, lumbar / hip passive stiffness, leg mass, contact geometry).

**Open questions**
* Whether 1-step-before-contact handover is fair: a limp body has no time to sag; cadavers are released earlier (they sag a little).

---

## 2026-11-17 — Ragdoll size, skin-frame orientation, animation labels (founder requests from the live view)
**Changed** (local, not committed)
* **`DdCharacterDesc.size_scale`** (library): every rig length times it, mass times its cube (on top of `mass_scale`); recorded (format 4), streamed, mirrored by the viewer. Test: at 1.09 the head stands 9 % higher (1.78 vs 1.64 m), mass 97 kg, still balances.
* **dragdoll-sa sizes each puppet** from its model's skin rest pose: head-to-ankle distance against humanoid16's 1.394 m. Measured: CJ 1.53 m -> 1.094 (matches the earlier centre-of-mass estimate of +9 %), male peds 1.09-1.12, others 1.00-1.02. SA's people are ~1.9 m: next to them SA cars are no longer oversized (relevant to 4.3).
* **Skin-frame orientation from the rest pose alone** (founder: "hookers have problems in their ragdoll torso geometry"). The old fit chose among 24 axis rotations by comparing with the current pose — and a ped that has just streamed in has all its bone matrices at zero, so the heading was arbitrary (logged fits of 31-41 cm, odd rotations). Now: the skin's up (pelvis -> head bind) must map to +Z and its left (right thigh 51 -> left thigh 41 bind) to -X; the current pose only reports how well it matches. All ped models now get the same rotation; CJ gets his own (0.9 cm on his idle pose). Per-model memory of fits kept as a fallback for skins without those bones; a one-time diagnostic logs the raw bind data of any model that still fails.
* **Animation labels:** `dragdoll_character_set_label` (library; host text, <= 63 bytes, recorded when it changes, streamed, shown above each head in the viewer). dragdoll-sa labels each puppet with its main animation, read from the game's own tables at run time: e.g. `walk_civi (man)`, `CAR_sit (default)`, `WOMAN_walkpro (pro)`, `IDLE_STANCE (player)`, `CHOPPA_Ride (choppa)`.
* **Test-harness bug found:** at the end of a recording `dragdoll_replay_advance` stays DD_OK with `at_end` set; a new test looped forever (10 min of CPU), an older one spun 100000 frames under its cap. Both now stop at `at_end`.
* Tests: size scale (1), label (1). Targeted run: 29 cases green in 1.8 s (x64-release); x86 builds; hot-reloaded into the running game and checked through the live tap and the log.

**Next**
* Founder: look at the overlap of ragdolls and models in game, and at a hooker's torso; a capture if anything is off.
* Then back to 4.3 with scaled pedestrians: the baseline runner gets `--size 1.09` to measure SA proportions too.

**Open questions**
* The scale is uniform; SA models may differ in limb proportions (per-segment fitting would need the skeleton map to carry lengths).
* Mass follows size cubed (97 kg at 1.09); SA has no masses for peds to compare with.

* **Labels carry the animation's time** (founder): "name (group) time/total", the game's own current time and total length in seconds, unconverted (SA keeps time, not frame numbers), e.g. `IDLE_STANCE (player) 0.607/1.500`, `walk_civi (man) 0.750/1.133`. Founder confirmed: names appear, hookers look right.

---

## 2026-11-17 — Task 4.3: reference cadaver data; the head arrives too slowly
**Changed** (local, not committed)
* Reference (read in full, open access): Paas, Östh & Davidsson 2015 (IRCOBI) + Paas 2015 thesis - 5 cadavers, small hatchback, 40 km/h, side-on, walking posture: WAD 1.70-2.25 m, head impact speed 8.7-14.5 m/s (one < 1 m/s: arm support), head contact ~138 ms, pelvis slides 30-45 cm over the bonnet before head contact, elbow contact 37-58 ms before the head. Added to `docs/reference/pedestrian-impact.md` §2.
* Runner switches `--gait` (walking posture: arms swung, elbows / forward knee bent) and `--size` (size_scale).
* **Our side-on sedan at 40 km/h vs the cadavers:** head contact 325 ms vs ~138; WAD 1.89 m (inside); head speed 0.2-0.3 x vehicle speed vs ~1.0; pelvis sliding ~10 cm vs 30-45. Facing the car the head contact is 125 ms (inside), side-on it is 300-500 ms.
* **Ruled out since the last entry:** walking posture (head contact later, not earlier), mass distribution (de Leva 1996 shares). Earlier: braking, car friction, pelvis size, hip stiffness.
* The clearest signal: the head is carried forward early (about 6 m/s within 100 ms) instead of staying put until the windscreen reaches it - something couples head and upper trunk to the pelvis too strongly or loads them too early in a side-on hit.

**Next**
* Systematic comparison with time histories: our head / T1 / pelvis paths relative to the car against the cadaver kinematics (the thesis' appendices; Kerrigan et al. corridors if obtainable), one variable at a time: lateral spine and neck stiffness / limits (side bending is what a side-on hit loads), shoulder girdle (no clavicle: the arm hangs from the upper spine), contact between the near arm and the trunk, solver stiffness of the joint limits. (Opus: Fable is no longer available.)

**Open questions**
* Whether the near arm should collide with the trunk at all in the rig (in the body it does, through soft tissue).

---

## 2026-11-17 — Task 4.3 continued (Opus; Fable no longer available): narrowing the late head strike
**Changed** (local, not committed)
* Rig format: `"no_collision_with": [body names]` (soft tissue between bodies, e.g. arms vs trunk); rig.cpp + character.cpp. Runner: `--gait`, `--size`, `--arms-forward`, `--kick` (floating limp body, only the pelvis given 11 m/s).
* **Experiments (sedan / sports / SUV, side-on, 30-50 km/h):**
  * Arms raised forward: the trunk turns much faster (head from 1.64 to 1.05 m in 150 ms, ~1.1 m back over the bonnet - close to the cadavers' timing) but the arms then land on the bonnet and hold the head off it. Arms are props, not the main cause.
  * Arms passing through the trunk (no collision with pelvis / spine), with and without walking posture: no improvement (head speed at contact 0.16-0.35 x vehicle speed).
  * Hip side-swing limit 20 / 10 deg (from 40): pelvis motion identical.
  * Freer spine and neck side bending (35 / 30 / 60 deg, low friction): the spine still bends at most ~24 deg in the first 50 ms and then straightens.
  * Pelvis sliding over the bonnet is ~57 cm before 150 ms vs 30-45 cm in the cadavers: not far off.
  * Kick test: the pelvis keeps only ~2 m/s of an 11 m/s kick (the hip joints share it with the thighs at once, correct for ball joints); head 0.07 m/s after 25 ms, 0.38 after 200 ms. The solver does not over-transmit.
* **Reframed:** the cadavers' ~11 m/s head speed at contact combines the head's motion relative to the car and its fall onto it. Our head's forward motion is not wildly off; the fall is missing (~3 m/s): the trunk rotates too slowly (6-9 rad/s; a rigid trunk whose base is yanked to 11 m/s would turn at ~22 rad/s).
* **New suspect:** Jolt's `RagdollSettings::Stabilize()` (called in `buildRagdollSettings`) redistributes mass along chains: the pelvis simulates at 5.1 kg instead of the rig's 8.4 kg (11.17 % of 75). The simulated body is not the designed one.

**Next**
* Print every segment's simulated mass and inertia against the rig; run the baseline with Stabilize off (or with its mass changes undone) and compare trunk rotation and head strike.
* If it is not that: trace the angular velocity of pelvis and upper trunk separately in the car hit, and the contact force at the bonnet edge vs time.

**Open questions**
* Whether `Stabilize()` is needed at all with 120 Hz steps and LinearCast (it was added for solver stability in Phase 1).

---

## 2026-11-17 — Task 4.3: the cause of the late head strike - Jolt's Stabilize() changed the body's masses
**Changed** (local, not committed)
* **Found:** `RagdollSettings::Stabilize()` (called since Phase 1) redistributes mass along chains for solver stability. On humanoid16 it gave every segment 3-6 kg: hand 0.46 -> 3.05 kg, forearm 1.22 -> 3.82, upper arm 2.03 -> 4.77, foot 1.03 -> 3.91; trunk segments 12.25 / 11.97 -> 6.10 / 5.97, thigh 10.62 -> 6.10, pelvis 8.38 -> 5.09 (total unchanged). Each arm weighed 11.6 kg instead of 3.7, the trunk 17 kg instead of 33: heavy arms propped and carried the trunk. This affected every simulation since Phase 1.
* **Removed** (`character.cpp`): the rig's own mass ratios (at most ~3:1 between neighbours) solve fine at 120 Hz. Dynamics tests (balance acceptance: quiet standing, hold_stance 60 s, bump_5kmh at 30-200 fps; scenarios; determinism; recordings; activation; behaviours; senses; impact report): 77 cases green after widening one pose round-trip tolerance from 5 mm to 1 cm (joint stretch mid-crumple now reaches ~9 mm). Benchmark 77.4 us per character per step (82-84 before).
* **Baseline v1** (all headings, `build/baseline/baseline_v1.csv`): sedan 40 km/h head contact 183 ms median (was 325; cadavers ~138), head speed at contact 0.94 x vehicle speed (was 0.2-0.3; cadavers ~0.8-1.3), WAD 2.18 m (cadavers 1.70-2.25); sedan 50 km/h 133 ms. SUV 83 ms and van 125 ms at 40 km/h (earlier than the sedan, as Li et al. 2021 report for SUV / MPV). Bus: forward projection at every speed. SUVs and vans now mostly wrap with the head on the bonnet (WAD 1.6-1.8 m).
* Still off: low cars at 10-30 km/h carry the body until they stop; 112 of 152 finished runs land short of the throw corridor - mostly runs where the body rides the bonnet 7-9 m, then slides off the front low and downward with the legs on the road (the corridor assumes a real flight). Car-surface friction 0.3 / 0.12 does not change this. Runs with a real flight (e.g. SUV side-on 40 km/h) land inside.
* Rig format `no_collision_with` kept (unused by humanoid16). Runner switches kept.

**Next**
* Founder: judge `hit_sedan_40kmh` (and the other hit_* scenarios) in the viewer, and an F7 ragdoll in the game: they now move with the designed masses.
* Then: why bodies stay on low bonnets (braking model at contact vs the cadaver tests' 10 ms; leg-road contact while carried); revisit the ADR-0024 acceptance "SUV / van -> forward projection" against the literature for adults (Simms 2013: forward projection when the pedestrian is short relative to the bonnet edge).

**Open questions**
* Should the throw corridor be checked only for runs with a real flight (no ground contact at separation)?

---

## 2026-11-17 — Task 4.3: SUVs and vans are forward projection with a head strike (class rule fixed)
**Changed** (local, not committed)
* Founder verdict: `hit_sedan_40kmh` and the other hit_* scenarios look right; in game, ragdolls now match the models' height (alignment not exact, accepted).
* Impact report: `contact_seconds` (first contact -> separation) and `com_over_front` = the most the centre of mass got behind the object's foremost point while in contact (-100 unknown). Runner CSV gains `com_over_front, contact_s, lead_height, com_height, lead_setback, bonnet_length`.
* **Finding:** on SUVs (leading edge 1.00 m) and vans (1.06 m), with the body's centre of mass at 0.97 m, the head and chest fold onto the bonnet / windscreen (head 75-83 ms, so the old rule said "wrap") but the centre of mass gets only 0.09-0.10 m behind the front (median; leading edge sits 0.15 / 0.25 m back): the body is pushed ahead, not carried. Sedans 0.34-1.35 m, sports cars up to 2.5 m, motorbikes 0.10-0.65 m.
* **Measured class rule** (`impact_report.cpp`, ADR-0024 amended): wrap now also needs the centre of mass carried past the leading edge; roof vault is checked only for wraps. Baseline v4 (`build/baseline/v4.csv`, 20-80 km/h): bus 24/24 forward; van 24/24 forward; SUV 19 forward / 5 wrap (side-on from one side at 40-60 km/h and 80 km/h front/back-on: borderline, leading edge level with the centre of mass); sedan 20 wrap / 2 somersault / 1 roof vault; sports 13 wrap / 3 somersault. The ADR-0024 §6 pattern holds. Impact-report tests green (3 cases).
* Gravity: the baseline uses 9.81 m/s² in both the simulation and the corridor, so it does not explain short throws; those are bodies riding low bonnets and sliding off with the legs on the road. SA's own gravity goes to ideas (measure in game), with vehicles bumping over bodies.

**Next**
* Why bodies stay on low bonnets at 10-30 km/h and slide off low (braking at contact vs the cadaver tests; leg-road contact while carried); check the throw corridor only for runs with a real flight.

**Open questions**
* Is the 5/24 SUV wrap share realistic for adults? (Literature: borderline when the leading edge is level with the centre of gravity.)

---

## 2026-11-17 — Task 4.3: the throw corridor was the wrong check, not the throws
**Changed** (local, not committed)
* **Bodies riding the bonnet** (trace of sedan 40 km/h side-on): after the head strike the body lies still on the bonnet while the car brakes at 7 m/s²; body-on-car friction (0.6 x 0.8 combined, ~6.8 m/s² at most) holds it until the car nearly stops. With car friction 0.2 it slides off ~0.4 s sooner. That is the physics of the carry phase; the total throw (10.9 m at 40 km/h, 22 m at 60) was never short. Body-on-bonnet friction stays unsourced (no open copy of Wood 2005 / Bhalla 2002 / the throw regressions could be fetched).
* **Corridor** (`impact_report.cpp`, header, ADR-0024 amended): (1) the general throw equation at the *measured* launch angle over body-on-road friction 0.66-0.84 - bodies leave SUVs / vans heading 4-8 deg downward, which the 0-45 deg form called 20 % short; (2) only for a real flight (feet off the road at separation), else `in_corridor = -1` and corridor -1 - buses push the body with the feet on the road; (3) separation = the last departure before the first ground contact: bounces on the bonnet had marked "separations" at 0.09 s followed by 2 s "flights". Viewer report text and the runner summary follow ("in corridor (of flights)").
* Baseline v6 (`build/baseline/v6.csv`), flights at 30-80 km/h inside the corridor: sedan 6 of 8, SUV 13 of 15, van 16 of 16, motorbike 15 of 16; sports cars almost never give a flight (the body slides off the low bonnet), 80 km/h -> roof vault 4/4. Classes unchanged by the corridor work. Impact-report tests green; `--trace` now runs 1.7 s and shows road contacts.

**Next**
* Founder: review a sample live (sedan / SUV / van at 40 km/h in the viewer) for the 4.3 gate; then the full four-preset test run (needs the founder's go) and commit.
* Then: instability sweep 5-120 km/h (gate), and the low-speed bodies left on low bonnets (sedan 10, sports 10-30 km/h never separate).

**Open questions**
* Body-on-bonnet friction (clothing on paint): needs a source; it sets how long bodies ride a braking car.
* Do bodies really stay on a sports car's bonnet at 10-30 km/h with immediate hard braking?

---

## 2026-11-17 — Task 4.3: literature read; throws match real-world accidents
**Changed** (local, not committed)
* Founder downloaded the papers automated fetching could not reach (sites block robots; ask the founder from now on). Read: Wood, Simms & Walsh 2005 (validated wrap / forward-projection models, Table 1 speed-distance bands from 185 + 21 real accidents, retardation coefficient 0.561 +- 0.101, restitution, impact times, separation mechanism); Batista 2008 (throw model; PC-Crash set-up with car-pedestrian friction 0.2; staged braking tests); Neale et al. 2021 (side-swipe throws much shorter than frontal formulas). The SafetyNet D3.1 report is road-safety statistics, nothing for us. All in `docs/reference/pedestrian-impact.md` §3 with sources.
* **Result:** baseline throw distances sit on Wood's mean curves (sedan 40 km/h 10.5-11.0 m vs mean 10.4; 60 km/h 21.4-22.5 vs 22.4; bus 40 km/h 9.9-11.0 vs forward mean 11.6) - 152 of 154 finished runs at 20-80 km/h inside the 0.1-99.9 percentile band for their class; the two outside are sports-car roof vaults at 80 km/h (outside the wrap model).
* Runner: new summary column "in real-world band (Wood 2005)" (`woodBand()` in `tools/baseline/baseline.cpp`); ADR-0024 amended: this band is the primary throw check.
* Fixed: `in_corridor` now starts at -1 (runs that never finish no longer read as "outside").
* Effective retardation of our body in real flights ~0.76 (SUV / van / motorbike / sedan), vs Wood's 0.56 +- 0.10 and dummy slides 0.73-0.84: at the sticky end. Not tuned: total throws already match. Wood also confirms two things seen here: forward-projection bodies keep their feet on the ground (no flight), and below ~6 m/s the body drops back onto the bonnet (low-speed riding is real).

**Next**
* Founder: review a sample live (sedan / SUV / van at 40 km/h) for the 4.3 gate; then the full four-preset test run (founder's go) and commit.
* Then the instability sweep 5-120 km/h (gate).

**Open questions**
* Body slide retardation 0.76 vs Wood's 0.56: lower the rig's body friction later (lengthens slides, shortens nothing else)? Only if the in-game look calls for it.
* Body-on-bonnet friction still has no measurement (0.2 in a PC-Crash set-up is a choice); Bhalla et al. 2002 not read (IRCOBI host refused).

---

## 2026-11-17 — Task 4.3: Bhalla 2002 and Untaroiu 2008 read; the wrap body is never thrown up
**Changed** (local, not committed)
* Read (founder downloads): Bhalla et al. 2002 (IRCOBI; MADYMO multi-body reference runs, Happer 2000 empirical fits, sensitivities) and Untaroiu et al. 2008 (reconstruction set-up, gait data). Saulić 2020 was already read. Reference file §3 updated, sources added.
* **Finding:** in Bhalla's MADYMO wrap (adult, small car, 0.7 g braking from 40 km/h, car friction 0.5) the car contact lasts ~270-300 ms and the lower torso follows an upward parabola; adults rise > 50 % on an SUV at higher speeds. Ours (side-on, 20-60 km/h): pelvis rises 0-15 %, sedan contact 0.9-1.6 s, launch 15-22 deg downward; Wood's model gives ~0.12 x combined speed upward after the head strike (~1.4 m/s at 40 km/h).
* Car friction 0.6 / 0.31 / 0.2 (combined 0.69 / 0.50 / 0.40) changes contact time and throws only a little: not the cause. Published set-ups use 0.5, 0.4 or 0.2 (choices, not measurements); kept at 0.6 for now.
* Total throws still match Wood's and Happer's real-world curves.

**Next**
* Find where the wrap body's rotation goes: trace angular momentum of the whole body and per segment from first contact to 400 ms (sedan, 40 km/h, side-on); suspects: zero restitution on body parts, joint friction, the head / chest strike absorbing it, the legs being held by the bumper. Compare with Bhalla's lower-torso trajectory.

**Open questions**
* Contact in our report counts any touch (a dragging hand); MADYMO's "vehicle contact" may count only load-bearing contact - compare like with like before judging the 0.3 s.

---

## 2026-11-17 — Task 4.3: why the wrap body is not thrown up - narrowed, not solved
**Changed** (local, not committed)
* Runner: `--momentum` (centre-of-mass height and speed relative to the car, angular momentum about the car's left axis, bodies touching the car with their delta-v, every 25 ms for 0.8 s) and `--car-restitution`.
* **What happens** (sedan 40 km/h, side-on): the bumper / leading-edge strike gives the centre of mass ~5 of 11 m/s by 50 ms; rotation about the left axis peaks at -125 kg m²/s and drops to -40 at 150 ms when the arms (125 ms) and head (150 ms) land; the trunk then slides **up the windscreen slope** with the car pushing only through friction (centre of mass 3.9 m/s slower than the car at 150 ms, level only at ~450 ms). The upward motion we have (+1.2 m/s at 200 ms, +0.19 m by 450 ms) is that ramp, not a throw. The legs swing up around the hips (shins to 1.6 m) while the trunk stays down.
* **Ruled out, one at a time** (contact time, launch angle, apex at 30 / 40 / 50 km/h, front- and side-on): pelvis reaching lower + thicker thighs; joint friction x0 and x3; car friction 0.2-0.6; car restitution 0.3; hip flexion limited to ~90 deg (straight-leg); all leave bodies leaving 15-57 deg downward with contact > 1.2 s. Very high car friction (2.0-5.0) brings the body to car speed by ~200 ms (closer to MADYMO's ~300 ms separation) - consistent with Wood's model treating the head / shoulder strike as sticking (real windscreens and bonnets dent and grip) - but then the body sticks to the bonnet and never leaves.
* **Conclusion:** two things are missing at once - grip at the secondary (head / shoulder) strike, and an upward / forward throw afterwards - and no parameter of the current contact model gives both. The comparison target so far is a multi-body model (MADYMO), not measurements; total throws already match real accidents. Not tuning blind.

**Next**
* Get measured cadaver trajectories (pelvis, T1, head relative to the car, 0-300 ms, full-scale PMHS tests: e.g. Kerrigan et al. 2005/2008, Subit et al. 2008) - founder to download - then compare like with like; if the gap is real, design the secondary-strike contact (local grip / deformation zone on bonnet and windscreen) as an ADR.
* Meanwhile 4.3 can close on what is validated: classes, head-strike timing, WAD, total throws (Wood 2005 bands) - founder's call.

**Open questions**
* Is a bonnet "deformation grip" the right model, or does the real body leave because the car pitches nose-down under braking and the body rotates over it? Both need the cadaver trajectories to decide.

---

## 2026-11-17 — Task 4.3: cadaver tests read; what they can and cannot settle
**Changed** (local, not committed)
* Founder downloaded IRCOBI papers through web.archive.org (ircobi.org is down; the site's archive index is in the workspace `research/ircobi.json`) plus Forman et al. 2015 (Stapp). Read: Paas et al. 2012 (IRC-12-79), Song et al. 2017 (IRC-17-25), Kerrigan et al. 2012 (IRC-12-75), Forman et al. 2015. Reference file §2 and sources updated.
* **Up to the head strike we match the cadavers:** real braking small sedan at 40 km/h (FSR02): pelvis on the leading edge 19 ms, elbow 74, shoulder 114, head 120 ms (WAD 2.00 m); other tests head 135-161 ms, WAD 2.0-2.2 m. Ours (sedan 40): head 133-192 ms, WAD 2.16-2.22 m. Our near arm lands at ~125 ms (cadaver elbow 74-117: we are a little late). The cadavers' "pelvis" contact is the hip / greater trochanter, which is the top of our thigh capsule (touching from ~25 ms), not our pelvis box.
* **After the head strike there is no cadaver trajectory:** every test catches the body (net, belts, sled stop) after ~120-250 ms. The one measured outcome with a real braking car is FSR02's throw: **11.2 m** forward (ours sedan 40 km/h: 10.5-11.0 m). So the "contact ~0.3 s, thrown upward" picture is MADYMO's, not a measurement.
* Forman 2015 confirms one mechanism from the previous entry: the shoulder **pockets into the windscreen** and pins, the head rotates about it, the lower body keeps moving (spine buckling). Our rigid windscreen lets the trunk slide up instead. Head angular velocity before the strike > 40 rad/s in real tests: worth measuring in ours.

**Next**
* Founder: close 4.3 on the validated measures (classes; head strike timing and WAD vs cadavers; total throw vs Wood 2005 bands and the FSR02 throw), with "secondary-strike grip / pocketing" as a later design item (ideas.md, ADR when needed) - or keep it open.
* Then: full four-preset test run (founder's go) and commit.

**Open questions**
* Do players see the long ride on the bonnet as wrong in game? That, not MADYMO, should decide whether pocketing gets built.

---

## 2026-11-17 — Task 4.3: founder accepted the validated measures; the high-speed gate fails
**Changed** (local, not committed)
* Founder accepted closing 4.3 on the validated measures (classes, head strike vs cadavers, throws vs Wood 2005 and FSR02). Before marking it done, ran the remaining gate check, "no instability 5-120 km/h": `dragdoll_baseline --speeds 5,15,25,35,45,70,90,100,110,120 --offsets 0,-0.3,0.3` (720 runs, `build/baseline/sweep.csv`).
* **Fails:** 14 runs end in DD_STATE_FAILED (instability detector): side-on sedan at 70 (offset 0.3), 110, 120 km/h; sports side-on at 90, 100, 110 km/h (all offsets); van front-on at 100 km/h. And bus side-on at 90 km/h (all 6 runs): the centre of mass ends 5-10 m behind the bus's front at 0.1-0.26 m height, launch 0.18-0.28 x speed - the body passed under or through the bus (clearance or tunnelling), and the class rule calls it a roof vault. Largest head delta-v per step ~31 m/s at 120 km/h (bus, SUV).
* Workplan: 4.3 stays open with the validated part ticked and this as its last item; the "thrown upward" item is parked under ideas.md "pocketing".

**Next**
* Reproduce one failure with `--trace` (sports 100 km/h side-on; bus 90 km/h side-on), find the first bad step (joint separation, tunnelling through the vehicle, body under the bus), fix, rerun the sweep.

**Open questions**
* Is the bus case tunnelling (fast thin contact) or a real gap under the bus in the pack geometry (clearance vs a lying body)?

---

## 2026-11-17 — Task 4.3 closed: fast vehicles tunnelled through the body
**Changed** (local, not committed)
* **Cause:** one collision check per 120 Hz step. A bus at 90 km/h moves 0.21 m per step - more than a limb's thickness - and its surface is a hollow triangle mesh, so body parts ended up behind the front face and were pushed out through the underside (centre of mass dropping at -5.5 m/s within 25 ms, the bus passing over the body). The same at 70-120 km/h for side-on low cars ended in DD_STATE_FAILED.
* **Fix** (`core/src/world.cpp`): `driveKinematics` returns how far any host-driven collider's surface moves this step (translation + rotation at its outermost point); the physics update uses `ceil(that / 0.05 m)` collision sub-steps, 1-4. Slow scenes are unchanged (1 sub-step); only fast vehicles pay.
* **Sweep** 5-120 km/h, 6 vehicles x 4 headings x 3 offsets (`build/baseline/sweep2.csv`): 720 runs, **0 failures** (was 14), bus 120/120 forward projection (6 false roof vaults before). Baseline 20-80 km/h (`v8.csv`): every finished run inside Wood's real-world band; side-on sedan 40 km/h now head strike 125 ms, WAD 2.07 m, head 1.15 x vehicle speed (cadavers 120-161 ms, 2.0-2.2 m, above impact speed) - closer than before. Tests: impact report, determinism, scenarios, hits, vehicles, kinematics, senses - 39 cases green (x64-release; x86 not built this session: founder rule on builds).
* Workplan: 4.3 done.
* Full four-preset run (founder's go): x64-debug, x64-release, x86-debug, x86-release - smoke, unit (all doctest cases) and bench_budget all pass.

**Next**
* Commit the 4.3 work (founder's word).
* Then 4.6 Impact + Carried + Guard (4.2 and 4.3 now done; 4.4 and 4.5 were already).

**Open questions**
* Sedans at 50 km/h now all count as somersaults (pitch -317 to -535 deg): the rule counts rotation from first contact to first ground contact, including the slow roll while riding the bonnet. Ravani's somersault is a rotation in the air - count pitch only after separation?

---

## 2026-11-17 — Task 4.6 started: handover on impact, Impact / Carried stages (awaiting founder review)
**Changed** (local, not committed)
* **Core:** `dragdoll_character_set_auto_handover(character, lead_seconds)` / `get_auto_handover`: a kinematic (host-animated) character turns dynamic by itself, keeping its animated velocities, when its threat is due within `lead_seconds` (0 = off). Recorded (format 5, record AutoHandover). The baseline runner's handover logic, now in the library for every host.
* **SDK:** the percept carries body transforms, each body's region and the head index.
* **car_hit** (ADR-0025 stages): **Impact** at first vehicle contact - a tensed struck leg yields at once; trunk and neck co-contract after the reflex latency. **Carried** (on the vehicle, not the ground, reflexes available) - Guard (chin tucked via pose "guard", forearms aimed between the head and the nearest vehicle contact) and Curl (pose "curl": spine 10-15 deg, hips 35, knees 40); trunk eases to relaxed over 0.5 s. **After** (off the vehicle 0.1 s) - relaxed with Guard, placeholder until 4.7. **Limp** at vitality 0 (the limp tier, ADR-0025 §2; one test updated to that meaning). Numbers and poses in `packs/behaviours/car_hit.json`.
* **Scenarios:** `react_<vehicle>_40kmh` (6): kinematic standing, unaware, car_hit, auto handover. Tests: impact chain (handover just before contact, brace 0.08-0.11 s after contact when unaware, carried, after; struck leg yields when startled), no auto handover -> stays kinematic. Behaviour / recording / replay / determinism / aim / scenarios green (x64-release).
* **Metric** (runner `--behaviour car_hit [--aware]`; 30/40/50 km/h x 4 headings x 3 offsets): unaware reflexes leave the head strike about as passive (sedan median per-step head delta-v 7.5 -> 8.4 m/s, max 17.9 -> 15.6; head speed at the strike 0.87 -> 1.01 x v: the braced neck carries the head with the trunk) - expected, the brace lands just before the head strike (ADR-0025 §3b). Aware (startle + shield): slower head strike (sedan 0.71 x v, motorbike 0.82 vs 0.99) and some strikes move to the bonnet, as with the cadaver whose arm propped the trunk. No failures.

* **Founder review 1:** "the doll covers its face, looks very cool"; an unaware pedestrian should react accordingly. Changed: Guard + Curl now wait for the reflex latency only when the character was startled before the contact (it saw the car); taken by surprise it waits `guard.unaware_latency` (0.25 s, placeholder - to be sourced). Unaware sedan 40 km/h: guard 0.24 s after contact; aware: as soon as it lies on the car (0.15 s). Test extended.
* **Founder review 2:** worse - "liked it more before"; when airborne after the hit the arms change pose, the guard comes too quick and too stiff. Cause: with the later guard the unaware body was already leaving the car, so the arms snapped into the guard in the air, at full tension within 0.05 s. Changed: a guard starts only while lying on the car (kept, never started, in the air); arms reach full tension over `guard.blend` 0.3 s; `guard.unaware_latency` back to 0.1 s (the setting stays). Guard now 0.1 s after contact (unaware) / 0.15 s (aware).

* **Founder request:** more rotation when hitting the floor - "looks slippery, asphalt is abrasive". Measured (runner: new `ground_turn` column = degrees the pelvis turns from first ground contact to rest; `--road-friction`, `--road-restitution`; 20-60 km/h, car_hit): combined body-road friction 0.69 / 0.75 / 0.89 / 1.02 -> median turn 275 / 282 / 316 / 324 deg; bounce 0.2 changes nothing; limp / relaxed / car_hit 316 / 257 / 282 deg; throws stay in Wood's bands throughout. Slide deceleration (~7.4 m/s²) already matches dummy slides (7.2-8.2). Trunk edge rounding (Jolt box convex radius up to 95 % of the thinnest half extent) gives identical results - Jolt collides the boxes as sharp boxes - so that option was removed again. **Conclusion:** not friction; most likely the trunk's flat box faces (a box on its back slides, an oval torso rolls). Candidate fix: trunk shapes rounder in cross-section (capsules or convex hulls) - a rig change that touches every validated number, so it needs the founder's go and a full re-validation.

**Next**
* Founder: judge `react_sedan_40kmh` (and the other react_*): struck side gives, ~0.1 s brace, chin tuck / forearms / curl on the car, loose after.
* Then close 4.6 (x86 build, commit on the founder's word), start 4.7.

**Open questions**
* Should the neck co-contract at all when unaware? It raises head speed at the strike in our model; real neck tone is said to reduce cervical injuries (Forman 2015). Phase 5 can tune it against the cadaver head-velocity corridors.
* **Founder:** keep the rig as it is (no trunk shape change). The "different" feel came from testing react_bus / react_suv / react_van: forward projections never lie on the vehicle, so they have no Carried stage and no Guard - by design (ADR-0025: a forward projection has no Carried); the airborne Guard / Reach come with 4.7. Workplan: 4.6 done.
* Next (replaces the Next above): commit 4.6 on the founder's word; then **4.7 Airborne + Landing + Tumble**.
