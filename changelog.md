# Changelog

## v1.0.0

First release.

- Reads the level off the running game: real object types, real hitboxes, no id guessing.
- Beam search over the measured 2.2 physics, on its own thread, with the level frozen
  while it thinks.
- Plays the route back through the real game to check it, and searches again knowing
  where the game actually killed it.
- Writes `.gdr.json`, `.mhr.json` and a plain JSON of every button change, at 240 steps
  per second.
