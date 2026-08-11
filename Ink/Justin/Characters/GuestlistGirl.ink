// GuestlistGirl — L_Outside placeholder NPC (Figure_3). Plain dummy content
// STRENGTHS: AESTHETICS, ENLIGHTENMENT
// WEAKNESSES: RHYTHM
// SUPER WEAKNESS: ZEN

plus a small StatEffect tag.

=== guestlist_girl ===
Please, I need to get in.
He's not paying attention to you.
You don't know what I've been through to get here tonight.
* [What's going on?]
BOUNCER_OUTSIDE: friend. You are not important. Tonight, there is no individual that goes by your name. You are one of many, and in this moment, you were not meant to be here.
    ** Is everything alright?
    This fucking brute is not letting me in.
    She spits at his feet. 
    Peasant. We should've killed you off with the rest. 
    *** [CONTINUE]
    She makes a sign to him, a symbol that carries the weight of centuries, the spilling over of grief into a different kind of violence, more subtle, symbolic and slow.

// Character 1
- * {aesthetics > 3} [Act cool] I don't get why they wouldn't let you in. 
You say it to make her feel good. You know someone like this is easily flattered.
  {CurrentPlayer ? character_one} She looks you up and down, then in the eyes, the back to your body. 
  They'll let you in. All this guy cares about is how hot you are. He doesn't let you in if you're ugly as shit. But I'm hot, aren't I? Everyone is looking at me. I know I am the centre of this place.
  
  ** {aesthetics > 2} [Flatter] Sure, you're hot. Now how am I supposed to get in?
  Her hand brushes your ear, she's close, you feel the hair of her cheek.
    *** [CONTINUE]
    Tell them you're on DJ Crisis' list. Tell them he reached out. He approached you of his own will. He wants to meet you. And they'll let you in, because you're brown. You're one of the church's sacred chosen elite. 
    ~ Connections += dj_crisis
        Come back when you've got to DJ Crisis. Don't forget about me.
        **** [CONTINUE]


* {psychedelics > 1}  [Psychedelics]
* {rhythm > 2} [Reason] He's just doing a job. He can't help being a wage slave.
* {zen > 3} [Patience] Let me help you. I'm trying to get in too.
    I don't have time for this.
    ~ Patience--


    *** [Accept] Thanks for the tip. I'll let you know if I get in. 
    Don't forget about me.
  


* {rhythm > 3} [Force] What did you just say?
  ~ Patience--
  You heard what you want to hear. Your imagination is racist, not me. 



* [Leave]
-> END

// == got_inside ==

// == found_dj_crisis ==
