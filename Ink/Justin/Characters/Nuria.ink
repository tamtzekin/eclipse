// Nuria.ink
// drug/banned book dealer, very rich
// Strengths: AESTHETICS + ZEN

=== nuria ===
// Check how annoyed they are first, then send player to dialogue path
{Patience >= neutral: -> first_chat}
// <= bored, not <: at exactly bored neither branch matched and the
// knot fell off its own end with nothing to say.
{Patience <= bored: -> annoyed_nuria}


== first_chat
She won't take her eyes off her flip phones, both of them.
* Do you know how I can get in?
  Still doesn't look up, shrugs anyway.
  ~ Patience--

  ** ...to that club. Over there.
     She stares into one phone, watching prayer videos on loop, DIOS TE BENDIGA DIOS HE SALVAJATE DIOS TE BENDIGA. 
     *** Is there a reason why you're not saying anything??
     She's texting a flood of people at once. She's too busy for you.
        -> DONE

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
  -> END

== ask_for_cigarette
  Five euros.
  * 'I'm not paying for a single cigarette[.'] that I could get by asking anyone else here, come on.
    'Then go and talk to them. I prefer to keep the market in perfect balance.'
    -> first_chat

  * {euros >= 5} (Wallet: €{euros}) Fine, take it.
    'Cherish this one, babe. It might be your last drag in this lifetime.' She sighs.
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

  * {aesthetics > 2} Looks like you know how to make money, the way you dress. You looking for work, baby? I'm open to collaborating, if you are.
    ** What kind of work?
       Forget it. I need someone a bit more discreet.
       -> DONE
    ** {zen > 2} I'll take 5%. Just tell me who it needs to go to.
       'Then you understand how this works, baby. Good.'
       Aesthetics Improved: {aesthetics}
        ~ aesthetics = aesthetics + 1
       ~ get(thick_book)
       -> DONE

== annoyed_nuria
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
