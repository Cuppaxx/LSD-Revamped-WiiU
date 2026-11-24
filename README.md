<div align="center">

<img src="docs/img/banner.png" alt="LSD: Revamped for Wii U" width="100%">

# LSD: Revamped for Wii U

**Walk through the dreams of *LSD: Dream Emulator* on the Wii U.**<br>
An unofficial native homebrew port of [LSD: Revamped](https://github.com/Figglewatts/LSDRevamped), rewritten in C with a
software PS1-style renderer.

[![License: MIT](https://img.shields.io/badge/license-MIT-a371f7?style=for-the-badge)](LICENSE)
[![Platform: Wii U (Aroma)](https://img.shields.io/badge/platform-Wii%20U%20%28Aroma%29-00a2e8?style=for-the-badge&logo=nintendo&logoColor=white)](#-quick-install)
[![Language: C](https://img.shields.io/badge/language-C99-555555?style=for-the-badge&logo=c&logoColor=white)](app/src)
[![Scripting: Lua 5.2](https://img.shields.io/badge/scripting-Lua%205.2-2c2d72?style=for-the-badge&logo=lua&logoColor=white)](app/lua/api.lua)
[![Status: beta](https://img.shields.io/badge/status-beta-ffcc33?style=for-the-badge)](#-known-limitations--roadmap)

<img src="docs/img/splash.gif" alt="Boot splash: Ported by Cuppax" width="480">

<sub>Ported by <b>Cuppax</b> · original game by OutSide Directors Company / Asmik Ace (1998) · LSD: Revamped by Figglewatts</sub>

</div>

> [!TIP]
> **Just want to play?** Download **`LSD-Revamped-WiiU.zip`** from the [latest release](../../releases/latest), extract it
> to the root of your Wii U SD card, and launch it from the Wii U Menu. Everything is inside: all 28 dreams, the
> movies, the English story text. See [Quick install](#-quick-install).
> Fan project, not affiliated with the rights holders ([disclaimer](THIRD_PARTY.md#disclaimer)).

---

## ✨ Features

- 🌙 **All 28 dreams**: the 14 original dreams of LSD: Dream Emulator and the 14 dreams of the
  [Additional Dreams](https://github.com/Figglewatts/LSDR-Additional-Dreams) mod, in one shared pool or originals only.
- 🗺️ **The dream journal**: days, wall links, the 19x19 **dream graph** (upper/downer, static/dynamic) that picks the
  next day's dream, the grey man, **flashbacks** from your recorded days, special days and the day-365 ending.
- 🧩 **Lua object scripts**: Revamped's own 100+ object scripts run on an embedded Lua 5.2 with a reimplementation of
  its scripting API (triggers, interactive objects, scripted links, async action runners).
- 🐘 **Animated models**: MOM/TOD models of the originals and the mod's animated objects.
- 🎨 **The PS1 look, faithfully**: software rasterizer with vertex snapping, Revamped's half-affine texture warping,
  stepped fog, gradient skies with suns, clouds and stars, palette-cycling water, 50 % blending.
- 🔊 **Sound**: per-dream music by day style, title music, footsteps that follow the ground under your feet,
  positional ambient sounds (DreamAudio) with roll-off and panning, link and fall sounds.
- 🎮 **Controls**: Revamped's FPS scheme or the classic **tank** controls, look behind, button rebinding, deadzone;
  GamePad, Pro Controller, Classic Controller and Wii Remote (+ Nunchuk); GamePad touch in menus.
- 🎬 **Special days and movies**: the opening, special video/text days, event movies, chart faces and the ending
  (MJPEG movies from the SD card), the **journal** screen and the **credits** roll.
- 🖥️ **Menus**: boot splash, title, pause, dream select, dream chart, an options hub (gameplay, controls, audio,
  graphics, dreams), 4:3 and 16:9.
- 🔭 **Far render distance**: see up to **160 tiles** (MAX: 256) instead of Revamped's 20. The default **AUTO** mode
  raises and lowers the distance live to hold 60 fps, with **scaled fog** so the whole view keeps the PS1 look and
  nothing pops at the edge. See [Far views](#-far-views).
- ⚙️ **Graphics options**: render distance (15 to MAX, or AUTO), fog (classic / scaled / off), internal resolution
  (240-480 lines), 4:3 / 16:9, fit / integer / stretch scaling, sky effects, vertex snapping, texture warping 0-1,
  object models, render threads, FPS counter.
- 🌐 **English story and dialogue**: the Japanese special-day diary texts are translated; level textures stay
  original.
- 💾 **Save file** with options, journal progress, chart history and recorded memories (`SD:/wiiu/apps/lsdr/save.txt`).

## 📸 Screenshots

<div align="center">

<img src="docs/img/sheet_dreams.png" alt="Original dreams" width="100%">
<sub>The original dreams: Bright Moon Cottage, Pit &amp; Temple, Kyoto, The Natural World, Happytown, Violence District,
Temple Dojo, Flesh Tunnels, Clockwork Machines, Long Hallway, Sun Faces Bridge, Moonlight Tower</sub>

</div>

### 🌀 In the dreams

<table>
<tr>
<td width="50%"><img src="docs/img/dreams/kyoto.png" alt="Kyoto"><br><sub><b>Kyoto</b>: gradient sky, clouds, the torii and the pond</sub></td>
<td width="50%"><img src="docs/img/dreams/happytown.png" alt="Happytown"><br><sub><b>Happytown</b>: night sky, floating objects</sub></td>
</tr>
<tr>
<td><img src="docs/img/dreams/natural_world.png" alt="The Natural World"><br><sub><b>The Natural World</b>: stepped fog over open fields</sub></td>
<td><img src="docs/img/dreams/pit_and_temple.png" alt="Pit &amp; Temple"><br><sub><b>Pit &amp; Temple</b>: the elephant, an animated model</sub></td>
</tr>
<tr>
<td><img src="docs/img/dreams/sun_faces_bridge.png" alt="Sun Faces Bridge"><br><sub><b>Sun Faces Bridge</b></sub></td>
<td><img src="docs/img/dreams/flesh_tunnels.png" alt="Flesh Tunnels"><br><sub><b>Flesh Tunnels</b></sub></td>
</tr>
<tr>
<td><img src="docs/img/dreams/clockwork_machines.png" alt="Clockwork Machines"><br><sub><b>Clockwork Machines</b></sub></td>
<td><img src="docs/img/dreams/bright_moon_cottage.png" alt="Bright Moon Cottage"><br><sub><b>Bright Moon Cottage</b>: where the journal begins</sub></td>
</tr>
<tr>
<td><img src="docs/img/addl/dreamscape_sun.png" alt="Dreamscape"><br><sub><b>Dreamscape</b> (Additional Dreams): sun, clouds and sky gradient</sub></td>
<td><img src="docs/img/dreams/kyoto_statue.png" alt="Kyoto statue"><br><sub><b>Kyoto</b>: the giant head</sub></td>
</tr>
</table>

<details>
<summary><b>➕ The Additional Dreams (click to expand)</b></summary>
<br>
<img src="docs/img/sheet_addl.png" alt="Additional Dreams" width="100%">
<sub>Baked from the LSDR-Additional-Dreams mod (textures by Royalty3D, catmilk, sphere and Bor).</sub>
</details>

### 🔭 Far views

<div align="center">

<img src="docs/img/m11/sheet_m11.png" alt="Render distance: classic 20 tiles vs AUTO vs MAX" width="100%">
<sub>Same camera, left to right: Revamped's classic 20 tiles, **AUTO** (what it settled at in Cemu, 58 to 160 tiles
depending on the dream) and **MAX** 256 tiles.</sub>

</div>

### 🌌 Menus

<table>
<tr>
<td width="50%"><img src="docs/img/menus/title.png" alt="Title screen"><br><sub><b>Title</b> (16:9)</sub></td>
<td width="50%"><img src="docs/img/menus/pause.png" alt="Pause menu"><br><sub><b>Pause</b> over the dream</sub></td>
</tr>
<tr>
<td><img src="docs/img/menus/dream_select.png" alt="Dream select"><br><sub><b>Dream select</b>: original and additional dreams</sub></td>
<td><img src="docs/img/menus/dream_chart.png" alt="Dream chart"><br><sub><b>Dream chart</b>: one square per day</sub></td>
</tr>
<tr>
<td><img src="docs/img/menus/options.png" alt="Options hub"><br><sub><b>Options</b> hub</sub></td>
<td><img src="docs/img/menus/options_graphics.png" alt="Graphics options"><br><sub><b>Graphics</b> options</sub></td>
</tr>
<tr>
<td><img src="docs/img/menus/journal.png" alt="Journal"><br><sub><b>Journal</b></sub></td>
<td><img src="docs/img/menus/button_capture.png" alt="Button rebinding"><br><sub><b>Rebinding</b> a button</sub></td>
</tr>
</table>

<details>
<summary><b>🗂️ More menu screens (4:3 pages, dialogs)</b></summary>
<br>
<img src="docs/img/sheet_menus.png" alt="Menu contact sheet" width="100%">
<table>
<tr>
<td width="33%"><img src="docs/img/menus/options_controls.png" alt="Controls"><br><sub>Controls</sub></td>
<td width="33%"><img src="docs/img/menus/options_audio.png" alt="Audio"><br><sub>Audio</sub></td>
<td width="33%"><img src="docs/img/menus/options_dreams.png" alt="Dreams options"><br><sub>Dreams</sub></td>
</tr>
<tr>
<td><img src="docs/img/menus/options_gameplay.png" alt="Gameplay"><br><sub>Gameplay</sub></td>
<td><img src="docs/img/menus/confirm_new_game.png" alt="New game"><br><sub>New game confirmation</sub></td>
<td><img src="docs/img/menus/title_43.png" alt="Title 4:3"><br><sub>Title (4:3)</sub></td>
</tr>
</table>
</details>

<sub>All screenshots were taken in the Cemu emulator from the port's own frame buffer (320x240 internal resolution,
scaled 2x without filtering).</sub>

## 🚀 Quick install

<img src="docs/img/icon.png" alt="Wii U Menu icon" width="96" align="right">

You need a Wii U with the [Aroma](https://aroma.foryour.cafe/) homebrew environment and an SD card.

1. Download **`LSD-Revamped-WiiU.zip`** (about 330 MB) from the [latest release](../../releases/latest).
2. **Extract it into the root of your SD card.** It adds two things:
   ```
   SD:/wiiu/apps/lsdr.wuhb          the game (app + all dream data + English text)
   SD:/wiiu/apps/lsdr/data/MOVIE/   the FMV movies (opening, special days, ending)
   ```
3. Start **LSD Revamped (Wii U)** from the Wii U Menu. Your save file appears at `SD:/wiiu/apps/lsdr/save.txt`.

That is all: no conversion, no extra downloads. Playing in **Cemu**? Extract the same zip into Cemu's emulated SD card
folder (`<Cemu>/sdcard/`) and launch the `.wuhb`.

<details>
<summary><b>Other downloads and building it yourself</b></summary>
<br>

- **`lsdr-app.wuhb`** (1.2 MB) is the app alone, without game data. It shows a *NO GAME DATA FOUND* screen until you
  put a converted data folder at `SD:/wiiu/apps/lsdr/data/` (see [docs/DATA.md](docs/DATA.md)). Useful if you want to
  build the data from your own clone of [LSD: Revamped](https://github.com/Figglewatts/LSDRevamped):
  ```sh
  git lfs install
  git clone https://github.com/Figglewatts/LSDRevamped.git
  git clone https://github.com/Figglewatts/LSDR-Additional-Dreams.git
  python tools/make_data.py --revamped LSDRevamped --addl LSDR-Additional-Dreams --out build/data --movies
  python app/build.py --data build/data     # all-in-one .wuhb, like the release
  ```
- The code is MIT. The game data in the release comes from LSD: Revamped (MIT) and the Additional Dreams mod; see
  [THIRD_PARTY.md](THIRD_PARTY.md).

</details>

<p align="center"><img src="docs/img/menus/no_data.png" alt="No game data screen (app-only build)" width="360"></p>

## 🎮 Controls

Defaults (FPS scheme). Every action except menu navigation can be rebound in **Options > Controls**; the tank scheme
uses the same buttons.

| Action | GamePad / Pro Controller | Notes |
|---|---|---|
| Move / strafe | Left stick (or D-pad) | Tank: up/down = steps, left/right = turn |
| Look | Right stick | Look speed and invert in Options > Gameplay |
| Run | **B** | hold or toggle (Options > Gameplay) |
| Interact | **A** | talk to / use objects |
| Look behind | **ZL** | |
| Look up / down | **X** / **Y** | mainly for the tank scheme (springs back) |
| Strafe left / right | **L** / **R** | tank scheme |
| Pause | **+** or tap the top-right corner of the GamePad screen | |
| Quit (saves) | **HOME** | |
| Menus | D-pad / left stick, **A** confirm, **B** back, touch | |

A Wii Remote held sideways works too (2 = A, 1 = B, A = X, B trigger = ZL); Classic Controllers and Wii Remote +
Nunchuk use the GamePad layout. Only the GamePad has been tested; the other mappings come from SDL2's controller table.

## 🔧 Building

```sh
# devkitPro with: wiiu-dev wiiu-sdl2 ppc-libjpeg-turbo
python app/build.py            # -> build/lsdr-app.wuhb (about 1.2 MB, no game data)
```

Works on Windows, Linux and macOS; `DEVKITPRO` selects the toolchain (default `C:\devkitPro` or `/opt/devkitpro`).
Lua 5.2.3 is vendored in `third_party/lua`. Details, options and test hooks: [docs/BUILDING.md](docs/BUILDING.md).

## 🧪 Data pipeline

The port reads converted data, never Unity files. `tools/make_data.py` drives the converters:

| Step | Script | Output |
|---|---|---|
| Dreams, environments, spawns, entities, graph, songs, Lua scripts, LBD/TIX | `convert.py` | `dreams.txt`, `STGxx/` |
| Animated MOM/TOD models | `models.py` | `STGxx/models.bin` |
| Sky textures | `sky_assets.py` | `sky/*.bin` |
| Additional Dreams (meshes, textures, models, scripts, sounds) | `addl.py` | `dreams_addl.txt`, `ADDxx/`, `ADDTEX/` |
| Footsteps, SFX, positional sounds | `audio_assets.py` | `audio.txt`, `SFX/` |
| Special days, text days, movies, credits | `special_assets.py` | `specials.txt`, `SPDAY/`, `MOVIE/`, `CREDITS/` |

Everything is documented in [docs/DATA.md](docs/DATA.md) and the [format specs](docs/formats/).

## 🧠 How it works

LSD: Revamped is a Unity game and Unity cannot target the Wii U, so this is a **reimplementation**, not a recompile.
The C code follows Revamped's C# systems (dream system and graph, player movement, environments, audio, menus) and
its shaders (classic PS1 shader, fog, sky), and loads the original PS1 formats through a C port of
[libLSD](https://github.com/Figglewatts/libLSD).

- **Renderer**: a software rasterizer (fixed-point scanlines, z-buffer, Gouraud, affine/half-affine textures) that
  projects on the main thread, rasterizes 16-row bands on 3+ threads and uploads one SDL texture per frame.
- **Scripting**: Lua 5.2.3 plus `app/lua/api.lua`, a Lua reimplementation of the MoonSharp API the scripts expect.
- **Audio**: one SDL mixer: music, 24 voices with pitch and panning, movie soundtrack.

Read [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the full tour and [docs/DEVLOG.md](docs/DEVLOG.md) for the
milestone-by-milestone development log.

## 🔭 Far views

Revamped draws 20 tiles. The Wii U build can go much further, so the draw distance is an option
(*Options > Graphics > World*):

| Setting | Values |
|---|---|
| **Render distance** | 15, CLASSIC (20), 30, 40, 60, 80, 120, 160, MAX (256), **AUTO** (default) |
| **Auto limit / target** | shown with AUTO: upper limit (40 to MAX, default 160) and 60 or 30 fps target |
| **Fog** | CLASSIC (fog ends the view, as before), **SCALED** (default: the classic fog gradient stretched over the whole view, the last quarter fades into the horizon), OFF |

**AUTO** measures the frame's render work twice a second and eases the distance in or out to stay inside the frame
budget, so small dreams show everything (160 tiles) while big, dense ones settle lower, and a heavy scene on a slower
console just settles nearer instead of dropping frames. `CLASSIC` distance with `CLASSIC` fog is pixel-identical to
Revamped's look.

What made it affordable: per-chunk bounding boxes tested against distance and view frustum, projection on all render
threads, indexed tile meshes (each corner projected once), triangles that cannot cover a pixel dropped before they are
queued (60-75 % of them at 160 tiles), per-band triangle bins, and hand-written replacements for the slow `sqrtf` /
`floorf` library calls (the Wii U CPU has no hardware square root). Numbers and details: [docs/DEVLOG.md](docs/DEVLOG.md)
(M11).

## ⚡ Performance

- **Wii U:** console logs (`log.txt`) showed roughly **55 to 230 fps** at the default 320x240 and the classic 20 tiles
  (before the far-view work), depending on the dream. The far-view code is tested in Cemu only: AUTO should settle
  nearer than in Cemu on the console, which is what it is for. Higher internal resolutions and 16:9 cost fill rate;
  *Render threads* and *Show FPS* help tuning.
- **Cemu:** about 60 fps (the frame cap) in every dream at 320x240 with 3 render threads and AUTO. At a fixed 160 tiles
  the heaviest views (Dreamscape, Kyoto) fall to 33 to 38 fps, which is why AUTO exists.

## 🧭 Known limitations / roadmap

- Most features were verified in **Cemu**; on real hardware the port has been run, but not every feature (movies, touch
  menus, every controller type, all special days) has been checked there yet.
- Pro Controller / Classic Controller / Wii Remote mappings are untested on hardware.
- Mod-dream particles (steam, drips) are not drawn; transparent triangles are not depth-sorted.
- Colliders of moving objects stay at their prefab position; the mixer has no ducking compressor like Revamped's.
- Revamped's alternative "Revamped" song library is not converted (the original soundtrack is used).
- Text and movie pages are 320x240 (pillarboxed in 16:9).
- Far views (AUTO / 160+ tiles) are verified in Cemu, not yet on hardware. FOG OFF has a hard edge at the view distance.
- Roadmap: hardware test pass, particles, depth-sorted transparency, performance work for 480-line modes.

## ❓ FAQ

<details>
<summary><b>Where is the game data?</b></summary>

Inside the release. `LSD-Revamped-WiiU.zip` contains the converted data of LSD: Revamped (MIT) and the Additional
Dreams mod, bundled into `lsdr.wuhb`, plus the movies. If you prefer, you can convert it yourself from your own clone
with `tools/make_data.py` ([docs/DATA.md](docs/DATA.md)).
</details>

<details>
<summary><b>Can I just download a .wuhb with everything inside?</b></summary>

Yes: that is `lsdr.wuhb` in the release zip. The only separate part is the `MOVIE` folder, which is read from the SD
card (about 260 MB).
</details>

<details>
<summary><b>Does it run in Cemu?</b></summary>

Yes, that is where most development and testing happened. Extract the release zip into Cemu's emulated SD card
(`<Cemu>/sdcard/`) and launch the `.wuhb`.
</details>

<details>
<summary><b>The movies do not play.</b></summary>

Movies are read from the SD card: make sure `SD:/wiiu/apps/lsdr/data/MOVIE/*.mjv` exists (the release zip puts
them there). The intro can be switched off in Options > Dreams.
</details>

<details>
<summary><b>Why a software renderer?</b></summary>

It reproduces the PS1 look exactly (affine warping, vertex snapping, stepped fog, palette water) without a GX2 shader
toolchain, and the Wii U's three cores are enough for 320x240. See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).
</details>

<details>
<summary><b>Is this the original game?</b></summary>

No. It is a port of the fan remake LSD: Revamped, which plays the original game's levels and art. Please support the
original creators and the Revamped project.
</details>

## 📁 Repository layout

```
.
├── app/
│   ├── src/            the port (C99): renderer, game loop, scripting host, audio, menus, save
│   ├── lua/api.lua     Lua implementation of LSD: Revamped's scripting API (bundled in the .wuhb)
│   ├── assets/         Wii U icon / boot images
│   └── build.py        portable build script (devkitPro) -> build/*.wuhb
├── tools/              data converters (Python) + make_data.py
├── third_party/lua/    Lua 5.2.3 (MIT), vendored so a fresh clone builds
├── docs/               BUILDING, DATA, ARCHITECTURE, DEVLOG, formats/, img/
├── LICENSE             MIT
└── THIRD_PARTY.md      credits, licences, disclaimer
```

Why this shape: the **app** and the **tools** are the two halves that run on different machines (console vs. PC) and
evolve together; **third_party** keeps unmodified upstream code apart from ours; **docs** holds everything a reader or
contributor needs, including the format specs that tie the two halves together; and anything derived from game data
(`build/`, `data/`) is ignored by `.gitignore` so it can never be committed by accident.

## 💜 Credits

- **Port**: Cuppax ([@Cuppaxx](https://github.com/Cuppaxx))
- **LSD: Revamped**, **libLSD** and **LSDR-Additional-Dreams**: [Figglewatts](https://github.com/Figglewatts) and
  contributors (MIT)
- **Additional Dreams textures**: Royalty3D, catmilk, sphere, Bor
- **LSD: Dream Emulator** (1998): OutSide Directors Company, Asmik Ace Entertainment, Osamu Sato
- **Lua 5.2.3**: Lua.org, PUC-Rio (MIT)
- **devkitPro, wut, SDL2 for Wii U, libjpeg-turbo**: their respective authors

## 📜 License

The port's code, tools and docs are under the [MIT License](LICENSE). Third-party components and credits:
[THIRD_PARTY.md](THIRD_PARTY.md). LSD: Dream Emulator's data is not part of this project and is not covered by its
licence.
