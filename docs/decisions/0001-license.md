# ADR-0001: License and contribution model

- **Status:** Accepted
- **Date:** 2026-11-12

## Context
DragDoll must be adopted by modders, engine users and contributors. Dependencies: Jolt (MIT), Dear ImGui (MIT), Tracy (BSD-3), doctest (MIT), Dawn (BSD-3).

## Decision
License all DragDoll code and data under **MIT**. Contributions use the **Developer Certificate of Origin** (`Signed-off-by:` on every commit); no CLA.

## Consequences
Maximum reach, compatible with all dependencies. Others may fork commercially without contributing back (accepted trade-off). DCO also serves as the contributor's clean-room attestation (ADR-0014).

## Sources
https://opensource.org/license/mit, https://developercertificate.org
