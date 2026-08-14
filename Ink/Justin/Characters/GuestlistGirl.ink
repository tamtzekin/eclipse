// GuestlistGirl — L_Outside placeholder NPC (Figure_3). Plain dummy content
// STRENGTHS: AESTHETICS, ENLIGHTENMENT
// WEAKNESSES: RHYTHM
// SUPER WEAKNESS: ZEN

plus a small StatEffect tag.

=== guestlist_girl ===
'Please, I need to get in.' She's not paying attention to you. 'You don't know what I've been through to get here tonight.'
* Is there a problem?
BOUNCER_OUTSIDE: Friend, you are not important. Tonight, on the list, there is no individual that goes by your name. You are one of many, and in this moment, you were not meant to be here.
    ** Is everything alright?
    'This fucking peasant is not letting me in.' She spits at his feet. 'We should've killed all of you off.'
    
     She makes a sign to him, a symbol that carries the weight of centuries, the spilling over of grief into a different kind of violence, more subtle, symbolic and slow.
        *** {rhythm > 3} [(Force)] What did you just say?
            ~ Patience--
            It's your imagination that's racist, not me. 
            -> DONE

    * {aesthetics > 3} You're trying to get in too?
    She looks you up and down, then in the eyes, the back to your body. 'They'll let you in.' Her hand brushes your ear, she's too close, you feel the hair of her cheek.
    
    Tell them you're on DJ Crisis' list. Tell them he approached you, of his own will. He wants to meet you.' 
        ~ Connections += dj_crisis
        **** [(Accept)] Thanks for the tip. I'll let you know if I get in.
        She winks. 'And don't forget who got you in.'
        -> DONE
        
        *** {rhythm > 2} [(Reason)] He can't help being a wage slave.
        -> DONE
        *** {zen > 3} [(Patience)] Let me help you. I'm trying to get in too.
            I don't have time for this.
            ~ Patience--
        -> DONE

* [Leave]
-> END

// == got_inside ==

// == found_dj_crisis ==
