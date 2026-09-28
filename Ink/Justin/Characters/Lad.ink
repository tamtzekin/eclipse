// Dance proficiency per style, 1-3: how reliably this dancer lands each switch in a battle.
VAR lad_hakken = 1
VAR lad_muzzing = 2
VAR lad_liquid = 1
VAR lad_jumpstyle = 1
VAR lad_shuffle = 1
VAR lad_tektonik = 1

=== lad ===
// A finished battle lands here: win and he warms up, lose and he's done with you.
{ dance_won == 1:
    ~ Patience++
    ~ dance_won = -1
    Alright. You can move.
}
{ dance_won == 0:
    ~ Patience = annoyed
    ~ dance_won = -1
    Get out of here.
}
What do you want?
+ [DANCE.] # MENU: danceBattle:ClubMusic
    -> END

* I need to get in tonight.
Nice.
    ** I mean, can you help me?
    Why should we?
    LAD 2: Who's this guy? 
    LAD: This is mine.
    LAD 3: Yeah, we can help you. If you give me a kiss.
    LAD: Hey bro, shut the fuck up.
    LAD 2: Show us how you dance. 
    LAD 3: Yeah. Do you even muzz?
        *** [Dance battle] Let's do this. # MENU: danceBattle:ClubMusic
        -> END
        *** Maybe later.
        Whatever. 
        -> DONE
        
    -> END

// His moveset, one pattern per line, played in order. L light (hands), H heavy (legs).
// Space = crotchet, - = quaver, = semiquaver, * = 1st and 3rd of a triplet.
// Two moves only: A then B in the lesson, mixed for the rest of the song.
=== lad_moveset ===
L L H
L H L L
-> END

// Called when you answer with the wrong limb: _heavy means you stamped when it was a hand move.
=== lad_wrong_heavy ===
Sloppy.
Use your arms.
-> END

=== lad_wrong_light ===
It's in your feet.
-> END

// After you beat him: he takes it, and points you at the back door.
=== lad_defeated ===
Alright. Alright. You can move.
Look — round the back. There's a guy there.
Tell him you danced me.
+ [Round the back.]
    -> END
+ [Thanks.]
    -> END

// Same again, once he's already lost to you: no lesson this time.
=== lad_battle_intro_again ===
Huh, you want to go again?
Come on then.
-> END

// Trash talk over the 4-bar intro before a battle; lines spread across the intro, no choices.
=== lad_battle_intro ===
This kid doesn't know how to muzz.
Do you... kid?
-> END

// The strafe lesson, first time only: a line every two bars while he moves and you keep facing him.
=== lad_battle_strafe ===
Keep up.
Follow me.
This track is fire.
Techno is a feeling, not like your house music.
Techno is a journey.
-> END

// Once you've got the moves and the footwork, the bar before it all counts.
=== lad_battle_both ===
Now both. Stay on me and switch.
-> END

// Said as he starts showing each style, four bars apiece.
=== lad_battle_demo ===
Follow my style.
-> END

// While he shows a style (the tutorial repeats it every four bars until you match him). A _word_ is lit up in the style's colour.
=== lad_show_hakken ===
This is _hakken_. Stamp it — LEFT, DOWN and RIGHT.
-> END

=== lad_show_muzzing ===
Show me you can _muzz_. UP and RIGHT.
-> END

=== lad_show_liquid ===
_Liquid_. Both hands — LEFT and RIGHT.
-> END

=== lad_show_jumpstyle ===
_Jumpstyle_. Off both feet — LEFT and DOWN.
-> END

=== lad_show_shuffle ===
_Shuffle_. Quick feet — DOWN and RIGHT.
-> END

=== lad_show_tektonik ===
_Tektonik_. Hop it — UP and DOWN.
-> END

// Called out during the countdown into each style; one is picked at random.
=== lad_lead_hakken ===
Push your energy down, into the ground.
Feet. _Hakken_.
-> END

=== lad_lead_muzzing ===
Bro, can you even _muzz_?
Watch me muzz. 
More arms, bro.
-> END

=== lad_lead_liquid ===
Smooth.
Techno is a feeling. 
-> END

=== lad_lead_jumpstyle ===
Off the floor, bro.
_Jump_ it.
-> END

=== lad_lead_shuffle ===
Quick feet.
_Shuffle_.
-> END

=== lad_lead_tektonik ===
One, two, three, four.
Harder. Faster. 
Watch me, bro. 
-> END
