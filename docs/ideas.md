# Idea Parking Lot

One line per idea. Reviewed at each phase retrospective; promoted only via ADR.

* Video-to-skeleton reference pipeline (WHAM / GVHMR / RTMPose) on self-recorded footage — post-v1.
* Reinforcement learning on the RTX 3080 if CMA-ES plateaus — post-v1.
* Bake DragDoll output into animation clips (glTF → IFP via community tools), e.g. for SA-MP.
* Behaviour packs as a community contribution format.
* Vogue-themed demo scenes ("death drop" showcase) for the launch video.
* Pi-style version numbers for the core once it freezes after 1.0 (TeX homage).
* Endorphin as a `private-reference` benchmark only, if a legal copy runs (check its EULA first).
* **Ballroom game** (post-v1 showcase, browser via the WASM playground): physics-based vogue contest; judges score death drops / dips / spins / floor work from physics metrics (drop height & speed, control at the catch, landing impulse, rotation, freeze hold, extension). Judging = the lab's fitness function turned into gameplay. Must be made *with* ballroom community members, crediting the culture's Black & Latino LGBTQ+ roots. Cheap early taste: a `death_drop` scenario in Phase 4 to test the Landing behaviour.
* "Crash Test DragDoll" as the display name / mascot of the SA v1 mod.
* Replay a truncated recording (killed game) up to its last complete frame instead of rejecting it (task 2.5).
* Vehicles react to bodies (founder 2026-11-17): a car driving over a lying body should bump over it as in GTA V; SA cars bump on some occasions too. Today vehicles are kinematic (infinite mass) to the ragdoll. Investigate after 4.3.
* Measure SA's own gravity in game (ped / car fall acceleration) so ragdoll flights look consistent with the game's world; the baseline uses 9.81 m/s².
* Secondary-strike grip ("pocketing"): a shoulder / head hitting a windscreen or bonnet dents it and pins locally (Forman 2015); our rigid surfaces let the trunk slide up instead, so wrap bodies ride the bonnet longer than multi-body models predict. Candidate: a local grip / deformation zone on vehicle upper surfaces. Only if the in-game look calls for it (2026-11-17, task 4.3).
