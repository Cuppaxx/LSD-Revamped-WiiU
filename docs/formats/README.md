# Data formats

Reference documents for the files `tools/` writes and `app/src` reads. They were written during development, so their
command examples use the development tree's paths: `src/` = the LSDRevamped clone, `ref/LSDR-Additional-Dreams/` = the
mod clone, `tools/out_*` = staging folders, `content/data` = a data folder. With this repository you normally run
`tools/make_data.py` instead (see [../DATA.md](../DATA.md)).

| Document | Covers |
|---|---|
| [MODELS_FORMAT.md](MODELS_FORMAT.md) | `models.bin`: MOM models (TMD meshes + TOD animations) as Revamped imports them |
| [ADDL_FORMAT.md](ADDL_FORMAT.md) | the Additional Dreams bake: `dreams_addl.txt`, `world.bin`, `amodels.bin`, `.tex` |
| [AUDIO_FORMAT.md](AUDIO_FORMAT.md) | `audio.txt`, footsteps, SFX bank, DreamAudio, link/fall sounds, music rules |
| [SPECIAL_FORMAT.md](SPECIAL_FORMAT.md) | `specials.txt`, special days, text days, `.mjv` movies, credits, flashbacks |

The dream table `dreams.txt` is described at the top of `tools/convert.py`.
