# Porting an emulator to the Adafruit Fruit Jam

What Pico2Atari8 learned bringing atari800 to the Fruit Jam (RP2350B) - for
the next port (Pico2Arcade, Galagino-based). Order of work that went well:
HDMI picture first, then sound (codec, then HDMI sound), then menu sound, then
USB pads, then the SD card. Everything here is in Pico2Atari8 `master`
(merge `b20d21d`, 2026-11-11); file:line pointers are to that tree.

Read the comments in the files named below: the reasons are written there,
next to the code, and this guide only points at them.

---

## 1. Board facts

### Pins (variant `adafruit_fruitjam`, `pins_arduino.h` in the core)

| What | GPIO | Notes |
|---|---|---|
| HDMI (HSTX) | CK- 12, CK+ 13, D0- 14 / D0+ 15, D1- 16 / D1+ 17, D2- 18 / D2+ 19 | `PIN_CKP`, `PIN_D0P`... The driver takes the + pins: `{ PIN_CKP, { PIN_D0P, PIN_D1P, PIN_D2P } }` (src/a8_display.cpp:1058) |
| I2S to the TLV320DAC3100 | BCLK 26, WS 27 (= BCLK + 1), DIN 24, MCLK 25 (not used), IRQ 23 | The codec runs its PLL from BCLK - no MCLK needed |
| Codec I2C | SDA 20, SCL 21 (I2C0) | Shared with STEMMA QT and the HDMI DDC |
| Codec reset | 22 (`PIN_PERIPHERAL_RESET`) | **Also the ESP32-C6's EN**: resetting the codec resets the ESP32 too. GPIO 22 is the other boards' PWM audio pin - don't drive it as audio here |
| Buttons | 1 = GPIO 0 (also BOOT), 2 = GPIO 4, 3 = GPIO 5 | Active low. Button 1 is BOOT: don't make "held at power-on" mean anything on it |
| USB host (PIO-USB) | D+ 1, D- 2, 5 V enable 11 (`PIN_5V_EN`, high = on) | The two USB-A ports sit behind an **on-board 2-port hub** - always there, even with nothing plugged in |
| microSD | CLK 34, CMD/MOSI 35, DAT0/MISO 36, DAT1 37, DAT2 38, DAT3/CS 39, detect 33 | = SPI0's pins (`PIN_SPI0_*`), or 4-bit SDIO on the same pins |
| PSRAM | CS 47, 8 MB | Max SCK 109 MHz; the core sets it up (`PSRAM` attribute puts statics there) |
| ESP32-C6 UART | TX 8, RX 9 (`Serial1`) | |
| NeoPixels | 32 (5 of them) | |
| LED | 29 | |

Pico2Atari8's `[env]` has TFT_eSPI pin flags (5, 8, 9 ...) for the other
boards. On the Fruit Jam TFT_eSPI is **never started** (`tft.init()` is not
called), so those pins are never touched - keep it that way, or button 3 and
the ESP32's UART get driven.

### Clocks

- The dvhstx HDMI driver moves `clk_sys` to **PLL_USB / 2 = 240 MHz** at
  start-up and runs HSTX from PLL_SYS at the mode's exact pixel clock.
- So **`board_build.f_cpu = 150000000L`** in platformio.ini. Any other value
  makes the core's `main()` re-program `clk_sys` on PLL_SYS, and then the
  driver re-tunes PLL_SYS for HSTX under it (platformio.ini, `[env:fruitjam]`
  comment; src/a8_display.cpp:608).
- Everything clocked from `clk_sys` (PIO dividers for I2S, PIO-USB) sees
  240 MHz. 22050 Hz I2S has a fractional PIO divider there (BCLK jitter; not
  audible in practice). 25000 Hz would be exact at 200 MHz, not at 240.

### platformio.ini (copy `[env:fruitjam]` from Pico2Atari8)

- `board = adafruit_fruitjam`, `board_build.f_cpu = 150000000L`.
- `lib_deps`: `https://github.com/alijani1/PicoMultiTrackAudio.git#v2.1.0`
  (only for its header-only codec driver `PicoTlv320*.hpp`) and
  `dvhstx=https://github.com/adafruit/Adafruit-DVI-HSTX.git#2.1.0` (only for
  Pimoroni's dvhstx driver inside it).
- `lib_ignore` both of them, and add their `src` folders to the include
  path instead (`-I.pio/libdeps/fruitjam/PicoMultiTrackAudio/src`,
  `-I.pio/libdeps/fruitjam/dvhstx/src`). Reason: `USE_TINYUSB` makes the core
  build with `lib_archive = no`, so every object of a library is linked whole
  - PicoMultiTrackAudio's file player dragged in LittleFS, and
  Adafruit-DVI-HSTX's Arduino wrapper needs Adafruit GFX. The driver sources
  are `#include`d into one of our own files instead (section 2).
- `-Iextern/pico-pio-usb` (a vendored, patched copy - section 4),
  `-Iextern/tinyusb-config -DCFG_TUSB_CONFIG_FILE=\"tusb_config_fruitjam.h\"`.
- `-DUSE_TINYUSB` (Adafruit TinyUSB: device on the native port for serial
  and the USB drive, host on PIO-USB).
- Feature flags Pico2Atari8 uses: `-DPICO_STANDALONE -DPICO_FRUITJAM
  -DA8_HDMI -DA8_HDMI_AUDIO -DA8_MENU_MUSIC -DA8_USB_HOST -DA8_SD
  -DA8_SD_SPI_MHZ=25 -DA8_SD_CACHE_SIZE=4194304 -DA8_SD_UNZIP_SIZE=3670016
  -DA8_TLV_OUTPUT=0`. Size knobs are compiler flags on purpose: future
  devices may have less flash / PSRAM.

---

## 2. Modules to copy and how they plug in

| Module | Pico2Atari8 file | What to take |
|---|---|---|
| HDMI output | src/a8_display.cpp:586-1154 (the `#ifdef A8_HDMI` half) | The scanline callback, the mode table, the core-1 loop, the menu canvas, the dvhstx includes at the end |
| Codec + I2S sound | src/pico_audio.cpp:16-414 (the `#ifdef PICO_FRUITJAM` block) | Drops into Pico2Arcade's pico_audio.cpp: same lineage, same API (its PWM half is the `#else`) |
| Menu sound / beep | src/pico_audio.cpp:584-711, src/sounds/menu16s8.h | Every board |
| USB host | src/a8_input.cpp:291-960, src/a8_stubs.c:26-41, extern/pico-pio-usb, extern/tinyusb-config | Parser and pad state are emulator-independent; the mapping to the machine is not |
| SD card | src/pico_romfs.cpp:363-625 (`#ifdef A8_SD`) | Pico2Arcade's pico_romfs.cpp is the same lineage |
| USB drive mode | src/pico_drive.cpp (card part: :77-103, :217-345) | Pico2Arcade's pico_drive.cpp is the same lineage |
| Flash writes | src/a8_flash.cpp:51 | The QMI restore and the USB power-cycle hook |

### Build constraint: no new .c/.cpp files

In Pico2Atari8 an extra object file (even an empty one) reorders the linker's
veneers in the *other* boards' builds (chain-booted slot builds), so new code
goes into existing files: the dvhstx driver is `#include`d at the end of
src/a8_display.cpp:1122-1154, Pico-PIO-USB's C and the HDMI audio packet code
into src/a8_stubs.c:22-34. Check whether Pico2Arcade has the same issue
before adding files.

### HDMI: the scanline-callback contract

- No frame buffer for the output: dvhstx calls a **scanline callback** for
  each output line just before it goes out:
  `hdmi.set_scanline_callback(hdmi_line)`; `void hdmi_line(uint32_t v_scanline,
  uint32_t active_line, uint32_t *dst)` fills `dst` with the line as RGB565,
  **two pixels per 32-bit word** (src/a8_display.cpp:758).
- It runs in the driver's **DMA_IRQ_2 handler at `PICO_HIGHEST_IRQ_PRIORITY`,
  on the core that called `hdmi.init()`** (dvhstx.cpp:925-929). It must be
  short, `__not_in_flash_func`, and read only SRAM (not flash, not PSRAM).
  Pico2Atari8 reads the emulator's 8-bit indexed `Screen_atari` through a
  256-entry palette of doubled RGB565 words (`hdmi_pal[i] = c | c << 16`):
  one load + one store per output word.
- Modes are `hdmi.init(w, h, MODE_RGB565, false, pins)` with w x h the
  *framebuffer* size the driver doubles: 320x240 -> 640x480, 400x240 ->
  800x480 etc. (table at src/a8_display.cpp:706). The callback writes
  `hdmi.get_width() / 2` words per line and gets `hdmi.get_height()` lines;
  Pico2Atari8 centres its 320x240 picture (each row twice) with black around.
- Displays differ: the first monitor showed nothing at 640x480@60 but took
  720x400@70, 720x576@50 and 800x480@60; 800x600 only sometimes. Default is
  **800x480@60** (mode 5). Settings > Screen mode lets the user pick, tries it
  for 10 s and reverts unless confirmed (src/a8_settings.cpp:612), and
  **button 3 held at power-on** goes back to the default
  (src/a8_settings.cpp:196) - a user who confirms a mode their display can't
  show has no other way back.
- 800x600: the driver's VESA timing has front porch 44 (1060-pixel lines);
  the first display showed 7 thin vertical bars. Pico2Atari8 overrides it
  with 40 by renaming the driver's table entry around the include
  (src/a8_display.cpp:1123-1149); a second display shows it clean.
- Menus: the existing TFT_eSPI drawing code draws into an **8-bit
  `TFT_eSprite`** (`A8Canvas`, src/a8_display.cpp:333) whose pixels are the
  320x240 menu canvas, shown through an RGB332 palette - all menu code runs
  unchanged. `A8Canvas::set_screen_size` fixes the sprite's "screen" size so
  `fillScreen()` clears all of it (it cleared only 240 wide before).
- Overlays (FPS, toast, on-screen keyboard) are composed once a frame on core
  1 into rows the callback then reads from the canvas instead of the game
  (`hdmi_compose`, src/a8_display.cpp:941). Write only finished rows there:
  the callback reads them while you write (half-drawn keys showed otherwise).

**For Pico2Arcade (not tried yet):** Galagino renders 224x288 RGB565 in
224x8 strips (`render_line`, galagino.cpp:256) and pushes each strip to the
TFT. The callback can't call `render_line` (too slow for an IRQ), so render
the whole frame into SRAM (224x288x2 = 129 KB RGB565, or 8-bit indices + a
palette at 64 KB) and let the callback read it. Vertical fit: 288 rows x 2 =
576 = exactly the 720x576@50 mode (but 50 Hz); otherwise a row map
(Pico2Atari8's `hdmi_src_row`, STRETCH) into 480 lines, or the landscape
build's existing 288 -> 240 downscale doubled to 480.

### Sound: the fill contract

- `pico_audio_init(rate)`, then the emulator pushes mono `int16_t` with
  `pico_audio_push_s16(data, bytes)`, which **returns the bytes taken**; the
  emulator is **paced by the ring** (it waits / retries until there is
  room). Pico2Atari8 runs atari800 at 22050 Hz. Pico2Arcade's
  pico_audio.cpp has the same API.
- Fruit Jam path: arduino-pico's `I2S` class (DMA_IRQ_0, 8 x 128-word
  buffers), 16-bit stereo, the mono sample in both halves, BCLK = 32 x rate
  (src/pico_audio.cpp:95). **Start I2S first, then configure the codec**: its
  PLL locks to BCLK (src/pico_audio.cpp:122). Codec driver: PicoTlv320 on
  the SDK's I2C, not `Wire`.
- Volume stays in the sample path (pct^2 curve); the codec sits at fixed
  levels; amp on/off = the DAC's soft mute (no pop). Headphone detection is
  an I2C read: poll it at most 4 times a second (`pico_audio_poll`).
- Output choice (Settings > Sound): Auto / Headphones / Speaker / Both /
  HDMI (`pico_audio_set_sound_output`, src/pico_audio.cpp:352).
- **HDMI sound**: the same samples go into a second ring (`push_hdmi`,
  src/pico_audio.cpp:201) instead of I2S; **core 1** takes them two at a
  time (`pico_audio_hdmi_take`, :233) and encodes HDMI audio packets at
  44100 Hz (= exactly 2 x 22050; each sample twice, the first the mean) into
  the driver's data-island queue, kept 200 packets deep (`hdmi_audio_feed`,
  src/a8_display.cpp:669). I2S is stopped and the codec muted meanwhile -
  two outputs would run off two clocks (crystal vs pixel clock) and drift.
  The packet queue costs **36 KB of SRAM**; DMA channel 3 is the driver's
  audio channel.
- **The HDMI output always starts with audio enabled** (data islands sent,
  silent while the sound goes to the codec). Switching sound to HDMI only
  changes where samples go. Restarting the output DVI <-> HDMI to switch made
  the monitor resync and lose the picture.
- Menu sound (every board): `pico_audio_menu_sound(rate, music)` around the
  game list; it is fed from the menus' input polling (~10 ms), so Volume and
  Sound output changes are heard at once. The beep (triangle, 1.2 kHz, 40
  ms) is mixed in by `pico_audio_push_s16` and advances only by the samples
  the ring accepted (src/pico_audio.cpp:607). Menu music = the Game Adapter's
  menu16s8 (16 kHz s8 mono, 122 KB flash), interpolated to the output rate.

### Input API

- `a8_input_frame()` once per emulated frame, `a8_input_menu()` from the
  menus; both pump the USB host task (section 3). The serial monitor is also
  a controller on the Fruit Jam (`serial_key`, src/a8_input.cpp:243) - useful
  before USB works.
- USB pad buttons map onto **virtual pins** that the existing per-game
  mapping (Game Settings slots) already reads, so the USB pad reuses the
  Joy-Con / DB9 mapping code (`usb_slots`, src/a8_input.cpp:917; `usb_down`).
  Don't build a second mapping path.

---

## 3. Core split, freezes and stalls (in detail)

### Final split in Pico2Atari8

| Core 0 | Core 1 |
|---|---|
| `setup()` / `loop()`: game list, then the emulator (atari800 on its own stack, paced by the sound ring) | `loop1()` (src/main.cpp:421) -> `a8_display_core1_loop()` (src/a8_display.cpp:1050) |
| USB host: Pico-PIO-USB's 1 ms frame timer (TIMER0 alarm 2) at **highest priority**; TinyUSB's task pumped from input polling | Starts the HDMI output (so the scanline IRQ, DMA_IRQ_2 at highest priority, lives here) |
| I2S DMA IRQ (DMA_IRQ_0), I2S ring writes | HDMI audio feed (`hdmi_audio_feed`) every loop pass |
| Flash writes, SD card, serial log | Overlay composing once per emulated frame |

**Rule learned the hard way: never put the PIO-USB frame timer and the HDMI
scanline IRQ on the same core.** Both need highest priority; neither may be
held up by the other.

Pico2Arcade today runs the emulator on core 1 (`setup1` never returns,
src/pico_core1.cpp:21) and renders / pushes the TFT on core 0. On the Fruit
Jam the HDMI IRQ goes to whichever core calls `hdmi.init()`, and USB must be
on the other one. Options: HDMI init on core 1 next to the emulator (the
emulator then gets interrupted once per output line), with USB + rendering +
HDMI audio feed on core 0 - or swap the emulator to core 0. Look at
**Adafruit_Arcade_Machines** (GitHub, adafruit) first: it is Galagino on the
Fruit Jam, and its `src/boards/fruitjam/usb_host_fruitjam.cpp` is where the
USB-host-on-core-0 pattern came from. Also: the HDMI audio feed must run
often (queue = 18 ms); if core 1 is a never-returning emulator loop, call
the feed from the emulator's frame loop or from core 0.

### Everything that hung, stalled or glitched, and the fix

1. **USB host on core 1 hung core 1** (worst one; took several rounds).
   - Symptom: a fraction of a second after boot, no sound at all (not even
     menu music), games stuck at "Unpacking", core 1's loop counter 0 in the
     heartbeat log. With or without a pad plugged in (the on-board hub is
     always there).
   - Cause: Pico-PIO-USB sends each packet from its 1 ms frame timer: start
     the TX DMA, *then* clear the PIO "done" flag, then spin on it. The HDMI
     scanline IRQ (higher priority on core 1) landed between start and
     clear, outlasted a short packet, so the clear wiped the real "done" -
     the spin never ended, inside the timer IRQ. Core 1 dead = HDMI sound
     feed dead = sound ring full = emulator (paced by it) stalled.
   - Tried, not enough: a delay hook; the race patch alone.
   - Fix: host moved to **core 0**, `usb_host.begin(1)` called on core 0 (the
     alarm pool is made on the calling core), then
     `irq_set_priority(timer_hardware_alarm_get_irq_num(timer0_hw, 2),
     PICO_HIGHEST_IRQ_PRIORITY)` (src/a8_input.cpp:666-687). The race patch
     stays in the vendored copy: start + clear inside
     `save_and_disable_interrupts()` (extern/pico-pio-usb/pio_usb.c:90-99).
   - Cost: TinyUSB's enumeration waits (150 ms debounce, resets, retries)
     spin on core 0 - the menu / game holds briefly when a device connects.
2. **Pad dead in games, fine in the menu.** Every game start unpacks a zip
   to flash and Game Settings saves to flash. A flash write runs with
   interrupts off for tens to hundreds of ms: no USB frames, the bus idles
   > 3 ms, devices **suspend**. The SNES pad never woke properly (kept
   repeating its last report). Diagnosed when toggling the FPS display (a
   saved setting) killed the pad. Fix: after each flash write switch the
   ports' 5 V off for 300 ms and back on (`a8_input_usb_flash_written`,
   src/a8_input.cpp:702, called from src/a8_flash.cpp:51) - a replug to the
   host; devices re-enumerate in about half a second. Several writes in a
   row give one power cycle, timed from the last.
3. **Flash writes and other subsystems** (all in src/a8_flash.cpp):
   - `rp2040.idleOtherCore()` + interrupts off around erase/program.
   - On the RP2350 the SDK's flash functions leave XIP in the boot ROM's
     slow mode: both cores then ran many times slower (games at 1-5 fps).
     Save and restore QMI `m[0].timing/rcmd/rfmt` around the write, in RAM.
   - I2S DMA can't ride out interrupts-off: `pico_audio_suspend()` /
     `resume()` around writes (codec muted first, so BCLK stopping isn't
     heard; its PLL relocks) - src/a8_settings.cpp:298, src/pico_drive.cpp.
   - **Picture**: parking core 1 also stops the HDMI scanline IRQ, so the
     picture can drop out for the write (the driver restarts its stream).
     Written down as a TODO in src/a8_display.cpp:616; avoid flash writes
     while the user is looking where you can (SD + PSRAM instead of the
     flash unzip area does exactly that - section 5).
4. **Unplug not noticed, then USB dead for good.** The hub reports removal
   on its status endpoint; TinyUSB's hub driver can stop polling it (a
   failed re-arm, an enumeration that never ends), after which no removal or
   new device is seen. A gone pad fails every request (report callback with
   `len == 0`; an idle pad NAKs instead and produces no callback). Fix: a
   pad whose reports fail for **500 ms** is taken as unplugged and the ports
   are power-cycled as in 2 (src/a8_input.cpp:709-735). Confirmed: "unplug
   works now, it recovers".
5. **Controller test froze on Start+Select.** The host task is not on an
   IRQ: it runs only when called. A screen with its own input loop didn't
   call it, so the release never arrived. Fix: pump `a8_input_usb_task()`
   from **every** input-polling path (`a8_input_menu`, `a8_input_frame`,
   `a8_input_button`).
6. **HDMI lost the picture when switching sound to HDMI** - see section 2
   (always start with audio; switching never restarts the output).
7. **Pads set up but never polled.** TinyUSB's header declares
   `tuh_hid_mount_cb` / `tuh_hid_umount_cb` weak, which made our
   definitions in a file including that header weak too, and the linker
   took Adafruit's empty ones. Fix: define them in a file that doesn't
   include it (src/a8_stubs.c:36-41) and forward.
8. **Second TinyUSB.** Fetching Pico-PIO-USB as a PlatformIO library pulled
   in another Adafruit TinyUSB that replaced the core's. Fix: vendor
   Pico-PIO-USB 0.7.2 into extern/pico-pio-usb (with its LICENSE) and
   compile its .c files via an existing source file.
9. **RAM.** With HDMI audio (36 KB queue) and USB the SRAM got tight:
   PIO-USB endpoint pool 32 -> 12 (188 bytes each;
   extern/pico-pio-usb/pio_usb_configuration.h:46), the emulator's own stack
   32 -> 16 KB on this build (src/main.cpp:63; the start-up log prints the
   stack actually used - check it). The build is at ~81% RAM. Put
   write-rarely buffers in PSRAM (`PSRAM static ...`, e.g. the keyboard
   strip, the SD cache), never anything the scanline callback reads.
10. **Tearing**: the emulator (59.92 Hz, paced by the sound) writes the
    screen while the callback reads it; a tear line may drift. Not seen as a
    problem yet; TODO is to start each emulated frame at HDMI vblank.

### Native USB vs PIO-USB

The RP2350's native USB controller is the **device** port (USB-C: serial
log, USB drive mode via Adafruit TinyUSB's MSC). The USB-A host ports are
**PIO-USB** (TinyUSB host on rhport 1, `configure_pio_usb(1, &cfg)`;
`begin(1)`). Pico2Atari8 uses PIO 2 and DMA channel 11 (`A8_USB_PIO`,
`A8_USB_DMA`), clear of the HDMI driver's DMA 0-3 and the I2S PIO. Check
your own PIO/DMA use against those.

---

## 4. USB host lessons (pads)

All in src/a8_input.cpp:291-960; the Controller test screen
(src/a8_ctltest.cpp:167, `draw_usb`) shows both pads' IDs, raw reports and
button numbers - build it early, it diagnosed most of these.

- **Enumeration buffer 1024, not 256.** TinyUSB's
  `CFG_TUH_ENUMERATION_BUFSIZE` holds the configuration descriptor and each
  HID report descriptor; a longer one is handed over **empty**
  (0e8f:3013's second interface is 263 bytes). Raised via a config file that
  includes Adafruit's and overrides one define
  (extern/tinyusb-config/tusb_config_fruitjam.h) - no framework edits.
- **Zero the pad layout** before parsing (`PadLayout lay{}`, and
  `L = PadLayout()` in the parser). An empty descriptor left stack garbage
  as the layout: the menus were stuck on UP.
- **Parse only inside Generic Desktop Joystick (0x04) / GamePad (0x05)
  application collections** (`hid_parse_layout`, src/a8_input.cpp:397): X
  (0x30), Y (0x31), hat (0x39), buttons 1-16, input data-variable fields of
  the first report ID that has any. A combo device's mouse X/Y and keyboard
  are not a pad.
- **Bind a player only to an interface with X or a hat** (or the known SNES
  pad). A pad's second "reports only" interface was taken as pad 2 and stole
  the player slot.
- **Use the range the device reports.** Axes are normalised from the
  descriptor's logical min/max to -1000..1000 (`usb_axis_norm`,
  src/a8_input.cpp:875) and fed to the same paths as the Joy-Con / pots
  (analog paddles, 5200 pots); directions from the stick with a 30% dead
  zone. Don't apply Joy-Con-style range learning to USB.
- **Button numbering**: generic button 1 = the main fire (B's slot), 2 = A,
  3 = X, 4 = Y, 5/7 = L, 6/8 = R, 9 = Select, 10 = Start. The Quad in
  controller mode sends its Red (main) button as HID button 1; with the old
  "1 = X" order it fired OPTION. All slots stay remappable per game.
- **SNES-style pad exception** (081f:e401): a fixed report layout
  (`usb_parse_pad`), not the descriptor.
- Start + Select = MENU. Pad 2 = player 2, sharing pad 1's map; pad 2 also
  drives the menus.
- Not HID (XInput / Xbox pads): connects, nothing reads it; the log says
  which device connected (`tuh_mount_cb`).
- Keyboard (boot protocol) is implemented (`usb_key`, F-keys) but **not
  tested on hardware**.
- Deferred: a learn mode (press each slot's button once, per VID:PID).

---

## 5. SD card, PSRAM, USB drive mode, menu sound

- **SD** (src/pico_romfs.cpp:363-625): the core's `SD` library on SPI0 at
  `A8_SD_SPI_MHZ` (25) or SDIO (`A8_SD_SDIO`). With a card in at power-on
  the game list comes from it (root, ROMS, ATARI, then folders 3 deep; long
  names), else from the flash drive - the flash drive is the fallback, not
  a second list. `romfs_init` probes the card first (src/pico_romfs.cpp:629).
- **PSRAM instead of flash writes**: a game's files up to
  `A8_SD_CACHE_SIZE` (4 MB) are read into a PSRAM area (bump allocator, up
  to 8 files, emptied when a game is picked - `romfs_cache_reset`) and run
  from there (`sd_map`, :469); zips unpack into PSRAM (`A8_SD_UNZIP_SIZE`,
  src/a8_zip.cpp:33). Bigger files are streamed from the card through a
  `fopencookie` stdio stream (`romfs_fopen_stream`, :562). Result: starting
  a game from SD writes **no flash** - no picture drop, no USB power-cycle,
  no audio suspend. Size knobs are flags (0 disables a cache).
- **USB drive mode with a card** (src/pico_drive.cpp:77-103, :217):
  the PC gets the **whole card, raw sectors** via Adafruit TinyUSB MSC, as
  PicoCartReader does - its own file system, never formatted by us. Raw
  access is SdFat's own card object from `SdCardFactory`
  (`romfs_card_raw_open`, src/pico_romfs.cpp:588), `DEDICATED_SPI`, 25 ->
  12 -> 4 MHz fallback; `SD.h`'s `getFs()` is protected, so don't try to go
  through it. Without a card: the FAT12 flash drive as before. **Eject on
  the PC restarts into the list** (MSC start/stop callback with
  `load_eject && !start`), on all boards. Sync the card and set the unit
  not-ready before restarting.
- **Menu music / beeps**: see section 2 (Sound). Fed from input polling, so
  any menu loop must poll input (it already must, for USB).
- **HDMI audio vs TLV320**: one or the other, never both (two clocks).
  Auto = headphones when plugged in, else speaker.

---

## 6. Status

**Confirmed on hardware (Fruit Jam):** HDMI picture in the modes the user's
displays take (800x480 default; 800x600 clean on a second display); TLV320
sound (headphones / speaker / auto); HDMI sound and switching without a
resync; menu music and beeps; Settings > Screen mode with the 10 s confirm
and the button-3 reset; Volume / Sound in Game Settings; USB gamepads in
menus and games (SNES-style, generic HID, analog stick, pad 2, per-game
mapping, X = OPTION default); unplug / replug recovery; games from the SD
card (PSRAM path); the SD card in USB drive mode.

**Built only / not confirmed:** USB keyboard; the picture dropping during
flash writes (expected, not measured); tearing; SDIO mode; 5200 analog
specifics over USB; the other boards with the shared beep / Volume / menu
sound / eject-restart changes.

**Open items** (docs/BACKLOG.md, "Fruit Jam: open items"): disk writes to
the SD card; USB keyboard test; pad learn mode; testing the other boards;
slot-UF2 drag-drop check; SD speed / SDIO.
