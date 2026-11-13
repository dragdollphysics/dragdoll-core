# ADR-0022: Recordings replay through the build that made them

- **Status:** Accepted
- **Date:** 2026-11-13

## Context
A `.ddrec` stores the inputs at the C ABI boundary, not the results; replay re-simulates them (ADR-0017). Determinism is promised only for the same binary and inputs (ADR-0007). The SA game uses the 32-bit portable build (SSE4.2, no AVX/FMA, ADR-0015); the workbench was x64 (AVX2 + FMA). Different instruction sets round differently in the last bit, and ragdoll contacts amplify that: an SA session replayed in the x64 viewer runs but diverges (verified: 958-frame SA session, "DIVERGED" on x64).

Options considered:
* **A. Replay with the recording's build:** an x86 viewer, loading the exact `dragdoll.dll` the game used.
* B. `JPH_CROSS_PLATFORM_DETERMINISTIC` in every build: identical maths everywhere, but slower physics in the game, which needs the headroom.
* C. Also record per-frame body transforms: exact to watch, but larger files and no re-simulation with new code.

## Decision
**A.** The viewer is built for x86 dev presets too. `dragdoll_view --dll <path>` loads (and hot-reloads) a given `dragdoll.dll`; `watch.ps1 -Replay` detects an x86 recording from its header, opens the x86 viewer and uses the game's `scripts\dragdoll.dll` when `DRAGDOLL_SA_GAME_DIR` is set (or `-Dll`). C may be added later for sharing clips.

## Consequences
* Verified: the same SA session replays **identical at every frame** with the x86 tools + game DLL.
* The game build keeps full speed; no determinism mode cost.
* A recording is tied to its DLL: after rebuilding the game's DLL, older recordings replay as "another build" and may diverge (still useful to watch, not for bit-exact checks). Keep the DLL next to recordings that matter.
* Re-simulating an old recording with *new* code (the point of hot reload) shows the new behaviour from the same inputs; divergence there is expected and wanted.

## Sources
* ADR-0007 (determinism scope), ADR-0015 (x86 portable build), ADR-0017 (recording at the boundary).
* Jolt Physics documentation: deterministic simulation and `JPH_CROSS_PLATFORM_DETERMINISTIC`.
