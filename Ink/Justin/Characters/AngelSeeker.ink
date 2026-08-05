// AngelSeeker.ink — "Girl in the Bathroom". Dummy content demonstrating:
//   - native Ink branching on synced quest-state variables (replaces the old
//     8-way hardcoded C++ entry-node lookup) — using guard-clause diverts,
//     since Ink's inline conditional blocks only support a single
//     condition + else, not an if/elseif/elseif/else chain.
//   - a skill-check choice (SKILLCHECK: tag, read by ParseSkillCheck)
//   - a visible stat gate (greyed + hinted — "GATE:ZEN: 1" tag, re-wrapped
//     to "[ZEN: 1]" in C++ — no literal brackets in .ink source, see
//     EclipseDialogueSubsystem.h)
//   - an effect tag ("+1 ZEN")
//   - a natively-hidden Ink choice (has_hair && has_eye) alongside a
//     menu-action tag ("MENU: enterStall") that fires the existing
//     DispatchMenuAction("enterStall") handler.

=== angel_seeker ===
{ met_npc && quest_stage == "ready": -> ready }
{ met_npc && has_hair && has_eye: -> close }
{ met_npc && has_hair: -> have_hair }
{ met_npc && has_eye: -> have_eye }
{ met_npc: -> still_looking }
-> intro

= ready
You came back. Did you find it?
-> choices

= close
You're close now. I can feel it.
-> choices

= have_hair
You found my hair. There's still the eye.
-> choices

= have_eye
You found the eye. Now the hair.
-> choices

= still_looking
Still looking, then.
-> choices

= intro
I've been waiting by this stall a long time. You feel it too, don't you? The Angel.
-> choices

= choices
~ met_npc = true

* [Tell me about the Angel.] # SKILLCHECK:ZEN:1
    She only speaks to the ones who slow down enough to listen. # +1 ZEN
    -> angel_seeker
* [You seem so sure of that.] # GATE:ZEN: 1
    You'd be sure too, if you'd seen what I've seen. # +1 RHYTHM
    -> angel_seeker
* {has_hair && has_eye} [I think I'm ready to go in.] # MENU: enterStall
    -> END
* [I should go.]
    -> END
