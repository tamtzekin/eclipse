// Quinn3.ink — L_Outside placeholder NPC (SKM_Quinn_Simple3).
// Demonstrates a HiddenStatEffect ("+1 ANNOYANCE").

=== quinn_3 ===
She won't take her eyes off her flip phones.

* [Do you know how I can get in?] # +1 ANNOYANCE
    Still doesn't look up, shrugs anyway.
    
* [Do you have a cigarette?]

* {aesthetics > 3} [I like the outfit.]
She stares. 'Do you talk to all women like that?'


-> first_knot

= first_knot
I don't know. This is not my club.
* [Option 1]
* [Option 2]
-> second_knot

= second_knot
* [Option 1]
* [Option 2]
-> END