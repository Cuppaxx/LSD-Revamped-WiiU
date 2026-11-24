# Additional Dreams: baked data (dreams_addl.txt, world.bin, amodels.bin, .tex)

Written by `tools/addl.py` from the Unity project of the mod
[LSDR-Additional-Dreams](https://github.com/Figglewatts/LSDR-Additional-Dreams) (MIT), cloned at
`ref/LSDR-Additional-Dreams`:

```
cd app
python -I ..\tools\addl.py content\data            (or any staging dir, e.g. ..\tools\out_addl)
       [--tex 512|1024] [--cell 16] [--verify] [--png [--side]] [--nosound] [--only ADD00,ADD03]
```

`addl.py` is independent of `convert.py` (it only copies `env_line`, `env_seq` and `lua_lambdas` out of convert.py's
source so both write identical `env` lines and Lua) and never touches `dreams.txt` or the STGxx folders.

Output (all paths relative to the data dir):

| file | what |
|---|---|
| `dreams_addl.txt` | the 14 dream records, same line syntax as `dreams.txt` plus the new lines below |
| `ADD00..ADD13/world.bin` | static geometry of the dream: texture table, material table, triangle chunks |
| `ADD00..ADD13/amodels.bin` | models of the dream's entities (FBX props, rigid hierarchies, animations) |
| `ADDTEX/*.tex` | textures, raw RGBA8 (shared by all dreams) |
| `ADDLUA/*.lua`, `ADDLUA/include/dreams.lua` | entity scripts, MoonSharp lambdas rewritten (like convert.py) |
| `ADDSND/*.wav` | DreamAudio clips, 22.05 kHz mono 16-bit PCM (converted with ffmpeg) |

Dream index `n` (0..13) is the position in `AdditionalDreams.asset`; folder = `ADD%02d`:
0 Dreamscape, 1 Amber Street, 2 Concretorium, 3 Power Station, 4 School, 5 Ziggurat, 6 Trash Out, 7 Forum,
8 Leisure Centre, 9 Mill, 10 Belvoir, 11 Pooley, 12 Railway, 13 Eastern. (`ADDnn` was chosen so it never clashes with
the original `STGnn` folders; the mod's own prefabs are also called STG00..STG13.)

All binary files are **big-endian**, no padding. `str` = `u8 length` + UTF-8 bytes (no terminator).

## 1. Conventions

* **Space**: Unity world space of the dream prefab, left-handed, +Y up, +Z forward, X right. 1 unit = 1 Unity metre
  = 1 LBD tile of the original dreams. Measured in the baked data: interior rooms are 1.0 high (Pooley, Belvoir,
  Railway, Eastern, Mill are 1-2 units high), the shadow person is 0.75 tall, the sitting school children 0.38,
  trees 1.2-1.44, a sofa 0.35, a table 0.24; the player's eye at 0.55 fits that scale.
  Everything (prefab hierarchy incl. nested prefab instances and prefab variants, FBX node hierarchy, scale, rotation)
  is already applied: world.bin vertices are world positions.
* **FBX import** (what the baker reproduces of Unity's ModelImporter): vertex/translation units x 0.01 (files are cm,
  "Convert Units" on), X mirrored (right- to left-handed) with the triangle winding reversed back, file axis system
  converted on the top-level nodes (Y-up/-Z-forward files: identity; Y-up/+Z-forward: 180 deg about Y; Z-up: -90 deg
  about X), a file with a single top-level node collapses into the root GameObject.
* **Front face**: a triangle `(a, b, c)` faces the side its normal `cross(b - a, c - a)` points to (computed on the stored
  coordinates as plain numbers). Projected to a screen with x right / y up, a front face is clockwise (Unity's
  convention). Only materials with `cull = 2` need this; the original app draws everything double-sided.
  Verified: with this rule the floors of all 14 dreams face up and ceilings face down (preview renders back-face cull).
* **UV**: `u` 0..1 left to right, `v` 0..1 **top to bottom** (already flipped from Unity's bottom-up), wrap = repeat
  (Unity wrap mode Repeat, point filtering). Texel = `(floor(u * w) mod w, floor(v * h) mod h)`.
* **Vertex colour** `r, g, b, a` bytes: shader result `rgb = clamp(tex.rgb * col.rgb * 2 / 255)` (LSDR.cginc
  `VERTEX_COLOR_SCALE 2`: 128 = neutral, 255 doubles). Meshes without a colour layer get 255,255,255,255 (Unity's
  default white), i.e. their texture is doubled, exactly as Unity shows them. `a` multiplies the alpha of blended materials.
  The mod project is in linear colour space but its dreams render inside LSD: Revamped, which is gamma: no conversions.

## 2. dreams_addl.txt

Line syntax identical to `dreams.txt` wherever a line exists there; all names with spaces use byte 0x1F like
convert.py. First line, once:

```
luainclude ADDLUA/include          directory searched by require "x" (the mod's dreams.lua: require "dreams")
```

Then per dream, in this order:

```
dream <idx> <folder> <walk> <linkable> <weight> <firstday> 2 0 <envoffset> <name...>
```
Same fields as dreams.txt. Layout **2** (new value) = mesh dream (world.bin), tile width 0. `walk` = WalkSpeed
(1.25 for all: the field is not set in the mod's assets), `firstday` 1 only for Dreamscape.

```
len <seconds>        DreamLengthSeconds (0 = random, all 14)
fx <0|1>             HasEnvironmentEffects
grey <0|1>           GreyMan
lgraph 0             no LBD tile contributions; the graph comes from gcontrib lines and the scripts' LogGraphContribution
world <file>         ADDnn/world.bin
models <file>        ADDnn/amodels.bin
songs revamped       no SongLibrary in any of the 14 dreams: Revamped then uses its RevampedSongLibrary
                     (MusicSystem.CurrentSongLibrary == null) -> pick a song as for an original dream of the day's style
group -1 <n>         + n `env` lines, exactly the dreams.txt format (convert.py env_line). All 14 use the plain list
env ...              (EnvironmentGroups empty); ChooseEnvironment(day) = list[(day - 1 + envoffset) % n]
```

```
gate <id> <oneIn> <daymask> <name>
```
A GameObject with `ChanceToAppear` and/or `AppearOnDay` (SDK components, evaluated once in Start()). ids are 0-based per
dream; gate lines come before the entities. The GameObject (and everything below it) is active only if
`(oneIn == 1 || random() < 1 / oneIn) && (daymask == -1 || dayKind(day, daymask))`. Each gate is rolled **once** per
dream load and shared by all entities listing it (e.g. WindowAudio's 1-in-2 gates both its Day and Night audio).
`daymask` = DayKind flags: 2 Odd, 4 Even, 8 One, 16 Two, 32 Three, 64 Four, 128 Five, 256 Six, 512 Seven
(One..Seven: `((day - 1) % 7) + 1 == n`), 0 = never; -1 = no AppearOnDay component.

```
ent <kind> <name> <active> px py pz yaw sx sy sz bcx bcy bcz bsx bsy bsz <kind fields> <firstkey> <nmodel> <nmesh> <child0>
```
Exactly the convert.py / MODELS_FORMAT.md `ent` line: kind `L` TriggerLua (`<script> <fn> <fnexit> <onlyonce>`),
`K` TriggerLink (`<dream index into the ADDITIONAL list> <spawn id|-> <lockinput> <affectslinkswithin> <onlyonce>`;
every link of the mod targets one of its own 14 dreams), `P` teleport (unused by the mod), `I` InteractiveObject
(`<script> <interactionkind 0 proximity, 1 view, 2 none> <distance>`), `T` any other BaseEntity (PlayerSpawn, Target,
DreamAudio...). Position/yaw/scale = world transform of the entity GameObject (yaw only, like convert.py), box = its
BoxCollider centre/size (local; for L/K/P it is the trigger volume, the SDK's BaseTrigger makes it a trigger).
`active` = active in hierarchy in the prefab (before gates/scripts). Script paths are `ADDLUA/<file>.lua`.
`firstkey/nmodel/nmesh/child0` as MODELS_FORMAT.md; `nmesh` is always 0 here (everything is a `model`).

**Names**: entity IDs containing `$$` (SDK: replaced by a GUID in Awake) are made unique by the baker:
`$$` -> `u<counter>` (e.g. `WindAudio_$$` -> `WindAudio_u17`). GameObject name == entity ID at runtime (BaseEntity
renames itself), so `child0`, `epath` and model paths use the entity names for GameObjects that are entities. The
scripts only ever look up IDs without `$$` (`ShadowTarget3`, `SwimmerA_Target1`, `GlowingFigure_Target7`, `TyreTarget`,
`__player`...). IDs are **not unique**: the DreamAudio children found with `GetChildByName("Audio")` (bike, birds,
basketball, glowing figure...) are all called `Audio`; resolve GetChildByName below the entity, not via GetEntity.

**Pseudo entities** `ent T gate_<GameObject name> ...`: a gated GameObject that is no entity itself but owns geometry,
a particle system or a GraphContributor (Concretorium's `DripPuddle`: puddle model + drip particles + graph
contribution + a DreamAudio child entity) gets a `T` line so its model/gates/particles/gcontrib have a carrier.
Nobody looks it up by name.

`ent` lines (pseudo entities included) appear in prefab hierarchy order, parents before children.

Lines that may follow an `ent` line (after its `model` lines), all new:

```
model <key> <path> <autoplay> <active> px py pz qx qy qz qw sx sy sz <nposes>     as MODELS_FORMAT.md, key = amodels.bin key
mpose <object> <active> px py pz qx qy qz qw sx sy sz                            as MODELS_FORMAT.md
mclips <n> <anim0> ... <anim n-1>
```
`mclips` (after the model's mpose lines, only when the model root has an `AnimatedObject`): `AnimatedObject.Clips[k]`
-> anim index in amodels.bin (-1 = not found). `PlayAnimation(k)` / `AnimatedObject.Play(k)` must go through it (the
same model is shared by instances with different clip orders, e.g. the bird). `autoplay` = AnimatedObject.AutoPlay
mapped the same way, or, if that is -1, the controller's **default state** (an enabled Animator plays it on enable;
e.g. the bike's `BikeMoving`, the snake's `SnakeWiggle`), else -1.
`model` world transform: `World(o) = T(ent.pos) R_yaw(ent.yaw) S(ent.scale) * T(p) R(q) S(s) * L(object 0..o)`;
object 0 of every model is the model root (identity pose, see 4.).

```
epath <parent entity name> <path>        this entity is nested inside another entity; path from the parent (Transform.Find)
gates <n> <id>...                        gates on this GameObject and its ancestors (innermost first); all must pass
audio <clip|-> <spatial> <fullrange> <range> <pitch> <volume> <delay> <playfor> <delaybefore> <waitaudible> <loop> <oneshot> <scripted>
                                         DreamAudio fields, clip = ADDSND/<name>.wav (see SDK DreamAudio.cs for the play logic)
gcontrib <dynamic> <upper> <radius>      GraphContributor: logs (dynamic, upper) once when the player comes within radius
aobj <path>                              InteractiveObject.AnimatedObject lives on this model path (this.PlayAnimation target)
startonenable 1                          InteractiveObject.CallStartOnEnable
particles <path> <tex|-> <looping> <prewarm> <duration> <lifeMin> <lifeMax> <speedMin> <speedMax> <sizeMin> <sizeMax>
          <rate/s> <maxParticles> <gravity> <shapeType> <angle> <radius> <r> <g> <b> <a>
                                         a ParticleSystem (Steam, DripPuddle): only its main parameters, no geometry
                                         (shape type 4 = cone; texture ADDTEX/steam.tex, alpha blended)
ecol box <enabled> cx cy cz sx sy sz [path]            solid (non-trigger) colliders of the entity, entity-local
ecol capsule <enabled> cx cy cz radius height dir [path]   (dir 0 X, 1 Y, 2 Z), path = child GameObject if not the
ecol sphere <enabled> cx cy cz radius [path]               entity itself (e.g. ChangingTree's "collider" capsule)
```

After all entities of the dream:
```
spawn x y z yawdeg dayone tunnel <name>      as dreams.txt (name = the PlayerSpawn entity's ID)
```

After all dreams:
```
graph <361 values>          the mod's GraphSpawnMap: 19x19, values are indices into the ADDITIONAL dream list (-1 none)
gcolor <idx> r g b          GraphSpawnMap display colour of each dream (dream chart)
```

## 3. world.bin

```
char[4] "AWLD"
u32     version = 1
f32     cell                           chunk cell size (16)
u16     ntex
tex[ntex]:      str file (e.g. "ADDTEX/atlas.tex"), u16 w, u16 h
u16     nmat
mat[nmat]:      u8  shader                0 cutout (ClassicDiffuse[Set]), 1 alpha blend (...AlphaBlend), 2 water (ClassicWater)
                u8  cull                  0 off (double-sided), 2 back faces culled (Unity CullMode)
                i16 tex[4]                texture index per texture set A, B, C, D (-1 = untextured = white)
                i16 pal[4]                water only: palette texture per set (-1 otherwise)
                f32 alpha                 water only: _Alpha (0.5)
                f32 speed                 water only: _AnimationSpeed (1)
                str name                  Unity material name (debug)
f32     bbox_min[3], bbox_max[3]       of all triangles
u32     nchunks
chunk[nchunks]:
        i16 cx, cz                     cell: triangle centroid x in [cx*cell, (cx+1)*cell), z likewise
        f32 min[3], max[3]             exact bounding box of the chunk's triangles (can exceed the cell)
        u32 ntris
        tri[ntris]                     76 bytes each:
          u16 mat                      index into the material table
          u8  flags                    bit 0 = drawn, bit 1 = solid (collision)
          u8  0
          3 x { f32 x, y, z; f32 u, v; u8 r, g, b, a }
```

* **Texture sets**: `tex[set - 1]` with Revamped's texture set number 1..4 (the same TEXA..TEXD day rule the app
  already uses for the original dreams). The `Atlas*` materials use `ClassicDiffuseSet` with 4 different atlases
  (atlas, atlas-royalty3d, atlas-bor-roy-figglewatts, atlas-catpat); all other materials repeat one index 4 times.
* **Shader 0 (cutout)**: `discard if tex.a <= 25` (LSDR.cginc `<= 0.1`), else opaque, z-write.
* **Shader 1 (alpha blend)**: no cutout, `alpha = tex.a * col.a / 255`, blend `src*a + dst*(1-a)`, no z-write, drawn after the
  opaque pass (Unity "Transparent" queue). Used only by models: the giant spheres and shadow people
  (IntenseColorAlpha), the aeroplanes (ColorAlpha). No static world triangle of the 14 dreams is blended.
  The app only has a 50 % blend: `alpha >= 0.5 -> 50 %` (or dither) is a fair approximation.
* **Shader 2 (water)**: `tex[set]` is the water map (128x128, the red channel is the palette position), `pal[set]` the
  256x1 palette; `index = frac(map.r / 255 - t * speed / 20)` (t in seconds, Unity `_Time[0] = t / 20`),
  `rgb = palette[index * 256] * col * 2`, `alpha = min(alpha, map.a)` (0.5): the same cycle as the app's `water.bin`
  water. UVs of water triangles sample the water map directly (repeat). Only palette A exists in the mod (set B-D
  materials are unset in Water.mat), so all four entries point to A. School (pond) and Leisure Centre (pool) use it.
* **Fog / draw distance**: LSDR.cginc does not draw vertices farther than the fog end (`min(fogEnd, drawDistance)`) from
  the camera; cull whole chunks by their bbox against fog end + the view frustum, then per triangle.
* **Collision** (`flags & 2`): every triangle of the FBX meshes that Unity gave a MeshCollider (the dream meshes have
  "Generate Colliders" on; the 86 flower meshes of Dreamscape have their collider disabled -> drawn, not solid), plus
  **collider-only triangles** (`flags == 2`, material named `collider`, never drawn) made from the static BoxColliders
  (tables, sofas, beds, shelves... 12 triangles each) and Capsule/Sphere colliders (8-sided prism, 28 triangles).
  Props without a collider (e.g. pizza boxes, laptop) are walk-through, as in Unity. Entities' colliders are `ecol` lines.
* Triangles that a single chunk holds are not split; huge triangles (big floors) just make that chunk's bbox larger.

## 4. amodels.bin

Same structure as models.bin (MODELS_FORMAT.md) with the triangle format of world.bin and named animations:

```
char[4] "AMDL"
u32     version = 1
u32     nmodels
model[nmodels]:
  str   key                 "Assets/Models/Props/bike.fbx" (the FBX path in the mod project)
  u16   nmeshes
  mesh[nmeshes]:  u32 ntris, tri[ntris] (76 bytes, as world.bin; mat indexes THIS dream's world.bin material table;
                  flags bit 0 = 1)
  u16   nobjects            Unity GameObjects of the imported model, parents before children
  object[nobjects]:
    i16 parent              -1 for object 0, else index into this list
    str name                FBX node name (object 0: the file name)
    f32 pos[3], quat[4], scale[3]   imported local pose (object 0: identity, its real placement is the model line)
    u8  active              1
    u16 nmeshes, u16 mesh[nmeshes]  meshes drawn with this object's transform (vertices object-local)
  u16   nanims
  anim[nanims]:
    str name                Unity clip name (e.g. "BikeCycling")
    f32 fps                 24 for every clip of the mod
    u16 length              clip length in frames (lastFrame - firstFrame); seconds = length / fps
    u8  loop                loopTime of the clip (0 = play once and hold the last frame)
    u16 ntracks
    track[ntracks]:
      u16 object
      u16 nkeys
      key[nkeys]: u16 frame, f32 pos[3], quat[4], scale[3]     absolute local TRS; keys sampled at every frame,
                                                               keys inside constant runs dropped (interpolate
                                                               linearly or hold, both are fine at 24 fps)
```

* **Object 0** is the root GameObject of the imported model (for a single-node FBX that node itself, which then also
  draws its mesh). Its keys (if a clip animates the FBX root, e.g. the basketball and tyre bounce, the popups' scale)
  are **relative to the model line's TRS**: `World(0) = EntityFrame * ModelTRS * Key0(t)`.
* **Skinned meshes** (schoolboy, schoolgirl, swimmer, fish, shadow person, snake, bird, ant hill, glowing figure) are
  rigidly bound: every triangle follows the bone with the largest summed weight of its corners and is stored in that
  bone's bind space, so the hierarchy animation drives them PS1-style. No vertex skinning needed.
* Animations come from the FBX takes as cut by the meta's clipAnimations (first/last frame, loop), sampled at 24 fps
  (the FBX frame rate; Unity's resampling rate). Clip -> AnimatedObject index: `mclips` line.
* Bone poses set in the prefabs on objects inside imported models (editor preview poses of bones, e.g. 312 swimmer bone
  rotations) are **not** applied (see 6.); animations overwrite them as soon as they play, and every skinned model
  in the mod plays a clip by default (autoplay) except the glowing figure (static).

## 5. ADDTEX/*.tex

```
char[4] "ATEX", u16 w, u16 h, then w*h RGBA8 texels, rows top to bottom
```
The four 1024x1024 atlases (8x8 grid of 128x128 textures) are box-filtered to 512x512 by default (64x64 per texture,
alpha-weighted so cutout edges do not darken; alpha averaged): 1 MB each instead of 4 MB, and closer to PS1 texel
density at 320x240. `--tex 1024` keeps the originals. Only one set (A-D) is needed per dream load.

## 6. What is not converted / approximated

* Unity's fileIDs of objects *inside* imported FBX models are hashes (ModelImporter `fileIdsGeneration: 2`) that the
  baker cannot compute (tried MD4/MD5/SHA1/xxHash64/xxH3/SpookyHash over "Type:<cls>-><path><n>" variants); only the
  root GameObject (919132149155446097) and root Transform (-8679921383154817045) IDs are known (constant).
  Prefab overrides targeting other FBX objects are matched by name where the intent is unambiguous and the count
  matches exactly (`FBX_FIXUPS` in addl.py): Dreamscape's 86 flower MeshColliders disabled, the ChangingPlant shrubs
  hidden (4 per pot), the STG13 cat beds showing only the cat they are named after; `m_IsActive/m_Enabled = 1`
  overrides are no-ops. Ignored: bone pose overrides (see 4.) and one `y += 0.01` move inside stg00.fbx.
* `PlayerRef` in stg00.fbx (an untextured box without material, under the floor) is dropped.
* Particle systems: `particles` lines only.
* `hospital_bed.fbx` material "atlas" has no remap in its meta: Unity makes a Standard-shader material from the FBX;
  baked as a cutout atlas material (texture A).
* Revamped (non-classic) shaders of particle materials are treated as their classic equivalents.
* The `.blend` files are ignored (the FBX exports are what Unity uses).
* Lua: besides convert.py's `lua_lambdas` (MoonSharp `|a| expr`), `!=` is rewritten to `~=` (MoonSharp accepts both;
  popup.lua uses `!=`). Note: the original game's `STG02/Scripts/angel.lua` has a `!=` too, which convert.py does not
  rewrite. All 25 converted scripts pass a Lua syntax check (`--verify`, needs the `lupa` module).
* Animator controllers: only "play state X" and the default state are modelled; the mod's controllers have no
  transitions or parameters. A clip of another FBX in the same controller (popup2-4 use the popup1-default
  controller) is not bound.

## 6b. How the app uses it (src/meshworld.c, render.c, level.c, model.c, dreamaudio.c; PORT-PLAN.md M8)

* `convert.py` runs `addl.py` into the same data dir (default `--cell 8`, `--noaddl` to skip). The app reads
  `dreams.txt` first (dreams 0..13), then `dreams_addl.txt` and appends its dreams (14..27); `K` link targets and the `graph`
  values of the mod are offset by 14 at load. `luainclude` sets `require`'s directory for scripts of mod dreams.
* world.bin: drawn triangles with an edge > 1.25 units are split at load for drawing (the original stays as a collision-only
  triangle); chunks are culled by their box (view distance = min(render distance option, fog end) + 1, and the frustum),
  then triangles by outcode; the visible chunks are projected on all render threads. Cull-mode 2 materials are back-face
  culled after projection (none of the 14 dreams' static world uses cull 2; models may).
* Textures: generic RTex path of the rasterizer (power-of-two wrap, nearest), cutout `a <= 25`, real alpha blending, water
  palette cycling; the texture set A-D follows the original dreams' day rule.
* Gates are rolled once per dream load (Ent.gate_ok); `epath` children follow their parent's transform and active state;
  scripts of mod dreams run `start()` for every active object at load (Unity's Start), CallStartOnEnable re-runs it on
  re-activation; `gcontrib` logs once within its radius (3D); `ecol` colliders block the player while their entity is
  active (boxes, upright cylinders); `audio` entities play through a small 8-voice mixer (linear roll-off, no panning,
  pitch by resampling); `particles` are ignored.

## 7. Numbers (default settings: --tex 512, --cell 16)

| dream | world tris (drawn / solid) | chunks (largest, drawn tris) | entities L/K/I/T | scripts | spawns | model instances (models, tris, anims) | audio / particles / gates |
|---|---|---|---|---|---|---|---|
| ADD00 Dreamscape | 93772 (93772 / 85160) | 76 (5284) | 0/3/72/113 | 5 | 64 | 72 (4, 568, 5) | 39 / 0 / 13 |
| ADD01 Amber Street | 11792 (11764 / 11744) | 8 (3055) | 0/5/46/44 | 6 | 22 | 133 (10, 1314, 5) | 18 / 0 / 6 |
| ADD02 Concretorium | 14614 (14614 / 14614) | 11 (3414) | 0/3/50/106 | 2 | 33 | 69 (3, 460, 0) | 54 / 34 / 38 |
| ADD03 Power Station | 22675 (22675 / 22675) | 14 (6129) | 0/1/1/77 | 1 | 23 | 1 (1, 476, 0) | 35 / 21 / 34 |
| ADD04 School | 17821 (17821 / 17821) | 33 (3113) | 0/3/106/74 | 7 | 64 | 364 (10, 1526, 5) | 10 / 0 / 5 |
| ADD05 Ziggurat | 1844 (1700 / 854) | 4 (678) | 0/1/0/10 | 0 | 8 | 9 (4, 40, 0) | 1 / 0 / 2 |
| ADD06 Trash Out | 6109 (6109 / 6109) | 19 (876) | 0/1/80/54 | 6 | 33 | 278 (9, 1084, 5) | 16 / 0 / 11 |
| ADD07 Forum | 1936 (1936 / 1936) | 6 (578) | 0/1/14/30 | 1 | 11 | 56 (4, 104, 4) | 19 / 0 / 5 |
| ADD08 Leisure Centre | 5060 (5024 / 5060) | 6 (1750) | 0/1/13/100 | 2 | 22 | 66 (5, 924, 2) | 0 / 0 / 54 |
| ADD09 Mill | 2456 (2336 / 1112) | 4 (902) | 1/1/14/32 | 3 | 11 | 19 (5, 362, 1) | 21 / 0 / 31 |
| ADD10 Belvoir | 2725 (2521 / 827) | 4 (1856) | 1/2/7/25 | 2 | 9 | 13 (5, 384, 0) | 15 / 0 / 23 |
| ADD11 Pooley | 1152 (1080 / 394) | 4 (526) | 1/1/2/11 | 2 | 6 | 7 (4, 306, 0) | 5 / 0 / 7 |
| ADD12 Railway | 1950 (1806 / 800) | 4 (1592) | 1/2/4/22 | 2 | 10 | 10 (5, 391, 0) | 11 / 0 / 17 |
| ADD13 Eastern | 2632 (2440 / 1154) | 4 (1058) | 1/2/2/31 | 2 | 11 | 9 (5, 498, 0) | 19 / 0 / 29 |

(T includes the PlayerSpawn entities and the pseudo entities.) Textures: the 4 atlases 512x512 (1 MB each) for all
dreams, plus water.tex 128x128 and the 256x1 palette (School, Leisure Centre) and steam.tex 8x8 (particles).

**Load**: visible triangles with a corner within fog end of a spawn point, all directions (a 90 degree frustum sees
about a third): Dreamscape avg 3.3k / max 10.2k at fog 12, 9.7k / 18.6k at fog 25; Power Station 8.0k / 16.6k at 12;
Amber Street 3.6k / 5.4k; Concretorium 4.4k / 7.3k; School 1.6k / 4.3k at 11; the interiors 1-2.5k total.
Dreamscape's terrain alone is 59.8k triangles (a ~0.5-unit grid) plus 139 "ScapeTree" instances (24.7k) and 86 flower
meshes (8.6k); Power Station's single mesh is 22.4k. Recommended runtime culling, in this order: chunk bbox vs fog end
(LSDR.cginc does not draw beyond it anyway) -> chunk bbox vs view frustum -> per triangle fog end / near-plane / back
face (cull = 2 materials). If Dreamscape / Power Station are still too heavy on hardware: a smaller `--cell` (8)
for finer culling, and a distance LOD that skips the ScapeTree / FlowerPatch instances beyond ~60 % of fog end (both
are separate draw groups by material/node; easy to flag in the baker if needed).
