# ADR-0014: Clean-room provenance

- **Status:** Accepted
- **Date:** 2026-11-12

## Context
DragDoll is openly inspired by a proprietary system owned by a large commercial publisher. Its credibility depends on provable independence.

## Decision
1. No code, data, parameters, recordings or leaked material derived from Euphoria, endorphin, RAGE, or any proprietary engine or mod enters any repository.
2. No decompiling them.
3. Contributors who have seen leaked RAGE/Euphoria source don't work on core.
4. Behaviours use **DragDoll's own vocabulary**.
5. Every major technique cites its published source in an ADR.
6. Datasets carry a provenance tag (`public`, `research-nc`, `private-reference`); CI plus a pre-commit hook reject `private-reference` data and anything derived from it.
7. Observations of commercial games are private notes or aggregate measurements only, never optimisation targets.


*Founder decision 2026-11-18 (measurements):* a commercial game's objects may be **measured** (a car's front, bonnet, windscreen and roof heights and lengths; how tall its people stand) to set the dimensions of our own generic shapes, e.g. the vehicle packs. Only our own rounded numbers per class (medians over several vehicles) enter the repository, under generic names; no meshes, slices, per-model values or names from the game. The raw measurements stay private, outside the repositories. Extended the same day (founder): the game's reaction animations (evasive step, dive, hands up, flee) may be measured the same way - recorded privately as DragDoll joint angles while the game plays them, then reduced to a few key poses with timing, averaged over several recordings and rounded (5 degrees, 5 cm), for the sandbox's own starting states; no animation data, curves or names from the game enter the repository.

*Founder decision 2026-11-18:* for the San Andreas integration (dragdoll-sa), a community reconstruction of the game (gta-reversed) may be **read** for interop facts - which game function does what, in which order, at which address. Nothing from it is copied into any repository; findings stay private notes in our own words; hooks are written fresh against plugin-sdk. Decompiling the game ourselves stays excluded.

## Consequences
Some useful data stays off-limits; in exchange the project is defensible and contributor-safe. Not legal advice; consult an IP lawyer before any commercial use.

## Sources
SAS Institute v World Programming (CJEU C-406/10, 2012); 17 U.S.C. 102(b)
