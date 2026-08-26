=== alina ===
// (top) is a LABELLED GATHER. Every branch below falls through to the
// bare `-` at the bottom, which loops back here — so there is exactly one
// `->` in the whole knot instead of one at the end of every choice.
//
// The label sits ABOVE the opening line on purpose: looping to a label
// placed after it would skip the line, and Alina would go silent between
// choices. Here the {a|b|c} sequence advances on every pass, exactly as it
// did when each branch ended with `-> alina`.
- (top)
{...This is it? | What are we still doing standing here? | I'm getting kind of thirsty, too. Did you find a smoke?}

* You don't sound like you want to be here.
    I don't know. I get this feeling. I can't describe it. She looks off into the distance at something. 'I need a cigarette.'
    ~ SideQuests += alina_needs_a_cigarette

* {SideQuests ? alina_needs_a_cigarette} Can you see anyone smoking around here?
    Everyone carries cigarettes at the club. It's basically currency. Can probably bribe your way in with an expensive pack of those herbal cigarettes. Banned across three dynasties.

* How are we going to get in?
    That's not hard. They'll let me in.

    ** I mean for me.
        I don't know, maybe you can talk your way in. With your looks or your mind.

    ** Because you're a woman.
        I'll take advantage of whatever situation I can.

    ** {aesthetics > 2} Because you're hot.
        It's not my fault other people decide to be uglier than me.

    // Nested gather: collects the three ** branches so they rejoin the
    // outer flow. Without it each one would need its own `->`.
    --

// `+` not `*` — a sticky choice, so leaving is always available. With `*`
// this option is used up after one visit and a player who came back would
// have no way out once the other choices were exhausted.
+ I'll be back.
    I'll wait here. Someone around here must be smoking something.
    -> DONE

// The one and only loop. Everything above lands here.
- -> top
