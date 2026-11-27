# gta6menu.asi  (GTA San Andreas 1.0 US / SA-MP, standalone — no plugin-sdk, no D3DX)

Install: put BOTH files in the game folder (or modloader\gta6menu\):
  gta6menu.asi   gta6menu.txd

Press ESC: the GTA 6 style menu is drawn over the stock pause menu.
  Resume       -> native resume
  Map          -> if moonloader\gps_zone_hud.lua exists: closes the pause menu and presses its
                  big-map key (ALT); otherwise opens the stock Map page
  Settings     -> AUDIO: master volume + mute (the game's Windows mixer volume)
                  GRAPHICS: brightness, vignette, colour tint, FPS cap
                  GAME OPTIONS: Controller / Language / Mod configuration (original stock pages)
                  THIS MENU: accent, background, 24h clock, FPS counter
                  (Audio/Display tabs also link to the original pages for SFX/radio volume,
                   resolution, anti-aliasing, draw distance, ...)
  MoonLoader   -> lists scripts in \moonloader, "Reload all scripts" sends CTRL+R
  Mod Loader   -> lists folders in \modloader, click to enable/disable (renames the folder with a
                  leading dot; applies on the next game start)
ESC on a stock page returns to the GTA 6 menu. ESC on a menu page = back.

Mod Loader support: put gta6menu.asi + gta6menu.txd + gta6menu.ini together in
  modloader\gta6menu\   (files are also found in the game folder).

gta6menu.ini (created next to the .asi):
  [menu]  accent=0..3  background=0..2  clock24=1  showfps=0
  [map]   script=gps_zone_hud.lua  key=18        (key = virtual-key code, 18 = ALT)
  [stock] nav_map=2  nav_options=5
          (DOWN presses from "Resume" to reach Map / Options in the stock menu)

Custom textures: assets/gta6menu.txd (RenderWare TXD, editable in Magic.TXD).
Regenerate from tools/src/*.png with:  python3 tools/make_txd.py

How the stock-menu part works (v3)
  The stock pause menu stays alive under the overlay, so the overlay "parks" it on the Brief page
  (an inert page) and swallows mouse input. Opening a stock page (Map, Controller/Audio/Display/
  Language/Mod configuration) closes the stock menu, reopens it fresh (cursor on Resume) and presses
  DOWN/ENTER from there, so it never depends on a remembered highlight.
  Inside a stock page use the pill at the bottom ("Click here to return to the GTA 6 menu").
  ESC on an overlay page = back, ESC on the main page = resume.

Diagnostics: the menu writes gta6menu.log next to the .asi (init, hook, menu open/close, every key it
sends, every click). If something misbehaves, send that file.

Audio/graphics notes
  Master volume/mute change the game's own entry in the Windows volume mixer (all game sound).
  Brightness/vignette/tint are drawn over the finished frame while you play (not in menus).
  FPS cap only lowers the frame rate (it can't exceed the game's own limiter).
  Settings are saved to gta6menu.ini ([audio] [graphics]).

Real game settings (v5)
  Settings reads and writes the ORIGINAL game settings directly (no stock pages needed):
    AUDIO     sound effects volume, radio volume, radio equaliser, radio auto-select,
              game master volume (Windows mixer), mute
    GRAPHICS  brightness, draw distance, frame limiter, mip mapping, widescreen,
              vignette, colour tint, FPS cap
    GAME      show HUD, radar mode, map legend, subtitles
    THIS MENU accent colour, background, clock format, FPS counter
    MORE      resolution/anti-aliasing, controls/key bindings, language, mod configuration
              (these need the game's own engine screens, so they open the original pages)
  Values live in FrontEndMenuManager (0xBA6748); the field offsets are in src/menu_offsets.h
  (GTA SA 1.0 US). Every row checks that the value it reads is sane, otherwise it shows
  "Unavailable" instead of writing anything. Changes are saved with the game's own save routine.
