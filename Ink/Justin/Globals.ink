// Globals.ink — shared Ink variables.

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

// Stats
VAR aesthetics = 10
VAR rhythm = 10
VAR zen = 10
VAR psychedelics = 10

// Money system
VAR euros = 15

// Inventory system
LIST Inventory = none, phone, (lighter), slim_cigarette, pack_cigarettes, bottle_empty, bottle_beer, (bottle_vodka), (cup_empty), cup_beer, thick_book, ticket
=== function get(x)
    ~ Inventory += x

=== function lose(x)
    ~ Inventory -= x 
    
=== function have(x)
    ~ return Inventory ? x

=== function dont_have(x)
    ~ return Inventory !? x
    
// Connections system (when you 'know someone')
LIST Connections = dj_crisis

// Character switch tracking
LIST CurrentPlayer = character_one, character_two, character_three

// Patience of NPC you're talking to
LIST Patience = annoyed, bored, (neutral), friendly, flirty
