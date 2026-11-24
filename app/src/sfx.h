/* sfx.h - the SFX bank and tables of data/audio.txt (tools/AUDIO_FORMAT.md): footsteps (LBD footstep keys for the original
 * dreams, texture-atlas cells for the Additional Dreams), the link/fall sound, the sound test, music rules */
#ifndef SFX_H
#define SFX_H
#include "audio.h"
#include "level.h"

void sfx_init(void);                                   /* reads audio.txt and preloads its clips (~1.1 MB) */
const AClip *sfx_clip(const char *path);               /* a clip from the global cache (loaded on first use, kept); NULL = missing */
/* PlayerMovement.playFootstepSound: the sound of the ground under the player's feet (nothing when airborne); returns the
 * key used (LBD key, atlas cell + 1000, -1 fallback, -2 nothing) */
int  sfx_footstep(const Level *lv, const Player *pl);
void sfx_link(unsigned *rng);                          /* DreamSystem.Transition with playSound: SE 9 at 0.25/0.5/1/2 */
void sfx_fall(unsigned *rng);                          /* EndDream(fromFall): SE 9 at 0.25/0.5 */
void sfx_soundtest(void);                              /* UIAudioSettings.SoundTest (restarts) */
int  sfx_mod_songs(const char *folder);                /* "ADDnn" -> the original dream whose song lists it plays, -1 none */
extern int g_sfx_songs_per_dream, g_sfx_day1_song;     /* musicrule original 5 2 */
extern int g_foot_log;                                 /* test hook: log every footstep with its tile */
#endif
