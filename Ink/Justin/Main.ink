// Main.ink — compile root for /Game/Justin/Dialogue/DA_MainStory.
//
// Every character/item gets a top-level knot (e.g. "=== angel_seeker ===")
// in one of the included files below. EclipseDialogueSubsystem never
// Continues from here directly — it always jumps straight to a knot via
// ChoosePath(NPC->DialogueId / Item->DialogueId), so this file's own body
// is just the INCLUDE list plus a terminator.

INCLUDE Globals.ink

INCLUDE Characters/AngelSeeker.ink
INCLUDE Characters/StallVoiceCalm.ink
INCLUDE Characters/StallVoiceErratic.ink
INCLUDE Characters/EnlightenedRaver.ink
INCLUDE Characters/Daesung.ink
INCLUDE Characters/Quinn1.ink
INCLUDE Characters/Quinn2.ink
INCLUDE Characters/Quinn3.ink
INCLUDE Characters/Figure2.ink
INCLUDE Characters/Figure3.ink
INCLUDE Characters/Figure4.ink
INCLUDE Characters/Figure5.ink
INCLUDE Characters/Figure6.ink

INCLUDE Items/Items.ink

-> DONE
