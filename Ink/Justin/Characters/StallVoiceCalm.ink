// StallVoiceCalm.ink — shared by Stall Voice 1 & Stall Voice 4 (same
// DialogueId on both placed actors, matching the pre-Ink setup). Audio-only,
// bIgnoreLineOfSight — player never sees this NPC.
// Demonstrates a MeterCompareGate ("STIMULATION < 3") combined with a
// MeterEffect ("+1 THIRST") tagged on the same choice.

=== stall_voice_calm ===
Someone's in here. Just — give me a minute, okay?

* You alright in there?
    Yeah. Just needed to sit down for a second.
    -> stall_voice_calm
* You look wiped out. Want some water? # STIMULATION < 3 # +1 THIRST
    Actually... yeah. Thanks.
    -> END
* I'll leave you to it.
    -> END
