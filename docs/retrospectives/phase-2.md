# Phase 2 retrospective — Workbench & tooling

Date: 2026-11-13. Planned ~80 h; done in two burst days (2026-11-12/13), all seven tasks.

## What shipped
* **Recording & replay (2.1):** `.ddrec` at the C ABI boundary, replay with per-frame hash check and divergence report; `dragdoll_inspect` (notes, per-body motion).
* **F9 annotations (2.2):** typed note + screenshot saved next to the recording.
* **ImGui panels (2.3):** Playback, Scenarios, Bodies (spin-in-place flagged); right-click push with an impulse slider.
* **Live editing (2.4):** rig file watched + per-joint friction sliders with Save/Revert; **hot reload of `dragdoll.dll`** with session restore (same step, same pushes).
* **SA session replay (2.5, ADR-0022):** every F7 drop in the game is recorded; replays **bit-identically** in the new x86 viewer with the game's own DLL (958-frame session verified; x64 diverges, as predicted).
* **Vehicle shape pack (2.6):** six classes as parameterised profiles → closed kinematic meshes; `hit_<class>_40kmh` scenarios.
* **Command channel (2.7):** viewer driven over 127.0.0.1 by `dragdoll_ctl` (load, step, state, push, shot) — exact numbers for Claude instead of screenshots.
* Physics fixes found through the loop: passive joint friction (twisting lower spine), kinematic mesh mass (debug assert).

## What went well
* **The founder's eye + recordings** found and pinned down defects tests had missed (spinning lower spine → F9 mark → `inspect` → fix + regression test in one pass).
* Building tools in the order "record → inspect → panels → hot reload" meant each one was immediately used by the next.
* Debug builds caught a real release-hidden bug (kinematic mesh mass); a Jolt assert reporter now names the failed check.
* Small, reviewed slices with a commit per slice kept the build green throughout.

## What went badly / surprises
* **Process slip:** three commits went out without the DCO sign-off (`-s`) before it was noticed; now in memory and done on every commit.
* Python patch scripts in shell heredocs mangled `\n` escapes twice (compile errors); prefer the Edit tool or raw strings for C++ snippets.
* Two viewer bugs reached the founder that a quick self-check would have caught (camera focus asked for 1 body of 16; replays far from the origin) — the new command channel now allows that self-check.
* Braking vehicles are inherently host-rate dependent (linear interpolation of a curved path); constant-speed tests still cover rate independence.

## Decisions taken (ADRs)
* **ADR-0022:** recordings replay through the build that made them (x86 viewer + game DLL for SA).
* Tracy profiler deferred to Phase 6 (optimisation), with the founder's agreement.

## Plan changes for Phase 3
* Use `dragdoll_ctl state` to verify each behaviour numerically before asking for a human verdict.
* Joint-limit sliders and live tension/stiffness sliders belong naturally in Phase 3 (motors), reusing the Rig panel.
* Known issues carried: SA mouse look in windowed mode; the 1 ms / 8-ragdoll budget (Phase 6); truncated recordings after a killed game (idea).
