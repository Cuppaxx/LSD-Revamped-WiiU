# Architecture

LSD: Revamped is a Unity game. Unity has no Wii U target, so this port is a **native reimplementation in C** (about
7.4k lines in `app/src`, plus a 600-line Lua API layer and 3.7k lines of Python converters). It reproduces Revamped's
behaviour from its C# code, scenes, prefabs and shaders, and draws everything with a **software PS1-style rasterizer**
uploaded to the screen through one SDL2 streaming texture.

```
         PC (once)                                         Wii U (every run)
┌──────────────────────────┐                ┌──────────────────────────────────────────────────────┐
│ LSDRevamped clone        │                │ main.c  game loop, fades, day/dream state machine     │
│ (+ Additional Dreams)    │                │  ├─ menu.c / splash.c / journal.c / special.c  UI     │
│   Unity YAML, LBD, TIX,  │  tools/*.py    │  ├─ dream.c   dream table + dream graph               │
│   MOM, ogg/wav, webm ... ├──────────────► │  ├─ level.c / meshworld.c   load + lay out a dream    │
│                          │  data/ folder  │  ├─ player.c / collide.c / input.c   walking          │
└──────────────────────────┘  on SD card    │  ├─ script.c + lua/api.lua + Lua 5.2   entity logic   │
                                            │  ├─ model.c   animated objects                        │
                                            │  ├─ render.c → raster.c (3+ threads)  ── SDL texture │
                                            │  ├─ sky.c     gradient sky, sun, clouds, stars        │
                                            │  ├─ audio.c / sfx.c / dreamaudio.c / video.c  sound   │
                                            │  └─ save.c    SD:/wiiu/apps/lsdr/save.txt             │
                                            └──────────────────────────────────────────────────────┘
```

## Data pipeline (`tools/`)

The app never parses Unity files. The converters read the Revamped project (Unity YAML assets and prefabs, `.meta`
GUID maps) and write compact, mostly **text tables + big-endian binaries** that the PowerPC reads directly:

- `convert.py` walks `OriginalDreams.asset` and each dream prefab: dream records, LBD chunk lists, environments per
  day kind (fog, sky colours), spawn points, every entity (Lua triggers, link triggers, teleports, interactive objects,
  targets) with world transform and box collider, the 19x19 dream graph, song libraries, water UV overrides. Lua
  scripts are copied with MoonSharp's lambda syntax (`|a| expr`) rewritten to plain Lua 5.2. Output: `dreams.txt` +
  per-dream folders (`STGxx/`) with the original LBD/TIX files.
- `models.py` reproduces Revamped's MOM importer (TMD meshes + TOD animations into Unity hierarchies and clips) and bakes
  `models.bin` per dream.
- `addl.py` bakes the Additional Dreams mod, which is built from ordinary Unity meshes rather than LBD tiles: chunked
  triangle soup with materials (`world.bin`), textures (`ADDTEX/*.tex`), models and clips (`amodels.bin`), scripts.
- `audio_assets.py` (footstep tables, SFX bank, positional DreamAudio records), `special_assets.py` (special days,
  movies as MJPEG `.mjv` + WAV, credits), `sky_assets.py` (sun/cloud/star textures).

Formats are documented in [`docs/formats/`](formats/). Original PS1 files (LBD, TIX, TIM, MOM) stay in their original
little-endian layout and are parsed on the console by `lsd.c`, a C port of the needed parts of libLSD.

## Renderer (`render.c`, `raster.c`, `sky.c`, `model.c`, `meshworld.c`)

1. **Projection (CPU, main + worker threads).** `level_render` walks every LBD chunk of the dream (laid out like
   Revamped's `LBDFastMeshSystem`: honeycomb or stacked), culls by view distance and frustum, transforms, clips at the
   near plane and projects triangles into two lists: opaque and 50 % blended. Mesh dreams project their visible chunks
   in parallel (`raster_parallel`, per-thread lists merged). Models go through the same path with a 3x4 matrix
   (`render_mesh_xf`).
2. **Rasterization (banded, multithreaded).** `raster_render` splits the frame into 16-row bands that the main thread
   and SDL worker threads take dynamically (render threads 1-4 in the options). Each band paints its sky rows, then draws
   both lists clipped to the band: fixed-point (16.16) scanlines, Gouraud colour with PS1 `x2` modulation, z-buffer,
   colour-0 cutout, 50 % blending, and two texture paths: the PS1-VRAM-style texture atlas (about 2048x512) for LBD tiles and a generic
   power-of-two texture path (wrap, cutout, alpha blend) for mesh dreams.
3. **PS1 look.** Vertex snapping to the pixel grid, Revamped's half-affine texture warp (`_AffineIntensity`,
   option 0-1: the per-triangle weight decides between a plain affine loop and exact u,v every 8 pixels), fog as
   Revamped's stepped `LSDRFog` (additive or subtractive, 256-entry lookup), water by palette cycling, and the
   environment sky: gradient plus sun (with sunburst), clouds and stars drawn per band.
4. **Present.** The frame (320x240 by default; 240/300/360/480 lines, 4:3 or 16:9) is uploaded as an SDL streaming
   texture and scaled fit / integer / stretch to the 1280x720 output, nearest-neighbour. Menus draw into their own
   240-line frame.

## Game logic (`main.c`, `dream.c`, `level.c`, `player.c`, `collide.c`)

- **Days and the dream graph** follow Revamped's `DreamSystem` / `DreamSequence`: a day lasts the start dream's length
  (or a random 60-360 s); walking into a wall for 1.7 s links to a weighted random dream; at the end of the day the
  graph position is computed from the area contributions (LBD tiles walked) and entity contributions (scripts), and the
  next day's dream comes from the 19x19 graph. Special days, the grey man, flashbacks and the day-365 ending hang off the
  same state machine (`special.c`, `flashback.c`).
- **Player**: Revamped's `PlayerMovement` constants (capsule radius 0.08, step 0.2, 48° slopes, walk/run speeds per
  dream, head bob, gravity), FPS and TANK schemes. **Collision** is a static triangle world on a 1x1 grid (`collide.c`)
  plus entity colliders; ground queries also return the triangle for footstep sounds.

## Scripting (`script.c`, `app/lua/api.lua`, Lua 5.2.3)

Revamped's objects are Lua scripts written against MoonSharp proxies of Unity objects. The port embeds stock Lua 5.2.3
(base, package, table, string, math, coroutine, bit32) and reimplements that API in Lua (`api.lua`) on top of a small
C host:

- C side: the entity table, trigger enter/exit against oriented boxes, proximity/view interaction, transforms pushed
  from Lua (`_set_xform`), animation control, audio and video calls, dream transitions, graph contributions.
- Lua side: `Vector3`, `GetEntity`, `this`, `GameObject`, `player`/`PlayerMovement`, a port of
  `LuaAsyncActionRunner` (`Do/Then/Until/WaitUntil/ThenLoop...`), `Condition`, `DreamSystem`, `Random`, `Colors`,
  `DreamAudio`, `VideoClip`... Unknown members return logged no-ops so a missing API call never stops a script.
- Scripts run lazily within 30 tiles (original dreams) or at load (mod dreams, Unity `Start`).

## Audio (`audio.c`, `sfx.c`, `dreamaudio.c`, `video.c`)

One SDL device (48 kHz stereo) opened at boot. The callback mixes a looping music WAV (resampled), 24 mono voices with
pitch and left/right gain (12 positional DreamAudio, 10 for 2D sounds such as footsteps and link sounds, 2 for the
menu) and the movie soundtrack, then clamps. Footsteps pick their sound from the LBD tile key under the player
(original dreams) or the texture rect under the feet (mod dreams). DreamAudio uses Unity's linear roll-off and
equal-power panning from the camera.

## Front end (`menu.c`, `splash.c`, `journal.c`, `special.c`, `save.c`)

A boot splash, the title screen (built from the original `TITLE.TIM` art), pause menu, an options hub (gameplay,
controls with rebinding, audio, graphics, dreams, credits), dream select, dream chart (`HGRAPH.TIM`), journal and
credits roll, all drawn with a 5x7 pixel font into a 320x240 or 426x240 frame, with touch support on the GamePad.
`special.c` plays the intro, special days (movie or text) and the ending; `video.c` decodes MJPEG with libturbojpeg.
`save.c` keeps options and journal progress in a key/value text file written atomically (`save.tmp` + rename).

## Files

| File | Role |
|---|---|
| `main.c` | boot, data lookup (SD card, then wuhb), test hooks, game loop, fades and transitions |
| `lsd.c/h` | LBD / TMD / TIX / TIM / MOM loaders (libLSD port), texture atlas |
| `dream.c/h` | `dreams.txt` / `dreams_addl.txt` parser, environment choice, graph |
| `level.c/h`, `meshworld.c/h` | load and lay out an LBD dream / a mesh dream |
| `render.c/h`, `raster.c/h` | projection, culling, fog, rasterizer and thread pool |
| `sky.c/h` | gradient sky, sun, clouds, stars |
| `model.c/h` | `models.bin` / `amodels.bin`, animation clocks, model drawing |
| `collide.c/h`, `player.c/h`, `input.c/h` | collision world, movement and camera, controllers |
| `script.c/h` | Lua host and entity layer |
| `audio.c/h`, `sfx.c/h`, `dreamaudio.c` | mixer, SFX bank and footsteps, positional sounds |
| `video.c/h`, `special.c/h`, `flashback.c/h` | movies, special days and endings, flashbacks |
| `menu.c/h`, `splash.c`, `journal.c/h` | front end |
| `save.c/h` | save file |
