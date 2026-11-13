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
