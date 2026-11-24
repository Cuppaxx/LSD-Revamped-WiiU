# Game data: build it from your own copy

The port ships **no game data**. Everything it plays (levels, textures, models, music, sounds, scripts, movies) is
converted on your PC from **your own clone** of LSD: Revamped, which includes the original LSD: Dream Emulator data
through git-lfs. The result is a `data/` folder that goes on the Wii U's SD card. That folder is for your own use:
**do not share or upload it**.

```
LSDRevamped clone ──┐
(+ Additional Dreams)├──> tools/make_data.py ──> data/ ──> SD:/wiiu/apps/lsdr/data/
                     │                                      + lsdr-app.wuhb in SD:/wiiu/apps/
```

## 1. Requirements (PC: Windows, Linux or macOS)

| Tool | Why |
|---|---|
| [Git](https://git-scm.com/) + [Git LFS](https://git-lfs.com/) | the Revamped repository keeps its assets in LFS (~10 GB checked out) |
| Python 3.8+ with [Pillow](https://pypi.org/project/pillow/) (`pip install pillow`) | the converters |
| [FFmpeg](https://ffmpeg.org/) on `PATH` | title music (.ogg -> .wav), Additional Dreams sounds, movies |
| a Japanese TrueType font (optional) | renders the Japanese text-day pages; found automatically on Windows (MS Gothic, Yu Gothic, Meiryo), Noto Sans CJK on Linux, or set `LSDR_JP_FONT=/path/font.ttc` |

## 2. Clone the sources (about 10 GB)

```sh
git lfs install
git clone https://github.com/Figglewatts/LSDRevamped.git
# optional: 14 more dreams
git clone https://github.com/Figglewatts/LSDR-Additional-Dreams.git
```

If the clone was made without LFS, run `git lfs pull` inside it. `make_data.py` refuses LFS pointer files.

## 3. Convert

```sh
python tools/make_data.py --revamped path/to/LSDRevamped \
                          --addl path/to/LSDR-Additional-Dreams \
                          --out build/data --movies
```

| Option | Effect |
|---|---|
| `--revamped DIR` | **required**: the LSDRevamped clone (the folder that contains `LSDR/`) |
| `--addl DIR` | the Additional Dreams clone; without it you get the 14 original dreams only |
| `--out DIR` | output data folder (default `build/data`) |
| `--movies` | transcode the 38 FMV movies (opening, special days, ending, chart faces) to `.mjv` + `.wav` (~250 MB, a few minutes) into `<out>/MOVIE` |
| `--music-all` | copy every music style (~800 MB) instead of the standard style only (~100 MB); the game falls back to an available song when a style is missing |
| `--no-audio` | skip sound effects / footsteps / positional DreamAudio |
| `--no-special` | skip special days, text days and the credits roll |
| `--texts-en FILE` | install an English text-day file as `SPDAY/texts_en.txt` (see below) |
| `--stage DIR` | scratch folder for intermediate files (default `build/stage`) |

`make_data.py` runs `tools/convert.py <repo> <out> --copy --music --audio --special [--movies] --mod <addl>` and then
copies the text-day pages and movies into the output folder. The individual converters can also be run by hand; their
usage is at the top of each file:

| Script | Produces | Format doc |
|---|---|---|
| `convert.py` | `dreams.txt` (dream table, environments, spawn points, entities, links, graph, songs), `STGxx/` LBD/TIX copies, per-dream `water.bin`, `models.bin`, the dream Lua scripts, `LuaInclude/`, `ETC/`, `MENU/`, `sky/` | header of `convert.py` |
| `models.py` (imported by convert.py) | `STGxx/models.bin`: animated MOM models | [MODELS_FORMAT.md](formats/MODELS_FORMAT.md) |
| `sky_assets.py` (imported) | `sky/{sun,clouds,stars}.bin` | header of `sky_assets.py` |
| `addl.py` | `dreams_addl.txt`, `ADD00..ADD13/{world.bin,amodels.bin}`, `ADDTEX/`, `ADDLUA/`, `ADDSND/` | [ADDL_FORMAT.md](formats/ADDL_FORMAT.md) |
| `audio_assets.py` | `audio.txt`, `SFX/`, footstep and DreamAudio tables merged into `dreams*.txt` | [AUDIO_FORMAT.md](formats/AUDIO_FORMAT.md) |
| `special_assets.py` | `specials.txt`, `SPDAY/`, `CREDITS/`, `MOVIE/` | [SPECIAL_FORMAT.md](formats/SPECIAL_FORMAT.md) |

## 4. Copy to the SD card

```
SD:/
└── wiiu/
    └── apps/
        ├── lsdr-app.wuhb          the app (release download or your own build)
        └── lsdr/
            ├── data/              <- the whole output folder of make_data.py
            │   ├── dreams.txt
            │   ├── dreams_addl.txt    (Additional Dreams)
            │   ├── audio.txt  specials.txt
            │   ├── STG00 … STG13/     the original dreams
            │   ├── ADD00 … ADD13/  ADDTEX/  ADDLUA/  ADDSND/
            │   ├── ETC/  MENU/  sky/  SFX/  SPDAY/  CREDITS/  LuaInclude/
            │   └── MOVIE/             optional, only read from the SD card
            ├── save.txt           written by the game
            └── log.txt            written by the game (diagnostics)
```

Sizes: about 180 MB with the standard music, + ~250 MB of movies, + ~700 MB with `--music-all`.

The app looks for every file in `SD:/wiiu/apps/lsdr/data/` first and then inside the `.wuhb`. The release `.wuhb`
only contains the port's Lua API layer (`api.lua`); do not put an `api.lua` on the SD card (it would override the app's
own, newer copy).

### All-in-one wuhb (personal use only)

If you prefer a single file, `python app/build.py --data build/data` bundles the data folder into `build/lsdr.wuhb`
(~190 MB). That file contains game data: never share it. Movies are still read from the SD card only.

## English text days

The special "text days" of the original are Japanese text pages. The app shows them as the original pages
(`SPDAY/*.TIM`, from your data) unless a `SPDAY/texts_en.txt` file is present, in which case it renders that English
text in the menu font. The format is a plain list of blocks:

```
text <id>        the page id from specials.txt (e.g. 010D)
| first line     every line of the page, prefixed with "| "
| ...
end
```

This repository does not ship a translation file; pass your own with `--texts-en`.

## Updating

Re-run `make_data.py` after updating the port: the converters and the app evolve together, and an old `dreams.txt`
may lack lines a newer app expects. Movies that already exist in the stage folder are not re-encoded.
