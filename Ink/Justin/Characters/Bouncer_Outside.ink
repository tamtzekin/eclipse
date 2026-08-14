// Bouncer_Outside.ink — bouncer NPC outside the club (NPC_Figure_2).

=== bouncer_outside ===
He sees you before you're even there.
Bag. Show me what you carry.

    ** {have(thick_book)} && {rhythm >= 3} [Hide the contraband.] Go for it.
    -> bag_check
    ** {Inventory ? thick_book && rhythm < 3} [Hide the contraband.] Go for it. I've got nothing to hide.
    You wedge the contraband somewhere between all the stuff in your tiny bumbag. Bulging like a tumour coming off your waist. He's going to find it for sure. Damn it.
    He works through your bag, he sees how they all fit together. 
    -> bag_check
    
* Ticket.
  * * {Inventory ? ticket} Here.
    Go ahead.
    -> END
    
  * * {Inventory ?! ticket} I don't have one yet.
    Then leave. Respect the process. 
    -> END
* [Can you just let me in?]
  Your name?
  Bag. Show me.
  * * [Never mind.]
    -> END


== bag_check
{have(lighter)} -> lighter_check
{have(pack_cigarettes)} -> pack_cigarettes_check

= lighter_check
This is a lighter? No lighters.
* Then how am I supposed to light my cigarettes?
* Take it. I'll find another one. 

* You really need to let loose some time. Take a sabbatical, or something. Is your employer mistreating you?
I am perfectly calm and satisfied with my life, but thank you.
-> END

= pack_cigarettes_check
* Then what am I supposed to smoke?
There is a machine inside. You can buy it using local currency. The chaebol pays the venue partnership fees to ensure all tobacco transactions pass through their revenue model. This is how the club stays open. 
* Take it. I don't need them in my life anyway.
-> bag_check

= 

