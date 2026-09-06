# Macro Maker

Works out a way through the level, checks it by playing it, and writes the answer out as
a macro your bot can replay.

## How it works

**It reads the level off the running game.** Every other tool that reads a level from
outside has to guess what an object id is and how big its hitbox really is, and guessing
wrong is the largest error source in all of them. Nothing is guessed here: the type is
the one the game itself assigned when it built the level, and the box is the rect the
game collides against, with that placement's rotation and scale already in it.

**It searches in measured physics.** The simulator underneath is built only from numbers
recorded out of 2.2081 as it ran -- 441,488 physics steps and 2,017 orb and pad
activations -- including the things every other simulator has wrong: the robot's jump
does not grow with how long you hold it, a gravity portal halves the speed you arrive
with, a mini wave moves twice its velocity, and mini is not one scale factor.

**The search is a beam, not a dice roll.** It branches only where the button actually
decides something, throws away states that are interchangeable with a better one, and
when the whole beam dies it gives a click back and makes the line that failed expensive
to be on. Random-input pathfinders cannot land a two-frame window; this can.

**Then the real game gets a vote.** The route is played back through the game itself at
240 steps a second. If the game kills it, that spot is marked as expensive and the level
is searched again knowing what really happened there. The simulator is good enough to
find routes and not good enough to be trusted, and this is how the two are reconciled.

## Using it

Open a level, pause, and press **Macro** -- or the hotkey, **G** by default. The level
freezes while it thinks, plays the route back to check it, and writes the macro to the
mod's folder. **H** plays the finished macro, **J** stops.

Macros come out as `.gdr.json` (what most Geode bots read, xdBot among them) and
`.mhr.json` (Mega Hack Replay), at 240 frames per second, in
`geode/mods/andre.macro-maker/macros`. Point *Also Copy Macros To* at your bot's own
folder and a second copy lands there.

## What it cannot do

- **Triggers.** Objects a move, rotate, scale, toggle or follow trigger picks up are
  dropped, because nothing static can say where they will be. Levels built out of moving
  parts will not solve.
- **Dual.** One player only.
- **Slopes** are ridden, but as flat-faced triangles rather than the real thing.

Turn on *Extra Logging* and it writes `last-capture.csv`: every object the simulator was
given, which is the only way to find out whether it read the level correctly rather than
guessing.
