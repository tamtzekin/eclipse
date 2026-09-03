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


// ─── lost_phone ──────────────────────────────────────────────────────────
// Worked example of an item you dig into rather than just pocket. The
// choices ARE the investigation: each one that lands unlocks the next, and
// the PIN only gives way once you've found the number somewhere on the
// case.
//
// No C++ backs any of this. Visit counts and VARs live in the story, which
// outlives any one dialogue, so walking away and pressing E again resumes
// exactly where you left off — that's what makes a hub knot like this work
// as a template for the next item.

VAR phone_pin_tries   = 0
VAR phone_saw_back    = false
VAR phone_saw_case    = false
VAR phone_saw_sticker = false
VAR phone_unlocked    = false

=== lost_phone ===
{ phone_unlocked:
    - The phone, open now. That wall of missed calls hasn't got any shorter.
    - else:
        { phone_pin_tries + phone_saw_back == 0:
            - Someone's phone, face down in the spill by the wall. The screen still has some life left in it.
            - else: The phone again. Still locked. Still not yours.
        }
}
-> phone_hub

= phone_hub
+ {not phone_unlocked} [Try a PIN.] -> phone_pin
+ [Turn it around.] -> phone_back
+ {phone_saw_back} [Feel the size of the case.] -> phone_case
+ {phone_saw_case} [Look at the stickers.] -> phone_stickers
+ [Take it.] # MENU: takeItem
    -> END
+ [Leave it.]
    -> END

= phone_pin
~ phone_pin_tries = phone_pin_tries + 1
{ phone_saw_sticker:
    - The date from the sticker. One, four, zero, three.
      ~ phone_unlocked = true
      The screen gives up without a fight. Missed calls, the whole column, all the same name.
      -> phone_hub
}
{ phone_pin_tries:
    - 1: Zero zero zero zero. Wrong. It tells you how many tries are left, which is generous of it.
    - 2: One two three four. Wrong, and slightly insulting to have tried.
    - 3: Your own PIN, out of habit. Wrong. Obviously.
    - else: You put in four numbers you don't believe in. It agrees with you.
}
-> phone_hub

= phone_back
{ phone_saw_back: You turn it over again, as though it will have changed. | You turn it over. }
~ phone_saw_back = true
The back of the case is a mess of glitter that's mostly gone, and a hairline crack running corner to corner. Something's been stuck on it. Several somethings.
-> phone_hub

= phone_case
~ phone_saw_case = true
You close your hand around it. The case is a size too big — the phone rattles inside it when you shake it. Whoever put this phone in this case wasn't the person who bought the case.
-> phone_hub

= phone_stickers
~ phone_saw_sticker = true
Three stickers, layered over each other in the order someone stopped caring. A club night from a promoter that folded. A cartoon dog. And underneath both, half-scraped, a strip of dymo tape with a date punched into it: 14.03.
Someone labelled their own phone with a date. People do that with the date they don't want to forget.
-> phone_hub
