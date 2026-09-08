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
    {Oh my god, how could this happen to me? | Where the FUCK is my phone? | This is the worst night ever. | What am I going to do?}
    
    * Are you good?
    No, I'm not 'good'. I've lost everything. Every single number and name and years of relationship-building and history and my money, oh my god, how much was in there?
    
        ** {rhythm > 1} You can't find your phone?
        I'm useless. I've lost everything.
        ~ new_quest(nuria_lost_her_phone)

            *** {zen > 1} It'll show up.
            And what if it doesn't? What if someone junkie finds it first? What if they take everything I have??
        
        ** {zen >= 1} It'll show up.
        Okay, and *when* will it show up?
            *** What does it look like?
            ...Like, a mobile phone? What am I supposed to tell you?
            ~ Patience--
        
            *** Where were you last when you used it?
            By the fence. It must be somewhere on the ground.
            -> DONE
            
        ** {rhythm >= 1} I can take a look for you. 
        I swear it was somewhere by the fence. But there's so much fence.
        ~ Patience++
        -> DONE 
                    
        ** Are you drunk?
        No. I'm PISSED OFF.
        ~ Patience--
        
      
* {have(phone)} Is this it?
  -> return_the_phone

  ** Have you tried ringing it?
     How the fuck am I supposed to ring it without a phone?
  ~ Patience--
     -> first_chat
  ** I'll keep an eye out.
     'Do that.' Back to the screen.
     -> first_chat

* {SideQuests ? nuria_lost_her_phone} Have you found your phone yet?
  It's here, on the ground. I can hear it.
  -> first_chat

* Do you have a cigarette?
-> ask_for_cigarette

* Do you know how I can get into that club?
My business is outside.

* Sorry. I thought you were someone else.
  Right.
  ~ Patience--
  -> END

== ask_for_cigarette
  i'll give you one if you help me find my phone.
  * What's so important about this phone anyway?
  I'm waiting on a call. Many calls, I guess.
    ** I'll give you five euros.
    This is not important right now.
    -> first_chat

//   * {euros >= 5} (Wallet: €{euros}) Fine, take it.
//     'Cherish this one, babe. It might be your last drag in this lifetime.' She sighs.
//     You lost €5 (Wallet: €{euros})
//     ~ euros = euros - 5
//     You got a single, precious, Slim Cigarette.
//     ~ get(slim_cigarette)
//     -> first_chat

  * {aesthetics > 1} I like your outfit.
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

== return_the_phone
A hundred missed calls.

* So you're a dealer.[]Am I going to get in trouble for this? Like, aiding a criminal?
    ** Don't be a pussy. 

  -> phone_returned

= phone_returned
~ lose(phone)
~ SideQuests -= nuria_lost_her_phone
I guess you mainlanders expect payment for your labour. This is what I can give you. 
~ cigarettes = cigarettes + 2
Don't make it a thing.
    * {zen < 2} Is that it...?
    * {aesthetics >= 1} Nice. {SideQuests ? alina_needs_a_cigarette: I was looking for some.}
    -> DONE

== annoyed_nuria
{not have(phone): I can't talk right now.} 
{have(phone): I've got a call to take. Things are moving tonight. Nice knowing you.}
-> END


// ─── yaps ────────────────────────────────────────────────────────────────
// Overhead one-liners, shown above the character's head between
// conversations. One line per line; the runtime harvests the whole knot
// once with ContinueMaximally and picks from it at random, so there are no
// choices and no state here — just what this person mutters when you're
// not talking to them. Each should hint at what they want.

=== nuria_yaps ===
// pre completing phone quest
Where is it...where is it...
Where the hell did it go?
Oh my god, I can hear it ringing from here.
Where is that little shit?

// post finding phone
Sixty a gram. That's what it costs.
We obey the market. Respect the market.
-> DONE
