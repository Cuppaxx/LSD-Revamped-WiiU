/* audio.c - see audio.h. The device is opened once (48000 Hz stereo s16, SDL converts if the hardware differs) and the
 * callback mixes the music and every voice into an int32 buffer, clamped at the end. Resampling is linear interpolation
 * with a 16.16 fixed-point step (music: source rate / device rate; voices: clip rate * pitch / device rate). */
#include "audio.h"
#include "video.h"
#include <SDL2/SDL.h>
#include <stdlib.h>
#include <string.h>

void lsd_log(const char *fmt, ...);

static SDL_AudioDeviceID dev;
static SDL_AudioSpec have;
static volatile unsigned callbacks;
static int music_vol = 256 * 6 / 10, sfx_vol = 256 * 7 / 10;     /* 0..256 */

/* music: s16 native interleaved, mch channels at mrate */
static int16_t *mpcm; static uint32_t mframes; static int mch = 2, mrate = 44100;
static uint32_t mpos, mfrac, mstep;
static float mgain = 1.0f, mfade_step;     /* fade: gain falls by mfade_step per output frame; reaching 0 stops the song */

typedef struct { const AClip *c; uint32_t idx, frac, step; int gl, gr, loop, on; } Voice;   /* gains 0..256 */
static Voice voices[AUDIO_VOICES];
static AClip ui_clip[2];

static int32_t *acc; static int acc_frames;

static inline int16_t smp_at(const int16_t *s, uint32_t n, uint32_t i, int loop) { return i < n ? s[i] : loop && n ? s[i % n] : 0; }

static void cb(void *ud, Uint8 *out, int len) {
  (void)ud; callbacks++;
  int ch = have.channels > 0 ? have.channels : 2, frames = len / (2 * ch);
  if (frames > acc_frames) { memset(out, 0, (size_t)len); return; }
  memset(acc, 0, sizeof(int32_t) * 2 * (size_t)frames);
  /* music */
  if (mpcm && mframes && music_vol > 0) {
    for (int f = 0; f < frames; f++) {
      int g = (int)(mgain * music_vol);
      uint32_t i = mpos, j = mpos + 1 >= mframes ? 0 : mpos + 1;
      int a0, a1, b0, b1;
      if (mch >= 2) { a0 = mpcm[i * mch]; a1 = mpcm[j * mch]; b0 = mpcm[i * mch + 1]; b1 = mpcm[j * mch + 1]; }
      else { a0 = b0 = mpcm[i]; a1 = b1 = mpcm[j]; }
      int l = a0 + (int)(((int64_t)(a1 - a0) * mfrac) >> 16), r = b0 + (int)(((int64_t)(b1 - b0) * mfrac) >> 16);
      acc[f * 2] += (l * g) >> 8; acc[f * 2 + 1] += (r * g) >> 8;
      mfrac += mstep; mpos += mfrac >> 16; mfrac &= 0xFFFF;
      while (mpos >= mframes) mpos -= mframes;                     /* the song loops */
      if (mfade_step > 0) { mgain -= mfade_step; if (mgain <= 0) { mgain = 0; mfade_step = 0; break; } }
    }
  }
  /* voices */
  for (int v = 0; v < AUDIO_VOICES; v++) {
    Voice *vc = &voices[v];
    if (!vc->on || !vc->c || !vc->c->s) continue;
    const int16_t *s = vc->c->s; uint32_t n = vc->c->n;
    int gl = vc->gl * sfx_vol >> 8, gr = vc->gr * sfx_vol >> 8;
    for (int f = 0; f < frames; f++) {
      if (vc->idx >= n) { if (vc->loop && n) vc->idx %= n; else { vc->on = 0; break; } }
      int a = s[vc->idx], b = smp_at(s, n, vc->idx + 1, vc->loop);
      int x = a + (int)(((b - a) * (int32_t)vc->frac) >> 16);
      acc[f * 2] += (x * gl) >> 8; acc[f * 2 + 1] += (x * gr) >> 8;
      vc->frac += vc->step; vc->idx += vc->frac >> 16; vc->frac &= 0xFFFF;
    }
  }
  video_audio_mix(acc, frames, have.freq > 0 ? have.freq : 48000, music_vol);   /* a movie's sound (video.c), at the music volume */
  int16_t *o = (int16_t *)out;
  for (int f = 0; f < frames; f++)
    for (int k = 0; k < ch; k++) {
      int32_t x = ch == 1 ? (acc[f * 2] + acc[f * 2 + 1]) / 2 : acc[f * 2 + (k & 1)];
      o[f * ch + k] = (int16_t)(x > 32767 ? 32767 : x < -32768 ? -32768 : x);
    }
}

int audio_init(void) {
  if (dev) return 0;
  if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) { lsd_log("audio: init failed: %s", SDL_GetError()); return -1; }
  SDL_AudioSpec want; memset(&want, 0, sizeof want);
  want.freq = 48000; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 1024; want.callback = cb;
  /* no ALLOW_* flags: the device takes exactly this format (SDL converts behind the scenes if the hardware differs) */
  dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
  if (!dev) { lsd_log("audio: no device: %s", SDL_GetError()); return -2; }
  acc_frames = (have.samples > 0 ? have.samples : 1024) * 4; acc = (int32_t *)malloc(sizeof(int32_t) * 2 * (size_t)acc_frames);
  SDL_PauseAudioDevice(dev, 0);
  lsd_log("audio: device %d Hz, %d channels, %d samples", have.freq, have.channels, have.samples);
  return 0;
}

void audio_shutdown(void) {
  if (dev) { SDL_CloseAudioDevice(dev); dev = 0; }
  free(mpcm); mpcm = NULL; mframes = 0; memset(voices, 0, sizeof voices);
}

int audio_ok(void) { return dev != 0; }
unsigned audio_callbacks(void) { return callbacks; }

void audio_set_volume(int music, int sfx) {
  music = music < 0 ? 0 : music > 10 ? 10 : music; sfx = sfx < 0 ? 0 : sfx > 10 ? 10 : sfx;
  music_vol = 256 * music / 10; sfx_vol = 256 * sfx / 10;
}

static void lock(void) { if (dev) SDL_LockAudioDevice(dev); }
static void unlock(void) { if (dev) SDL_UnlockAudioDevice(dev); }

/* decode a WAV and convert it to s16 native with the given channels (0 = keep) at its own rate; *out is malloc'd */
static int convert_wav(const uint8_t *wav, size_t len, int channels, Uint8 **out, Uint32 *out_len, SDL_AudioSpec *src) {
  SDL_AudioSpec spec; Uint8 *buf; Uint32 blen;
  if (!SDL_LoadWAV_RW(SDL_RWFromConstMem(wav, (int)len), 1, &spec, &buf, &blen)) return -2;
  if (src) *src = spec;
  if (channels <= 0) channels = spec.channels;
  SDL_AudioCVT cvt;
  int need = SDL_BuildAudioCVT(&cvt, spec.format, spec.channels, spec.freq, AUDIO_S16SYS, (Uint8)channels, spec.freq);
  if (need < 0) { SDL_FreeWAV(buf); return -3; }
  if (need == 0) { *out = (Uint8 *)malloc(blen); if (!*out) { SDL_FreeWAV(buf); return -4; } memcpy(*out, buf, blen); *out_len = blen; SDL_FreeWAV(buf); return 0; }
  cvt.len = (int)blen; cvt.buf = (Uint8 *)malloc((size_t)cvt.len * cvt.len_mult);
  if (!cvt.buf) { SDL_FreeWAV(buf); return -4; }
  memcpy(cvt.buf, buf, blen); SDL_FreeWAV(buf);
  if (SDL_ConvertAudio(&cvt) < 0) { free(cvt.buf); return -3; }
  *out = cvt.buf; *out_len = (Uint32)cvt.len_cvt;
  return 0;
}

int audio_music_start(const uint8_t *wav, size_t len) {
  Uint8 *b; Uint32 n; SDL_AudioSpec spec;
  int rc = convert_wav(wav, len, 0, &b, &n, &spec);
  if (rc) { audio_music_stop(); return rc; }
  int ch = spec.channels > 0 ? spec.channels : 1;
  int fr = have.freq > 0 ? have.freq : 48000;
  lock();
  int16_t *old = mpcm;
  mpcm = (int16_t *)b; mch = ch; mrate = spec.freq; mframes = n / (2u * (unsigned)ch); mpos = 0; mfrac = 0;
  mstep = (uint32_t)((double)mrate / fr * 65536.0); mgain = 1.0f; mfade_step = 0;
  unlock();
  free(old);
  return dev ? 0 : -1;
}

void audio_music_stop(void) { lock(); int16_t *old = mpcm; mpcm = NULL; mframes = 0; unlock(); free(old); }
void audio_music_fade(float seconds) {
  if (!mpcm) return;
  if (seconds <= 0) { audio_music_stop(); return; }
  lock(); mfade_step = 1.0f / (seconds * (have.freq > 0 ? have.freq : 48000)); unlock();
}
int audio_music_on(void) { return mpcm != NULL && (mgain > 0 || mfade_step == 0); }

int audio_clip_load(const uint8_t *wav, size_t len, AClip *out) {
  memset(out, 0, sizeof *out);
  Uint8 *b; Uint32 n; SDL_AudioSpec spec;
  if (convert_wav(wav, len, 1, &b, &n, &spec)) return -3;          /* mono s16 native at its own rate */
  out->s = (int16_t *)b; out->n = n / 2; out->rate = spec.freq;
  return 0;
}
void audio_clip_free(AClip *c) { free(c->s); memset(c, 0, sizeof *c); }

static int g256(float g) { int v = (int)(g * 256.0f + 0.5f); return v < 0 ? 0 : v > 256 ? 256 : v; }

void audio_voice_play(int v, const AClip *c, float gl, float gr, int loop, float offset, float pitch) {
  if (v < 0 || v >= AUDIO_VOICES) return;
  lock();
  Voice *vc = &voices[v]; vc->c = c; vc->gl = g256(gl); vc->gr = g256(gr); vc->loop = loop; vc->frac = 0; vc->on = c && c->s && c->n;
  if (vc->on) {
    int fr = have.freq > 0 ? have.freq : 48000; if (pitch <= 0) pitch = 1;
    vc->step = (uint32_t)((double)c->rate * pitch / fr * 65536.0); if (!vc->step) vc->step = 1;
    double o = offset > 0 ? (double)offset * c->rate : 0;
    uint32_t i = (uint32_t)o;
    if (i >= c->n) { if (loop) i %= c->n; else vc->on = 0; }
    vc->idx = i;
  }
  unlock();
}
void audio_voice_gain(int v, float gl, float gr) { if (v >= 0 && v < AUDIO_VOICES) { voices[v].gl = g256(gl); voices[v].gr = g256(gr); } }   /* int stores: no lock needed */
void audio_voice_stop(int v) { if (v < 0 || v >= AUDIO_VOICES) return; lock(); voices[v].on = 0; voices[v].c = NULL; unlock(); }
int  audio_voice_on(int v) { return v >= 0 && v < AUDIO_VOICES && voices[v].on; }

int audio_play2d(const AClip *c, float pitch, float vol) {
  if (!dev || !c || !c->s) return -1;
  for (int v = AUDIO_POOL0; v < AUDIO_POOL0 + AUDIO_POOL_N; v++)
    if (!voices[v].on) { audio_voice_play(v, c, vol, vol, 0, 0, pitch); return v; }
  return -1;                                                     /* all ten busy: dropped, like AudioPlayer */
}

void audio_sfx_load(int slot, const uint8_t *wav, size_t len) {
  if (slot < 0 || slot > 1 || !wav) return;
  audio_clip_free(&ui_clip[slot]);
  if (audio_clip_load(wav, len, &ui_clip[slot])) lsd_log("audio: bad menu sound %d", slot);
}
void audio_sfx(int slot) { if (slot >= 0 && slot <= 1 && ui_clip[slot].s) audio_voice_play(AUDIO_UI0 + slot, &ui_clip[slot], 1, 1, 0, 0, 1); }
