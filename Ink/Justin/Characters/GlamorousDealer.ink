// GlamorousDealer.ink — SKM_Quinn_Simple.
// drug/banned book dealer, very rich
// Strengths: AESTHETICS + ZEN

LIST GlamorousDealer_Patience = annoyed, bored, (neutral), friendly, flirty

=== glamorous_dealer ===
// Check how annoyed they are first, then send player to dialogue path
What is it?
{GlamorousDealer_Patience <= neutral: -> first_chat}

What is it?
{GlamorousDealer_Patience >= neutral: -> first_chat}


== first_chat
She won't take her eyes off her flip phones, both of them.
* [Do you know how I can get in?]
  Still doesn't look up, shrugs anyway.
  ~ GlamorousDealer_Patience--

  ** [...to that club. Over there.]
     She stares into one phone, watching the artificial salvation prayer video loop to infinity.
     *** [Uh, bye.] -> END

  ** [You seem busy. Forget about it.]
  -> END

* [Do you have a cigarette?]
-> ask_for_cigarette

* [Sorry. I thought you were someone else.]
  She isn't aware you exist.
  ~ GlamorousDealer_Patience--
  ** [Leave.]
-> END

== ask_for_cigarette
  Five euros.
  * [I'm not paying for a single cigarette.]
    Everything is an exchange, babe, this is how we maintain absolute balance.
    -> first_chat

  * {euros > 5} [(Wallet: €{euros}) Fine, take it.]
    'Cherish this one, babe. It might be your last drag in this lifetime.' She winks.
    You lost €5 (Wallet: €{euros})
    ~ euros = euros - 5
    You got a single, precious, Slim Cigarette.
    ~ Inventory += slim_cigarette
    -> first_chat

  * {aesthetics > 1} [I like the outfit.]
    She stares. 'Do you talk to all women like this?'
    ~ GlamorousDealer_Patience--
    Aesthetics Damaged: Level {aesthetics}
    ~ aesthetics = aesthetics - 1

  * {aesthetics > 5} [Looks like you know how to make money, the way you dress.]
    What, you looking for work, baby? I'm open to collaborating, if you are.
    ** [What kind of work?]
       Forget it. I need someone a bit more discreet.
    ** {zen > 3} [I'll take 5%. Just tell me who it needs to go to.]
       'Then you understand how this works, baby. Good.'
       Aesthetics Improved: {aesthetics}
        ~ aesthetics = aesthetics + 1
       ~ Inventory += thick_book

== rejected
Listen, you need to get out of my face. I'm busy tonight.
* [Leave.]

-> END
