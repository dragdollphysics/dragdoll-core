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

---

## 2026-11-17 — Task 4.7 started: Airborne / Landing / Tumble (awaiting founder review)
**Changed** (local, not committed; 4.6 committed as 1a08de9, not pushed)
* **car_hit**: After replaced by **Airborne** (Reach: both hands to where the head will meet the ground - head carried by the body's horizontal velocity for the time to landing, at ground level - elbows a little bent, arms ease in over 0.15 s, chin tucked; without time to reach, a guard begun on the car is kept), **Landing** (keep aiming until a body touches; then the arms yield, relaxed in 0.08 s), **Tumble** (relaxed, forearms loosely near the head; a bounce back up -> Airborne). A **head-falling** trigger (head < 0.9 m above the ground and dropping > 0.8 m/s) switches the arms from guard to reach also while still on the car (bodies sliding off head first). New tunables: reach.*, landing.*, tumble.*, after.tension, airborne.curl, impact.brace (pack).
* Runner: `--params file` (a behaviour pack replacing the built-in one).
* **Metric: first ground contact = head** (20-60 km/h, 4 headings, 3 offsets, ~320 finished runs each): limp 5.5 %; relaxed, no reactions 11.5 %; car_hit 13.2 %; car_hit without brace and limp after the car 12.0 % (noise about +-2 points). Muscle tone at all is what doubles head-first landings (a toned body pitches over like a stiff stick; a limp one folds and its limbs land first); the reactions neither help nor hurt measurably. Tried and no better: faster / earlier / straighter reach, curl in the air, limp after the car. Most landings come within a fraction of a second of leaving the car (slide-offs: flight ~0 s), too soon for any arm to arrive.
* Tests green (behaviour, scenarios, recording, replay, determinism).

**Next**
* Founder: judge `react_suv_40kmh` (forward projection: a real flight, reach visible) and `react_sedan_40kmh` (slide-off).
* Decide the 4.7 metric: "beat the limp tier" (5.5 %) is not reachable this way; "no worse than an alive body without reactions" (11.5 %) is met within noise. Tuck / extend (Ha et al. 2012) for the rotating flights (SUV / van, 0.3-1.5 s) is the remaining lever.

**Open questions**
* Is head-first ~12 % plausible for living pedestrians? No source yet for which body part touches the ground first in real crashes.
* **Founder review:** react_suv_40kmh "looks quite right"; the reactions read as instinct / reflex (landing on the hands, covering the face) - the intended frame. Changed: **any fall starts the chain**, not only a vehicle contact - a physics-driven body off any vehicle, airborne or tipped over (upright < 0.8) with the head dropping, starts the reflex clock; after 0.1 s the hands reach for the ground. Landing now means something other than the legs touches the ground (standing on the feet is not landing). Car-hit metric unchanged within noise (head first 13.6 %).
* **Scenarios** `death_drop` (standing, relaxed, car_hit, shoved backwards) and `death_drop_limp` (the same, limp) - the cheap landing test from ideas.md. Test: with reflexes the reach starts at 0.58 s and a hand lands first (0.78 s); limp falls faster, a dangling hand first at 0.55 s. 30 test cases green.
* **4.7 metric (proposed):** "first ground contact is not the head" no worse than a living body without reactions (relaxed: 11.5 %; car_hit 13.2-13.6 %, within noise). Beating the limp tier (5.5 %) is not a goal: a toned body pitches over like a stick. Tuck / extend in long rotating flights -> later (Phase 5 lab can tune it against this metric).
* **Founder:** death_drop looks better and more natural than limp; 4.7 done. Reminder from the founder: the first use case is SA vehicles hitting pedestrians - effort goes there first. The any-fall trigger is for the wider DragDoll scheme (car hits already enter through Impact); its landing code is shared with car hits. Next: 4.8 Settle + Handback, then the SA wiring (auto handover, car_hit, rest poses) so car hits can be judged in game.


---

## 2026-11-17 — In-game wiring (dragdoll-sa): car hits handed to DragDoll (awaiting the founder's first in-game test)
**Changed** (local, not committed; dragdoll-sa `src/logic.cpp`, `src/loader.cpp`)
* Every nearby pedestrian's puppet (kinematic, posed from the game's animation) now runs `car_hit` with relaxed tone and **auto handover** (12.5 ms lead). When DragDoll hands it over, the mod stops animating it and records the game's pelvis bone relative to the ragdoll's root.
* **Pose override:** the loader forwards plugin-sdk `pedRenderEvent` (just before a ped is drawn) to the logic (`dd_logic_ped_render`); for a ragdolled ped its bone matrices (world space) are rebuilt from the ragdoll: rotations via `dragdoll_skeleton_map_from_pose`, positions chained from the pelvis along the skin's bind offsets. Unknown yet: whether SA recomputes the bone matrices after that point (if so, the override must move).
* **Root sync:** after each DragDoll step the ped's position follows the ragdoll's pelvis, its speed is zeroed, its game collision is off (restored when released).
* **End (until 4.8):** released after 20 s, or when the ped is gone / far away. Ragdolled puppets are kept regardless of the 20-puppet limit.
* x86-release build of dragdoll-sa (core included) clean; logic + dragdoll.dll deployed and hot-reloaded; the loader changed -> game restart needed.

**Next**
* Founder: restart the game (then redeploy the loader), drive into pedestrians at 30-60 km/h with sedans / SUVs / vans; report how the hit, the flight and the landing look, and anything odd (pose jumping back, ped snapping, invisible ped). Captures as usual.
* **First in-game test** (founder captures live_20261117_135922 ... 140316; analysers in the scratchpad: hits / runover): hand-overs happen at real hits from walking, idling, turning and running animations; the first hit itself looks plausible (e.g. 53 km/h -> CoM 1.6 m, 7.8 m/s; 45 km/h -> 7.5 m/s). **"Takeoff"**: several bodies are launched a second time 1-3 s after the hit, at 17-21 m/s (67-75 km/h) and up to 4.2 m high - every time CJ's car is 2-5 m away (its front at the body): the player drives into the lying body; the car proxy is kinematic (infinite mass) and solid down to its clearance, the body is wedged under its front edge and squeezed out, and the game ped's own collision is off. Same issue as "vehicles react to bodies" (ideas.md), now visible in game. "Car in a strange position": nothing conclusive in the capture (wheel parts spin, so tilt says nothing); log oddity - car model 405's outline proxy has its nose 0.04 m above its floor (others 0.65-1.06 m). Releases at ~5 s without "ped gone" = the 60 m distance rule while CJ drives on.
* **Bodies on the ground, step 1 (founder's choice):** the game ped's own collision now comes back on once its body lies on the ground (3+ bodies on the ground, none on a vehicle, not carried) - SA's own car-over-ped bump should return; it stays off during the hit and on the bonnet. Log line "lies on the ground ... game collision back on". Deployed by hot reload. If not enough: DragDoll pass-through + capped push for landed bodies (ADR-0026 to write).
* **Bodies on the ground, step 2 (founder: "do the pass through now"):** core `dragdoll_character_set_vehicle_pass_through` (ADR-0026): once the body has landed after a hit, moving colliders' contacts with it are rejected in Jolt's contact validation (characters are known by their collision group: Jolt does not keep the rig's per-part user data - found while testing); recorded (format 6). Test: a car that stopped after the hit drives on at 8 m/s over the body - colliding it shoves the body at 8 m/s, passing through the body stays put (0 m/s). 27 related test cases green. dragdoll-sa sets it for every armed pedestrian (with the game collision back on once landed, step 1, for the game's own bump). Deployed by hot reload. The slight push is not done (only if the founder wants it).
* **Founder:** pass-through "not quick enough" - off for now (dragdoll-sa `kPassThrough = false`; the core API stays, ADR-0026). Founder's hypothesis for the launches: bodies caught by the spinning wheel cylinders (kinematic, rubber friction 0.9, surface ~14 m/s at 50 km/h) are wound in and flung out like a meat grinder. Test: wheel colliders off (`kWheelColliders = false`; the car's body collider stays; sphere-built cars then have no wheels either). Game collision for landed peds stays on (step 1). Deployed by hot reload.
* **Founder:** no more takeoffs with the wheel colliders off - but bodies get caught under the car. Pass-through back on, switching at the moment the trunk or head touches the road after the hit (latched in the impact tracking, `lyingDown`), even while the car still touches the body; legs never count (standing), arms neither (a hand reaching down from the bonnet would drop the body through the car). ADR-0026 amended. 24 related test cases green (clean x64 rebuild). SA: `kPassThrough = true`, wheels still off; deployed by hot reload.
* **Founder:** pass-through on touching the floor is "good for now" as a baseline. Next asks, done in this round (not yet tested in game):
  * **Riders:** no ragdoll while riding (dragdoll-sa: auto handover 0 while `bInVehicle`); core: a character never collides with the object it rides (`set_riding` group; contacts rejected in Jolt's contact validation; test: overlapping its own "bike" it is not pushed, 0.27 m by another object) - the mod keeps the riding group 1.5 s after the game takes the rider off, so a thrown rider is not shot upwards out of its bike's shape (the founder's "ejected straight up").
  * **Dying only at rest:** the ped's game collision goes off when a car is due within 0.3 s, so the game never runs its own hit (it used to start the dying routine mid-flight, blood following the body); restored if no hit follows.
  * **Handback** (4.8 started in game): when the body has lain still 0.6 s (or after 20 s) the ped is put down lying where the body lies, heading from the body (face up: feet forward; face down: head forward - to be checked in game), in floor_hit / floor_hit_f via the game's die task (`CTaskComplexDie`, run over by car) if the impact was >= 40 km/h (placeholder), else the game's get-up animation (getup / getup_front); the ragdoll pose fades into the game's over 0.35 s, then the ragdoll is removed.
* **Founder review of the first handback:** a dead ped (blood on the floor) got up and froze on a standing frame; the transition took too long (body still in its last ragdoll position, then lying, then up at once). Causes (log): every handback took the "gets up" path (all impacts < 40 km/h) though the game had already killed some peds -> a dead ped got the get-up animation; and waiting for stillness took 3-20 s. Changed (founder's design): **Settle starts the moment the trunk or head touches the road** (the pass-through moment): the behaviour detaches, muscles relaxed, the game ped is placed lying where the body is and starts floor_hit / floor_hit_f at full weight; every 10 frames the game's own lying pose is read from its bones (before the override) and set as the ragdoll's target pose, so the body eases into exactly the pose the game continues from; rolling over swaps the pose. Handback after 0.4-1.2 s (as soon as still), blend 0.25 s. Killed if the game already counts the ped dead (health <= 0, die/dead state) or impact >= 40 km/h. Deployed (hot reload); not yet seen in game.
* **Founder:** "the loop is complete, now refinement" - next, study the game's own reactions to car hits (how peds react, how long until they are on their feet) to replicate them. Added: dragdoll-sa **F6** toggles DragDoll's car hits (off: no auto handover, no pre-hit collision change; the puppets still record each ped's animation) and pedestrian labels now carry the game's health and ped state ("<anim> (<group>) t/total | hp H state S"). New `tools/captures/game_reactions.py`: per hit ped, the timeline of the game's animations with durations and health / state changes. Deployed (hot reload; after a reload F6 starts ON again).

**Next**
* Founder: press F6 (log: "car hits OFF"), capture several vanilla hits (sedan / SUV / bike, 20-60 km/h, side / front / back), notes as usual; then run `game_reactions.py` on them -> timings and animation sequences to replicate (knock-down time, lying time, get-up, dead vs alive by speed).
* Commit the 4.7 + in-game work on the founder's word (dragdoll-core and dragdoll-sa both have uncommitted changes).
* **First vanilla captures** (live_20261117_153435 "crash into people vanilla", _153623 "faster"): the animation labels showed walk / idle throughout the hits - the label used SA's "main association", which skips partial animations, and the game's knock-down / fall / death animations are partial. Fixed (dragdoll-sa `animationLabel`: the most blended-in of all associations, plus a second one above 0.3 blend). What the captures do show (health / state): faster hits kill outright (hp 100 -> 0; state die, then dead about 1.0-1.3 s later); slower hits take hp in steps (100 -> 70 / 61 / 52 -> ... -> 0), several peds hit repeatedly before dying; a surviving ped (hp 4) walked on. Re-capture needed for the animation sequences and timings (F6 again: a hot reload turns car hits back ON).
* **Vanilla reactions measured** (capture live_20261117_153917 "run over people", F6 off after the first hits; `tools/captures/game_reactions.py`; labels now name the most-blended animation of all, partial ones included, and keep "| hp H st S" within the 63-byte limit - the old long labels lost it):
  * Aware ped: `EV_dive` (2.3 s) -> `Getup_front` (1.4 s).
  * Knocked down, survives: `KO_skid_back` / `KO_skid_front` / `KO_spin_R` for 1.8-2.1 s after the hit, then `Getup_front` / `Getup` (1.4 s), then walks or runs: on its feet ~3.3-3.5 s after the hit. `KO_skid_back` ends face down (-> Getup_front), `KO_skid_front` face up (-> Getup) - consistent with our facing-based choice.
  * Killed: `KO_skid_back` or `KD_right` with health 0 at once (11 of the 15 knock-downs in this capture); survivors kept 40-70 health.
  * Car speed at each hit not recoverable from this capture (the puppets' threat data near contact was stale) -> the game's kill threshold by speed is still unknown.
* **Matched to the game (founder: "match them"):** a survivor now stays down (held in the game's lying pose by its muscles) until at least 2.0 s after the hit before handing back to get up - the game's own KO_skid / KO_spin last 1.8-2.1 s, then Getup 1.4 s. Killed peds hand back as before (dying where they lie). Kill rule factored out (`killedBy`: game already counts it dead, or impact >= 40 km/h placeholder). Deployed by hot reload.
* **Founder (pier captures live_20261117_164228 ragdoll on / _164451 off):** with the ragdoll on, some peds return to their game routine "a bit under the floor". The captures cannot show it directly: a ped's puppet is not re-posed after its ragdoll is released, so its stored position freezes mid get-up. Likely cause: the lying ped was placed at DragDoll's ground + a fixed 1.0 m, while the game afterwards stands the ped on its own floor (DragDoll's copy of a pier deck can sit lower, and the 1.0 m was a guess). Changed (dragdoll-sa `placePedLying`): the ground is the game's own (`CWorld::FindGroundZFor3DCoord` from 1 m above the pelvis; DragDoll's only when the game finds none within 1 m of it), and the origin height is the ped's own (`GetDistanceFromCentreOfMassToBaseOfModel`, as the game places peds; 1.0 m fallback). Diagnostics in the log: each handback prints both grounds and the origin height; 3 s after each survivor's release, its origin height over the game's ground. Deployed by hot reload.
* **Founder:** with the ragdoll on, a car hit has no bump, no crunch sound, no pain scream ("like crashing into plushies"); wants the game's own routine, not sounds triggered by hand. Cause: dragdoll-sa switched the ped's game collision off 0.3 s before contact (so the game would not kill it mid-air), so the game never saw a car-vs-ped collision - and those effects all come from it (DragDoll's kinematic car cannot give a bump either). Changed (`kLetGameHit = true`): no pre-hit switch-off; the game collision stays on through the hit for 0.15 s after the handover, then goes off while the body flies (back on once it lies on the road). The log prints the ped's health / state / animation when that window ends - to see whether the game kills it at contact (the old blood-in-the-air problem). `kLetGameHit = false` restores the old way. Deployed by hot reload.
* **Founder (game hit on):** crashing a gang, often one ped "jumps very high in the same position it was crashed"; some die rotated (screenshot: body twisted, arm up into the wheel). Log: at every handover the game had already had its hit (health 0, state die, KO_skid_back / KD_left / KO_spin playing) - its ped collision is larger than DragDoll's contact, so it comes first. Causes: (1) the game launches a hit ped upwards (the vanilla captures show 2-3 m) and the kinematic puppet followed that for a frame or two, so the ragdoll took over with that upward speed (flights up to 3.1 s in the log); (2) the game's own knock-down / death animation kept playing under floor_hit, so the lying pose (and the handback blend) was that animation, turned against the heading we set. Changed: the game's own hit (health drop, dying, or a KO_ / KD_ animation, with a car due in the last 0.3 s) hands over at once from last frame's motion, before the puppet follows the launch; while lying, every animation other than floor_hit / floor_hit_f is blended out. Deployed by hot reload.
* **Founder: "something really weird" at the end of the session.** Log: ped 22640 was killed (handback "dies there" at 339785.7), released, and - as every nearby ped - got a fresh, armed puppet on its corpse. 26 s later the player drove slowly into the corpse: it was handed over to DragDoll again (label FLOOR_hit_f, health 0, dead), its car_hit behaviour ran on a corpse, and it never settled or handed back before the log ended (5 s). Changed: the auto handover is off for dead peds (health <= 0, die / dead state), like riders - a corpse is the game's (its own bump). Lead set each frame from riding / dead / F6. The early-handover detection worked (4 "game's own hit came first" this session). Deployed by hot reload.
* **Founder: the game closed by itself.** Windows event log: gta_sa.exe access violation at +0xd1750 (in its animation-blend code, next to RpAnimBlendClumpGetFirstAssociation at 0x4D15E0) at 18:16:57, about 1-2 s after two settles / get-ups; the first crash at that address (all earlier ones, 11-13 Nov, were +0x3480db). New since the previous build: while lying, the other animations were blended out with a negative delta, which makes the game delete them - a game task still holding one would crash in the animation code. Changed: they are set to weight 0 and kept (never deleted). Added a crash report: the logic installs an unhandled-exception filter (removed on shutdown / hot reload) that logs "CRASH: exception ... at <module>+<offset>" and the return addresses on the stack. The cause is a strong guess, not proven (no stack from that crash).
* **Founder (screenshots):** a ped drawn lying face down in mid-air at waist height, through a wall, while the game has it walking (viewer label "walk_gang1 | hp 100 st 1"; the puppet copies the game's bones, so the game itself draws it so). Log: survivors 4875 / 3852 handed back (get up) and 3 s later walked with the origin 1.00 m over the ground - the pose is lying, the position is standing. Not explained yet; suspect the zero-weight animations left from the lying pose (kept since the crash fix) or the game's own fall task. Added a diagnostic: an armed ped whose head is not 0.2 m above its pelvis while no lying / get-up / knock-down animation plays logs "DRAWN LYING" once, with its matrix and every animation on it (blend, delta, flags, time). Deployed by hot reload.
* **Floating lying ped explained** (the live tap serves one client - the viewer had it - so the answer came from the new log diagnostic): "DRAWN LYING while walk_gang1 (state 1, hp 100)": its only animation, walk_gang1, at blend 0.00 delta 0.00. My crash fix had set every other animation to weight 0 with delta 0 while lying, including the full-body walk; the game later reused that association for walking without blending it in again, so with all weights 0 the skin showed its rest pose - which is lying (the skin frame is rotated 90 deg) - at standing height. Changed: only the game's partial knock-down animations (KO_* / KD_*) are muted that way (weight 0, never deleted); full-body ones are left to floor_hit's own blend (the game's BlendAnimation fades them the normal way). The diagnostic now skips peds in vehicles (car_sit false positives). Peds already stuck this way stay so until the game changes their animation. Deployed by hot reload.
* **Game switches (founder: liked the first in-game wiring more; wants every fix since then switchable from the viewer).** Core: `dragdoll_world_set_host_options(world, json)` - the host announces its own switches (JSON array of name / label / help / value / optional choices); the live tap sends them with the next frame and to every viewer that attaches; the viewer's new "Game switches" panel (checkboxes, a combo for choices, help on hover) answers through the existing host-message channel with "set <name> <value>". Not simulated, not recorded; no-op without a tap. dragdoll-sa: 12 switches, defaults = current behaviour, saved in `scripts\dragdoll_switches.ini`, logged at start: floor_collision, pass_through, wheels (cars seen from then on), game_hit (first wiring: off at handover / blocked before contact / let through 0.15 s / let through + early handover), no_riders, own_vehicle_grace, settle, handback (off: released after 20 s), survivor_down, mute_knockdown, game_floor, no_corpses. Without settle the handback uses the first rule (lying still 0.6 s). Changes apply from the next hit. Built core x86-release + x64-release (viewer), the mod; hot-reloaded; viewer restarted attached. Tests not run (founder's rule). My guess for the founder's "less lively": `settle` (reflexes stop at the first trunk touch).

### 2026-11-17 — medics never revive DragDoll's dead (dragdoll-sa)
**Changed:** founder: ambulance medics run forever in front of bodies DragDoll killed. Our handback kills by setting `CTaskComplexDie` directly, skipping the game's damage routine; suspect the ped's `bAllowMedicsToReviveMe` stays off. Switch 13 `medics` (default on) sets it at a handback death; the log line "dies at handback - medics-may-revive flag was N" shows the game's value. New log: `dead <handle> (ours|game's)` for every dead ped within 60 m at first sight and every 3 s while a medic (task 100) is within 20 m (hp, state, revive / dying-stuck / collision flags, simplest task, height over ground, anim, each medic's distance and task).
**Next:** founder crashes a ped, waits for the ambulance; if medics still loop, compare our `dead` lines with a game's own kill (F6 off).
**Open questions:** is the flag the cause, or the medic's go-to point (task id / distance in the log will say)?

## 2026-11-17 — Back to the first in-game wiring (founder); clean contract to be designed
**Changed** (local, not committed)
* Founder: stop patching game details one by one; the goal is a clean contract - start the ragdoll on a crash, release it, the game carries on with its own routines. Everything since the first in-game wiring is saved as a plain copy in `../saved-game-fixes-2026-11-17/` (both repos, full trees: settle, handback, game hit modes, ground, crash report, game switches, medics).
* dragdoll-sa back to the first wiring's behaviour: auto handover (car_hit, relaxed), game collision off at the handover, root sync, pose override, wheel colliders on, released after 20 s or when the ped is gone (collision restored). Riders ragdoll too; corpses can be re-ragdolled. Kept tools only: F6 (car hits on / off), animation labels with health / state, the crash report. Removed: the switches and their ini file, pass-through use, riding grace, pre-hit / early handover, settle, handback, ground placement, KO muting, dead / height / drawn-lying diagnostics.
* Core: host options API and the viewer's "Game switches" panel removed; the riding collision rule (a rider never collides with what it rides) and its test removed (riding stays perception-only, as before the wiring). Kept: 4.7, and the vehicle pass-through API (ADR-0026) - unused by the mod.
* Built x86-release (core + mod), hot-reloaded. Viewer (x64) not rebuilt: it still has the panel code, but nothing is sent to it.

**Next**
* Discuss and write the clean handover / release contract (new ADR replacing ADR-0025 §3a): the game owns the ped's state (hit, damage, death, tasks); DragDoll owns only the drawn pose and the position while ragdolled.

**Open questions**
* How to keep the game's knock-down timeline and the ragdoll's in step at release.
* **Contract decided (founder: "game-first"): ADR-0027 "The game decides, DragDoll draws"** (amends ADR-0025 §3a for game hosts). Existing SA physics ragdoll mods checked first (static only): one starts physics on the game's own knock-down / death animations, the other hands back to the game's get-up after lying still - both follow the game.
* dragdoll-sa implemented: no auto handover; the ragdoll starts when the game registers a car hit (health drop this frame + last damage "rammed / run over by car", `m_nLastWeaponDamage`), from last frame's motion. While ragdolled the mod writes only drawn bones, position (over the pelvis, origin never below standing height on the game's floor) and zero speed - no collision, health, task, flag or animation writes. Release: the game's get-up (any `getup*` animation or the get-up state; heading set from the body to suit front / back), alive again after dead (revived), or 20 s without a get-up for a survivor; then a 0.25 s drawing-only blend. Corpses stay sleeping ragdolls until the game moves on or they are 60 m away. Log lines: "the game's car hit (hp a -> b, state, anim) - handed over", "release after ... - its get-up / alive again / fallback (... body lies face up/down)". Built x86-release, hot-reloaded; not yet seen in game.
* **First game-first session (founder):** hits work as the contract says - the game kills most peds outright (hp 100 -> 0 in one frame: its own cash drop, blood), survivors (hp 67-70) are released on the game's own get-up 2.0-2.2 s after the hit (vanilla timing), corpses stay sleeping ragdolls until the game despawns them (~30 s, "ped gone"). Bug: the viewer label froze at the handover (hp 100: the label was only set while kinematic, and the hit is detected one frame before the label update). Fixed: the label (animation, hp, state) updates while ragdolled too. Hot-reloaded.

## 2026-11-18 — Studying the game's car-hit pipeline before the tracer
**Changed**
* Founder decision: the community reconstruction of the game may be read (read only) for interop facts - ADR-0014 amended; nothing from it enters any repository. Findings are private notes outside the repos (`research/sa-car-hit-pipeline.md` in the workspace).
* Outcome in short (details only in the private notes): the hit, the damage, the die / fall-and-get-up decision, the down time, the front / back get-up choice and the medics' accident report are separate game steps with known entry points; the central hit function itself is not reconstructed, so the pipeline tracer is still needed for its inside and for the exact frame order.

**Next**
* Founder: discuss the entry points; then build the pipeline tracer (Sonnet) to answer the open questions in the notes.
* **Pipeline tracer built (dragdoll-sa `src/tracer.cpp`, dev tool):** 18 inline hooks (safetyhook, bundled with plugin-sdk) on the game's car-hit path - the hit function, the kill task (creation + step), event handling, the damage response and calculator, fall-and-get-up / fall / die constructors, the down-time setter, the get-up start, the accident report, the dead-ped cash, the per-ped collision gate, the ped-hit sound, the medic's revive. Every hook only logs and calls the original. A ped becomes traced when the game hits it with a car (30 s): every hook involving it is logged with frame number, inputs, health before / after and the caller address, plus one line per frame when its state / task tree / animation changes. DragDoll's own handover / release are noted in the same log. Output `scripts\dragdoll_trace.log`. Installed at logic start, removed at shutdown / before a hot reload. Built x86-release; deployed, not yet run (game closed).
* **First trace session (founder):** 18 / 18 hooks installed, no crash, 645 hit calls. Confirmed in our own measurements (details in the private notes): the game sorts each car contact by its collision impulse - below ~6 nothing, 6-12 a knock-down with 30 damage, 12 and up a kill with 1000 damage; it records the car as "ignored" on the ped (its own pass-through) for ~1.2 s; a survivor's fall task comes the next frame and its get-up ~1.9 s after the hit; a killed ped's die task comes 1-2 frames later and, once dead, the game itself drops the cash, reports the accident and turns the corpse's collision off; a medic's revive comes back as a get-up. With DragDoll on, all of that still runs underneath the ragdoll.
* **Car side of the crash (founder: the car loses too much speed in vanilla, worst through standing gangs).** Tracer extended with 7 hooks on the game's physics (push forces, car/ped collision and friction, bonnet) - the game crashed twice ~0.5 s into play (same address, in the game's streaming code; also on a cold start), though every address / signature checks out. Those 7 hooks were removed (back to the proven 18). The car side is now measured without them: the hit function's hook logs the car's speed / mass / health going in and coming out (the change inside it = the game's own push-back), plus the player car's speed at the previous frame end (the drop to the hit function's entry = the real collision), and the hitting car's speed / health at each frame end for 2 s. Built, deployed, not yet run. Reason found in reading (to confirm): the hit function very likely shoves the car back with a fixed force per ped (~13 km/h per kill, ~8 per knock-down for cars up to ~1.4 t; real-world momentum ~2-3 km/h).
* **Crash hunt (founder's call: find the hook, not work around it).** The 7 physics hooks came back, each switchable in `scripts\dragdoll_tracer.ini` (read at start). The founder's restarts found it: **`ApplySoftCollision` (two bodies)** crashes the game ~0.5 s into play; the other six (push forces, collision, friction, bonnet) run fine. Cause unknown (address / signature match). Soft collision is what the game uses for wheel contacts (and stuck bodies) - its share of the car's slowdown is still in the frame-end -> hit-function drop. Running config: 24 hooks.
* **Car-speed session (founder, F6 off):** the car's slowdown is not the collision (~0-1 km/h) but one push-back the game's hit function applies to the hitting car on every knock-down and kill: a single combined force from one place inside it, sized ~12x the collision impulse, taking ~35-41 % of the car's speed per ped. Three gang members in 0.1 s: 105 -> 21 km/h (80 % lost; real-world momentum ~11 %). Details in the private notes.
* **Founder: skip the push entirely.** dragdoll-sa: the combined-force hook (now always installed) skips the game's push-back on the hitting car - only that one call from inside the hit function, only on that car - while F6 is on (DragDoll car hits); F6 off = the game's own push for comparison. Trace line "push-back on the hitting car ... SKIPPED". ADR-0027 point 6 (exception). Hot-reloaded, 24 / 24 hooks, game running; not yet felt in game.
* **Push-back skip in game (founder: "HECK YEAH NOW WE'RE TALKING!!!").** Trace: 35 push-backs skipped, none applied (F6 on), no crash. Gang runs keep their speed: six peds at 100 km/h -> 100.2, four at 71 -> 71.8, three at 81 -> 80.3 (vanilla: three at 105 -> 21). Small drops between hits are the collision / driving, not the hit function.
* **Committed the milestone** (founder: "good gold R&D results"): dragdoll-core 8d207c5, dragdoll-sa b75a478 (not pushed; unit tests not run this round; x86 + x64 built).
* **Start wired into the hit function:** the per-frame "health dropped + car weapon" watcher is gone; the hook on the game's hit function calls `dragdollGameCarHit` right after the game ran it - if the game hurt the ped (knock-down or kill branch; bumps do nothing), the armed, kinematic puppet is handed over in that same call, from the previous frame's motion. Riders and peds without a puppet stay the game's. Log: "the game's car hit, impulse X by car N (...) - handed over to DragDoll inside it". Hot-reloaded; not yet seen in game.
* **Start inside the hit function, in game (founder session):** no crash; 34 hurt hits (knock-downs, kills, peds hit again after getting up: 70 -> 40 -> 10 -> 0), all 34 handed over in the same frame as the game's hit; 35 push-backs skipped; survivors released on the game's own get-up. **The flip, measured:** of 14 releases with a clear facing, 8 matched the game's get-up and **6 flipped** (game chose the face-up get-up for a body lying face down, or the reverse); 7 more bodies lay on their side. The game picks front / back from its own knock-down animation, not from how the body landed.
**Next:** decide how the get-up should match the body (founder).
* **Get-up flip fix (founder: "fix the flip"):** the get-up start hook shows the game's own front / back check how the DragDoll body lies (the "front" animation mark, 0x800, set or cleared on the ped's animations for that one call, then that bit restored on every animation that was there before); the game itself starts the matching get-up, and our release takes its heading from the same choice. Sideways bodies keep the game's choice. Trace line "GetUp choice ped N: body lies face X, the game would have chosen Y -> same / corrected". ADR-0027 point 7. Hot-reloaded; not yet seen in game.
* **Get-up flip fix in game:** 29 get-ups - 9 corrected to match the body (8 face-down bodies the game would have got up the face-up way, 1 the reverse), 20 already matched.
* **Founder: "sometimes peds stand up two times".** Cause (trace): peds hit while diving away (the game's "evasive dive and get up" task in the event-response slot) get the fall-and-get-up task in the physical-response slot; that one gets up first (our release), then the dive task resumes and plays its own get-up ~36 frames (1.2 s) later. The plain game does the same (vanilla ped 5633: get-ups at +52 and +88 frames) - a game quirk, more visible with the ragdoll. Separate, harmless: "blocked" get-up retries every frame while a car is over the ped (one visible get-up). Decision pending (founder).
* **Founder: end the dive.** At the release (the game's get-up has started), a waiting "evasive dive and get up" task in another primary slot is ended through the game's own mechanism (MakeAbortable immediate, slot cleared via the task manager); never the fall-and-get-up. Log "the game's waiting dive-and-get-up task (slot N) ended - no second get-up". ADR-0027 point 8. Hot-reloaded; not yet seen in game.
* **Dive fix in game:** 3 waiting dive tasks ended ("no second get-up"), no refusals, no crash.
* **Founder (screenshot): a long dark spike from one ped's body while ragdolled - only model 13 (woman, black shirt, pink number).** Not explained from the code (bone tree read the usual way). Added a once-per-model diagnostic in the pose override: compares the game's own bone matrices (scale, distance to parent) with what DragDoll is about to draw and logs "bone check model N: ... differ" plus each differing bone. Hot-reloaded; needs one more hit on that model.
* **Spike, first bone check (model 13):** all 32 bones at scale 1 and the right distance from their parent (pelvis 6 cm vs 2 cm from the root - too small to matter). Model 13 has three extra bones (ids 301, 302, 201 - body-shape bones) that the sa_ped map moves rigidly with their parent from the skin's rest data. Check extended: per bone, the local rotation (game vs ours) and the skin rest data's axis lengths / skew. Hot-reloaded; needs another model-13 ragdoll.
* **Spike, second check:** the extra bones (301 / 302 / 201) match the game exactly (0 deg), all skin rest data clean (scale 1, no skew) - not the cause. The one bone both checks flagged is the root (id 0): the game keeps it ~2 cm from the pelvis; the pose override drew it at the ped's origin, which the mod keeps at standing height (6-20 cm away, ~1 m once the body lies flat). Likely model 13 has vertices partly tied to the root -> pulled up towards the origin = the spike. Fixed: the root is drawn at its rest offset from the pelvis (the ped's origin unchanged for the game's logic). Hot-reloaded; the founder confirms in game.
* **Founder: spike is gone** (root bone drawn at its rest offset from the pelvis). The once-per-model bone check stays in the log as a dev diagnostic (one summary + per-bone lines for each model the first time it ragdolls).

**Next**
* Commit this round on the founder's word (start inside the hit function, get-up matches the body, no second get-up after a dive, root bone fix).
* Open: the soft-collision hook crash (unknown cause); peds on their side keep the game's get-up choice.
* **Founder: a car was stopped by a gang (after the push-back skip).** Trace: the car hit the group at 83 km/h, all 7 push-backs skipped, the logged collisions cost 0.1-0.4 km/h each - yet the same frame ended at 17.9 km/h. The collision gate showed one hit ped going from "ignored" (the game's own pass-through) to "unable to move", which the game handles through its one-body collision (the other entity treated as immovable) - not hooked. Suspected: the game peds DragDoll holds keep their collision at the body's place, and cars run into them as posts. Step 1 (founder): logging hooks on the one-body collision (0x5435C0) and one-body soft collision (0x543890), switchable in the ini, both on: 26 / 26 hooks, game running.
* **One-body collision hooks in game:** both ran without crashing, but no car ever collided with a ped through them (0 calls). The session's one big drop (car 78 -> 44 km/h in a frame) had no ped contact at all (the hit peds were dead, collision off by the game) and no car damage - something else (kerb / object). So the earlier gang stop (83 -> 18 km/h in the frame of the hits, peds alive / dying, collision on) points to the one car / ped path we cannot watch: the two-body soft collision (wheel contacts) - the hook that crashes the game. Open (founder): find a crash-free way to watch it, or test the gate fix directly.
* **Founder: "you are watching other data - at the end of the run the car went from 100 to 0".** Correct: last run, frame 193181 - three gang members hit at 108 km/h, all ped contacts together cost ~2 km/h (4 push-backs skipped, still 106 km/h after the last hit), yet the frame ended at 6.9 km/h and the car's health fell 440 -> 350 -> 157 over two frames: a solid impact, not the peds (the tracer only logged ped contacts). Added: for 2 s after a car hits a ped, every collision it has (two-body and one-body, with anything) that costs speed or health is logged with what it hit. Hot-reloaded.
* **Many gang runs, not repeatable (founder: "maybe let it slide").** Session: 157 hits, 97 handovers, 104 push-backs skipped, no crash. Every contact that cost a hit car speed in the 2 s after a ped hit (13) was with another car (traffic; largest -18.6 km/h); none with peds, buildings or objects, none near the earlier 106 -> 7 km/h stop (which also took ~280 car health: a solid impact behind / among the gang). Decision: let it slide; the all-contacts logging stays in the tracer.
