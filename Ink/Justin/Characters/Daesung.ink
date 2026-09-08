// Daesung.ink — "rich guy passed out". Dummy content, demonstrates an
// ItemGate ("GATE:EMPTY_BOTTLE", re-wrapped to "[EMPTY_BOTTLE]" in C++ — no
// literal brackets in .ink source, see EclipseDialogueSubsystem.h) — greyed
// out with a "(no EMPTY_BOTTLE)" hint until that item is in the player's
// inventory.

=== daesung ===
(slumped against the wall, eyes half shut) ...hey. Hey, is that a bottle.

* [Here — fill it up if you need.] # GATE:EMPTY_BOTTLE
    ...you're an angel.
    -> END
* [Just resting?]
    Somethin' like that.
    -> END

// ─── yaps ────────────────────────────────────────────────────────────────
// Overhead one-liners, shown above the character's head between
// conversations. One line per line; the runtime harvests the whole knot
// once with ContinueMaximally and picks from it at random, so there are no
// choices and no state here — just what this person mutters when you're
// not talking to them. Each should hint at what they want.

=== daesung_yaps ===
Same faces, different night.
I'm not queueing again. Not for this.
-> DONE
