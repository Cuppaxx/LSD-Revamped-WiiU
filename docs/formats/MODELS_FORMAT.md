# models.bin and the dreams.txt model lines

Written by `tools/convert.py` (logic in `tools/models.py`). One `models.bin` per dream folder
(`data/<STGxx>/models.bin`) holding every MOM model (TMD meshes + TOD animations from an LBD's MML section) that an
entity of that dream uses. Every dream STG00..STG13 has one.

## Coordinate conventions

Same as the level code: Unity space, left-handed, Y up, +Z forward. Vertices are already `(x, -y, z) / 2048`.
A transform is TRS applied to column vectors, `p' = T * R * S * p`; quaternions are Unity's `(x, y, z, w)`.
UVs are VRAM-atlas texel coordinates exactly as `lsd.c` `uv_to_atlas` produces them (`-1, -1` = untextured);
the textures are in the dream's normal TIX atlas. Colours follow `build_object` (128 = neutral for textured
prims without colour).

## models.bin

All integers and floats are **big-endian**. `str` = `u8 length` + bytes (no terminator). No padding anywhere.

```
char[4] "MODL"
u32     version            = 1
u32     nmodels
model[nmodels]:
  str   key                "<LBD path relative to the dream folder>#<MOM index>"  e.g. "M001.LBD#2", "N1/M024.LBD#0"
  u16   nmeshes            one per TMD object of the MOM; mesh index = TMD object index
  mesh[nmeshes]:
    u32 ntris
    tri[ntris]             73 bytes each:
      3 x { f32 x, y, z; f32 u, v; u8 r, g, b, a }   (a is always 255)
      u8 alpha_blend                                  (TMD ABE bit, same as LTri.alpha_blend)
  u16   nobjects           GameObjects of the model, parents always before children
  object[nobjects]:
    u16 id                 TOD object id (0 = the animation root "0"). For a MOM without animations: the TMD index
    i16 parent             index into this object list; -1 = child of the model root (see "model" line below)
    str name               Unity GameObject name: the id as text ("0", "5", "17", ...)
    f32 pos[3], quat[4], scale[3]    initial local pose (what Unity's imported asset stores)
    u16 nmeshes
    u16 mesh[nmeshes]      mesh indices drawn with this object's world transform (vertices are object-local)
  u16   nanims             = number of TODs = AnimatedObject.Clips.Length; anim k = Clips[k] = TOD k
  anim[nanims]:
    u16 nframes            TOD frame count (NumberOfFrames - 1)
    u16 length_frames      Unity clip length in frames = latest key frame of all tracks; seconds = length_frames / 20
    u16 ntracks
    track[ntracks]:
      u16 object           index into the object list
      u16 nkeys
      key[nkeys]:          sorted by frame, at most one key per frame
        u16 frame
        f32 pos[3], quat[4], scale[3]   absolute local TRS of the object at that frame
```

Models without animations (static MOMs) have `nanims = 0` and one object per TMD object, named `"{index}"`,
parent -1, identity pose, drawing that mesh.

### Animation semantics (what Unity does)

* 20 frames per second (MOMHelper `frameTime = 1/20`), clips loop (`loopTime = true`); Unity's clip length is
  `length_frames / 20` s, so the loop period is `length_frames` frames, and `WaitForAnimation` /
  `GetAnimationLengthSeconds` return that length. A clip whose only keys are on frame 0 has length 0.
* Every key stores all 10 components, so a track fully owns its object's local TRS while the clip plays.
  Objects without a track in the playing clip keep whatever local TRS they had.
* Sampling: the converter's intent (and model.h) is "hold the latest key at or before the frame". Strictly,
  Unity interpolates between keys with flat (zero) tangents, because MOMHelper's `curve.keys[i].inTangent = ...`
  modifies a copy and has no effect. Keys are usually on every frame, so stepping vs. interpolating differs by
  less than one 50 ms frame. Quaternion signs are made continuous along each track (EnsureQuaternionContinuity),
  so a component-wise lerp + normalise is safe if you want smoothing.
* Before an animation is played (Animator sits in an empty "idle" state) the objects show their pose: the
  initial pose from models.bin, replaced per instance by the `mpose` lines below.
* `Play(k)`: start clip k at time 0. `Stop()`: Animator disabled, pose freezes. `Resume()`: continue.
* `AutoPlay >= 0` on the instance: `Play(AutoPlay)` on start and again every time the object is (re-)enabled.

How the baked data was derived (all verified against the C# importer, see models.py): hierarchy from TOD 0
frame 0 (ObjectControl Create -> object parented to "0"; TMDDataID -> mesh `ObjectID - 1` on that object;
ParentObjectID -> reparent). Coordinate packets: scale, then translation, then rotation; Absolute replaces,
Differential multiplies scale / adds translation / post-multiplies `AngleAxis(-RotX/4096, right)`,
`AngleAxis(RotY/4096, up)`, `AngleAxis(-RotZ/4096, forward)`; absolute rotation = `x * y * z` of those; translation
`(X, -Y, Z) / 2048`, scale `/ 4096`. The Transforms are shared by all clips, so state carries over from TOD k to
TOD k+1, and the stored initial pose is the state after the LAST TOD with TOD 0 frame 0's coordinate packets applied
once more on top (MOMImporter calls PoseObjectInFirstFrame after building the clips). 98 of the 281 prefab copies match
this initial pose exactly; the others differ only by intentional author edits, which come out as `mpose` lines.
TOD Attribute packets are ignored (Unity ignores them too).

Half of the baked MOMs (50 of 100) end with a TMD object of 8 triangles textured only with fully transparent
texels (a box around the model, invisible with alpha cutout; e.g. TVBear's mesh 5). It is kept as-is, Unity draws it
too; a renderer may skip triangles whose texels are all transparent if it wants to save time.

## dreams.txt additions

### `ent` lines (every kind, not only `I`)

Four tokens are appended at the very end of every `ent` line, after the kind-specific fields:

```
ent <kind> <name> ... <existing fields> <firstkey> <nmodel> <nmesh> <child0>
```

* `firstkey` - key of the first `model` line of this entity, else of the first `mmesh` line, else `-`.
* `nmodel`   - number of `model` lines that follow (animated / AnimatedObject instances).
* `nmesh`    - number of `mmesh` lines that follow them (loose meshes, no Animator).
* `child0`   - name of the entity's first child Transform (`transform.GetChild(0)`), `-` if none. This is what
  `SetChildVisible(v)` toggles (`GetChild(0).SetActive(v)`). Usually `0`, i.e. the model's animation root.

The existing `sscanf` calls in dream.c ignore these (they read a fixed number of fields from `rest`).
For an `I` line the tokens after `<script> <ikind> <idist>` are: `%63s %d %d %47s`
(for `T` lines `rest` starts with them; `L`: after 4 fields, `K`: after 5, `P`: after 5).

Example:
```
ent I TVBear 1 7.0000 1.0000 8.1875 -360.00 1.0000 1.0000 1.0000 0.0000 0.0000 0.0000 1.0000 1.0000 1.0000 STG00/Scripts/bmcBear.lua 0 0.6 M001.LBD#2 1 0 0
model M001.LBD#2 . -1 1 0.00000 0.00000 0.00000 0.000000 0.000000 -0.000000 1.000000 1.00000 1.00000 1.00000 1
mpose 0 1 0.00000 0.00000 0.00000 -0.000000 1.000000 -0.000000 -0.000000 1.00000 1.00000 1.00000
```

### `model` lines (follow their `ent` line; `nmodel` of them)

```
model <key> <path> <autoplay> <active> px py pz qx qy qz qw sx sy sz <nposes>
```

* `key` - the models.bin key.
* `path` - GameObject path of the instance root below the entity, names joined by `/`; `.` = the entity
  GameObject itself. Spaces in names are written as byte 0x1F (like entity names).
  `this.PlayAnimation(k)` / `StopAnimation` / `ResumeAnimation` / `GetAnimationLengthSeconds` / `WaitForAnimation`
  on an InteractiveObject, and `GetEntity(x).AnimatedObject` on any entity, address the instance with path `.`
  (InteractiveObject uses `GetComponent<AnimatedObject>()` on its own GameObject).
  `this.GameObject.GetChildByName("UnderwaterDudeOnWaterA").AnimatedObject.Play(0)` addresses the instance whose
  path is that name (STG02 UnderwaterDudeOnWater has two instances, A and B).
* `autoplay` - AnimatedObject.AutoPlay (-1 = none, else clip index played on start / enable).
* `active` - 1 if the instance root and every GameObject between it and the entity are active in the prefab.
* `p q s` - TRS of the instance root in the runtime entity frame. The renderer's world transform for model object o is
  ```
  World(o) = T(ent.pos) * R_yaw(ent.yaw) * S(ent.scale) * T(p) R(q) S(s) * L(root..o)
  ```
  where `L` is the chain of local TRS from the model's top object down to o. The baker folds any non-yaw part of
  the entity's real rotation into `q` (e.g. STG00 TV is rotated about Z), so the entity's `yaw` field stays the
  only rotation the runtime needs to know. For `.` instances this is normally the identity.
* `nposes` - number of `mpose` lines that follow.

### `mpose` lines (follow their `model` line)

```
mpose <object name> <active> px py pz qx qy qz qw sx sy sz
```

The prefab's copy of the model stores a different local pose (or an inactive flag) for this object; use it
instead of the models.bin initial pose for this instance. Typical cases: object `0` turned 180 degrees about Y
(TVBear, Bear, Peacock*, Angel...) or moved (STG00 Pterodactyl `0` at (0, -0.5625, -0.3125)); clips never key
object `0`, so these offsets are permanent. Other objects' overrides are mostly the pose left by previewing a clip
in the editor and are overwritten as soon as a clip that keys them plays. `active` 0 (64 lines) hides that object
and its subtree until something re-enables it (author-hidden parts: object 10 of the hanged girls / Ghost3,
17 of the rainbow penguins, 26 of the flowers / PurplePlayer, 21 of the geishas / RedPlayer, 11 of Girl1/2, 34 of
Dog). The Animator does not touch active flags.

### `mmesh` lines (after the `model` lines; `nmesh` of them)

```
mmesh <key> <tmd index> <path> <active> px py pz qx qy qz qw sx sy sz
```

A loose GameObject in the entity that draws mesh `<tmd index>` of model `<key>` with a fixed transform (entity
frame, same convention as `model`), no animation. Used by STG00 TV and the BedPerson/BedEmpty/BedDead entities,
STG03 birds, balloons, rainbows, shark fins, camels, FatherAndChild, FlyingBoat, STG05 corpses and the sailor.
`active` 0 = the GameObject (or a parent below the entity) is inactive in the prefab (e.g. the second mesh of
the shark fins and corpses).

### Lua hooks that address model parts

* `SetChildVisible(v)` -> `child0` (see above): toggles the instance/mesh whose path starts with that name, or, when
  `child0` is the model's own top object (`0`) of the `.` instance, that object (and with it the whole model).
* `GetChildByName(name)` -> `Transform.Find(name)`: either a `model` path (UnderwaterDudeOnWaterA/B) or a model
  object name of the `.` instance (`swimmingFish.lua`: `GetChildByName("0").LocalPosition = ...` moves object `0`).
* `SetRenderersActive(v)` -> every mesh-drawing GameObject below the entity, including inactive ones
  (all meshes of all instances and all `mmesh` lines).
* Entities nested inside another entity own their own models (e.g. STG05 HangedGirlN has no model; its
  `HangedGirlN_Group*` / `_Single` T entities do; STG04 Trumpeters vs Trumpeter1..6; STG00 PersonInBed vs
  BedPerson/BedEmpty/BedDead).
