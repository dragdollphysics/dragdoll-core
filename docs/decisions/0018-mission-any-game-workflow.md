# ADR-0018: Mission — a workflow to replace animations in any game

- **Status:** Accepted
- **Date:** 2026-11-12

## Context
DragDoll's lasting value is not one mod but a **repeatable way** to take any game's canned reaction animations (hit, fall, knockdown…) and replace them with an active ragdoll. The San Andreas car-hit mod proves the idea; the workflow is what the community can carry to other games and engines.

## Decision
DragDoll ships three things, in this order of importance:
1. **The core** (`dragdoll-core`): engine-agnostic active ragdoll behind a stable C ABI (ADR-0002, ADR-0017).
2. **The Integration Workflow** (`docs/integration/`): a step-by-step, game-agnostic playbook, plus templates and tools, for wiring DragDoll into a game:
   1. **Find the trigger** — which animations/events to replace (data table: animation name → behaviour + handoff delay).
   2. **Map the skeleton** — host bones → DragDoll 16-body rig (rig mapping JSON).
   3. **Mirror the world** — stream nearby collision as static proxies; moving objects/vehicles as kinematic proxies.
   4. **Override the pose** — write local bone rotations after the host's animation update, with per-bone weights.
   5. **Sync the root** — host character transform follows DragDoll's root; host physics for that character suspended while active.
   6. **Hand back** — choose the host's get-up/death animation from DragDoll's state and recovery hints.
   7. **Stay compatible** — exclusions (missions, cutscenes, player, multiplayer), config file, packaging rules.
   8. **Verify** — side-by-side clips, reference metrics, stability and performance checks.
3. **Reference integrations** that follow the workflow exactly: first `dragdoll-sa` (GTA San Andreas, car hits), later others by the community.

Every reference integration must be explainable as "the workflow, applied to game X". Anything game-specific that turns out to be generic moves into the core or the workflow templates.

## Consequences
* Phase 6 produces the SA plugin **and** the first version of `docs/integration/`, written from that experience.
* Integration-facing API stays small and documented; integration pain is treated as a core bug.
* Community ports (other games via their mod loaders; engines like Godot, Unity, Unreal) need the workflow, not the founder.
* Legal guardrails (ADR-0014) are part of the workflow: no game assets, no decompiled code in DragDoll repos.

## Sources
workplan §0; ADR-0017; `research/sa-ragdoll-mods.md` (workspace) — patterns from existing SA mods.
