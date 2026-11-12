# Pico2Atari8 backlog

Ideas parked for later, newest first.

## Fruit Jam: open items after HDMI / sound / USB pads / SD (parked 2026-11-11)

Done on the `fruitjam-sound` branch and confirmed on hardware: HDMI sound and
screen modes, menu music and beeps, USB gamepads (generic HID, analog, pad 2,
unplug recovery), games from the microSD card, and the card in USB drive mode.
Still open:

1. **Disk writes to the SD card.** ATRs on the card are read-only (loaded into
   PSRAM, or streamed for big ones). Write back changed sectors to the card's
   file (TODO in src/pico_romfs.cpp, the A8_SD block).
2. **USB keyboard test.** The keyboard path (src/a8_input.cpp `usb_key`,
   F-keys) is built but untested on hardware.
3. **Learn mode for pad buttons.** Generic HID pads number their buttons
   freely (the Quad in controller mode sends Red as button 1). Press each slot's
   button once to map it, per device (VID:PID), instead of the fixed
   1=B 2=A 3=X 4=Y order.
4. **Other boards with the shared changes.** Quad, Duo 5200, Duo, duo_tiny:
   menu beeps, Volume in Game Settings, menu sound running in the menus
   (amp hiss?), and Eject on the PC now restarting drive mode.
5. **Slot UF2 drag-drop.** Check that the `picotool --abs-block` slot UF2s
   (quad / duo5200 / duo) write only their slot on a BOOTSEL drag-drop.
6. **SD speed.** Big copies over SPI at 25 MHz; try SDIO (`A8_SD_SDIO`) on the
   Fruit Jam's slot pins.

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
