# Building the app

The app is plain C99 + SDL2 for the Wii U, built with devkitPro. There is no Makefile: `app/build.py` compiles every
file, links, and packs the `.wuhb` (Aroma homebrew bundle).

## 1. Install devkitPro

Follow <https://devkitpro.org/wiki/Getting_Started>, then install the Wii U packages:

```sh
# Windows: run these in the devkitPro MSYS2 shell; Linux/macOS: prefix with sudo, use dkp-pacman if that is its name
pacman -S --needed wiiu-dev wiiu-sdl2 ppc-libjpeg-turbo
```

`wiiu-dev` brings devkitPPC, wut, `elf2rpl` and `wuhbtool`; `wiiu-sdl2` the SDL2 port; `ppc-libjpeg-turbo` the
JPEG decoder used by the movie player.

## 2. Build

```sh
git clone https://github.com/Cuppaxx/lsdr-wiiu.git
cd lsdr-wiiu
python app/build.py
```

Output: `build/lsdr-app.wuhb` (about 1.2 MB). This **app-only** build contains no game data and is the file you can
share; it needs the data folder on the SD card ([DATA.md](DATA.md)). Without data it shows a "NO GAME DATA FOUND"
screen.

| Option / variable | Meaning |
|---|---|
| `DEVKITPRO` | devkitPro root. Default `C:\devkitPro` on Windows, `/opt/devkitpro` elsewhere |
| `LUA_DIR` | Lua 5.2.3 sources, default `third_party/lua` (vendored) |
| `LSDR_EXTRA_CFLAGS` | extra compiler flags, e.g. `-DLSDR_START_FILE=\"t.txt\" -DLSDR_LOG_NAME=\"t.log\"` for test builds |
| `--out DIR` | output folder (default `build/`; objects in `build/obj/`) |
| `--data PATH/data` | personal all-in-one build `build/lsdr.wuhb` with your data folder bundled (contains game data: do not share) |
| `--name NAME` | output file name |
| `--jobs N` | parallel compiles (default: all cores) |
| `--clean` | rebuild everything |

Builds are incremental (an object is rebuilt when its `.c` or any header in `app/src` is newer).

## 3. Run

- **Wii U (Aroma):** copy the `.wuhb` to `SD:/wiiu/apps/` and the data folder to `SD:/wiiu/apps/lsdr/data/`; the app
  appears on the Wii U Menu.
- **Cemu (2.x):** load the `.wuhb` with *File > Load*. Cemu has no real SD card mount for homebrew, so the app falls back
  to Cemu's emulated SD card: put the data folder at `<Cemu user folder>/sdcard/wiiu/apps/lsdr/data/` (on Windows
  `%APPDATA%\Cemu\sdcard\...`; also the location of `save.txt` and `log.txt`), or use an all-in-one `--data` build.

## 4. Logs and test hooks

`SD:/wiiu/apps/lsdr/log.txt` gets a line per second with fps, projection and raster times, triangle counts and the
player position; useful for performance reports.

For development, a `start.txt` in the data folder switches on test hooks: `<dream index>` starts that dream directly
(saving is then disabled), plus tokens such as `nolink`, `demo` (auto-walk), `debug` (X overlay, Y free-fly, D-pad
dream cycling), `log`, `cam X Y Z YAW PITCH`, `tp X Y Z`, `today N`, `shots T ...` (dump frames to `shotN.bmp`),
`keys T:K ...` / `pad T:NAME:SECS ...` (scripted input), `menu`, `splash`, `tank`, `footdbg`. See `main.c` and the
[development log](DEVLOG.md) for the full list. Never ship a `start.txt`.

## Source layout

See [ARCHITECTURE.md](ARCHITECTURE.md) for what each file does.
