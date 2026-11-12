# ADR-0017: Host input/output contract

- **Status:** Accepted
- **Date:** 2026-11-12

## Context
Hosts (SA, engines) play their own animation and must keep their game logic consistent with the physical body.

## Decision
Hosts supply **target poses**; DragDoll returns **local-space bone rotations**, a separate **character root transform**, **per-bone blend weights**, and a **state** (`active`, `recovering`, `settled`, `dead`) with recovery hints (face up/down, heading). Animation playback stays in the host.

## Consequences
Hosts can override partially (upper-body flinch while legs keep locomotion), keep camera/AI/collision on the body, and choose get-up animations.

## Sources
workplan D17
