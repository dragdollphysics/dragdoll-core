# ADR-0026: Vehicles and bodies on the ground

- **Status:** Accepted
- **Date:** 2026-11-17

## Context
Host vehicles are kinematic colliders in DragDoll (ADR-0016, ADR-0024): they follow the game's cars exactly and
cannot be pushed. That is right for the hit itself - a 75 kg body does not slow a 1.5 t car measurably - but wrong for
a car driving over a body that already lies on the road. In the first in-game test (2026-11-17) bodies were launched a
second time, at 17-21 m/s and up to 4 m high, whenever the player drove on: the car's proxy is solid down to its ground
clearance, the body was wedged between its front edge and the road and squeezed out. In reality the car rides over the
body (a bump), pushing it a little at most.

## Decision
* **Pass-through, opt-in per character:** `dragdoll_character_set_vehicle_pass_through(character, enabled)`. Once the
  body has landed after a hit (the impact report's first ground contact after leaving the object that struck it),
  moving colliders no longer collide with it (the contact pair is rejected in Jolt's contact validation).
  *Amended 2026-11-17 (founder's test):* the switch happens at the moment the trunk or head touches the ground after
  the hit, even while the vehicle still touches it - waiting for the separation let bodies get caught under the car
  first. Legs do not count (the pedestrian stands on its feet when the car arrives), nor arms (a hand reaching down
  while the body still lies on the bonnet would drop it through the car). The static
  level still collides. Off by default; recorded (format 6).
  *Amended 2026-11-18 (San Andreas, game-first start - ADR-0027):* a second event switches pass-through on, needing no
  DragDoll impact: since the character became physics-driven, the first moment it touches **no moving object** and has
  its trunk or head on the ground - or arms and legs both on it (lying on its side, propped up). The host's own hit can
  hand the body over without DragDoll's vehicle ever touching it, so no impact report starts; and a body propped on
  arms and legs never had its trunk on the road - in game, cars carried such corpses tens of metres. Both events are
  contact states, no timers. Tests: a body handed over and knocked onto its side, then driven over at 8 m/s: 9.2 m/s
  shove colliding, 0 passing through.
  *Amended 2026-11-18 (founder: walking over a dead ragdoll moved it):* once down, a pass-through body ignores every
  **host-driven (kinematic) body** - vehicle proxies and animated characters alike (the player on foot is a kinematic
  puppet; in the game a corpse's collision is off and it is walked over). The level and other ragdolls still collide.
  Test: a kinematic walker through a body lying on its side at 1.5 m/s: 1.9 m/s shove colliding, 0 passing through.
* **The bump belongs to the host:** DragDoll does not move host vehicles. In San Andreas the game ped's own collision is
  switched back on once its body lies on the ground (dragdoll-sa), so the game's car-over-ped handling gives the bump.
* **Not decided yet:** a slight, capped push of the body by a car passing over it. Added only if the founder finds the
  body too still under a passing car.

## Consequences
* No more launches by a car driving over a lying body; a car passing over it leaves it where it lies.
* A second car hitting a landed body at speed also passes through it (like a corpse in the game). Acceptable for v1;
  revisit with the push if it looks wrong.
* Test: `vehicle pass-through: a car driving over a landed body does not launch it` (colliding: the body is shoved at
  the car's 8 m/s; passing through: it stays put).
