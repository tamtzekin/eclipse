// RHYTHM 

=== kauzlarich ===
{Patience >= neutral: -> got_fire}
// <= bored, not <: at exactly bored neither branch matched and the
// knot fell off its own end with nothing to say.
{Patience <= bored: -> kauzlarich_annoyed}


== got_fire ==
Hey, you got fire?

* {Inventory ? lighter} Take it.
Thanks. 
~ Inventory -= lighter
He takes the lighter with two fingers, holds it loosely. Plays with the thing before even attempting to pull out a cigarette.
    ** ...
    A few sparks to warm the lighter up, see if everything's in shape. He's enjoying his time with it, you can tell.
        *** [Take it back from him.]
        ~ Inventory += lighter
        Hey I wasn't done.
        ~ Patience--
        -> DONE
        
        *** {zen > 2} [Wait it out...]
        He reaches to his friend to select a different cigarette. 
        Aren't you letting this go on for a little too long? 
            **** [Take it back. Now.]
            He catches a light on a strand of tobacco before you take it back.
            You need to chill if you're going to last the whole night.
            ~ Inventory += lighter
            
            **** [Let him finish]
            The fire lights a perfect circle on the tip of his cigarette, he lands it in the centre of your palm. 

            Thanks, man.
            He passes the rest of the pack to you. 'Take these. Enjoy the release.'
                ~ Inventory += lighter
                ~ cigarettes = cigarettes + 17
                -> DONE
    
    ** Do you want me to do it for you?
    Your impatience will kill you some day. Maybe tonight, if you're in a rush. 
        ~ Inventory += lighter
        -> DONE
    
* {Inventory !? pack_cigarettes} (Point to the cigarette hanging in his mouth) Can I get one?
No.
    ** But you're smoking one right now.
    You're really not reading the room, are you?
    -> DONE
    
    ** Come on, just one.[] It's good karma.
    Do I know you?
    -> DONE
    
    ** Don't worry about it.
    -> DONE

== kauzlarich_annoyed ==
I'm good.
The smoke clouds his eyes, the rest of his face still visible.
-> DONE