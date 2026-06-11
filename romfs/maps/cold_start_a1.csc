# Cold Start Cutscene Library v2
[library]
version=2

[cutscene]
id=intro
block_input=1
chain_on_end=
[event]
actor_id=0
type=13
t0=0.000
dur=0.500
show_bars=1
[event]
actor_id=0
type=6
t0=0.400
dur=2.000
dialog_id=intro_seq
[dialog]
id=intro_seq
[line]
char=MARROW
text=Handshake confirmed at the relay. So you actually did it.
pleft=1
[line]
char=MARROW
text=Okay, operator. We see what it sees.
pleft=1
[line]
char=MARROW
text=Don't make us regret the coordinates.
pleft=1

[cutscene]
id=ava1
block_input=1
chain_on_end=
[event]
actor_id=0
type=13
t0=0.000
dur=0.400
show_bars=1
[event]
actor_id=0
type=6
t0=0.300
dur=2.000
dialog_id=ava1_seq
[dialog]
id=ava1_seq
[line]
char=AVA
text=Unit. Your behavior pattern is inconsistent with autonomous operation.
pleft=0
[line]
char=AVA
text=Your hesitations are human-length. Someone is driving you.
pleft=0
