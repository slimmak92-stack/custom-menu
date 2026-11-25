# gta6menu.asi  (GTA San Andreas 1.0 US / SA-MP, standalone — no plugin-sdk, no D3DX)

Install: put BOTH files in the game folder (or modloader\gta6menu\):
  gta6menu.asi   gta6menu.txd

Press ESC: the GTA 6 style menu is drawn over the stock pause menu.
  Resume      -> native resume
  Map         -> opens the stock Map page
  Settings    -> opens the stock Options page (all original game options)
  MoonLoader  -> lists scripts in \moonloader and reloads them (sends CTRL+R)
Press ESC on a stock page to come back to the GTA 6 menu.

gta6menu.ini (created next to the .asi):
  [menu]  accent=0..3  background=0..2  clock24=1  showfps=0
  [stock] nav_map=2  nav_options=5
          (how many DOWN presses from "Resume" reach Map / Options in the stock menu;
           change them if your stock menu has a different order)

Custom textures: assets/gta6menu.txd (RenderWare TXD, editable in Magic.TXD).
Regenerate from tools/src/*.png with:  python3 tools/make_txd.py
