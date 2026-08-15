// GuestlistGirl — L_Outside placeholder NPC (Figure_3). Plain dummy content
// STRENGTHS: AESTHETICS, ENLIGHTENMENT
// WEAKNESSES: RHYTHM
// SUPER WEAKNESS: ZEN

=== guestlist_girl ===
'Please, I need to get in.' She's not paying attention to you. 'You don't know what I've been through to get here tonight.'
* Is there a problem?
BOUNCER_OUTSIDE: Friend, you are not important. Tonight, on the list, there's no soul that goes by your name. You're one of many, and in this moment, you weren't meant to be here.
-> shes_on_the_list

* {aesthetics > 3} You're trying to get in too?
    -> trying_to_get_in

== shes_on_the_list
    ** [Ask the girl what's wrong] Is he bothering you?
    'This peasant is not letting me past. I am on the list. Do you understand what that means?' She screws her nose. 'I thought we killed off the rats.'
    
     She makes a sign at him. Some archaic finger-shape like a butterfly, but its meaning was violent.
        *** What list?
        I am one of DJ Crisis' group. He takes us everywhere with him. To the empire and every little shithole country on its flight path.
        ~ Connections += dj_crisis
        -> guestlist_girl
        
        *** {rhythm > 3} What did you say?
            ~ Patience--
            'It's your imagination that's racist, not me.' She pouts, pokes out her tongue. 
            -> DONE
            
        *** {rhythm > 2} He can't help being a wage slave.
        BOUNCER_OUTSIDE: 'Every world has its system that must be obeyed, or the world frays and we collapse with it.'
        GUESTLIST_GIRL: 'I don't have time for philosophy!! My mother's in the Party and she'll ruin your life and your family and your dumb girlfriend too. My mum will bleed you dry.'
        BOUNCER_OUTSIDE: 'I don't subscribe to the Party.'
            -> guestlist_girl
        
        *** {zen > 3} Let me help you. I'm trying to get in too.
            'I'm going to miss his set. Oh my god, if I miss this set I'm going to ... I don't know what I'll do, listen, babe, can you just talk to the peasant in front of me here? Tell him I'm with DJ Crisis.'
            
            **** 'That's a strange name for a DJ.'
            'The fuck do you know?'
            ~ Patience--
            -> DONE
            
            **** 'I'll try my best.'
            Just hurry up.
            ~ Patience++
            -> DONE

    ** Could you just let us in?
        BOUNCER_OUTSIDE: 'This is not about you, or her. It's about the List.'
        
        *** {zen > 4} I'd like to attune myself to the inner workings of the List.
        'The List is the law of our reality. The List decides the collisions of forces that exist in this moment. We are here only because the List allows us to share this space.'
        
            **** And if I were to ... get on the List?
            Then you would enter.
            
            **** {zen > 5} Is there a specific List I should be on?
            The Librarian's list.
            -> DONE 
            
        *** Guess I'll find another way in. (Leave)
        -> DONE

== trying_to_get_in
    She looks you up and down, then in the eyes, the back to your body. 'They'll let you in.' Her hand brushes your ear, she's too close, you feel the hair of her cheek.
    
    Tell them you're on DJ Crisis' list. Tell them you're one of his boys.' 
        ~ Connections += dj_crisis
        ** Got it[.], I'll come find you when I'm in.
        She winks. 'And don't forget who got you there.'
        -> DONE
        

* [I'm going to find another way...(Leave)]
-> END

// == got_inside ==

// == found_dj_crisis ==
