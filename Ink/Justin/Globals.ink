// Globals.ink — shared Ink variables.
//
// Synced one-directionally from UEclipseGameStateSubsystem into the story
// right before each ChoosePath (see EclipseDialogueSubsystem::OpenDialogue).
// Ink content reads these to branch; it never writes them back — all actual
// state changes still flow through the existing tag → ApplyStageEffect path
// (see the header comment in EclipseDialogueSubsystem.h for the directive
// grammar: "[STAT: N]" gates, "+N STAT" / "+N METER" effects, etc., now
// authored as Ink tags instead of Articy stage-direction fields).

VAR has_hair = false
VAR has_eye = false
VAR quest_stage = "intro"
VAR met_npc = false
