// Ending.ink — the demo's closing beat.
//
// Played by UEclipseDemoFlow over a full-screen black, with no speaker: the
// prompt opens via UEclipseDialogueSubsystem::OpenKnot, so there's no NPC and
// no portrait. Reaching `-> END` closes the dialogue, and that close is what
// raises the END SCREEN (REPLAY / QUIT) — see UEclipseEndArmer.
//
// The knot name here must match Project Settings -> Game -> Eclipse Demo ->
// "Ending Dialogue Knot" (default: get_into_club).

=== get_into_club ===
The door gives way behind you.
Inside, the bass is already in your chest before you've taken a step.

* [Step in.]
    Whatever the night is, it starts here.
    -> END

* [Look back at the street one last time.]
    The queue, the cold, the cigarettes traded for nothing. All of it behind you now.
    -> END
