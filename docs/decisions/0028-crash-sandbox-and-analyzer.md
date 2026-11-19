# ADR-0028: One crash sandbox and a knowledge-based analyzer (what is tested is what is seen)

- **Status:** Accepted
- **Date:** 2026-11-18

## Context
The vehicle tests built their own hidden scenes (their own car, speed, dummy, braking) while the viewer showed other,
fixed scenarios (`hit_*`, `react_*`, `aware_sedan_40kmh`) that no test checked beyond "runs without errors". The
founder could not see what the tests ran, and a test car that never braked contradicted the viewer's cars, which
always braked (in the game, measured: the car keeps a median 98 % of its speed 1.5 s after a hit). The founder: no
placebo scenarios; one parametrised sandbox; an analyzer built from the research that decides whether a crash passed.

## Decision
1. **One crash sandbox** (`scenarios/include/dragdoll_scenarios/crash.hpp`): every vehicle-hits-pedestrian run is the
   scenario named by its settings, `crash:vehicle=...,kmh=...,limp=...,aware=...,brake=...,delay_ms=...,decel=...,
   facing=...,size=...,offset=...`. Game mode = what the SA mod does (host-animated, `car_hit`, handover at contact,
   pass-through on the road). Vehicle packs carry their own `brake_decel`. The viewer's "Crash sandbox" panel plays any
   setting; tests and the crash lab run the same code.
2. **The analyzer** judges a finished run with rules kept as data (`packs/analyzer/crash_rules.json`), each with its
   conditions, measure, expected range, source and verification tag (docs/reference/pedestrian-impact.md):
   * **sanity** and **contract** rules (physics breaks; DragDoll's own promises) -> **FAIL**: a defect;
   * **corridor** rules (published ranges, only where their conditions hold: front height vs the centre of gravity,
     speed, side-on, braking, class) -> **OUT OF RANGE**;
   * **reference** notes where nothing published applies (a vehicle that keeps going, vaults and somersaults, struck
     from the front or behind, a body still on the vehicle at the end) -> **NO REFERENCE**: for the founder to judge.
   The verdict of a run is the worst of its rules. Observations of commercial games are never rules (ADR-0014).
3. **The crash lab** (`tools/crashlab`, `dragdoll_crashlab`) sweeps a grid (10 vehicles x 8 speeds x 4 drive / brake
   modes x 8 facings x 2 sizes x 2 awareness = 10 240 runs, about 2 minutes on 12 cores; `--sample N` for a seeded
   subset) into `crashlab/results.csv`; the viewer's "Crash results" window lists it, and any row plays there with
   exactly its settings (runs are deterministic, ADR-0007).
4. Unit tests that hit a pedestrian with a vehicle run on the sandbox and check the analyzer's sanity / contract rules;
   the old car scenarios are removed from the viewer.

## Consequences
* A test fact is visible: open its `crash:` name in the viewer.
* Rules move with the research: adding a published finding is a new rule in the JSON, not code.
* First sample (1 000 runs): FAIL 13 (all `no_explosion` at 10 km/h: a body part faster than 2 x the vehicle + 5 m/s),
  OUT OF RANGE 175, NO REFERENCE 612, PASS 200.
* Still outside the sandbox: hidden scenes in other tests (pass-through over a lying body, frozen corpses, perception,
  senses, aim, world) - next to move where something can be watched.
