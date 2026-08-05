// StallVoiceErratic.ink — Stall Voice 2. Audio-only, erratic.
// Demonstrates a HiddenStatGate ("ANNOYANCE >= 4") — evaluated straight
// from UEclipseGameStateSubsystem's hidden Annoyance stat (never synced
// into an Ink variable), silently dropping the choice with no hint.

=== stall_voice_erratic ===
(muffled, fast) No no no it's fine, it's FINE, just — just gimme a sec —

* [Hey. Breathe. It's okay.]
    (a shaky exhale) ...okay. Okay. Sorry.
    -> END
* [Can you just hurry up in there?] # ANNOYANCE >= 4
    (silence, then a slammed stall wall)
    -> END
* [Take your time.]
    -> END
