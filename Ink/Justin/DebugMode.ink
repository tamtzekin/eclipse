// DebugMode.ink — dev harness for testing individual characters and items
// in isolation, without going through UE / EclipseDialogueSubsystem.
//
// Holds the single canonical include list (every character + item file) and
// the debug menu on top of it. Main.ink includes THIS file and diverts
// straight to debug_menu, so opening either file in Inky and hitting Play
// gets you the menu. This file can also be run standalone with the bundled
// inklecate binary, from Ink/Justin/:
//   ../../Plugins/Inkpot/ThirdParty/InkCommandLine/mac/inklecate -p DebugMode.ink
//
// In Inky, Play always runs whichever file is set as the *main* ink file,
// not just the focused tab — right-click a tab and choose "Set as Main Ink
// File" if Play isn't landing on debug_menu.
//
// The debug_menu / angel_seeker_setup knots below ship inside the compiled
// DA_MainStory asset as harmless unreachable content — EclipseDialogueSubsystem
// always ChoosePaths straight to a specific NPC/item knot and never Continues
// from the story's root, so none of this is ever seen in-game.
//
// Note: reaching "-> END" inside any character's own knot ends the whole
// Ink run (END is absolute in Ink, not scoped to how you got there) — so
// after a conversation finishes, re-run inklecate to pick another character
// rather than expecting to land back on this menu.

INCLUDE Globals.ink

INCLUDE Characters/AngelSeeker.ink
INCLUDE Characters/Chainsmoker.ink
INCLUDE Characters/StallVoiceCalm.ink
INCLUDE Characters/StallVoiceErratic.ink
INCLUDE Characters/EnlightenedRaver.ink
INCLUDE Characters/Daesung.ink
INCLUDE Characters/GlamorousDealer.ink
INCLUDE Characters/Alina.ink
INCLUDE Characters/Tomas.ink
INCLUDE Characters/Bouncer_Outside.ink
INCLUDE Characters/GuestlistGirl.ink
INCLUDE Characters/Figure4.ink
INCLUDE Characters/Figure5.ink

INCLUDE Items/Items.ink

-> debug_menu

=== debug_menu ===
Pick a character to start their dialogue.

// OUTSIDE THE CLUB
+ [Alina] -> quinn_2
+ [Tomas] -> tomas

+ [Glamorous Dealer] -> glamorous_dealer
+ [Chainsmoker] -> chainsmoker
+ [Guestlist Girl] -> guestlist_girl
+ [Bouncer Outside (bouncer_outside)] -> bouncer_outside

// BATHROOM
+ [Angel Seeker (angel_seeker)] -> angel_seeker_setup
+ [Stall Voice, calm (stall_voice_calm)] -> stall_voice_calm
+ [Stall Voice, erratic (stall_voice_erratic)] -> stall_voice_erratic
+ [Enlightened Raver (enlightened_raver)] -> enlightened_raver
+ [Daesung (daesung)] -> daesung
+ [Figure 4 (figure_4)] -> figure_4
+ [Figure 5 (figure_5)] -> figure_5
+ [Red Wristband (red_wristband)] -> red_wristband

+ [quit] -> END

= angel_seeker_setup
Angel Seeker branches on quest state — pick a starting point before diving in (default state is met_npc=false, has_hair=false, has_eye=false, quest_stage="intro").

+ [Fresh, never met]
    ~ met_npc = false
    ~ has_hair = false
    ~ has_eye = false
    ~ quest_stage = "intro"
    -> angel_seeker
+ [Met her, still looking]
    ~ met_npc = true
    ~ has_hair = false
    ~ has_eye = false
    -> angel_seeker
+ [Have hair only]
    ~ met_npc = true
    ~ has_hair = true
    ~ has_eye = false
    -> angel_seeker
+ [Have eye only]
    ~ met_npc = true
    ~ has_hair = false
    ~ has_eye = true
    -> angel_seeker
+ [Have both, ready for the stall]
    ~ met_npc = true
    ~ has_hair = true
    ~ has_eye = true
    ~ quest_stage = "ready"
    -> angel_seeker
