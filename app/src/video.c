/* video.c - see video.h. Container (all integers big-endian):
 *   0 "LMJV", 4 u16 version 1, 6 u16 w, 8 u16 h, 10 u16 fps num, 12 u16 fps den, 14 u16 flags (bit0 .wav exists),
 *   16 u32 frames N, 20 u32 max frame bytes, 24 u32 audio rate, 28 u32 audio samples, 32 u32 offset[N+1], JPEG data */
#include "video.h"
#include <SDL2/SDL.h>
#include <turbojpeg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void lsd_log(const char *fmt, ...);

static char s_base[200] = "fs:/vol/external01";
static FILE *vf; static char vname[32];
static uint32_t vw, vh, vfn, vfd, nframes, maxbytes, *offs;
static uint8_t *jbuf; static uint32_t *pix; static int cur_frame = -1, have_pix;
static tjhandle tj;
static uint32_t t0;                       /* SDL ticks at open */
/* audio (shared with the callback under alock) */
static SDL_SpinLock alock;
static int16_t *apcm; static uint32_t an, arate;
static volatile uint32_t apos;            /* samples played (source rate) */
static uint32_t afrac;
static volatile int aplaying;
static uint32_t last_apos, last_change_ms; static float last_change_t;
/* decode cost (logged) */
static uint64_t dec_us_sum, dec_us_max; static int dec_n;

void video_set_base(const char *b) { if (b) snprintf(s_base, sizeof s_base, "%s", b); }

static FILE *open_movie_file(const char *name, const char *ext) {
  char p[300]; FILE *f;
  snprintf(p, sizeof p, "%s/wiiu/apps/lsdr/data/MOVIE/%s.%s", s_base, name, ext);
  if ((f = fopen(p, "rb"))) return f;
  snprintf(p, sizeof p, "/vol/content/data/MOVIE/%s.%s", name, ext);   /* in case someone bundles them */
  return fopen(p, "rb");
}

int video_exists(const char *name) { FILE *f = open_movie_file(name, "mjv"); if (!f) return 0; fclose(f); return 1; }

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0]; }
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[1] << 8 | p[0]); }

/* a PCM s16 mono WAV, whole file -> native samples */
static int load_wav(const char *name) {
  FILE *f = open_movie_file(name, "wav"); if (!f) return -1;
  fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
  uint8_t *b = n > 44 ? (uint8_t *)malloc((size_t)n) : NULL;
  if (!b || fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); fclose(f); return -2; }
  fclose(f);
  if (memcmp(b, "RIFF", 4) || memcmp(b + 8, "WAVE", 4)) { free(b); return -3; }
  int ch = 0, bits = 0; uint32_t rate = 0; const uint8_t *data = NULL; uint32_t dlen = 0;
  for (long p = 12; p + 8 <= n;) {
    uint32_t cl = le32(b + p + 4);
    if (!memcmp(b + p, "fmt ", 4) && cl >= 16) { ch = le16(b + p + 10); rate = le32(b + p + 12); bits = le16(b + p + 22); if (le16(b + p + 8) != 1) { free(b); return -4; } }
    else if (!memcmp(b + p, "data", 4)) { data = b + p + 8; dlen = cl; if ((long)(p + 8 + dlen) > n) dlen = (uint32_t)(n - p - 8); break; }
    p += 8 + cl + (cl & 1);
  }
  if (!data || bits != 16 || ch < 1 || !rate) { free(b); return -5; }
  uint32_t ns = dlen / (2u * (unsigned)ch);
  int16_t *s = (int16_t *)malloc((size_t)ns * 2 + 2);
  if (!s) { free(b); return -6; }
  for (uint32_t i = 0; i < ns; i++) {              /* little-endian -> native; stereo is averaged (the baked files are mono) */
    int acc = 0; for (int c = 0; c < ch; c++) acc += (int16_t)le16(data + (i * ch + c) * 2);
    s[i] = (int16_t)(acc / ch);
  }
  free(b);
  SDL_AtomicLock(&alock); apcm = s; an = ns; arate = rate; apos = 0; afrac = 0; aplaying = 1; SDL_AtomicUnlock(&alock);
  return 0;
}

void video_stop_audio(void) { SDL_AtomicLock(&alock); aplaying = 0; SDL_AtomicUnlock(&alock); }

void video_close(void) {
  SDL_AtomicLock(&alock); int16_t *old = apcm; apcm = NULL; an = 0; aplaying = 0; SDL_AtomicUnlock(&alock);
  free(old);
  if (vf) { fclose(vf); vf = NULL; }
  free(offs); offs = NULL; free(jbuf); jbuf = NULL;
  if (dec_n) lsd_log("video %s: %d frames decoded, avg %u us, max %u us", vname, dec_n, (unsigned)(dec_us_sum / dec_n), (unsigned)dec_us_max);
  dec_n = 0; dec_us_sum = dec_us_max = 0;
  nframes = 0; cur_frame = -1; have_pix = 0; vname[0] = 0;
}

int video_is_open(void) { return vf != NULL; }
const char *video_name(void) { return vname; }

int video_open(const char *name) {
  video_close();
  FILE *f = open_movie_file(name, "mjv");
  if (!f) { lsd_log("video %s: no MOVIE/%s.mjv on the SD card, skipped", name, name); return -1; }
  uint8_t h[32];
  if (fread(h, 1, 32, f) != 32 || memcmp(h, "LMJV", 4) || be16(h + 4) != 1) { fclose(f); lsd_log("video %s: bad header", name); return -2; }
  vw = be16(h + 6); vh = be16(h + 8); vfn = be16(h + 10); vfd = be16(h + 12); nframes = be32(h + 16); maxbytes = be32(h + 20);
  if (!vw || !vh || vw > 1024 || vh > 1024 || !vfn || !vfd || !nframes || nframes > 1000000 || !maxbytes || maxbytes > (4u << 20)) { fclose(f); lsd_log("video %s: bad sizes", name); return -3; }
  offs = (uint32_t *)malloc(4u * (nframes + 1)); jbuf = (uint8_t *)malloc(maxbytes);
  if (!pix || vw * vh > 320u * 240u) { free(pix); pix = (uint32_t *)malloc(4u * (vw * vh > 320u * 240u ? vw * vh : 320u * 240u)); }
  if (!offs || !jbuf || !pix) { fclose(f); free(offs); offs = NULL; free(jbuf); jbuf = NULL; lsd_log("video %s: out of memory", name); return -4; }
  uint8_t *ob = (uint8_t *)offs;
  if (fread(ob, 4, nframes + 1, f) != nframes + 1) { fclose(f); free(offs); offs = NULL; free(jbuf); jbuf = NULL; return -5; }
  for (uint32_t i = 0; i <= nframes; i++) offs[i] = be32(ob + i * 4);   /* (no-op order on the big-endian Wii U) */
  vf = f; snprintf(vname, sizeof vname, "%s", name);
  if (!tj) tj = tjInitDecompress();
  int ar = (be16(h + 14) & 1) ? load_wav(name) : -1;
  t0 = SDL_GetTicks(); last_apos = 0; last_change_ms = t0; last_change_t = 0; cur_frame = -1; have_pix = 0;
  lsd_log("video %s: %ux%u %u/%u fps, %u frames (%.1f s), audio %s", name, vw, vh, vfn, vfd, nframes, nframes * (float)vfd / vfn, ar == 0 ? "ok" : "none (wall clock)");
  return 0;
}

/* the clock: samples played while the audio advances; the wall clock carries on when it stops (end of the wav, no device) */
float video_time(void) {
  if (!vf) return 0;
  uint32_t now = SDL_GetTicks();
  if (apcm && arate) {
    uint32_t p = apos;
    if (p != last_apos) { last_apos = p; last_change_ms = now; last_change_t = (float)p / arate; return last_change_t; }
    float since = (now - last_change_ms) / 1000.0f;
    if (p == 0 && since < 0.5f) return 0;                      /* the device has not pulled yet */
    if (p < an && since < 0.25f) return last_change_t + since; /* between two callbacks: interpolate */
    return last_change_t + since;                              /* ended or stalled: wall clock from there */
  }
  return (now - t0) / 1000.0f;
}

int video_update(void) {
  if (!vf) return 0;
  int i = (int)(video_time() * vfn / vfd);
  if (i >= (int)nframes) return 0;
  if (i != cur_frame) {                                        /* late frames are simply skipped (each JPEG is stand-alone) */
    uint32_t a = offs[i], b = offs[i + 1];
    if (b > a && b - a <= maxbytes && fseek(vf, (long)a, SEEK_SET) == 0 && fread(jbuf, 1, b - a, vf) == b - a) {
      uint64_t c0 = SDL_GetPerformanceCounter();
      /* 0xAARRGGBB words: bytes A,R,G,B on the big-endian Wii U = TJPF_ARGB (B,G,R,A = TJPF_BGRA on a little-endian host) */
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
      int pf = TJPF_ARGB;
#else
      int pf = TJPF_BGRA;
#endif
      if (tjDecompress2(tj, jbuf, b - a, (unsigned char *)pix, (int)vw, (int)vw * 4, (int)vh, pf, TJFLAG_FASTDCT | TJFLAG_FASTUPSAMPLE) == 0) have_pix = 1;
      else if (dec_n < 3) lsd_log("video %s: frame %d: %s", vname, i, tjGetErrorStr2(tj));
      uint64_t us = (SDL_GetPerformanceCounter() - c0) * 1000000 / SDL_GetPerformanceFrequency();
      dec_us_sum += us; if (us > dec_us_max) dec_us_max = us; dec_n++;
      if (dec_n == 30) lsd_log("video %s: first 30 frames decoded in avg %u us (max %u us)", vname, (unsigned)(dec_us_sum / 30), (unsigned)dec_us_max);
    }
    cur_frame = i;
  }
  return 1;
}

void video_draw(Frame *f) {
  for (int k = 0; k < f->w * f->h; k++) f->color[k] = 0xFF000000u;
  if (!have_pix || !vw || !vh) return;
  /* fit inside, keeping the movie's aspect: scale s = min(W/w, H/h), centred */
  int dw, dh;
  if ((uint64_t)f->w * vh <= (uint64_t)f->h * vw) { dw = f->w; dh = (int)((uint64_t)f->w * vh / vw); }
  else { dh = f->h; dw = (int)((uint64_t)f->h * vw / vh); }
  int ox = (f->w - dw) / 2, oy = (f->h - dh) / 2;
  if (dw == (int)vw && dh == (int)vh) {
    for (int y = 0; y < dh; y++) { uint32_t *d = f->color + (size_t)(oy + y) * f->w + ox; const uint32_t *s = pix + (size_t)y * vw;
      for (int x = 0; x < dw; x++) d[x] = s[x] | 0xFF000000u; }
    return;
  }
  for (int y = 0; y < dh; y++) {
    const uint32_t *s = pix + (size_t)((uint64_t)y * vh / dh) * vw; uint32_t *d = f->color + (size_t)(oy + y) * f->w + ox;
    uint32_t step = (uint32_t)(((uint64_t)vw << 16) / dw), u = 0;
    for (int x = 0; x < dw; x++, u += step) d[x] = s[u >> 16] | 0xFF000000u;
  }
}

void video_audio_mix(int32_t *acc, int frames, int rate, int vol256) {
  if (!aplaying) return;
  SDL_AtomicLock(&alock);
  if (apcm && an && rate > 0) {
    uint32_t step = (uint32_t)(((uint64_t)arate << 16) / (uint32_t)rate), p = apos, fr = afrac;
    for (int f = 0; f < frames && p < an; f++) {
      int a = apcm[p], b = p + 1 < an ? apcm[p + 1] : a;
      int x = a + (int)(((int64_t)(b - a) * fr) >> 16);
      x = (x * vol256) >> 8;
      acc[f * 2] += x; acc[f * 2 + 1] += x;
      fr += step; p += fr >> 16; fr &= 0xFFFF;
    }
    if (p >= an) p = an;
    apos = p; afrac = fr;
  }
  SDL_AtomicUnlock(&alock);
}
