# Third-party notices, credits and disclaimer

## Disclaimer

**LSD: Revamped for Wii U is an unofficial, non-commercial fan project.** It is not affiliated with, endorsed by or
connected to Asmik Ace Entertainment, OutSide Directors Company, Osamu Sato, Sony Interactive Entertainment, Nintendo,
or the LSD: Revamped project. All trademarks belong to their respective owners.

**No original game data is distributed.** This repository and its release builds (`lsdr-app.wuhb`) contain only the
port's own source code, tools, documentation and screenshots. To play, you build the data folder yourself from your own
clone of the LSD: Revamped repository (see [docs/DATA.md](docs/DATA.md)); that folder contains material derived from
LSD: Dream Emulator and must not be redistributed. The README screenshots show the port running for the purpose of
documenting it.

## LSD: Dream Emulator (1998)

The original PlayStation game by **OutSide Directors Company**, published by **Asmik Ace Entertainment**, created by
Osamu Sato and team. Its levels, textures, models, music, sound effects, movies and art (the files the converters read:
LBD, TIX, TIM, MOM, SEQ/VAB, STR...) are copyrighted by their owners and are **not** included here.

## LSD: Revamped

- https://github.com/Figglewatts/LSDRevamped - MIT License, Copyright (c) 2016 Sam (**Figglewatts**) and contributors.
- The port is a native C reimplementation of LSD: Revamped's behaviour for the Wii U. It was written by reading
  Revamped's C# code, Unity scene/prefab/asset files and shaders (dream system, dream graph, player movement, environment
  and fog, classic PS1 shader, audio, menus, special days) and reproducing them; no Revamped source file is copied
  verbatim, but much of the logic, constants and file layouts are derived from it. The Lua object scripts and the
  `LuaInclude` helpers that run in the game are Revamped's own (MIT); they are converted from your clone at data-build
  time and are not stored in this repository.
- The LSD: Revamped logo (`logo-trans.png` from the Revamped repository, MIT) is used in `app/assets/*.png` (Wii U icon
  and boot images) and in `docs/img/banner.png` / `docs/img/icon.png`.
- The title-screen music and menu sounds the app plays are Revamped's, converted from your clone (not distributed).

## libLSD

- https://github.com/Figglewatts/libLSD - MIT License, Copyright (c) Sam (**Figglewatts**).
- `app/src/lsd.c` and the Python converters read LBD / TMD / TIX / TIM / MOM / TOD data following libLSD's format
  descriptions (C and Python ports of the needed parts, written for this project).

## LSDR-Additional-Dreams

- https://github.com/Figglewatts/LSDR-Additional-Dreams - MIT License, Copyright (c) 2024 Figglewatts.
- Used only through the data pipeline (`tools/addl.py`): the 14 additional dreams are baked from your own clone of the mod.
- Texture credits from the mod: **Royalty3D** (@royalty3D), **catmilk** (@catmilk_catmilk), **sphere** (@pinkosand),
  **Bor** (@PyotrAlexRoma).

## Lua 5.2.3

- https://www.lua.org/ - MIT License, Copyright (C) 1994-2013 Lua.org, PUC-Rio.
- Vendored unmodified in [`third_party/lua/`](third_party/lua/) (licence in `third_party/lua/LICENSE`).

## Toolchain and libraries linked into the app (not in this repository)

| Component | Use | License |
|---|---|---|
| [devkitPro / devkitPPC](https://devkitpro.org/) | PowerPC cross compiler, `elf2rpl`, `wuhbtool` | GPL (tools; no runtime code beyond newlib / libgcc, which carry linking exceptions) |
| [wut](https://github.com/devkitPro/wut) | Wii U runtime / system headers | zlib |
| [SDL2 for Wii U](https://github.com/devkitPro/SDL) (`wiiu-sdl2`) | video output, audio device, controllers | zlib |
| [libjpeg-turbo](https://libjpeg-turbo.org/) (`ppc-libjpeg-turbo`, libturbojpeg) | decoding the movies' MJPEG frames | IJG / BSD-3-Clause / zlib |

Binary releases of the app therefore include code from wut, SDL2 and libjpeg-turbo under their licences.
libjpeg-turbo asks binary distributions to state: *"This software is based in part on the work of the Independent JPEG
Group."*

## Data-pipeline tools (run on your PC, not distributed)

Python 3, [Pillow](https://python-pillow.org/) (MIT-CMU) and [FFmpeg](https://ffmpeg.org/) (LGPL/GPL) are used by
`tools/` to convert your own copy of the data.

## The port

Wii U port, renderer, engine, tools and documentation: **Cuppax** ([@Cuppaxx](https://github.com/Cuppaxx)), MIT License
(see [LICENSE](LICENSE)).
