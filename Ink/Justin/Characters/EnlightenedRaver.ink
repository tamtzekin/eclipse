// EnlightenedRaver.ink — dummy content, demonstrates a plain StatEffect tag.

=== enlightened_raver ===
Yo. You feel that? The whole room's basically breathing right now.

* [That's... actually kind of beautiful.] # +1 AESTHETICS
    Right?? I KNEW you'd get it.
    -> END
* [I don't feel anything.]
    Give it time.
    -> END

// ─── yaps ────────────────────────────────────────────────────────────────
// Overhead one-liners, shown above the character's head between
// conversations. One line per line; the runtime harvests the whole knot
// once with ContinueMaximally and picks from it at random, so there are no
// choices and no state here — just what this person mutters when you're
// not talking to them. Each should hint at what they want.

=== enlightened_raver_yaps ===
The bass is doing the thinking for me tonight.
You're carrying something heavy. I can hear it.
-> DONE
