// Florin.ink — bouncer NPC outside the club (NPC_Figure_2).

=== florin ===
He sees you before you're even there.
Bag. Show me what you carry.

    * {Inventory ? thick_book && rhythm > 3} [Hide the contraband.] Go for it.
    -> bag_check
    * {Inventory ? thick_book && rhythm < 3} [Hide the contraband.] Go for it. I've got nothing to hide.
    You wedge the contraband somewhere between all the stuff in your tiny bumbag. Bulging like a tumour coming off your waist. He's going to find it for sure. Damn it.
    
    * {Inventory !? thick_book} Go for it. 
    -> bag_check
    
    * [I'm not going in yet.] I need to ... I'll be right back.
    You are wasting all of our time.
        ~ Patience--
    
    - -> ticket_check
    

== bag_check ==
{He works through your bag, he sees how every item you own fits together, how it tells your history to him. He knows your weaknesses, your greatest fears. He knows what's in there already by the sound of each item rubbing against one another, he's waiting for you to admit to what you're carrying. | He waits for you to hand over the next item. | And the next.}

* {have(lighter)} Just an ordinary lighter. -> lighter_check
* {have(pack_cigarettes)} These smokes, but... -> pack_cigarettes_check
* {have(slim_cigarette)} But this is my last, slim cigarette. -> slim_cigarette_check
* {have(bottle_vodka)} Vodka. I'm not going to hide it, I admit. -> bottle_vodka_check
    
= lighter_check
This is a lighter? No unregulated lighter fluid allowed inside.
* Then how am I supposed to light my cigarettes?
Many more fires will make their way to you tonight.
-> bag_check 

* Take it. I'll find another one. 
  ~ lose(lighter)
  -> bag_check

* You really need to loosen up. Take a sabbatical, or something. Is your employer mistreating you?
    I am perfectly calm and satisfied with my life, but thank you.
    ~ Patience--
    ~ lose(lighter)
    -> bag_check

= pack_cigarettes_check
This is not an approved brand of cigarettes.

* Then what am I supposed to smoke?
There is a machine inside. You can buy it using local currency. The chaebol pays the venue partnership fees to ensure all tobacco transactions pass through their revenue model. This is how the club stays open. 
  ~ lose(pack_cigarettes)
-> bag_check 

* Take it. I don't need them in my life anyway.
  ~ lose(pack_cigarettes)
  -> bag_check

= slim_cigarette_check
We can't allow this type of cigarette into the venue.
  ~ lose(slim_cigarette)
    ** Fine, whatever. One cigarette doesn't make a difference.
    I don't know if I understand you. 
    -> bag_check
    
    ** {rhythm > 3} Why, because it's a girl's cigarette?
    Deconstructing your masculinity is your own journey to take, my friend. It will be a challenging, but rewarding path for you to embark on. I wish you the best, even though we'll probably never meet again.
    ~ rhythm = rhythm - 1
    -> bag_check

= bottle_vodka_check
Where is this from?
  ~ lose(bottle_vodka)
    * {psychedelics > 2} The glorious kingdom.
        You can consume more alcohol inside.
      -> ticket_check
        
    * {zen > 2} The unified empire.
      Then take it back. This is approved and endorsed.
      ~ get(bottle_vodka)
      -> ticket_check
    
    * {rhythm > 2} The bottle shop near my flat. 
    You're not funny, you know.
    ~ Patience--
    -> ticket_check

== ticket_check ==
// got_into_club is the demo's end trigger (UEclipseDemoSettings::
// EndTriggerQuest) — every branch that actually gets the player through the
// door has to set it, or the demo has no ending.
Now, ticket?
* {have(ticket)} Here.
    Go ahead.
    ~ lose(ticket)
    ~ new_quest(got_into_club)
    -> END
    
* {dont_have(ticket)} I don't have one.
    'Then purchase one.' He points to the right. 'Respect the process. The economy relies on you to survive.'  
    -> END
    
* Can you just let me in?
  Which List?
  ** {Connections ? dj_crisis} DJ Crisis.
  He steps aside without looking at you. 'Enjoy the release.'
  ~ new_quest(got_into_club)
  -> END
  ** [That list.] The...main list? What other list is there? How many?
  Obviously you're not meant to be here.
  ~ Patience--
  -> END

// ─── yaps ────────────────────────────────────────────────────────────────
// Overhead one-liners, shown above the character's head between
// conversations. One line per line; the runtime harvests the whole knot
// once with ContinueMaximally and picks from it at random, so there are no
// choices and no state here — just what this person mutters when you're
// not talking to them. Each should hint at what they want.

=== florin_yaps ===
Bag. I will need to see the bag.
No unregulated lighter fluid inside. It is not personal.
The economy relies on you to survive.
Every world has a system that must be obeyed.
There is a machine inside for tobacco. Use local currency.
-> DONE
