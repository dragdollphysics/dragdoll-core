# ADR-0021: The workbench starts on raylib

- **Status:** Accepted
- **Date:** 2026-11-12

## Context
The plan (ADR-0011 / workplan §3.2) called for a Dawn (WebGPU) + Dear ImGui workbench. Phase 1 needed a way for a human to judge scenarios, so a raylib viewer was built (`dragdoll_view`): orbit/pan camera, debug-geometry drawing through the C ABI, HUD, auto-restart, joint-limit overlay. It was reviewed and approved by the founder and is cheap to extend.

## Decision
* The workbench is `dragdoll_view`, built on **raylib**; panels use **Dear ImGui through rlImGui** when needed (Phase 2).
* Dawn/WebGPU is **deferred** to the web playground (post-v1), where it is actually required.
* The workbench only uses the public C ABI (as before), so a renderer swap never touches the core.

## Consequences
* No Dawn build complexity or GPU-API churn during Phase 2.
* OpenGL via raylib is plenty for debug geometry of a few dozen bodies.
* raylib can also target the web later, which may make a Dawn port unnecessary.

## Sources
workplan §3.2; Phase 1 retrospective.
