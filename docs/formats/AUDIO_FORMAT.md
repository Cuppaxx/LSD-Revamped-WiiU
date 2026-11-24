# Audio data for the Wii U port (tools/audio_assets.py)

Everything LSD: Revamped plays except the music tracks and the title-screen sounds: footsteps, the link/fall sound, the
SFX-volume test sound, and the 315 `DreamAudio` entities of the 14 original dreams (the Additional Dreams' DreamAudio
are already baked by `addl.py`, see ADDL_FORMAT.md; this file uses the same `audio` line so one runtime,
`app/src/dreamaudio.c`, plays both). Source references are to `src/LSDR` (C#) unless noted.

```
python -I tools\audio_assets.py <LSDRevamped repo> <outdir> [--mod DIR] [--rate 22050] [--all-samples] [--merge DATADIR] [--verify]
python -I tools\audio_assets.py src tools\out_audio                      (staging, what phase 1 produced)
python tools\convert.py <repo> <data> ... --audio                        (= the above + --merge <data>, run after addl.py)
```

| output (paths relative to the data dir) | content |
|---|---|
| `audio.txt` | the tables below (text, one record per line, `#` = comment) |
| `SFX/SE_<n>.wav` | sample *n* of the PS1 `SND/SE.VB` bank (Revamped's pre-extracted `SND/LoadedSamples/SE/Sample<n>.wav`) |
| `SFX/sfxtest.wav` | `UIAudioSettings.SoundTestClip` (Sound/Menu/sfxTest.wav) |
| `ADDSND/mouse-click.wav` | Additional Dreams footstep sound (same folder and naming as addl.py's DreamAudio clips) |
| `SFX/bank/<BANK>_<n>.wav` | only with `--all-samples`: every sample of SND/*.VB and ETC/ETCSE.VB (nothing plays them; not merged) |

All WAVs: RIFF/WAVE, PCM signed 16-bit little endian, **mono**, `--rate` Hz (default **22050**), 44-byte header
(`fmt ` + `data` only, written with ffmpeg `-fflags +bitexact`). The PS1 sources are 11025 Hz, so `--rate 11025` halves
the size with no loss. Default run: **29 files, 26.3 s of sound, 1,160,658 bytes (1.11 MB)** + `audio.txt` (64 KB).
The mod ships byte-identical copies of the SE bank (`Assets/Audio/SFX/Sample*.wav`); the baker maps those to the same
`SFX/SE_<n>.wav` files instead of duplicating them (addl.py does not do this: its `ADDSND/Sample7.wav` duplicates `SFX/SE_7.wav`).

`--merge DATADIR` copies `audio.txt`, `SFX/` (without `SFX/bank`) and `ADDSND/` into DATADIR and inserts lines (section 3)
right after the matching `ent` lines of `dreams.txt` and `dreams_addl.txt`. It is idempotent (previous merged lines
directly after an ent line are replaced) and prints how many records found their ent line (currently 348/348 and 22/22).

---------------------------------------------------------------------------------------------------------------------

## 1. audio.txt

Tokens are separated by single spaces. Entity names use chr(31) for spaces, exactly like dreams.txt.
Floats are written as Unity serialized them (e.g. `0.25`, `12.79`). Lines in file order:

```
audiofmt 1 <rate>                               format version 1, sample rate of the SFX/ADDSND WAVs
clip <path> <frames> <rate> <seconds>           every clip the file names (preload list); path is data-dir relative

foot lbdfallback <clip|-> <pitch>               original journal (dreams.txt, dreams 0..13): LBDFootstepIndex.Fallback
foot lbd <key> <clip|-> <pitch>                 LBDFootstepIndex.Index[key]; key = LBD tile byte 4 & 0x7F ("footstep")
foot uvatlas <w> <h>                            Additional Dreams journal (dreams_addl.txt): UVFootstepIndex.AtlasDimensions
foot uvfallback <clip|-> <pitch>                UVFootstepIndex.Fallback
foot uv <x> <y> <w> <h> <clip|-> <pitch>        UVFootstepIndex.Index: atlas pixel rect -> sound, in serialized order

link <clip> <n> <pitch1..n>                     DreamSystem.LinkSound and the pitches Transition() picks from
fall <clip> <n> <pitch1..n>                     the sound EndDream(fromFall) plays (same clip)
soundtest <clip>                                played when the SFX volume is changed in the options

musicrule original 5 2                          see section 5 (SONGS_PER_DREAM = 5, day 1 plays song number 2)
modsongs <ADDnn> <orig dream index>             ADDnn uses the original dream's song lists (section 5)

dream <idx> <folder> <naudio>                   original dream (same idx/folder as dreams.txt); records below belong to it
klink <entname> <px> <py> <pz> <0|1>            TriggerLink.PlayLinkSound of the `ent K` with that name and position
aent <entname> <kind> <px> <py> <pz> <active> <live> <id> <parent|-> <ox> <oy> <oz>
                                                a DreamAudio: the `ent` line it belongs to (name, kind, world position as
                                                written by convert.py, %.4f), m_IsActive of its GameObject, active in
                                                hierarchy, the DreamAudio's own ID, the nearest ancestor that is an entity
                                                (the sound follows it) and the local offset in that parent's space
audio <13 fields>                               DreamAudio fields, identical to ADDL_FORMAT.md (below)
asrc <12 fields>                                the AudioSource component next to it (below)

modlink <ADDnn> <entname> <0|1>                 PlayLinkSound of the Additional Dreams' TriggerLinks (by name)
end
```

`audio <clip|-> <spatial> <fullrange> <range> <pitch> <volume> <delay> <playfor> <delaybefore> <waitaudible> <loop> <oneshot> <scripted>`
= DreamAudio.cs `Clip Spatial FullVolumeRangeUnits RangeUnits Pitch Volume DelayBetweenPlaysSeconds PlayClipForSeconds
DelayBeforePlayingSeconds WaitUntilAudibleBeforePlaying Loop OneShot ControlledWithScript`.

`asrc <rolloff> <min> <max> <spatialblend> <volume> <pitch> <loop> <playonawake> <doppler> <priority> <mute> <enabled>`
= AudioSource `rolloffMode` (0 logarithmic, 1 linear, 2 custom), `MinDistance`, `MaxDistance`, spatial blend
(first key of `panLevelCustomCurve`, 0 = 2D, 1 = 3D), `m_Volume`, `m_Pitch`, `Loop`, `m_PlayOnAwake`, `DopplerLevel`,
`Priority`, `Mute`, `m_Enabled`.
For all 315 original DreamAudio the AudioSource agrees with the DreamAudio (DreamAudio.OnValidate copies the fields in
the editor): rolloff **1 linear**, min = FullVolumeRangeUnits, max = RangeUnits, blend 1, same pitch/volume/loop,
playOnAwake 0, doppler 1, priority 128. `asrc` is therefore informational; the runtime may ignore it. (The mod's 61
AudioSources are all linear too.) Note that the AudioSource values matter in one case: `Loop`/`OneShot` sources play
with the AudioSource's own volume/pitch (section 4.4), which are equal to the DreamAudio fields at load.

### Counts (default run)

| dream | DreamAudio | scripted | loop | oneshot / other | clips |
|---|---|---|---|---|---|
| STG00 Bright Moon Cottage | 11 | 8 | 5 | | SE 3 5 17 21 23 24 25 28 |
| STG01 Pit | 2 | 2 | 0 | | SE 6 7 |
| STG02 Kyoto | 40 | 28 | 0 | | SE 10 12 13 14 15 16 29 31 |
| STG03 Natural World | 152 | 39 | 77 | | SE 7 9 12 13 20 23 26 27 28 29 |
| STG04 Happytown | 35 | 30 | 4 | | SE 1 3 4 5 7 9 12 |
| STG05 Violence District | 56 | 20 | 1 | | SE 3 5 7 9 12 18 19 |
| STG06 Moonlight Tower | 1 | 0 | 0 | | SE 7 |
| STG07 Temple Dojo | 2 | 2 | 0 | | SE 13 24 |
| STG08 Flesh Tunnels | 8 | 2 | 0 | | SE 16 23 29 |
| STG09 Clockwork Machines | 2 | 0 | 0 | | SE 3 10 |
| STG10 Long Hallway | 1 | 1 | 0 | | SE 26 |
| STG11 Sun Faces Heave | 3 | 0 | 3 | | SE 9 |
| STG12 The Void | 2 | 2 | 0 | | SE 20 |
| STG13 Monument Park | 0 | | | | |

Totals: 315 sources (134 ControlledWithScript, 12 OneShot, 90 Loop, 8 with DelayBefore > 0, 7 with PlayClipFor > 0, all spatial, all
WaitUntilAudible = 1), 210 of them nested under another entity (aparent). All DreamAudio GameObjects are active in the
prefab (active = live = 1 everywhere); scripts deactivate some parents at start(). Entity names with `$$` occur more than
once (STG03 BridgeAudio_$$ / WaterfallAudio_$$ / WindAudio_$$ / WindSubAudio_$$, STG05 SplashWind_$$ /
SplashWind_Sub_$$, STG11 WindAudio_$$); no script looks them up, the merge matches them by position. Every DreamAudio
is its own entity (`ent T <ID>`): none shares a GameObject with an InteractiveObject or trigger.

---------------------------------------------------------------------------------------------------------------------

## 2. What is NOT baked, and why

* **Music** tracks: convert.py's `song` lines + `--music` / `--music-all` copy them as they are. Note: the original
  tracks (`STGxx/LoadedTracks/*.wav`) are **44100 Hz stereo** 16-bit, not 32 kHz (~800 MB for all 745). Converting them
  to 32000 Hz mono would save 2.76x; that is convert.py's business, not done here.
* **Title / menu music and hover/press sounds**: convert.py `--copy` writes `MENU/*.wav` (menu agent).
  `PauseCanvas.prefab` uses the same click.wav / select.ogg (= MENU/press.wav, hover.wav).
* **RevampedSongLibrary** (`UnityEditor/Resources/Sound/Music/RevampedSongList.asset`, 189 community .ogg songs, 119 MB
  as ogg, 3808 s = 244 MB as 32 kHz mono WAV, 672 MB as 44.1 kHz stereo): only used when the option *Use original soundtrack* is OFF (default ON). Not baked.
* **SND/*.VB/.VH instrument banks** (AMBIENT, CARTOON, ELECTRO, ETHNOVA, HUMAN, LOVELY, STANDERD; 221 samples; plus the 30 unused SE samples) and
  **ETC/ETCSE.VB** (2 samples, 5512 Hz): no asset or script in Revamped references their samples (the PS1 SEQ music is
  replaced by pre-rendered tracks). Available with `--all-samples` (all 255 bank samples, 320 s, ~14 MB at 22050 Hz) for experiments only.
* `Sound/SE_00003.ogg` (Player.prefab `PlayerMovement.FootstepClip`): the field is never read; unused.
* `Sound/Dream/linkSound.wav`, `Sound/womp-womp.wav`: unreferenced. The link sound is SE sample 9.
* `Original/STG03/stg03.unity` (references SE 9): an editor scene not in the build; ignored.
* FILM/*.STR videos have their own audio (special_assets / video work), not here.
* The mixer (`Mixers/MasterMixer.mixer`): Master <- Music, SFX groups, each with an Attenuation (= the volume options)
  and a Send into a "Duck Volume" compressor on Master. The compressor is not reproduced.

---------------------------------------------------------------------------------------------------------------------

## 3. Lines the merge adds to dreams.txt / dreams_addl.txt

Inserted directly after the `ent` line they belong to (before its `model`/`mpose`/... lines; the app's parser attaches
every per-entity line to the last `ent`, so this is equivalent). The current dream.c already parses `audio`; the others
are ignored by it until implemented.

```
audio <13 fields>                 dreams.txt, after `ent T <DreamAudio ID>`: exactly the ADDL `audio` line, clip = SFX/SE_<n>.wav
asrc <12 fields>                  dreams.txt, after that audio line: the AudioSource (informational)
aparent <parent> <ox> <oy> <oz>   dreams.txt, before the audio line, for 210 sources nested below another entity
                                  (BearAudio under Bear, PterodactylAudio under Pterodactyl...): the source position is
                                  parent.world * (ox, oy, oz) (parent's TRS, i.e. it moves/rotates with the parent).
                                  Most offsets are (0,0,0) or (0,0.25,0). The parent is another `ent` of the same dream
                                  (not necessarily earlier in the file: convert.py does not sort parents first).
                                  Like the ADDL `epath`, but only for the position (the ent stays a top-level entity for
                                  GetEntity, which is how the original scripts find it).
linksound <0|1>                   dreams.txt and dreams_addl.txt, after `ent K ...`: TriggerLink.PlayLinkSound
                                  (original: 16 of 33 triggers are 1; mod: PizzaLink and CatsLink are 1, the other 20 are 0).
                                  No line = 0 (the C# default) - e.g. the mod's TriggerLink_Cage prefab instances.
```

---------------------------------------------------------------------------------------------------------------------

## 4. Runtime semantics (what the C side must implement)

### 4.1 Mixing / groups
* Two groups as in Revamped: **Music** (MusicPlayer, one looping source) and **SFX** (everything in this file: footsteps,
  link/fall, sound test, all DreamAudio). The options' music and sound volumes scale these groups.
* Listener = the player camera (`Main Camera` of Player.prefab holds the AudioListener): player position + eye height,
  including head bob. Yaw of the camera for panning (4.5).

### 4.2 One-shot 2D sounds (Torii `AudioPlayer.PlayClip`)
Footsteps, link, fall and sound test go through `AudioPlayer`: a pool of up to **10** non-spatial AudioSources; a
request takes a channel that is not playing, adds one if fewer than 10 exist, otherwise the sound is **dropped** (no
stealing). Each plays its clip once at volume 1 (SFX group) with the given pitch. Port: 2-4 dedicated voices are enough
(footsteps are ~0.03-0.5 s); drop when none is free.

### 4.3 Footsteps (PlayerMovement.cs)
**When** (the setting `EnableFootstepSounds`, default true, gates every footstep; add it to OPTIONS):
* FPS controls (the default scheme, what the port does): every frame with non-zero move input (stick, locked input, or
  scripted `UseExternalInput`/StartMovingForward):
  `if (!stepping || t > StepTime) { stepping = true; t = 0; halfDone = false; footstep(); }`
  `if (sprinting && !halfDone && t > StepTime / 2) { halfDone = true; footstep(); }`; input zero -> `stepping = false`.
  `t` is the time since the step started (TimeSince, frame-quantised: the step fires on the first frame after 0.2 s).
  So: a footstep immediately on starting to move, then every **StepTimeSeconds = 0.2 s** (Player.prefab value), and
  when running one more at **0.1 s** into each step. Input counts, not actual movement: pushing into a wall still
  makes footsteps. Head bob uses the same `t / StepTime`.
* Classic (tank) controls: each step is a 0.2 s coroutine (`takeStep`): footstep at its start, plus one when
  `progress > 0.5` while sprinting.
* `sprinting` = Run held and moving forward (input y > 0) and not externally driven.
* **No pitch randomness** for footsteps: the pitch is the table value.

**Which sound** (`getFootstepSound`): a capsule cast straight down from the player's capsule (radius 0.08, height 0.4,
skin 0.01: bottom sphere centre at feet + 0.1, cast distance 0.16, i.e. it finds ground up to ~0.14 below the feet).
Nothing hit (airborne) -> no sound. Otherwise the journal's footstep index decides:
* **Original dreams (dreams.txt) - LBDFootstepIndex**: if the hit collider is an LBD chunk:
  `cx = (int)(hit.x - chunk.x + 0.5)`, `cz = (int)(hit.z - chunk.z + 0.5)` (C truncation; chunk origin = centre of
  its tile 0), outside 0..19 -> fallback; `key = chunk.Tiles[cx * 20 + cz].FootstepSoundAndCollision & 0x7F` (the
  *main* tile of the cell, never an extra tile); key in `foot lbd` -> that entry, else fallback. Hitting anything
  else (entity colliders, models) -> fallback. A `-` clip (key 0) -> **silence**.
  Revamped places raw tile `i` at local x = i / 20, z = i % 20 (LBDImporter.cs), hence `cx * 20 + cz`. The app's
  collide.c/render.c place raw tile `idx` at x = idx % w, z = idx / w; whichever is right for the app, take the key
  from the **raw tile the app draws/collides under the player** (lsd.c already stores `LTile.footstep = byte4 & 0x7F`).
  Worth one check in a dream with mixed keys (Happytown STG04: keys 12, 14, 15, 16, 17, 19) that the transposition
  is consistent.
  Key usage in the LBDs (drawn main tiles): STG00 1 3 5 7 (0: 15); STG01 1 7; STG02 6 (most), 1 7 10 11;
  STG03 1 (most) 7 8 9 20 21; STG04 12 14 15 16 17 19 (13 -> fallback); STG05 7 2 22 (127 -> fallback); STG06 5;
  STG07 7; STG08 23 6 7; STG09 7 21; STG10 3 (all tiles); STG11 0 only (silent dream); STG12 mostly 0 (silent), 1 7
  (125 -> fallback); STG13 1 7. Unknown keys 13, 125, 127 use the fallback (SE 3, pitch 1).
* **Additional Dreams (dreams_addl.txt) - UVFootstepIndex**: needs the texture coordinate of the hit point on the
  mesh (MeshCollider only; box/capsule colliders give (0,0) -> y flips to 1024 -> no rect -> fallback).
  `px = (int)(u * 1024)`, `py = (int)((1 - v_unity) * 1024)`; ADDL triangles already store `v` top-to-bottom
  (= 1 - v_unity), so `py = (int)(v_addl * 1024)`. The first `foot uv` rect (in file order) with `x <= px < x + w` and
  `y <= py < y + h` wins (RectInt.Contains), else fallback. The rects are the 8x8 grid of 128-px cells of the mod's 1024x1024
  atlases, so effectively `cell = (py / 128) * 8 + px / 128` for the atlas materials; a triangle of a non-atlas material
  gives a meaningless cell (Revamped behaves the same). The raw interpolated UV is used (no wrap): UVs > 1 can fall
  outside all rects -> fallback.
* Play through 4.2 at the table pitch (pitch = playback-rate factor: 4 = four times faster and higher).

### 4.4 DreamAudio (Entities/DreamAudio.cs; also what dreamaudio.c implements)
Per source, state: `playing` flag (starts false), coroutine, `t` think timer.
* **OnEnable** (entity becomes active in hierarchy, including at dream load): if `scripted` -> nothing; else StartPlaying.
* **OnDisable**: StopPlaying (stop the sound, kill the coroutine). `playing` is NOT reset.
* **StartPlaying**: if a coroutine is running -> return. If `loop` or `oneshot`: `AudioSource.Play()` directly
  (restarts the clip from 0 if it was sounding; looping if `loop`; uses the AudioSource volume/pitch) - no
  audibility wait, no delays. Else start the coroutine.
* **coroutine**: `if (!waitaudible || scripted) playing = true;` wait until `playing`; then forever:
  wait `delaybefore` (if > 0); set pitch/volume from the DreamAudio fields; `Play()` (restart from 0);
  if `playfor > 0`: wait `playfor`, Stop; then wait `delay` (if <= 0: one frame). The delay counts from the start of
  the clip when `playfor` = 0, so a clip longer than `delay` is cut off and restarted (e.g. Happytown OperaSinger:
  SE 1, 0.49 s, restarted every 0.1 s).
* **Update** (only when not playing and not scripted): every 0.5 s check `|player - source|^2 < range^2`
  (player = the player's transform position, i.e. the feet, not the camera) -> `playing = true`. Once true it stays
  true for the life of the dream (re-enabling skips the wait).
* **StopPlaying**: `AudioSource.Stop()` + stop coroutine.
* Lua (DreamAudioProxy): `Play()` = StartPlaying, `Stop()` = StopPlaying, `Volume` / `Pitch` get/set the DreamAudio
  fields. A set takes effect at the next coroutine iteration (before each Play), never on a Loop/OneShot source that is
  already playing, and is **clamped to 0..1** when it reaches the AudioSource (Unity clamps `AudioSource.volume`;
  OperaSinger walks Volume down and up by 0.075 per 0.25 s, going below 0 / above 1 in the field). Pitch: Unity
  allows -3..3 on AudioSource.pitch; values here are 0.25..4 for footsteps (AudioPlayer) and 0.25..1 for DreamAudio.
* A source on an inactive entity cannot start (Unity refuses Play on inactive objects).

Differences in the current app/src/dreamaudio.c worth fixing (for the C author, not changed here):
1. Clip duration ignores pitch: a non-looping clip lasts `frames / rate / pitch` seconds (pitch 0.25 = 4x longer);
   dreamaudio.c ends it after `frames / rate` (cuts SE 17 / SE 5 / SE 7 at pitch 0.25 to a quarter). Likewise a voice
   joining late must start at clip position `snd_t * pitch`.
2. `start_playing` returns when a Loop/OneShot sound is already sounding; Unity restarts it from 0 (matters for scripted
   OneShots played repeatedly).
3. Distance for the audibility check is from the player's feet; attenuation is from the camera (listener). dreamaudio.c
   uses the eye for both (difference 0.55 units, minor).
4. Positions of sources nested under moving entities (`aparent`) should follow the parent (pterodactyls, birds, trains,
   UFOs, boats...). The original scripts move the parent entity, never the audio entity.

### 4.5 Attenuation, panning (Unity/FMOD 3D rules)
`d` = distance listener - source. `mn` = min distance (= FullVolumeRangeUnits), `mx` = max distance (= RangeUnits).
* Linear (rolloff 1; every DreamAudio, original and mod): `g = d <= mn ? 1 : d >= mx ? 0 : (mx - d) / (mx - mn)`.
* Logarithmic (rolloff 0; unused here, for completeness): `g = d <= mn ? 1 : mn / min(d, mx)` (stops attenuating at mx,
  never reaches 0).
* Custom (2; unused): `g = curve(d / mx)`.
* Final gain = `g * volume * SFX volume option`. Spatial blend 0 (2D; `spatial` = 0, none in the originals) -> `g = 1`,
  centre pan. Values in between blend linearly between the 2D and 3D gain/pan.
* Panning (spatial blend 1, spread 0): Unity pans by direction. A simple equal-power approximation:
  `s = dot(normalize(src - listener).xz, camera_right.xz)` in -1..1, `left = sqrt((1 - s) / 2)`, `right = sqrt((1 + s) / 2)`
  (normalised so centre = 0.707 each; or use 1 at centre if the 2D mix is kept at full level). dreamaudio.c currently
  has no panning; optional.
* Doppler level 1 is set but nothing moves fast enough to matter; ignore.

### 4.6 Voice allocation (suggestion, matches what Unity does with virtual voices)
Unity mixes up to 32 real voices and virtualises the quietest (all priority 128). STG03 has 152 sources, ~77 looping
ambiences (wind, water, bridges) but with ranges of 3-15 units only a handful are audible at once. dreamaudio.c's
scheme (each source keeps its own clock; every frame the loudest audible sources get a voice; a source that gets a voice
later joins at its current position) reproduces that. 8-12 voices for DreamAudio + 2-4 for 4.2 is plenty.

### 4.7 Link and fall sounds (DreamSystem.cs)
* `Transition(... playSound ...)`: `MusicSystem.StopSong()` (1 s fade-out) and, if `playSound`, play the link clip (SE 9)
  through 4.2 with a pitch chosen uniformly from **{0.25, 0.5, 1, 2}** (`link` line).
  - Wall link (PlayerMovement, 1.7 s against a wall): `Transition()` -> playSound = **true**.
  - Link triggers (`ent K`): playSound = the trigger's `linksound` (default 0 when the line is missing).
  - Lua `DreamSystem.TransitionToDream()`: playSound = the value set by `DreamSystem.SetTransitionSounds(b)` since the
    last transition, **true** if never set; reset to true after every TransitionToDream. (Original scripts never call
    SetTransitionSounds -> always true; the mod's triggerInteriorLink.lua sets false.)
  - Console LoadDream / dream select: no sound.
* `EndDream(fromFall = true)` (falling out of the world): link clip with pitch from **{0.25, 0.5}** (`fall` line);
  normal day end (timer): no sound.

### 4.8 Sound test
`soundtest` clip (3.3 s) is played through 4.2 when the sound volume option changes (UIAudioSettings.SoundTest).

---------------------------------------------------------------------------------------------------------------------

## 5. Music (checked against convert.py's `song` lines; nothing missing for the originals)
* `UseOriginalSoundtrack` defaults to **true** -> `OriginalSongLibrary`: per original dream 7 style lists
  (SongStyle order Standard, Ambient, Lovely, Human, Electro, Ethnova, Cartoon = convert.py STYLE_KEYS, `song <style>`).
  convert.py's lists are complete and in asset order (STG00/03/05/09: 4 songs per style, the others 5).
* Style: one random style per day (`rollSongStyle`, uniform 0..6).
* Song on every dream load (including links back into the same dream): `songNumber = (day == 1) ? 2 : random(0..4)`
  (SONGS_PER_DREAM = 5), `song = list[style][songNumber % count]` -> with 4 songs index 0 is twice as likely; day 1 always
  plays song index 2 of the style. The app (main.c) picks uniformly and ignores the day-1 rule.
* Music loops (`AudioSource.loop = true`); `StopSong` fades out over 1 s; a new song starts at full volume after the old
  one stopped.
* **Additional Dreams**: `SongLibrary` is null in all 14 mod dream assets; with UseOriginalSoundtrack the
  OriginalSongLibrary is used with `DreamNumber = GetDreamIndex(dream) % 14`, the dream's index in the *mod* journal, so
  **ADDnn plays original dream nn's song lists** (`modsongs` lines). The app instead takes a random original dream.
* With UseOriginalSoundtrack OFF: `CurrentDream.SongLibrary` (null everywhere) -> RevampedSongLibrary:
  revampedSongLibraryScript.lua returns no song in 1 of 8 loads, else a uniformly random song of RevampedSongList (189).
  Not baked (section 2).

---------------------------------------------------------------------------------------------------------------------

## 6. Audio-related Lua API used by the scripts

DreamAudio access is always `GetEntity("<ID>").DreamAudio` (originals) or `this.GameObject.GetChildByName("Audio").DreamAudio`
(mod, see ADDL_FORMAT.md). Methods: `Play()`, `Stop()`, properties `Volume`, `Pitch` (DreamAudioProxy.cs; there is no
`SetVolume`). Video `videoClip.Play(color)` and `AnimatedObject.Play/Stop` are not audio.

| script | entity looked up | calls |
|---|---|---|
| STG00 tv.lua | TVAudio | Play |
| STG00 pterodactyl.lua | PterodactylAudio | Play |
| STG00 futon.lua | FutonAudio | Play |
| STG00 baby.lua | BabyAudio | Play |
| STG00 bmcBear.lua | <name>Audio (BearAudio, TVBearAudio) | Play |
| STG00 doublefaceRun.lua | <name>Audio | Play |
| STG01 pitPterodactyl.lua | PterodactylAudio, PterodactylScreech | Play (both) |
| STG02 geisha.lua, boat.lua, statue.lua, tengu.lua, minotaur.lua, baby.lua | <name>Audio | Play (baby: in an action chain) |
| STG02 drumSticks.lua | DrumStickAudio | Play in `.Do` |
| STG02 bell.lua | BellAudio | Play in `.Do` |
| STG02 rickshaw.lua | <name>Footsteps, <name>Creaking | Play (both) |
| STG03 ufo.lua, bird.lua, horse.lua, door.lua, demon.lua | <name>Audio | Play |
| STG03 rocket.lua | <name>SirenAudio, <name>EngineAudio | siren Play, Stop; engine Play (action chain) |
| STG03 lion.lua | <name>RoarAudio, <name>StepAudio | Play |
| STG04 whale.lua, train1/2/3.lua, penguinSlide.lua, marcher.lua | <name>Audio | Play |
| STG04 train3Trigger.lua | <name>Audio | Play, then Stop in a chain |
| STG04 trumpeters.lua | TrumpetersAudio | Play |
| STG04 operaSinger.lua | <name>Audio | `audio.Volume = audio.Volume - 0.075 * direction` every 0.25 s after interaction (read + write) |
| STG05 spaceship.lua, plane.lua | SpaceshipAudio, PlaneAudio | Play |
| STG05 hopskotch.lua, gunman.lua, ghost.lua | <name>Audio | Play (ghost twice) |
| STG05 hangedGirl.lua | <name>_Audio | Play |
| STG05 carCrash.lua | CarHornAudio, CarSplashAudio | horn Play/Stop, splash Play (chain) |
| STG05 can.lua | CanAudio | Play `.Do`, Stop `.Then` |
| STG05 boat.lua, corpseKicker.lua | <name>Audio, CorpseKickerAudio | looked up, never played |
| STG06 flyingMinotaur.lua | <name>Audio | Play |
| STG07 dog.lua | DogBarkAudio | Play |
| STG07 chairBalancer.lua | ChairBalancerAudio | Play `.Do` |
| STG08 sumo.lua | SumoStrikeAudio, SumoFootstepAudio | footstep Play/Stop/Play, strike Play (chain) |
| STG08 standingBabyGroup.lua, runningBaby.lua | <name>Audio | Play |
| STG10 endOfHallway.lua | <name>Audio | Play |
| STG12 hoopKid.lua | <name>Audio | Play |
| mod basketball, bike, birdCircle, birdForward, hospitalMonitor, glowingFigure, popup, tyre | child "Audio" | Play |
| mod triggerInteriorLink.lua | - | `DreamSystem.SetTransitionSounds(false)` before TransitionToDream |

`<name>` = `this.GameObject.Name`. Other sound-relevant Lua calls: `DreamSystem.TransitionToDream()` (link sound per
4.7). No script plays a clip directly, changes music, or uses `Pitch`.
