# Special days, flashbacks, movies, ending, intro and credits

Written by `tools/special_assets.py`:

```
python -I tools\special_assets.py <repo> <outdir> [--q 8] [--no-movies] [--force] [--only SPDAY01A,...] [--jobs 4]
python -I tools\special_assets.py src tools\out_special          (the staged copy; about 30 s with 6 jobs)
python -I tools\convert.py <repo> <out> ... --special              (opt-in hook: runs it with convert's <out>)
```

Needs Python 3, PIL, `ffmpeg` on PATH (the same lookup convert.py uses for the menu sounds, `shutil.which('ffmpeg')`;
here it's the chocolatey 7.0.2 build), and a Japanese TrueType font for the text pages (`C:\Windows\Fonts\msgothic.ttc`,
falling back to Yu Gothic or Meiryo).

Everything below comes from these Revamped sources, quoted by file so it can be checked:
`Assets/Scripts/Dream/DreamSystem.cs`, `DreamSequence.cs`, `Game/GameSaveData.cs`, `Game/IntroVideoController.cs`,
`UI/Graph/UIGraph.cs`, `UI/Credits/*.cs`, `Visual/FlashbackImageEffect.cs` + `Shaders/FlashbackImageEffect.shader`,
`Torii/UI/ToriiFader.cs` + `Shaders/FadeBlend.shader`, `Entities/Dream/Greyman.cs`, SDK `Runtime/Data/*SpecialDay*.cs`,
`DreamJournal.cs`, `GraphContribution.cs`, `Entities/GraphContributor.cs`, `Entities/MovieClip.cs`,
`DreamControl/{Text,Video}SpecialDayControl.cs`, scenes `intro.unity`, `text_dream.unity`, `video_dream.unity`,
`titlescreen.unity`, `loading_screen.unity`, and the data `Resources/Original/OriginalDreams.asset`, `SpecialDay/**`.

## 0. Output files

| file | what | size |
|---|---|---|
| `specials.txt` | all tables (special days, ending, chart face, intro, MovieClip entities, constants) | 16 KB |
| `SPDAY/SPDAYnnX.TIM` | the 47 original PS1 text-day images (IMG1/IMG2), 320x240 16-bit TIM | 7.2 MB |
| `SPDAY/TXT_<id>.TIM` | the same 47 texts rendered the Revamped way (white Japanese text centred on black), 4-bit TIM 320x240 | 1.8 MB |
| `SPDAY/texts.txt` | UTF-8 text of each text day (reference only) | 14 KB |
| `MOVIE/<NAME>.mjv` | 38 movies, MJPEG frames in the container of section 3 | 151 MB |
| `MOVIE/<NAME>.wav` | their audio, 22050 Hz mono s16le PCM WAV | 62.5 MB |
| `MOVIE/movies.txt` | per-movie frames / durations / sizes (verification output) | |
| `CREDITS/credits.txt`, `CREDITS/*.TIM` | the credits roll text and the 6 section images (72 px wide, 16-bit TIM) | 59 KB |
| `preview/*.png` | decoded-back previews: text page vs original TIM, mid frame of every movie, credit images. Not for shipping | |

Every TIM loads with the app's existing `lsd_decode_tim` (4/8/16-bit, first CLUT, opaque ARGB).
Paths in `specials.txt` are relative to the data root (`SD:/wiiu/apps/lsdr/data/`), so copy the whole staged folder
there. Like the rest of `data/`, it's original game content: user-supplied, not for redistribution.

## 1. `specials.txt`

A line-based text file like `dreams.txt`. `#` lines are comments. Tokens are separated by single spaces.

```
lsdr-specials 1
day <day> <asset> <kind> <always> <dyn> <upper> <n>
choice <kind> <asset> <always> <dyn> <upper> <movie|-> <tim|-> <page|->     (n of these follow each day line)
ending <movie>
chartface <n> <movie> x n
intro <n> <movie> x n
movieclip <dream folder> <entity ID> <movie>
unreferenced <n> <movie> x n
const <NAME> <value...>
```

* `day`: an entry of `DreamJournal.SpecialDays` (a `SerializableDictionary<int, AbstractSpecialDay>`: `_keys` is hex of
  little-endian int32 day numbers, `_values` the asset refs in the same order). Day numbers are the journal's 1..365
  `DayNumber`. 41 days: 2 7 14 15 21 28 35 42 43 49 56 63 70 77 81 84 91 98 105 112 119 120 126 134 141 148 155 162
  169 176 183 190 197 202 204 259 267 284 308 328 358. Day 365 is not a special day. Several days share a set, e.g.
  `SP_2_84_169_358` is days 2, 84, 169 and 358.
  `kind` is `random` (`RandomSpecialDay`), `video` or `text`. In the original journal every day is `random` with 6
  choices: set NN has 2 videos (`NNA`, `NNB` = `FILM/SPDAYNNA/B`) and 4 texts (`NNC..NNF`). One data quirk: `SP_63_148_284`
  lists `010E` twice and never `010C` (so 010E is twice as likely and 010C never shows). This is kept as in Revamped.
* `always` (`AbstractSpecialDay.Always`): play it even when the "special days" option is off. All 0 in the original data.
* `dyn upper` (`AbstractSpecialDay.Contribution`): the day's graph contribution. All 0,0 in the original data. NOTE:
  Revamped logs the contribution of the **day entry** (the random set), not of the chosen child.
* `choice video`: `movie` = a name in `MOVIE/` (`<movie>.mjv` + `<movie>.wav`).
* `choice text`: `tim` = the original PS1 image of that text (vertical Japanese, the PS1 look), `page` = the Revamped
  rendering of `TextSpecialDay.Text` (`TextJa` if not blank, else `TextEn`; all original texts are Japanese only).
  **Recommendation:** show `tim` by default. It is legible at 320x240, authentic, and needs no font. `page` is what
  Revamped shows, so it could be an option.
* `ending`: `DreamJournal.EndingVideo` (= `ENDING`).
* `chartface`: `DreamJournal.ChartFaceVideos`, 20 movies (SPDAY01A..SPDAY10B, about 11 minutes), see 6.
* `intro`: `IntroVideoController.IntroVideos` in `intro.unity` (OPENINGA..G), see 7.
* `movieclip`: SDK `MovieClip` entities in the dream prefabs (STG02 PetalsVideoClip=EVENT4, STG04 FerrisWheelVideo=EVENT2,
  STG05 PlaneVideoClip=EVENT3, STG05 FlyingVideoClip=EVENT5, STG09 GearsVideoClip=EVENT1). Lua does
  `GetEntity(id).VideoClip.Play(Unity.ColorRGB(r,g,b))` (bell.lua, ferrisWheel.lua, ghost.lua, plane.lua, trashBin.lua,
  orrery.lua), see 5.
* `unreferenced`: EVENT6 (a byte-identical copy of EVENT5). It's still transcoded.
* `const`: the timing constants used below.

## 2. The fader (needed by every flow below)

Revamped has one full-screen fader (`ToriiFader`) with a colour, an alpha `a` and a blend mode, drawn over everything,
including UI and video. `FadeIn(colour, secs)` sets the colour (keeping the current `a`) and moves `a` linearly to 1
over `secs`. `FadeOut(secs)` moves it to 0. `SetFade(colour)` sets the colour **including its alpha 1** at once. The
completion callback runs at the end. A new forced fade first runs the old fade's callback. `forced:false` fades are
dropped if a fade is running.

Per pixel, with `c` = fade colour and `a` = alpha (channels 0..1, saturating):

| blend | result | used for |
|---|---|---|
| Alpha | `dst*(1-a) + c*a` | default; zone-colour day end, videos, title |
| Add | `dst + c*a` | link flashes (non-black), the white random ending |
| Subtract | `dst - (1-c)*a` (darkens towards `c`; black → `dst - a`) | black links, falls, day start, black random ending |

The blend mode is sticky: it stays until someone calls `SetBlend`, and `LoadDream`'s final fade-out resets it to Alpha.
The app's current `fade_frame` is the Alpha case only. Add and Subtract are needed for faithful random endings and links.

## 3. Movie container `.mjv` + `.wav`

All 38 sources are VP8 320x240 @ 15 fps with Vorbis 37800 Hz stereo (PS1 STR/XA rips). They are transcoded to:

* **Video**: one baseline JFIF JPEG per frame (ffmpeg `-c:v mjpeg -q:v 8 -pix_fmt yuvj420p -huffman optimal`, `fps=15`,
  320x240, 4:2:0, full-range YCbCr), each frame a complete stand-alone JPEG (own DQT/DHT, about 600 bytes of headers).
* **Audio**: separate `<NAME>.wav`, RIFF PCM s16le, 22050 Hz, mono (the app already plays WAV through SDL).

Byte layout, all integers **big-endian**:

```
off size
0   4    magic "LMJV"
4   u16  version = 1
6   u16  width  (320)
8   u16  height (240)
10  u16  fps numerator (15)
12  u16  fps denominator (1)
14  u16  flags: bit0 = a .wav with the same base name exists
16  u32  frame_count N
20  u32  max_frame_bytes (largest JPEG, at most 32 KB here: size one read buffer with it)
24  u32  audio sample rate of the .wav (22050)
28  u32  audio sample count of the .wav (mono samples; duration = count / rate)
32  u32  offset[N+1]   absolute file offsets; frame i = bytes [offset[i], offset[i+1]); offset[N] = file size
32+4(N+1)  JPEG data, frame after frame
```

**C decoder (libjpeg-turbo is already in `$DEVKITPRO/portlibs/ppc`: `libturbojpeg.a` + `turbojpeg.h`):**

```c
// open: fread 32-byte header, check magic/version, fread (N+1)*4 offsets, byte-swap on LE hosts (the Wii U is BE: no swap)
tjhandle tj = tjInitDecompress();
uint8_t *buf = malloc(max_frame_bytes);
// per frame i:
fseek(f, off[i], SEEK_SET); fread(buf, 1, off[i+1] - off[i], f);
tjDecompress2(tj, buf, off[i+1] - off[i], (uint8_t *)argb, 320, 320 * 4, 240, TJPF_BGRA /* = ARGB8888 bytes on BE */,
              TJFLAG_FASTDCT | TJFLAG_FASTUPSAMPLE);
```

Pick the pixel format that matches the app's frame buffer (`0xAARRGGBB` words on big-endian = bytes A,R,G,B =
`TJPF_ARGB`; check against the SDL texture format the renderer uses). Alternative without turbojpeg: SDL2_image is also
in portlibs (`IMG_Load_RW` on a `SDL_RWFromConstMem` of the frame), at the cost of a surface allocation per frame.

**Playback clock:** audio is the master. Start the WAV and the video together. Show frame
`i = floor(audio_samples_played * 15 / 22050)`. If the WAV is shorter than the video (SPDAY07A is 1.0 s shorter, the
others match within 0.1 s), keep counting on the wall clock after the audio ends. The movie ends when
`i >= N` (Unity `loopPointReached`). Decode only when `i` changes (15 decodes per second at any app frame rate) and drop
frames when late (Unity `SkipOnDrop` 1). Present letterboxed "fit inside" (`VideoPlayer.AspectRatio` FitInside on the
camera near plane): the movies are 4:3, so they fill the 320x240 frame, with pillarbox bars in the 16:9 mode. Black
background. Video audio volume = the **music** volume option (`VideoPlayerVolumeSetter`). Music is stopped while a
movie plays.

**Cost:** about 110 KB/s video + 43 KB/s audio read from SD, average 100 KB/s per clip (24..205 KB/s). Decode measured on
the PC: 0.44 ms/frame (x86 libjpeg-turbo with SIMD, through PIL). Espresso has no AltiVec, so turbojpeg runs its plain C
paths. Estimate **4-6 ms per 320x240 frame** on a 1.24 GHz core with FASTDCT/FASTUPSAMPLE, about 6-9 % of one core at
15 fps (not measured on hardware: log the `tjDecompress2` time on first use). The 3D renderer is idle during a movie,
so this is free in practice. Memory: one 32 KB read buffer + the 300 KB ARGB frame.

Per-clip sizes are in `MOVIE/movies.txt`. Examples: ENDING 32 s = 3.1 MB + 1.4 MB, SPDAY01A 32 s = 3.6 + 1.4 MB,
OPENINGA 71 s = 11.3 + 3.1 MB, SPDAY12A 13 s = 1.0 + 0.6 MB. Total 151 MB video + 62.5 MB audio for 38 clips
(25 minutes). `--q 12` gives about 25 % less video, `--q 5` about 40 % more. Verified: every frame of every file starts
with SOI and ends with EOI, the middle frame of each decodes with PIL/libjpeg as a baseline (SOF0) JPEG, and
`preview/movies_mid_frames.png` shows all 38. Why not a custom palettised/delta codec: the PS1 movies are MDEC (DCT)
full-colour footage with noise and dissolves (SPDAY05B is pure static). A palette loses colour, deltas barely help, and
turbojpeg is already built for the target.

## 4. Special days

**Where:** `DreamSystem.BeginDream()`, the "start dream" button on the title (CONTINUE in the port), before choosing
a dream:

```
CurrentSequence = new day sequence (empty area/entity contribution lists)
random texture set for the day (as for a normal day)
if specials has day == save.day and (option SpecialDaysEnabled (default ON) or day.always):
    fader: FadeIn(black, 1 s)  (blend unchanged, normally Alpha at the title)
    on faded: HandleDay  -> for random: pick one choice uniformly (duplicates count twice)
              then log an AREA contribution (day.dyn, day.upper) into CurrentSequence   (0,0 in the original data)
    return            (no dream is loaded, no day timer, CurrentDream stays null)
else BeginPlayableDream(normal dream)
```

**Video choice** (`VideoSpecialDay.HandleDay` → scene `video_dream`): black background, fader `FadeOut(black, 0.5 s)`
(from the black of the previous fade), then start the movie (`VideoSpecialDayControl.BeginVideoDay`). Skip: A or + on
the GamePad (Unity `buttonSouth` / `start`; on the keyboard any key), only while the movie is playing and only once.
Skip and natural end do the same thing: `VideoFinished()` then `EndDream()` (below). No pause menu.

**Text choice** (`TextSpecialDay.HandleDay` → scene `text_dream`): black screen with the text (port: draw the `tim`
or `page` TIM full screen), fader `FadeOut(black, 1 s)`. There is **no timeout**: it stays until A or + is pressed
(any key on a keyboard), then `EndDream()`. The press is accepted from the first frame, even during the fade-in.
Revamped layout for `page`: Unity UI Text, white, MiddleCenter, best fit 10..40 on a 1280-wide reference, rect inset
100/60 reference px → 25/20 px at 320x240. Baked with MS Gothic 12 px (11 or 10 px when it would not fit), char-wrapped
like Unity's horizontal wrap, 14 px line pitch.

**EndDream from a special day** (`endDream(fromFall:false, timedOut:false)`):

```
if save.day == 365: ending = ENDING                 (never true for the original table, kept for completeness)
save: append CurrentSequence as a finished day (graph position = evaluate(area=[(0,0)], entity=[]) = (0,0)),
      day += 1 (366 -> 1 and year += 1), save to disk; flashbackDisabledForDays-- if > 0
fade colour: black (CurrentDream is null, so not "in a dream")
FadeIn(black, 1 s) with blend Alpha (FADE_SECS_CUTSCENE because the active scene is video_dream / text_dream)
on faded: ending ? play ending (8.) : return to title
```

So a special day counts as a full day at chart position (0,0), and tomorrow's dream comes from `graph[0,0]`.
Revamped **always returns to the title after any day**. If the port chains days without the title, run the next
`BeginDream` at this point instead, with the same fades.

**Settings:** add an option "special days" (default ON) to the save. `Always` overrides it.

## 5. Movies played by dream objects (MovieClip, `DreamSystem.PlayVideo(clip, colour)`)

Called from Lua as `GetEntity(ID).VideoClip.Play(Unity.ColorRGB(r,g,b))` (red in 5 scripts, black in trashBin.lua).
The port needs a `VideoClip` field on those entity tables. Map the entity ID to a movie with the `movieclip` lines:

```
playingVideo = true; stop music; stop the flashback loop and the day timer
fader FadeIn(colour, 1 s, forced:false)  (current blend)  -> on faded: show the video screen, FadeOut(black, 0.5 s)
  -> start the movie; on end or skip: VideoFinished() (playingVideo = false); EndDream()
```

`EndDream()` here is the normal day end: day+1 with the current sequence (its real contributions; position from them),
fade black 1 s (cutscene scene), then title (or the ending on day 365). Inside a flashback the same call ends the
flashback without advancing the day (9.). `endDream` ignores calls while `playingVideo` is set.

## 6. Chart face (`UIGraph.OnEnable` / `showFace`)

When the dream chart is opened (title or pause), if `!save.seenChartFace`, the journal has chart-face videos, and all four
face points `(-1,1) (1,1) (0,0) (0,-3)` are among the positions of the finished days:
wait 1.5 s with input blocked, then highlight the squares at those points one at a time, 1.2 s apart (in the order those
positions first appear in the history), then set `seenChartFace = 1`, save, and `PlayVideos(chartface list)`:

```
stop music; FadeIn(black, 0.5 s) -> video screen; FadeOut(black, 0.5 s) -> play video 0
each video: on end or skip -> FadeIn(black, 0.5 s) -> if more: FadeOut(0.5 s) and play the next ; else return to title
```

New save field: `seenface 0|1` (JournalSaveData.SeenChartFace).

## 7. Boot intro (`loading_screen` → `intro` → `titlescreen`)

On every boot, after data has loaded, `IntroVideoController` plays **one random movie of `intro`** (OPENINGA..G,
64..83 s). Fader `FadeOut(0.1 s)`. A, + or any key skips while it plays. On end or skip: `FadeIn(black, 1 s)` → title
scene (the title then fades in from black over 1 s). For the port: play it after the "PORTED BY" splash and before the
title. A test hook or option to skip it is a good idea. The title music starts with the title, not during the movie.

## 8. Day 365 ending and the random endings (`DreamSystem.endDream`)

`endDream(fromFall, timedOut)` is called by the day timer (`timedOut=true`), Lua `EndDream()`, falling out of the
world (`fromFall=true`), and the end of a movie or text day. Without `InFlashback`:

```
if playingVideo or already ending: return
if fromFall: play the link sound at pitch 0.25 or 0.5; lock controls      else gravity off
if save.day == 365: ending = ENDING            (checked BEFORE the increment)
save.appendDay(sequence); day += 1 (365 -> 1, year += 1); save; if flashbackDisabledForDays > 0: --
colour = fromFall ? black : zone colour of today's graph position (green/blue/magenta/black/cyan/yellow/red/white,
         the app already has zone_color)
stop music, timers, flashback

if timedOut:   "random ending", RandUtil.Int(4):
   0: blend Alpha, SetFade(black)  (instant black, alpha 1) -> onFaded
   1: blend Subtract, FadeIn(black, 1.25 s)                 -> onFaded
   2: blend Alpha,    FadeIn(red,   1.25 s)                 -> onFaded
   3: blend Add,      FadeIn(white, 2.5 s)                  -> onFaded
else:
   secs = cutscene scene ? 1 : fromFall ? 2 : 3 ; blend = fromFall ? Subtract : Alpha ; FadeIn(colour, secs) -> onFaded

onFaded: CurrentDream = null; ending ? playEnding(ENDING) : return to title
```

`playEnding`: `playingVideo = true`; blend Alpha; show the video screen; `FadeOut(black, 0.5 s)`; play `ENDING`
(32 s, skippable like the others); on end: `FadeIn(black, 1 s)` → title. There are **no credits after the ending** in
Revamped. The credits roll is only reached from Settings → About (`OnCreditsShouldOpen`). The new year starts at day 1
with `year += 1`. The history and all other progress are kept (`IncrementDayNumberWithSequence` never clears).

"Return to title" (`ReturnToTitle`): blend Alpha, load the title, `FadeOut(1 s)`.
Pause → "return to title" is `ForceEndDream`: no progress, `FadeIn(black, 1 s)` → title (the port already does this).

New save field: `year N` (JournalSaveData.YearNumber, informational, shown nowhere in the original UI except the journal
display).

## 9. Flashbacks

### When
Not a replacement for a day. It's a separate **FLASHBACK** button on the title menu
(`titlescreen.unity` → `DreamSystem.BeginFlashback`), enabled when

```
CanFlashback = flashbackDisabledForDays <= 0 && (total entity contributions over ALL saved days) > 100
```

`flashbackDisabledForDays` is set to 10 when the grey man touches the player (`Greyman.playerEncountered`), decremented
at each normal (non-flashback) day end, and is **not saved** (`[NonSerialized]`, so it resets to 0 when the game
restarts). Faithful port: keep it in RAM only. A persisted `flashoff N` save line is the obvious fix if wanted.
The day number never changes in a flashback, nothing is saved, and no contributions are recorded.

### State machine
```
BeginFlashback (title):
  random texture set (by day), roll tonight's song style
  pool = every entity contribution of every saved day (flattened)
  FadeIn(black, 3 s) -> InFlashback = true
                        start the day timer: uniform 60..360 s (MIN/MAX_SECONDS_IN_DREAM, ignores DreamLengthSeconds)
                        ev = uniform random element of pool
                        dream = dream named ev.dream (missing name: random dream of the journal)
                        spawn override = (ev.pos, ev.yaw)
                        LoadDream(dream, transitioning=true); after load: if entity ev.entityID exists, SetActive(true)
  loop:
    secs = uniform 8..16 ; t = 0 ; wait until t >= secs   (t keeps running across wall links)
    ev = uniform random element of pool (with replacement)
    dream = by name (missing: random) ; spawn override = (ev.pos, ev.yaw)
    lock controls ; FadeIn(white, 2 s) (current blend)
    on faded: 1 in 2: new random texture set ; LoadDream(dream, transitioning=true)
              after load: controls on ; t = 0 ; if entity FIRST_EV.entityID exists: SetActive(true)   <- Revamped bug:
              it re-activates the FIRST event's entity, not this one. Use ev.entityID (intended) or copy the bug.
  day timer expires -> endDream(timedOut=true) with InFlashback: NO random ending, no day advance:
       colour WHITE, blend Alpha, FadeIn(white, 3 s) -> title
  fall -> black Subtract 2 s -> title ; Lua EndDream -> white Alpha 3 s -> title ; pause/return to title -> as usual
```

### LoadDream differences while InFlashback
* Spawn: if a spawn override is set, put the player at `(pos.x + 0.01, pos.y + skinWidth(0.01), pos.z - 0.01)` with
  yaw `ev.yaw`, pitch 0, instead of the spawn points. The override is cleared after this one load.
* Environment: `dream.RandomEnvironment()`, i.e. a uniformly random environment of the dream (not the day rule).
* Music: as usual (random song of the rolled style; day 1 forces song 2).
* **Image effect** `FlashbackImageEffect` on the player camera: `rgb += 0.4` (saturating), i.e. add 102 to each 8-bit
  channel of the rendered 3D frame (sky and world, before the fader and UI). Off outside flashbacks.
* Wall links work as usual: random linkable dream, normal spawn rule. Fade time is `linkFadeSeconds(kind) * 2/3`
  (wall 1.25 → 0.833 s, listed wall/character 0.4 → 0.267, doorway 2 → 1.333). The **fade colour is a random one of
  white, red, yellow, blue, cyan, green, magenta** (`RandUtil.RandColor`), because there is no current sequence
  (black links stay black).
* No `CurrentSequence`, so `LogGraphContribution*` calls are ignored and `ChartZone` (Lua) returns "white".
* The grey man can still appear (his area contribution is ignored, but he still sets `flashbackDisabledForDays = 10`).

### Coordinates
`ev.pos` is the Unity **transform position of the player** when the entity contribution was logged. In Revamped that's
the CharacterController origin: capsule centre y +0.3, height 0.4, skin 0.01, so the feet stand at about origin + 0.11,
and the camera is at origin + 0.5. The port's `Player` stores feet with eye height 0.55. **Port rule:** record the port's
own feet position (`pl.x, pl.y, pl.z`) and its yaw in Unity degrees (the convention of `spawn ... yawdeg` in
dreams.txt), and restore it the same way. Only if Revamped JSON saves are ever imported do you add 0.11 to y.
(Not verified in-game.)

## 10. Graph contributions: what the port must record

Revamped keeps **two lists per day** (`DreamSequence`):

* `area`: `(dyn, upper)` from LBD chunks (`lgraph`), `DreamArea` triggers (none in the original data), the grey man
  `(-10,-10)` and special days.
* `entity`: `GraphContribution{dyn, upper, playerPos(x,y,z), playerYaw, entityID, dreamName}`, logged by
  `LogGraphContributionFromEntity(d, u, entity)`. Sources:
  1. **Lua `this.LogGraphContribution(d, u)`** on interactive objects (`InteractiveObjectProxy` → entity contribution
     with `this`'s ID). 96 calls in the original scripts (baby, bmcBear, futon, astronaut...). The port's
     `h_log_graph` currently adds these to the **area** list. They must go to the entity list.
  2. `GraphContributor` components (`ContributionRadius` default 5, checked every 0.25 s, once per load, logs with its
     `SourceEntity`'s ID). None exist in the original dream prefabs. They exist in the Additional Dreams mod
     (`gcontrib` lines, already parsed into `Ent.gc`). `addl.py` doesn't write which entity `SourceEntity` points to.
     Use the ID of the entity the line belongs to (ambiguity, see the report).
  3. `DreamSystem.LogGraphContributionFromEntity(d, u, entity)` from Lua (DreamSystemProxy): same record.

Day position (`EvaluateGraphPosition`, C integer division truncates toward zero like C#):

```
eval(list) = list empty ? (0,0) : (sum.d / n + last.d / 3, sum.u / n + last.u / 3)
wrap(v)    = v < -9 ? 9 : v > 9 ? -9 : v          (per component)
A = wrap(eval(area)); E = entity empty ? A : wrap(eval(entity))
pos = wrap((A + E) / 2)                            (component-wise, integer division)
```

The current app evaluates a single list. With the two lists, positions change for days with object interactions.

### Save format additions (`save.txt`, key/value lines like the existing ones)

```
year <n>                                    JournalSaveData.YearNumber (day 365 -> 1 increments it)
seenface <0|1>                              chart face videos already shown
opt_specialdays <0|1>                       option, default 1
ec <dayidx> <d> <u> <x> <y> <z> <yawdeg> <dreamidx> <entityID...>
                                            one entity contribution; dayidx = index in the hist list (0 = oldest
                                            finished day), dreamidx = index in the app's dream list (original 0..13,
                                            additional 14..27), entityID = rest of the line (IDs can contain spaces);
                                            floats with %.3f
```

* Append the `ec` lines of the current day when it ends (the same place that appends `hist`). Flashbacks write nothing.
* `CanFlashback` = number of `ec` lines > 100 (and the RAM-only `flashbackDisabledForDays` <= 0).
* If `hist` is trimmed at `SAVE_MAX_HISTORY`, drop that day's `ec` lines and renumber. Revamped keeps everything
  forever. A cap of a few thousand `ec` lines (at most a few hundred KB of text) is plenty.
* A dream name that's no longer valid, or a dreamidx out of range: random dream (Revamped does the same for old saves
  without a dream name).

## 11. Credits (`CREDITS/credits.txt`)

```
lsdr-credits 1
section <image TIM path|-> <sort 0|1> <title...>
name <flags> <text...>        flags: '-' plain, 'i' italic, 'b' bold, 'ib'; text may be empty ("name - ")
```

Names are already sorted when `sort` is 1 (ordinal sort of the raw strings incl. tags; Unity sorts culture-aware, so
the order differs only for the few non-ASCII names). Revamped (`UICredits`, 800x600 reference canvas): a title text,
then each section (image 90 reference px wide = 36 px at 320x240, the TIMs are baked 72 px wide for 2x; section title
font 22, names 12, centred column), then an end cap. The column scrolls up at 1 reference unit per 50 Hz fixed step =
50 units/s = **20 px/s at 240 lines**. It ends when scrolled past its height + 500 units, or at once when the Run button
(B) is pressed. Then back to the About/settings screen. Some names use characters outside ASCII (Bösel, Marándola,
сорока), which the 5x7 font can't draw: transliterate or skip them.

## 12. Summary of new UI/states for the app

| state | entered from | shows | input | exits to |
|---|---|---|---|---|
| INTRO_MOVIE | boot (after splash) | random `intro` movie | A/+ skip | fade black 1 s → title |
| SPECIAL_VIDEO | CONTINUE on a special day | `choice video` movie | A/+ skip (while playing) | EndDream: day+1, fade black 1 s → title |
| SPECIAL_TEXT | CONTINUE on a special day | `tim` (or `page`) full screen | A/+ (no timeout) | as above |
| EVENT_MOVIE | Lua `VideoClip.Play(col)` | `movieclip` movie | A/+ skip | EndDream (day+1 unless flashback) |
| ENDING_MOVIE | day 365 ended | `ENDING` | A/+ skip | fade black 1 s → title |
| CHARTFACE | chart opened, face complete, not seen | face highlight, then the 20 `chartface` movies | A/+ skips each | title |
| FLASHBACK | title FLASHBACK (enabled by CanFlashback) | dreams with +0.4 tint, jumps every 8-16 s | normal play | timer/fall/EndDream → title, day unchanged |
| CREDITS | options/about | scrolling credits | B ends | back |
