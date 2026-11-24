/* audio.h - the mixer: one SDL device opened once at boot (48 kHz stereo s16), the music stream (a looping WAV in memory,
 * resampled to the device rate) and AUDIO_VOICES sound voices (mono clips, per-voice pitch by linear-interpolated
 * resampling, left/right gain). Groups as in Revamped's MasterMixer: Music (the song) and SFX (everything else).
 * Voices sound whether or not a song is playing. */
#ifndef AUDIO_H
#define AUDIO_H
#include <stdint.h>
#include <stddef.h>

#define AUDIO_VOICES   24
#define AUDIO_DA0      0       /* voices 0..11: DreamAudio sources (dreamaudio.c allocates them, loudest first) */
#define AUDIO_DA_N     12
#define AUDIO_POOL0    12      /* 12..21: one-shot 2D sounds (Torii AudioPlayer.PlayClip): footsteps, link, fall, sound test */
#define AUDIO_POOL_N   10
#define AUDIO_UI0      22      /* 22, 23: menu hover / press */

typedef struct { int16_t *s; uint32_t n; int rate; } AClip;     /* mono s16 (native endian) at its own rate */

int  audio_init(void);                              /* opens the device; 0 = ok (no device: everything is silent) */
void audio_shutdown(void);
int  audio_ok(void);
unsigned audio_callbacks(void);                     /* how often the device asked for data (diagnostics) */
void audio_set_volume(int music, int sfx);          /* options 0..10 each */

/* music: a WAV file (any rate, mono or stereo), copied and looped; replaces the song playing */
int  audio_music_start(const uint8_t *wav, size_t len);   /* 0 = playing */
void audio_music_stop(void);
void audio_music_fade(float seconds);               /* MusicSystem.StopSong: fade out, then stop */
int  audio_music_on(void);

/* clips and voices */
int  audio_clip_load(const uint8_t *wav, size_t len, AClip *out);   /* 0 = ok */
void audio_clip_free(AClip *c);
/* start clip c on voice v: gains 0..1 (before the SFX volume option), offset = position in the clip in seconds,
 * pitch = playback rate factor (0.25 = 4x slower and lower) */
void audio_voice_play(int v, const AClip *c, float gl, float gr, int loop, float offset, float pitch);
void audio_voice_gain(int v, float gl, float gr);
void audio_voice_stop(int v);
int  audio_voice_on(int v);
/* AudioPlayer.PlayClip: a free voice of the 2D pool plays c once (no stealing: dropped when all are busy); returns the
 * voice or -1 */
int  audio_play2d(const AClip *c, float pitch, float vol);

/* menu sounds (UIButtonSounds) on their own voices: a new one restarts it */
enum { SFX_HOVER, SFX_PRESS };
void audio_sfx_load(int slot, const uint8_t *wav, size_t len);
void audio_sfx(int slot);
#endif
