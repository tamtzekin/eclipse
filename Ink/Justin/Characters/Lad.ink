// Dance proficiency per style, 1-3: how reliably this dancer lands each switch in a battle.
VAR lad_hakken = 1
VAR lad_muzzing = 2
VAR lad_liquid = 1
VAR lad_gloving = 1
VAR lad_tektonik = 1

=== lad ===
Floor's yours whenever. Think you can keep up?
+ [Dance battle.] # MENU: danceBattle
    -> END
+ [Not now.]
    -> END

// Trash talk over the 4-bar intro before a battle; lines spread across the intro, no choices.
=== lad_battle_intro ===
This kid doesn't know how to muzz.
Do you... kid?
-> END

// Said as he starts showing each style, one bar apiece.
=== lad_battle_demo ===
Follow my style.
-> END

// While demoing each style, one bar apiece. A _word_ is lit up in the style's colour.
=== lad_show_hakken ===
This is _hakken_. Stamp it — DOWN.
-> END

=== lad_show_muzzing ===
_Muzzing_. Right hand — RIGHT.
-> END

=== lad_show_liquid ===
_Liquid_. Both hands — LEFT and RIGHT.
-> END

=== lad_show_gloving ===
_Gloving_. Left hand — LEFT.
-> END

=== lad_show_tektonik ===
_Tektonik_. Hop it — UP.
-> END

// Called out during the countdown into each style; one is picked at random.
=== lad_lead_hakken ===
Feet! _Hakken_!
Down, down — _hakken_!
Hit the floor. _Hakken_.
-> END

=== lad_lead_muzzing ===
Bro, can you even _muzz_?
Now... show me you can _muzz_.
Right hand — _muzzing_!
-> END

=== lad_lead_liquid ===
Smooth it out. _Liquid_.
Both hands — _liquid_!
Get _liquid_ on me.
-> END

=== lad_lead_gloving ===
Gloves up — _gloving_!
Left hand, light it up. _Gloving_.
Show me your _gloving_.
-> END

=== lad_lead_tektonik ===
_Tektonik_, go go go!
Up! Hop it — _tektonik_!
Old school. _Tektonik_.
-> END
