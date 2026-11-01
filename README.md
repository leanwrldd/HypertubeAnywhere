# Hypertube Anywhere

Jump into a hypertube at any point along it, or hop out of the one you're riding, with one key.

## Features

- **Enter anywhere:** stand within 6 m of a hypertube and press **K**. You're pulled into the tube at the
  closest point and sent the way you're looking. Works on foot, while hovering, or while parachuting.
- **Exit anywhere:** press **K** while travelling to leave the tube where you are.
- **Rebindable:** the key is under *Options > Controls > Hypertube Anywhere*.
- **Entrances idle when nobody's around:** hypertube entrances normally draw full power all the time.
  With this mod they drop to the 0.1 MW idle draw unless a player is within 20 m, and spin back up as
  you approach.

## Multiplayer

Entering and exiting run on the server and use the same networking as the game's own entrances, so
every player needs the mod. **Multiplayer and dedicated servers have not been tested yet.** If
something misbehaves for clients, please open an issue.

## Known limitations

- Exiting drops you where you are; next to walls or terrain you may land on top of or beside the tube.
- Don't run this alongside *Hypertube Exit Anywhere*: both use K, and pressing it triggers both mods.

## Credits

Inspired by *Hypertube Exit Anywhere* by Andre Aquila. Written from scratch in C++; no code is shared.

Source: https://github.com/leanwrldd/HypertubeAnywhere
