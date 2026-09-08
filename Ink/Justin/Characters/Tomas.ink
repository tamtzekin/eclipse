// Quinn2.ink — L_Outside placeholder NPC (SKM_Quinn_Simple2).
// Demonstrates an IdentityGate ("GENDER == female") — hidden, no hint.

=== tomas ===
{...This is it? | He looks off into the distance. | I need to stop talking. Find a way in. -> DONE}

* You don't sound like you want to be here.
    Can we go inside? This cold is driving me mad. 
      ~ SideQuests += cold_tomas_wants_to_go_in

-> tomas

* Have you got the tickets?
I didn't know I was supposed to get them. Check that window by the door.
-> tomas

* Think we can sneak in?
If you can get on the list, maybe.
-> tomas


  ~ SideQuests += alina_needs_a_cigarette
    - "cold_tomas_wants_a_jacket":     ~ return "Tomas needs a jacket"
    - "you_need_a_drink":              ~ return "You're thirsty, find something to drink"

// ─── yaps ────────────────────────────────────────────────────────────────
// Overhead one-liners, shown above the character's head between
// conversations. One line per line; the runtime harvests the whole knot
// once with ContinueMaximally and picks from it at random, so there are no
// choices and no state here — just what this person mutters when you're
// not talking to them. Each should hint at what they want.

=== tomas_yaps ===
This cold is driving me mad.
I didn't know I was supposed to get the tickets.
There's a window by the door. Someone should try it.
I need to stop talking and find a way in.
If we got on the list, maybe. Maybe.
-> DONE
