# ADR-0019: Governance — community-owned from day one

- **Status:** Accepted
- **Date:** 2026-11-12

## Context
The founder's intent is to connect existing ideas, give the project a strong and passionate start, and then let the community carry it — not to own it. Repositories under a personal account tie the project's identity to one person.

## Decision
* All repositories live under the GitHub organization **dragdollphysics**, not a personal account.
* Copyright is held by **"The DragDoll contributors"** (MIT, ADR-0001); contributions under DCO. No CLA, no relicensing power for any single person.
* The founder acts as **initial steward**: sets direction during bootstrap (Phases 0–7) via ADRs.
* After v1: maintainers are added from active contributors (merged work + review participation); org ownership is shared by **at least two maintainers** so the project never depends on one account.
* Decisions are made in public: ADRs for architecture, GitHub Discussions for direction. Lazy consensus among maintainers; disputes resolved by maintainer vote.
* A `GOVERNANCE.md` describing this, plus CONTRIBUTING and Contributor Covenant, ship with the v1 launch (Phase 7).

## Consequences
* The founder can step back at any time without the project stalling.
* Org name is independent of anyone's personal account name.
* Slightly more process than a solo project; kept minimal until there are multiple maintainers.

## Sources
Common open-source governance practice (e.g. "lazy consensus" as used by Apache projects).
