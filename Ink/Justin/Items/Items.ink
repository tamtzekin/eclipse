// Items.ink — item-interaction dialogue. One knot per interactable item;
// AEclipseItemActor::DialogueId points at the matching knot name.
//
// "Take it" is tagged "# MENU: takeItem" — dispatched through the existing
// DispatchMenuAction switch (EclipseDialogueSubsystem.cpp), which calls
// ActiveItem->Pickup() then CloseDialogue(). Matches "startGame"'s existing
// close-and-commit pattern — the choice has no further Ink content after it.

=== red_wristband ===
A red wristband, still warm. Someone must have dropped it recently.

* [Take it.] # MENU: takeItem
    -> END
* [Leave it.]
    -> END
