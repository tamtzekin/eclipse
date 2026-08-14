// GlamorousDealer.ink — SKM_Quinn_Simple.
// drug/banned book dealer, very rich
// Strengths: AESTHETICS + ZEN

=== glamorous_dealer ===
// Check how annoyed they are first, then send player to dialogue path
{Patience >= neutral: -> first_chat}
{Patience < neutral: -> second_chat}


== first_chat
She won't take her eyes off her flip phones, both of them.
* Do you know how I can get in?
  Still doesn't look up, shrugs anyway.
  ~ Patience--

  ** ...to that club. Over there.
     She stares into one phone, watching the artificial salvation prayer video loop to infinity.
     *** My friend is missing.
     You notice her eyes look at you for a second, but she's texting someone. 'Does your friend have dark hair?
        **** That's her.
        Then this is the girl who I'm waiting for tonight. It's 9 o clock. She's supposed to be here. So if you can tell me where she is I'd like to know soon.
        ~ Patience++
        -> DONE

        **** [CONTINUE]
        -> dark_hair

        **** No. She was blonde.
        –Then we have the wrong person in mind.
        Smiles at you, but not like she cares.
        -> DONE
     *** Uh, bye.
     She rolls her eyes.
     -> DONE

  ** You seem busy. Forget about it.
  She nods.
    -> DONE

* Do you have a cigarette?
-> ask_for_cigarette

* Sorry. I thought you were someone else.
  She isn't aware you exist.
  ~ Patience--
  ** [Leave.]
-> END

== ask_for_cigarette
  Five euros.
  * I'm not paying for a one cigarette.
    Everything is an exchange, babe, this is how we maintain absolute balance.
    -> first_chat

  * {euros > 5} (Wallet: €{euros}) Fine, take it.
    'Cherish this one, babe. It might be your last drag in this lifetime.' She winks.
    You lost €5 (Wallet: €{euros})
    ~ euros = euros - 5
    You got a single, precious, Slim Cigarette.
    ~ get(slim_cigarette)
    -> first_chat

  * {aesthetics > 1} I like the outfit.
    She stares. 'Do you talk to all women like this?'
    ~ Patience--
    Aesthetics Damaged: Level {aesthetics}
    ~ aesthetics = aesthetics - 1
    -> DONE

  * {aesthetics > 5} Looks like you know how to make money, the way you dress.
    What, you looking for work, baby? I'm open to collaborating, if you are.
    ** What kind of work?
       Forget it. I need someone a bit more discreet.
       -> DONE
    ** {zen > 3} I'll take 5%. Just tell me who it needs to go to.
       'Then you understand how this works, baby. Good.'
       Aesthetics Improved: {aesthetics}
        ~ aesthetics = aesthetics + 1
       ~ get(thick_book)
       -> DONE

== second_chat
We're done talking.
-> END

== dark_hair
* I'm here to pay her debt.
-> END
* I'm not responsible for what she's done.
-> END
* Sorry, you've got the wrong person.
-> END

== rejected
Listen, you need to get out of my face. I'm busy tonight.
* [Leave.]

-> END
