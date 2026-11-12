# DragDoll — Workplan

> **DragDoll** = rag**doll** + **drag** (ballroom/vogue: poses, dips, death drops) + **drag** (the physics force).
> An open-source (MIT), real-time **active ragdoll** library in C++20 on top of Jolt Physics: characters that react to the world with momentum, self-preservation and style instead of canned animations.

## Manifesto (README draft)
Games still play the same canned "fall down" clip whether you're hit by a scooter or a bus. DragDoll gives characters a body that *reacts*: it braces, wraps, tumbles, lands and gets back up — driven by physics and behaviours, not keyframes. Free, open, engine-agnostic, built from published research, and made for modders first.
**Mission (ADR-0018):** a repeatable **workflow to replace canned reaction animations in any game** with DragDoll's active ragdoll. **Governance (ADR-0019):** community-owned from day one; the founder kickstarts, the community carries it.
**DragDoll will never:** ship proprietary code or data, require a specific engine, or hide its cheats (any non-physical assist is explicit and off by default).

---

## 0. First Main Goal — DragDoll SA v1
**A GTA San Andreas (PC classic 1.0 US) mod in which DragDoll takes over pedestrians hit by vehicles.**

> When an NPC is hit by a vehicle, DragDoll takes over: **pre-impact** (brace/dive if aware) → **impact** → **wrap / roof-vault / forward projection** (depending on vehicle front shape and speed) → **airborne** → **landing** → **ground settle** → **hand back** to the game (death or get-up).

**Ship criteria**
* Preferred over existing SA ragdoll mods in side-by-side comparison videos.
* Throw distance vs impact speed within published reference corridors; correct impact pattern per vehicle class.
* 0 crashes in a 4-hour play session; any DragDoll failure (`failed` state) falls back to the game's own animation instead of crashing.
* ≤ 8 simultaneous ragdolls, **< 1 ms added CPU per frame** (Ryzen 9 3900X).
* Correct exclusions: missions, cutscenes, player (opt-in), SA-MP (auto-disabled). Works on Hoodlum and Compact 1.0 US exes.

Everything below serves this goal first. SA is the **first reference integration of the workflow** (ADR-0018), not the architecture: the core stays engine-agnostic, and everything learned in SA is written down as the game-agnostic Integration Workflow.

Prior art: workspace file `../research/sa-ragdoll-mods.md`, kept outside the repo (teardown of 4 existing mods — none do active control).

---

## 1. Workspace & Repositories
* All repos live under the GitHub organization **[dragdollphysics](https://github.com/dragdollphysics)** (ADR-0019).
* `open-euphoria/` — local, **non-git** workspace (internal joke, never published). Holds research, archives, and the repos below.
* `dragdoll-core/` — library, C API, behaviours, workbench, lab, tools, tests, scenarios, docs. Monorepo until v1.
* `dragdoll-sa/` — San Andreas ASI plugin (separate repo; consumes the C API; created in Phase 6, spike in Phase 1).
* This workplan lives at `dragdoll-core/docs/workplan.md`; research notes stay in the workspace (`open-euphoria/research/`), outside any repo.
* Never commit absolute paths; build dirs are git-ignored.

**dragdoll-core layout (TeX/LaTeX layering, see D13)**
```
core/        Layer 0 — Jolt wrapper, rig, motors, sensors, pose I/O, recording (stable, strict review)
capi/        dragdoll.h — the C ABI (opaque handles, POD structs, versioned)
behaviours/  Layer 1 — standard behaviours; may include ONLY behaviour_sdk.h (dogfooding)
packs/       Layer 2 — data: rigs, vehicle shapes, tuning presets (JSON)
workbench/   native WebGPU + ImGui app (hot-reload host)
lab/         dragdoll-lab — headless optimisation loop
tools/       dragdoll-inspect (recording queries), mcp bridge
tests/  scenarios/  docs/decisions/
```

---

## 2. Locked Decisions (ADRs)
One page each in `docs/decisions/`. Changed only by a new ADR. **Planning rule:** after Day 1 this workplan changes only via ADRs or end-of-phase retrospectives.

| # | Decision | Choice |
|---|----------|--------|
| D1 | License | **MIT**. Contributions via **DCO** sign-off (no CLA). |
| D2 | World ownership | DragDoll **always owns its Jolt world**. Hosts register **proxy colliders** (static meshes, kinematic vehicles/objects) with bulk add/remove for streaming. No shared-host-world mode (Godot's Jolt and other WASM modules aren't reachable anyway). |
| D3 | Physical honesty | Internal torques only (equal-and-opposite pairs). Optional **assist** force: explicit, capped, logged, off by default, always 0 in acceptance runs. |
| D4 | Conventions | Meters, kg, seconds, radians; quaternions `(x,y,z,w)`; right-handed. **Up axis is a per-world setting** (default +Y; SA plugin uses +Z). All core math uses the world up vector, never a hardcoded axis. |
| D5 | Math | Jolt math types internally; plain C POD at the ABI. No GLM. |
| D6 | Timestep | Fixed **120 Hz** default (60–240), accumulator inside `dragdoll_world_step(dt)`, interpolated output between last two states. |
| D7 | Determinism | Same binary + same inputs ⇒ bit-identical results, any thread count. Golden hashes stored **per platform** (win-x64, win-x86, later wasm). Cross-platform determinism opt-in. |
| D8 | Joint drive | Jolt constraint motors (`SwingTwist`/`Hinge`, position mode, torque limits) via `Ragdoll::DriveToPoseUsingMotors`. Explicit torques only for balance. |
| D9 | Gain units | Natural frequency (Hz) + damping ratio ζ, not raw Kp/Kd. |
| D10 | Rig | Data-driven JSON rig + host-skeleton mapping (`JPH::SkeletonMapper`). 16 bodies: Pelvis, Lower/Upper Spine, Head, L/R Upper Arm, Forearm, Hand, L/R Thigh, Shin, Foot. No neck/clavicles in v1. Masses from de Leva (1996). |
| D11 | Tooling | CMake ≥ 3.24 + FetchContent (pinned tags) + Ninja + ccache; **MSVC** (Win32 x86 + x64); doctest; GitHub Actions; **Tracy** profiler. |
| D12 | Jolt build flags | Set once in `dragdoll_jolt_config` interface target, propagated everywhere. Debug renderer only in workbench; FP exceptions in Debug; no double precision. |
| D13 | Layered architecture | TeX/LaTeX model: Layer 0 core (slow, semver) → Layer 1 behaviours built **only** on `behaviour_sdk.h` → Layer 2 data packs. Behaviour SDK becomes a C ABI only after it stabilises (≥ v0.2). |
| D14 | Clean-room provenance | No code, data, parameters, recordings or leaked material derived from Euphoria, endorphin, RAGE, or any proprietary engine/mod in any repo. No decompiling them. Contributors exposed to leaked RAGE/Euphoria code stay out of core. Behaviours use **DragDoll's own vocabulary**. Every technique cites its published source in an ADR. Datasets carry provenance tags (`public` / `research-nc` / `private-reference`); CI + pre-commit reject `private-reference` data and anything derived from it. |
| D15 | Platforms | **Win32 x86 is first-class from day 1** (SA is 32-bit): CI builds and tests x86 and x64. Static CRT (`/MT`) for the SA DLL, no external runtime deps. **Guest-process budgets:** < 64 MB memory per world (tracked via Jolt allocator hooks), worker threads configurable by the host (SA default 2–4), DragDoll never calls host code from its own threads, perf tracked on **x86 as well as x64**. WASM deferred to post-v1. |
| D16 | Fast impacts | Jolt continuous collision (`EMotionQuality::LinearCast`) on limbs and vehicle proxies from day one (120 km/h ≈ 28 cm per 120 Hz step > shin thickness). |
| D17 | Host output contract | Every host receives: **local-space bone rotations**, a separate **character root transform**, **per-bone blend weights**, and a **state** (`active`, `recovering`, `settled`, `dead`) plus recovery hints (face up/down, heading). A **`failed`** state (NaN / instability detected → character deactivated, host falls back to its own animation). **Activation from motion:** hosts hand over two consecutive poses so bodies start with the animated velocities. Hosts supply target poses; animation playback is the host's job. |
| D18 | Mission | A game-agnostic **Integration Workflow** (`docs/integration/`) is a first-class deliverable; SA is its first reference integration. |
| D19 | Governance | Org-owned, copyright "The DragDoll contributors", DCO, founder as initial steward, ≥ 2 org owners after v1, `GOVERNANCE.md` at launch. |
| D20 | C++ conventions | Never throw across the ABI (noexcept + catch-all → `DdResult`), `/fp:precise`, RAII, caller-owned output buffers, no global ctors, no host callbacks from worker threads, Jolt-style naming. |

---

## 3. Testing, Tooling & the Improvement Loop

### 3.1 Tests (CI, every push)
* Unit tests: quaternion shortest-arc/double-cover, swing-twist decomposition, rig loading, gain conversions, up-axis handling.
* Determinism: golden state hash per scenario per platform.
* Headless scenario suite with numeric pass criteria; randomized seeds for success rates.
* Perf budget tracked in CI (µs/character/step); > 10 % regression fails. Target **≤ 0.1 ms/character/step** native x64; x86 tracked separately. Memory per world tracked against the 64 MB guest budget (D15).
* **Reference machine:** AMD Ryzen 9 3900X (12C/24T, AVX2, no AVX-512) + NVIDIA RTX 3080, Windows 11. All perf numbers in this plan refer to it.

### 3.2 Native Workbench (the source of everything else)
* **Rendering:** `webgpu.h` + Dawn (D3D12/Vulkan) + Dear ImGui (WebGPU backend) — same code can compile to the browser later. Fallback if Dawn causes early friction: raylib for the first weeks.
* **Hot reload:** simulation + behaviours live in a DLL reloaded on rebuild; window/camera/UI persist in the host exe; state restored by **deterministic replay** to the same frame (Jolt `SaveState`/`RestoreState` when layout is unchanged).
* **Live tuning:** all gains/profiles/behaviour params in JSON, editable live — no recompile.
* **Debugging:** Visual Studio or RAD Debugger (breakpoints from DragDoll into Jolt), Tracy, RenderDoc.

### 3.3 Recordings, Notes & Queries
* `.ddrec` = input log (exact replay) + periodic snapshots (~1/s) + event stream (behaviour switches, CP outside support, contacts, joint/torque limits hit, NaN guards) + per-frame metrics.
* **[SA] Recorded at the C API boundary:** every call a host makes (proxy colliders, kinematic paths, target poses, activations, step dt) is logged, so **any host session — e.g. a San Andreas play session — replays bit-identically in the workbench**, with breakpoints and hot reload, without the game running.
* **Annotations:** `F9` marks a moment, `F9…F10` a range; text note + auto screenshot + camera + current params.
* **`dragdoll-inspect`** CLI answers precise queries instead of dumping data (`--notes`, `--at T --window W --fields …`, `--render A:B --sheet` contact sheets). Token-efficient input for AI-assisted iteration.
* Every annotated issue can become a regression scenario with a metric and threshold.

### 3.4 Live Bridge
* Workbench command socket (localhost only) + thin **MCP server**: `spawn_scenario`, `apply_impulse`, `spawn_vehicle`, `set_param`, `step`, `pause`, `get_state`, `screenshot`, `query_recording`. You watch and hold breakpoints; Claude drives.

### 3.5 dragdoll-lab — Closed Optimisation Loop
* Headless worker pool (~22 threads on the 3900X), one deterministic single-threaded sim per worker.
* **CMA-ES** over analytical controller parameters (not neural nets — consistent with the v1 no-ML rule).
* Fitness = reference-corridor match + success rate + smoothness (jerk/jitter) + energy − assist use (must be 0).
* Randomized scenarios + **held-out validation set** against overfitting.
* Runs for days: checkpoints + resume, watchdog, SQLite results DB, dashboard; best candidates auto-recorded as `.ddrec` for human review (F9 notes feed back into fitness).

### 3.6 Reference Data (clean provenance only in repos)
* **Pedestrian-impact biomechanics literature** (primary for v1): impact pattern by vehicle front height & speed (wrap / roof-vault / forward projection), throw-distance-vs-speed relationships, contact sequence (legs → pelvis/hood edge → torso → head), head wrap-around distance.
* Licensed mocap (CMU: `public`; AMASS: `research-nc`), self-recorded footage (`public`).
* Observations from commercial games/tools: private notes and aggregate measurements only (`private-reference`), never targets, never in repos.

---

## 4. Phases
Planned by **effort hours, not calendar weeks** (total ≈ **460 h** to v1, ±50 %). Work arrives in bursts (see §4.0); the plan measures progress by milestones reached, never by dates. Phases may interleave where marked.

### 4.0 Working Rhythm (bursty, hyperfocus-friendly)
* **Milestones, not deadlines.** Each phase is a checklist of small, self-contained tasks (≈ 1–6 h each) that end in something visible: a passing test, a new scenario, a video clip. A burst session finishes whole units; a pause never leaves things half-broken.
* **Resume log** — `docs/devlog.md`: every session ends with 3 lines: *what changed*, *what's next (one concrete task)*, *open questions*. Re-entry after days or weeks costs minutes, for you and for Claude.
* **Idea parking lot** — `docs/ideas.md`: new ideas that pop up mid-burst (ML, endorphin, other games, new behaviours…) get one line there and the current task continues. Reviewed at each phase retrospective; promoted only via ADR.
* **Definition of done** per task: builds on x86 + x64, tests green, devlog updated.
* **The PC works while you rest:** queue `dragdoll-lab` runs and long CI before stepping away; results are waiting on return.
* **Visible progress fuels momentum:** each phase ends with a short clip recorded in the workbench, kept in `docs/progress/`.

### Phase 0 — Decisions (~10 h)
* [x] ADRs D1–D17 (`docs/decisions/0001…0017`), `CLAUDE.md`, manifesto (README), `dragdoll-core` skeleton.
* [x] ADR-0018 (mission), ADR-0019 (governance), ADR-0020 (C++ conventions).
* [x] Pre-Phase-1 retrospective: six SA-readiness refinements folded into Phases 1–6 (marked **[SA]**).
* [ ] `dragdoll-sa` skeleton (deferred to the Phase 1 SA spike).
* [x] Git identity (repo-local, GitHub noreply) + first signed-off commit.
* [x] GitHub organization `dragdollphysics` (ADR-0019).
* [x] `dragdollphysics/dragdoll-core` repo + first push (SSH).
* [x] `docs/devlog.md`, `docs/ideas.md`, `docs/progress/` (§4.0).

### Phase 1 — Core Foundation + SA Spike (~60 h)
**Goal:** deterministic 16-body ragdoll through `dragdoll.h`; prove a 32-bit Jolt DLL runs inside San Andreas.
* [x] CMake + FetchContent (Jolt), `dragdoll_jolt_config`, presets, clang-format, `.gitignore`, `LICENSE` (task 1).
* [~] CI (x86 + x64) workflow written (task 2) — awaiting first green run on GitHub. doctest + Tracy join when first needed.
* [ ] C API skeleton: `DdWorld`, `DdCharacter`, `DdPoseBuffer`; create/destroy/step; proxy colliders (bulk add/remove); impulses; state readback; up-axis setting.
* [ ] Fixed-step accumulator + interpolated output (D6).
* [ ] **[SA]** Host-rate test: stepping with 1/25–1/30 s plus random jitter gives identical physics to a steady 120 Hz feed.
* [ ] **[SA]** Jolt allocator hooks → per-world memory accounting; host-configurable worker thread count.
* [ ] Rig JSON, `RagdollSettings` (`Stabilize`, `DisableParentChildCollisions`), anatomical joint limits, de Leva masses, CCD on limbs (D16).
* [ ] **SA spike (`dragdoll-sa` prototype):** minimal ASI (plugin-sdk) that loads the 32-bit DragDoll DLL, logs ped positions and RpHAnim bone matrices, and drops one passive ragdoll on a key press. Retires the biggest technical risk early.
* **Acceptance:** `drop_from_1m` stable (no NaN/explosion, limits respected) on x86 and x64; golden hashes stable across thread counts 1/4/8; host-rate jitter test green; memory per world < 64 MB; SA spike runs 10 min without crash (version confirmed by plugin-sdk).

### Phase 2 — Workbench & Tooling (~80 h, can interleave with Phase 1)
**Goal:** the development cockpit.
* [ ] WebGPU (Dawn) + ImGui window; Jolt `DebugRenderer` implementation; time scale, click-impulse, body inspector.
* [ ] Hot-reload DLL host + replay restore.
* [ ] `.ddrec` recording, F9/F10 annotations, `dragdoll-inspect`.
* [ ] Tracy integration; MCP bridge.
* [ ] **[SA]** C-API-boundary recording + host-session replay in the workbench (a recorded host session reproduces bit-identically).
* [ ] **Vehicle shape pack** (own geometry): sedan, sports car, SUV, van, bus, motorbike — parameterized bumper height, hood angle, windshield angle; kinematic driving at set speed/heading/braking.
* **Acceptance:** edit → rebuild → see change without closing the window in < 15 s; a recorded session replays bit-identically; Claude can run a scenario and read a note via MCP/inspect.

### Phase 3 — Active Body (~60 h)
**Goal:** a standing character that holds poses and stays upright under small disturbances.
* [ ] Pose drive via motors (D8/D9); tension profiles (`tense`, `relaxed`, `limp`, `stunned`) with smooth blending.
* [ ] Host pose input + full output contract (D17).
* [ ] **[SA]** Activation from motion: initialise bodies from two consecutive host poses (positions + velocities); test `walk_then_activate` shows no velocity pop.
* [ ] **[SA]** Mock 32-bone host skeleton (SA bone IDs/hierarchy as facts, own proportions) → rig mapping round-trip test.
* [ ] **[SA]** `failed` state: NaN/energy-explosion guards deactivate the character and report it; host fallback test.
* [ ] **Standing balance:** CoM, support polygon, Capture Point `xCP = x + v/ω`, ankle strategy, hip strategy (internal torques via stance hip). No stepping.
* **Acceptance:** `hold_stance` — upright 60 s under 5 N·s random pokes; joint tracking error < 5° RMS; low-speed `bump_5kmh` absorbed without falling; `walk_then_activate` momentum preserved within 5 %; mock-skeleton mapping round-trip error < 0.5°; forced-NaN test ends in `failed`, never a crash.

### Phase 4 — Car-Hit Vertical Slice (~70 h)
**Goal:** the core experience, end to end, in the workbench.
* [ ] **Perception:** vehicle time-to-collision, contact classification (bumper / hood / windshield / roof / ground), airborne detection, ground probe, settle detection.
* [ ] **Behaviour chain** (DragDoll names, final vocabulary in ADR): *Anticipate* (brace hands toward hood or dive, if aware & time allows) → *Impact* (stiffness spike, knee/hip give) → *Wrap / Vault / Project* → *Guard* (protect head, curl) → *Airborne* (flail, prepare landing) → *Landing* (absorb, roll/slide) → *Settle* (injured on ground) → *Handback*.
* [ ] Reference metrics implemented: impact pattern classification, throw distance, head contact position, contact order, time to settle.
* **Acceptance:** sedan at 30 km/h produces a wrap with correct contact order in ≥ 90 % of randomized runs; SUV/van produce forward projection; bus never produces a roof vault; no instability 5–120 km/h; human review notes recorded.

### Phase 5 — dragdoll-lab (~50 h)
**Goal:** overnight optimisation of the car-hit chain.
* [ ] Worker pool, CMA-ES, fitness (3.5), checkpoints/resume, SQLite, dashboard, provenance tags + CI guard (D14).
* [ ] Scenario space: vehicle class × speed (5–120 km/h) × angle × offset × ped heading × awareness × braking.
* **Acceptance:** a 48-hour unattended run completes with resume after a forced kill; best parameters beat the hand-tuned baseline on the held-out set; reviewed and accepted in the workbench.

### Phase 6 — dragdoll-sa Plugin (~100 h, can interleave with Phase 5)
**Goal:** the mod.
* [ ] **Triggers:** the game's vehicle-hits-pedestrian path + animation-name table (data file, e.g. `KD_left/right`, `KO_skid_front/back`, `KO_spin_L/R`) with handoff delay.
* [ ] **Rig mapping JSON** for the SA 32-bone skeleton (IDs from research notes).
* [ ] **Collider streaming:** nearby COL meshes → static proxies; vehicles → kinematic proxies from their COL data with velocity.
* [ ] **Pose override:** write local bone rotations into the RpHAnim hierarchy after the game's animation update; per-bone weights.
* [ ] **Root sync:** ped position follows DragDoll root; game physics for that ped suspended while active.
* [ ] **Handback:** choose game get-up/death animation by face-up/down + heading; blend out.
* [ ] **Compatibility:** exclusions (missions, cutscenes, player opt-in), SA-MP auto-disable, Hoodlum/Compact exes, `dragdoll-sa.ini` with keys familiar to existing-mod users.
* [ ] **[SA] Dev mode:** reload `dragdoll.dll` on a key press inside the running game; capture in-game sessions as `.ddrec` for workbench replay.
* [ ] **[SA] Fallback:** on `failed`, hand the ped back to the game's own animation.
* [ ] Packaging: single static-CRT ASI + ini + data. Never touches `main.scm`, `ped.ifp`, or game scripts.
* [ ] **Integration Workflow v1** (`docs/integration/`, ADR-0018): the 8 steps (trigger → skeleton → world → pose → root → handback → compatibility → verify), each with the SA implementation as worked example, plus templates (trigger table, rig mapping JSON, ini skeleton, verification checklist).
* **Acceptance:** all ship criteria in §0.

### Phase 7 — v1 Release & Launch (~30 h)
* [ ] Side-by-side video vs existing mods; hero GIF; README with manifesto.
* [ ] `GOVERNANCE.md` (ADR-0019), CONTRIBUTING, Contributor Covenant, DCO, issue/PR templates, `good first issue` labels, public roadmap + ADRs, GitHub Discussions.
* [ ] Optional build-in-public from Phase 4 on: short clips from `docs/progress/` posted so an audience exists on launch day.
* [ ] Releases on GitHub + SA mod sites; devlog post. Legal note: unofficial mod, no game assets or code included.

---

## 5. After v1 (Roadmap, not commitments)
**Community ports via the Integration Workflow** (other games through their mod loaders; engines like Godot/Unity/Unreal) — the main growth path.
Curriculum order (frequency × visibility): **v2 shots** (`KO_shot_*`) → **v3 falls** (`FALL_*`, `FLOOR_hit*`, high falls) → **ejected from vehicles** (`CAR_fallout_*`, `BIKE_fall_off`) → **explosions** → **melee**. Then:
* Stepping recovery (Capture-Point stepping, two-bone IK, SIMBICON-style feedback).
* GTA III / Vice City ports (same plugin, different addresses/rig maps).
* WASM build → public web playground (workbench compiled to the browser); three.js npm package (`@dragdoll/core`); Godot GDExtension (community "help wanted").
* Behaviour SDK → stable C ABI; behaviour packs.
* Video-to-skeleton reference pipeline (WHAM/GVHMR/RTMPose) on self-recorded footage.
* Reinforcement learning on the RTX 3080 if parameter optimisation plateaus.
* Offline baking of DragDoll output to animation clips (glTF; community tools to IFP) — e.g. for SA-MP.

---

## 6. Non-Goals (v1)
* Custom collision / rigid-body code (100 % Jolt).
* Facial/finger physics; neck/clavicle bodies.
* Locomotion generation (host animation drives walking).
* Machine-learned policies (parameter optimisation only).
* Any reaction other than vehicle hits.
* Multiplayer sync (SA-MP/open.mp), Definitive Edition, mobile versions.
* Double precision, soft bodies, GPU simulation.

## 7. Legal Guardrails
* "Euphoria" and "NaturalMotion" are trademarks: "inspired by" only; never in names, APIs, binaries or marketing claims of equivalence.
* Clean-room policy D14. Never read leaked source code.
* SA plugin: hooks via plugin-sdk in a separate repo; no game assets, no decompiled code, no redistributed game files.
* Existing mods: ideas and config patterns only; never their code or binaries (no visible licenses).
* **Patent check** (first-pass search of NaturalMotion / Zynga / Take-Two claims vs our techniques) before Phase 4; formal freedom-to-operate opinion before any commercial use.
* I'm-not-a-lawyer disclaimer applies; consult an IP lawyer for anything commercial.

## 8. Day 1 Checklist
1. Create `dragdoll-core/`, `git init`, layout from §1, `LICENSE` (MIT), `.gitignore`, `.clang-format`, `CLAUDE.md` (conventions D4, build/test commands, D12 flags, ADR index, §4.0 working rhythm). Move this workplan to `docs/`; create `docs/devlog.md` and `docs/ideas.md`.
2. Write ADRs D1–D17.
3. CMake: FetchContent Jolt (pinned), doctest, Tracy; `dragdoll_jolt_config`; x86 + x64 presets.
4. GitHub Actions: build + test x86 and x64 (MSVC).
5. One capsule falling onto a plane at fixed 120 Hz through a minimal `dragdoll.h`; first determinism-hash test passes on both architectures.

## 9. Key References
* Jolt Physics docs & samples (Ragdoll, SkeletonMapper, motors, CCD).
* de Leva, P. (1996). Adjustments to Zatsiorsky-Seluyanov's segment inertia parameters.
* Pratt, J. et al. (2006). Capture Point: a step toward humanoid push recovery.
* Yin, K., Loken, K., van de Panne, M. (2007). SIMBICON: Simple Biped Locomotion Control.
* Zordan, V., Hodgins, J. (2002). Motion capture-driven simulations that hit and react.
* Zordan, V. et al. (2005). Dynamic response for motion capture animation.
* Tan, J., Liu, K., Turk, G. (2011). Stable Proportional-Derivative Controllers.
* Simms, C., Wood, D. (2009). *Pedestrian and Cyclist Impact: A Biomechanical Perspective.*
* Hansen, N. — CMA-ES tutorial.
* Workspace `research/sa-ragdoll-mods.md` — existing SA mod teardown (not in repo).
