# Pico2Atari8 backlog

Ideas parked for later, newest first.

## Joy-Con paddle mode: make it discoverable (parked 2026-11-10) - DONE in 2.1

Done by Game Settings (src/a8_gamecfg.cpp): the Thumbstick row (Joystick /
Paddle) and the PADDLE MODE toast at game start. Still open: point 1, the
Quad manual (GameAdapterRGBPico) - done together with the Game Settings
section there.

The Joy-Con already switches between joystick 1 and paddle 1 at any time in an
800 game: **Yellow + thumbstick click** (src/a8_input.cpp, commit 9562383). The
choice is remembered per game (a8_settings.cpp, the "JOYP" game-hash list). But
users don't find it: they think the Joy-Con is a paddle only in paddle-only
games. Nothing switches it automatically.

Proposed:

1. **Document it.** Add a row to the Atari Games control table in the Quad
   manual (GameAdapterRGBPico docs/manuals/UserManual_Booklet_Quad.md, about
   line 458): "Yellow + click: thumbstick as paddle 1 / joystick 1, remembered
   per game". The Duo 5200 manual too, if its build has the toggle.
2. **A PDL key on the on-screen keyboard (Blue)**, next to DRV, that toggles the
   mode, so no two-button combo is needed.
3. **A toast at game start** ("JOY-CON: PADDLE") when the game was saved in
   paddle mode, so users know why the stick doesn't steer.

On hold: the per-game button mapping (next item) changes the same per-game
storage and may absorb this. The paddle flag could become part of each game's
mapping record, and PDL could live on the mapping screen. Revisit after it.
