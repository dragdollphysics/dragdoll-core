# ADR-0002: DragDoll owns its physics world

- **Status:** Accepted
- **Date:** 2026-11-12

## Context
Hosts use different physics engines (SA: custom; Godot: its Jolt module is not reachable from a GDExtension; web: separate WASM modules with separate heaps). A shared-world mode has no real customer.

## Decision
DragDoll **always owns its own Jolt `PhysicsSystem`**. Hosts mirror the relevant environment as **proxy colliders**: static meshes/primitives and kinematic bodies (vehicles, moving objects) with velocities. The API supports **bulk add/remove** for streaming.

## Consequences
One integration path for every host. Hosts must stream nearby collision (SA: COL data). Interaction back into host physics is limited to reported contact impulses the host may apply.

## Sources
workplan D2
