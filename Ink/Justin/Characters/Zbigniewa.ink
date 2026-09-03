//

=== zbigniewa ===
{Patience >= neutral: -> ask_zbigniewa_for_ticket}
// <= bored, not < bored: annoyed and bored are both "she's done with you",
// and with the strict form a Patience of exactly bored matched neither
// branch, so the knot fell off its own end with nothing to say.
{Patience <= bored: -> zbigniewa_annoyed}


== ask_zbigniewa_for_ticket ==
{She's not looking at you. Whatever you do, whoever you are, she doesn't actually care. Her cigarette a tower of ash. She stares into it like there are a billion universes in each speck of burnt chemical. | She eyes you. | She's waiting for you to say something. Anything. | She doesn't look at you. Looking at her phone.}

* {florin} Ok I talked to the bouncer.
'Oh really? And what extreme religion did he put you onto? Did he make you feel we are all small, insigificant nothings with no agency in this ball of fire?'
    ** {florin && psychedelics >= 2} Yes. Everything you just said.
    She's surprised you bothered trying. That's enough for her. 'If you need another one let me know.'
    ~ Inventory += ticket
    
    ** He says you have to let me in. 
    'He's a friend. Not my boss.' She stares at the figures in the field, smoking, gossiping, imagining she was inside dancing, not sitting in the cold, talking to idiots.
    -> DONE
    
    ** He told me that I'm on the list.
    'He doesn't have the list.' Her face is ice. 'I do.'
    -> DONE
* {rhythm >= 1} You don't look like you want to be here.
    A twitch in her face, some sign of life. 'I'm trying to work.'
    ** {rhythm > 2} Have you ever thought of getting a different job?
    'Do I have a choice?'
    
        *** {nuria} Have you thought of dealing?
        'Under the empire's laws dealing is illegal and a reportable offence. According to the Statutes I would have to report you right now to the higher police.' She tilts her head. 'Their car is over there.'
        -> DONE
        
        *** You could work behind the bar.
        'Have *you* ever worked behind a bar?'
        -> ask_zbigniewa_for_ticket
    
* One ticket, please.
    -> buy_ticket

* I need to check with my friend[. (Leave)], give me a second.
Rolls her eyes. 'Take as long as you want. Maybe don't come back.'
    -> DONE

== buy_ticket==
'Twenty.'
* {euros < 20} I don't have that kind of cash.
Get it, then.
    ** I can't do anything else for you to let me in?
    Are you on the list?
        *** {Connections ? dj_crisis} DJ Crisis invited me. 
        -> dj_crisis_list
        
        *** {rhythm > 3} Yeah, the DJ's list.
        'You'll have to be a tiny bit more specific.'
            **** I'm sorry, I don't know what I was doing. I'm lying to you.
            'We're done here.' The ash on her cigarette a perfect grey and black form, still, unmoved by the breeze.
            ~ Patience--
            -> DONE
            
            **** Listen, if you could let me in this once[...] I will get you back. I promise.
            Come back when you have something to offer to me.
            -> DONE
            
        *** I don't know, what lists are there?
        You don't look like you know.
        -> DONE
            
    ** {euros <= 10} What if I gave you half now, half later?
        And how will I know you'll come back?
        *** {aesthetics > 5} I look trustable, don't I?
        'No.'
            **** Please, I'll do anything.
            No means no.
            -> DONE
            
            **** Forget it, sorry for bothering you.
            -> DONE

    ** Alright, I'll see if I can find another way in.
    Have fun out there, soldier. The world is yours to conquer.
    -> DONE
    
* {euros >= 20} Here's 20.
She slips a paper ticket over to you. Stained with cigarette ash. 
    ~ euros = euros - 20

* {cigarettes >= 20} Would you accept twenty cigarettes?
'Maybe.' She runs the numbers in her head. 'Twenty-five and I'll put you on the list.'
    ** {cigarettes >= 25} I got you.
    'There we go. Good boy.' Slides it over and starts smoking one immediately.
    ~ Inventory += ticket
    -> DONE
    
* {pack_cigarettes} How about a pack?
She takes a look at the art. Squints to read the lettering, some phased-out, illegal language. 'I only smoke singles.'
    ** How much to get in, then?
-> buy_ticket

* {euros >= 20} Take a tip.[]You need one.
~ euros = euros - 20
She slides it back. 'I'm not a hooker.' 
    ~ euros = euros + 20


== dj_crisis_list ==
Her eyes scan a piece of paper in front of her, out of your sight. The secret list. 
'And who put you on this list?'
    * [DJ Crisis, obviously] Uhh, the ... DJ?
    She smiles for a second, or maybe you imagined it, maybe it's an illusion, because she looks mad.
    'Don't waste my time again.'
    ~ Patience--
    -> DONE
            
    * {rhythm > 2} His manager.
    DJ Crisis doesn't have a manager.
    ~ Patience--
    -> DONE
            
    * {rhythm > 3} His friend.
    She puckers her lips to the entrance. 'What, the girl over there?' 
        ** Yes.
        'We don't let her in anymore.'
        -> DONE
        
        ** No, an old friend.
        'If you are going to keep bullshitting me I'll get my colleague over there to remove you.'
        ~ Patience--
        -> DONE

== zbigniewa_annoyed==
'20, I said. Twenty. Get it, or get out of my face.' Her finger runs along the edge of the counter.

    * {euros >= 20} Hand it over quietly.
    She gives it to you. Finally. 'Tonight will be a long night for you.'
    ~ euros = euros - 20
    -> DONE
    
    * {euros < 20} I still don't have it.
    'Are they all this dumb?' Like she's mumbling to the ghost behind her.
    -> DONE
