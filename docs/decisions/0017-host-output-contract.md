# ADR-0017: Host input/output contract

- **Status:** Accepted
- **Date:** 2026-11-12

## Context
Hosts (SA, engines) play their own animation and must keep their game logic consistent with the physical body.

## Decision
Hosts supply **target poses**; DragDoll returns **local-space bone rotations**, a separate **character root transform**, **per-bone blend weights**, and a **state** (`active`, `recovering`, `settled`, `dead`) with recovery hints (face up/down, heading). Animation playback stays in the host.

**Amended 2026-11-12 (pre-Phase-1 retrospective):**
* **Activation from motion:** the host passes two consecutive poses (and their time delta) when activating a character, so bodies start with the animated linear and angular velocities — no freeze-then-fall.
* **`failed` state:** NaN or energy-explosion guards deactivate the character and report `failed`; the host must fall back to its own animation. DragDoll must never take the host down with it.
* **Recording at the boundary:** every host call can be logged, so any host session replays exactly in the workbench (ADR-0007).

## Consequences
Hosts can override partially (upper-body flinch while legs keep locomotion), keep camera/AI/collision on the body, and choose get-up animations.

## Sources
workplan D17
