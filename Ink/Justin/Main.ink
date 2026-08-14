// Main.ink — compile root for /Game/Justin/Dialogue/DA_MainStory.
//
// Every character/item gets a top-level knot (e.g. "=== angel_seeker ===")
// via the include list in DebugMode.ink. EclipseDialogueSubsystem never
// Continues from here directly — it always jumps straight to a knot via
// ChoosePath(NPC->DialogueId / Item->DialogueId), so this file's own root
// content is never seen in-game (the compiled DA_MainStory asset just
// carries the debug menu as harmless unreachable content).
//
// Diverting into DebugMode.ink here is purely for local Inky testing: Main
// is the natural "root" file to have open, so Play now drops you straight
// into the debug menu instead of an empty "End of story".

INCLUDE DebugMode.ink


-> debug_menu
