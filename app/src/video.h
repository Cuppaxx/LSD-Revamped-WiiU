/* video.h - the movie player: .mjv (MJPEG frames, tools/SPECIAL_FORMAT.md section 3) + .wav (22050 Hz mono s16) from
 * SD:/wiiu/apps/lsdr/data/MOVIE/<NAME>.{mjv,wav} (movies are not bundled in the wuhb; a missing file just means "no movie").
 * One movie at a time. Frames are decoded with libturbojpeg into a 320x240 ARGB buffer only when the shown frame changes
 * (15 per second). The audio is the master clock: frame = samples played * fps / rate; when the audio has ended (or there
 * is no audio device / no .wav) the clock continues on the wall clock.
 *
 * AUDIO INTEGRATION: the movie's sound is mixed by the app's audio callback. audio.c calls
 *   video_audio_mix(acc, frames, device_rate, music_vol256)
 * once per callback (after the music and the voices, before the clamp), where acc is the int32 stereo accumulator
 * (L,R interleaved, `frames` frames). It adds the movie audio at the music volume (Revamped VideoPlayerVolumeSetter) and
 * advances the clock. It takes its own spin lock, never the device lock, so it is safe to call from the callback. */
#ifndef VIDEO_H
#define VIDEO_H
#include <stdint.h>
#include "raster.h"

void video_set_base(const char *sdbase);      /* "fs:/vol/external01" style mount; movies are read from <base>/wiiu/apps/lsdr/data/MOVIE */
int  video_exists(const char *name);          /* the .mjv is there */
int  video_open(const char *name);            /* 0 = playing (clock starts now); <0 = missing or bad file (nothing to play) */
void video_close(void);
void video_stop_audio(void);                  /* Pause(): the picture stays (video_draw), the sound stops */
int  video_is_open(void);
/* advance: decode the frame for the current clock; returns 1 while the movie runs, 0 once it has ended (i >= N) */
int  video_update(void);
/* draw the current frame into f: fit inside (4:3 movie: fills a 320x240 frame; pillarbox in wider frames), black around it */
void video_draw(Frame *f);
float video_time(void);                       /* seconds into the movie */
const char *video_name(void);

/* called from the audio callback (see above) */
void video_audio_mix(int32_t *acc, int frames, int rate, int vol256);

#endif
