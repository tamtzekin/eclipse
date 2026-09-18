// Dance proficiency per style, 1-3: how reliably this dancer lands each switch in a battle.
VAR dance_tester_hakken = 1
VAR dance_tester_muzzing = 2
VAR dance_tester_liquid = 1
VAR dance_tester_gloving = 1
VAR dance_tester_tektonik = 1

=== dance_tester ===
Test dancer. Floor's yours whenever.
+ [Dance battle.] # MENU: danceBattle
    -> END
+ [Teach me first.] # MENU: danceTutorial
    -> END
+ [Not now.]
    -> END

// Trash talk over the 4-bar intro before a battle; one line per bubble, no choices.
=== dance_tester_battle_intro ===
This kid doesn't know how to muzz.
Do you... kid?
-> END
