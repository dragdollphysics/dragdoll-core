# Architecture Decision Records

Each ADR is one page. Change a decision only by adding a new ADR that supersedes it.

| ADR | Title | Status |
|-----|-------|--------|
| [0001](0001-license.md) | License and contribution model | Accepted |
| [0002](0002-world-ownership.md) | DragDoll owns its physics world | Accepted |
| [0003](0003-physical-honesty.md) | Internal torques only; explicit assist budget | Accepted |
| [0004](0004-conventions.md) | Units, handedness and up axis | Accepted |
| [0005](0005-math.md) | Use Jolt math types | Accepted |
| [0006](0006-timestep.md) | Fixed 120 Hz step with interpolated output | Accepted |
| [0007](0007-determinism.md) | Determinism and golden hashes | Accepted |
| [0008](0008-joint-drive.md) | Jolt constraint motors for pose tracking | Accepted |
| [0009](0009-gain-units.md) | Stiffness as frequency and damping ratio | Accepted |
| [0010](0010-rig.md) | Data-driven 16-body rig | Accepted |
| [0011](0011-tooling.md) | Build and tooling | Accepted |
| [0012](0012-jolt-flags.md) | Single source of truth for Jolt build flags | Accepted |
| [0013](0013-layered-architecture.md) | TeX/LaTeX-style layering | Accepted |
| [0014](0014-clean-room.md) | Clean-room provenance | Accepted |
| [0015](0015-platforms.md) | Win32 x86 is first-class | Accepted (amended) |
| [0016](0016-fast-impacts.md) | Continuous collision for fast impacts | Accepted |
| [0017](0017-host-output-contract.md) | Host input/output contract | Accepted (amended) |
| [0018](0018-mission-any-game-workflow.md) | Mission — a workflow to replace animations in any game | Accepted |
| [0019](0019-governance.md) | Governance — community-owned from day one | Accepted |
| [0020](0020-cpp-conventions.md) | C++ conventions | Accepted |

## Template
```
# ADR-NNNN: Title
- **Status:** Proposed | Accepted | Superseded by ADR-XXXX
- **Date:** YYYY-MM-DD
## Context
## Decision
## Consequences
## Sources
```
