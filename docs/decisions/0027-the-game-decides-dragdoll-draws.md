# ADR-0027: The game decides, DragDoll draws (host contract for game integrations)

- **Status:** Accepted
- **Date:** 2026-11-17
- **Amends:** ADR-0025 §3a (Settle / Handback) for game hosts

## Context
The first San Andreas integration (2026-11-17) started the ragdoll just *before* a car touched the pedestrian and then
made the game's decisions for it: who dies (an impact-speed placeholder), the death task, health, the get-up animation,
the lying animation, which animations to mute, the ped's collision. Every one of those replaced a decision the game
normally makes, and each broke something downstream: no bump / crunch / scream (the game never saw its hit), peds
launched upwards, rotated deaths, a crash in the game's animation code, a ped drawn lying while walking, medics unable
to revive our dead. Each fix was another patch on the game. The founder: start the ragdoll cleanly on a crash, release
it, and let the game carry on with its normal routines.

The two existing physics ragdoll mods for San Andreas (research/sa-ragdoll-mods.md, static inspection) both follow the
game: one starts physics when the game starts one of its own knock-down / fall / death animations, the other hands the
ped back to the game's own get-up after it lies still.

## Decision
1. **The game decides; DragDoll draws.** While a ped is ragdolled the host writes exactly three things on the game's
   character: its **drawn bones** (from the ragdoll), its **position and heading** (where the body is) and its
   **speed** (zero, so the game's physics does not move it). It never writes health, tasks, states, flags,
   animations or collision. Inside its own simulation DragDoll may do as it likes (e.g. ADR-0026 pass-through).
2. **Start = the game's own hit.** The ragdoll starts when the game has registered a vehicle hit on the ped (San
   Andreas: its health dropped this frame and its last damage was "rammed / run over by car"), from the ped's motion in
   the frame before (activation from motion; the game's upward launch is never copied). The game's own hit effects
   (bump, sound, scream, damage, wanted level, accident report) all happen unchanged. Pre-impact perception and
   reactions keep running on the kinematic puppet; they are not visible until the handover.
3. **During:** the game runs its own routine for that ped (knock-down, dying) as if DragDoll were not there.
4. **Release = the game's next step, not a DragDoll timer.**
   * **Survivor:** when the game starts its get-up, the host puts the ped where the body lies (on the game's floor),
     heading from the body, and lets go; the drawn pose blends into the game's over a fraction of a second (drawing
     only).
   * **Dead:** the body stays a (sleeping) ragdoll, drawn where it lies, until the game itself moves on - a medic
     revives it (its get-up: as a survivor), it despawns, or it is far from the player. This replaces ADR-0025 §3a's
     "the ragdoll is destroyed for every outcome" for game hosts: no lying pose is forced on a game corpse.
   * Always: when the game's ped is gone, or far from the player.
6. **Exception (founder's decision, 2026-11-18): no push-back on the car.** On every knock-down and kill the game's
   hit function pushes the hitting car back with a force that grows with the impact - measured: 35-41 % of the car's
   speed per pedestrian (three in 0.1 s: 105 -> 21 km/h), while a 75 kg body takes a few percent in reality. While
   DragDoll's car hits are on, that one push (on the hitting car, from inside the hit function) is skipped - the game
   never applies it; every other force in the game is untouched, and the collision itself still slows the car a little.
   F6 off restores the game's push for comparison.
5. **When something breaks, first ask which game decision DragDoll took over** - and give it back, rather than adding
   a patch.

## Consequences
* Removed from the host: the kill-speed placeholder, the death task, health / medics flags, the forced lying
  animation and the muting of the game's knock-down animations, the pre-hit collision changes, Settle.
* The ragdoll starts a frame or two after the game's ped collision (larger than DragDoll's contact) registers the hit;
  DragDoll's own car proxy then strikes the already-handed-over body.
* The game picks the get-up (front / back) from its own knock-down, not from how the body landed: a body landing the
  other way up flips at the release. To be measured before anything is done about it.
* A survivor whose body is still moving when the game starts its get-up is released anyway.
* Corpses cost one sleeping character each while near the player.
* Open: whether the game ped's own collision should stay on while its body flies (extra bumps / hits) - left as the
  game has it until seen in game.

## Sources
* research/sa-ragdoll-mods.md (existing SA ragdoll mods: triggers and release, static inspection).
* Founder's in-game reviews, docs/devlog.md 2026-11-17.
