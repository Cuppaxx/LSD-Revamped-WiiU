/* dreamaudio.c - DreamAudio entities (Entities/DreamAudio.cs, tools/AUDIO_FORMAT.md 4.4-4.6) of every dream: the 315 of the
 * original dreams (clips SFX/SE_n.wav, many nested below a moving entity: `aparent`) and the mesh dreams' ones (ADDSND).
 * Looping ambiences, repeated clips and script-triggered sounds, positional with Unity's linear roll-off (full volume
 * within FullVolumeRangeUnits, silent at RangeUnits) measured from the listener (the camera) and panned by the camera's
 * right vector. At most AUDIO_DA_N sources are mixed: each frame the loudest audible ones get a voice; a source keeps its
 * own clock, so one that gets a voice later joins at the right place (clip position = seconds since start * pitch).
 *
 * Per source (StartPlaying when the entity becomes active unless ControlledWithScript, StopPlaying when it becomes
 * inactive, DreamAudio.Play/Stop from Lua):
 *   Loop or OneShot: AudioSource.Play(): the clip from the start (a playing one restarts), looping or once.
 *   else the coroutine: wait until `playing` (set at once if !WaitUntilAudible or scripted, else by the 0.5 s audibility
 *        check from the player's feet), then forever: wait DelayBeforePlayingSeconds, set volume/pitch from the fields,
 *        (re)start the clip, after PlayClipForSeconds stop it (if > 0), wait DelayBetweenPlaysSeconds (at least a frame). */
#include "script.h"
#include "audio.h"
#include "sfx.h"
#include "lsd.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { char name[48]; AClip c; const AClip *use; float secs; } Clip;   /* use: own c, or a clip of the global SFX cache */
typedef struct {
  int ent; int clip;              /* entity index, Clip index (-1 none) */
  int was_live, co, phase;        /* co: coroutine running; phase 0 wait audible, 1 delay before, 2 playing, 3 delay between */
  int audible;                    /* DreamAudio._playing: set once within range (or at once), kept for the dream's life */
  float t, think;                 /* phase timer, audibility check timer */
  int snd; float snd_t; int snd_loop;   /* the clip sounding: on, seconds since its start, looping */
  float vol, pitch;               /* the DreamAudio fields (Lua may change them) */
  float pvol, ppitch;             /* what the AudioSource plays with (set at Play) */
  int voice;                      /* -1 virtual */
  float gain, gl, gr;
} Src;

static Clip *clips; static int nclips;
static Src *src; static int nsrc;
static int voice_src[AUDIO_DA_N];
static const Dream *cur;

static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

void da_free(void) {
  for (int v = 0; v < AUDIO_DA_N; v++) { audio_voice_stop(AUDIO_DA0 + v); voice_src[v] = -1; }
  for (int i = 0; i < nclips; i++) if (clips[i].use == &clips[i].c) audio_clip_free(&clips[i].c);
  free(clips); free(src); clips = NULL; src = NULL; nclips = nsrc = 0; cur = NULL;
}

static int clip_index(const char *name) {
  for (int i = 0; i < nclips; i++) if (!strcmp(clips[i].name, name)) return clips[i].use ? i : -1;
  Clip c; memset(&c, 0, sizeof c); snprintf(c.name, sizeof c.name, "%s", name);
  if (!strncmp(name, "SFX/", 4)) c.use = sfx_clip(name);           /* the SE bank: shared, loaded once */
  else {
    size_t n; uint8_t *b = lsd_read_file(name, &n);
    if (!b) lsd_log("missing sound %s", name);
    else { if (audio_clip_load(b, n, &c.c)) lsd_log("bad sound %s", name); free(b); }
  }
  Clip *nc = (Clip *)realloc(clips, sizeof(Clip) * (size_t)(nclips + 1)); if (!nc) return -1;
  clips = nc; clips[nclips] = c;
  if (!c.use && c.c.s) clips[nclips].use = &clips[nclips].c;      /* fixed up below once the array stops moving */
  nclips++;
  return clips[nclips - 1].use ? nclips - 1 : -1;
}

void da_level(const Dream *d) {
  da_free();
  cur = d; if (!d) return;
  int n = 0; for (int i = 0; i < d->nent; i++) n += d->ent[i].aud != NULL;
  if (!n) return;
  src = (Src *)calloc((size_t)n, sizeof(Src));
  for (int i = 0; i < d->nent; i++) {
    const AudioDef *a = d->ent[i].aud; if (!a) continue;
    Src *s = &src[nsrc++]; s->ent = i; s->voice = -1; s->clip = clip_index(a->clip); s->vol = a->vol; s->pitch = a->pitch;
  }
  size_t bytes = 0;
  for (int i = 0; i < nclips; i++) {                                 /* own clips: point at their final place */
    if (clips[i].c.s) clips[i].use = &clips[i].c;
    if (clips[i].use) { clips[i].secs = clips[i].use->rate > 0 ? (float)clips[i].use->n / clips[i].use->rate : 0; if (clips[i].use == &clips[i].c) bytes += clips[i].c.n * 2; }
  }
  lsd_log("dream audio: %d sources, %d clips (%u KB own)", nsrc, nclips, (unsigned)(bytes / 1024));
}

static Src *src_of(int ent) { for (int i = 0; i < nsrc; i++) if (src[i].ent == ent) return &src[i]; return NULL; }

/* active in hierarchy: the entity, and the entity it is nested below (aparent) */
static int src_live(const Dream *d, const Src *s) {
  const Ent *e = &d->ent[s->ent];
  return e->live && (e->aparent < 0 || e->aparent >= d->nent || d->ent[e->aparent].live);
}
/* where the sound is: the entity, or parent.world * offset for nested sources (pterodactyls, trains, boats...) */
static void src_pos(const Dream *d, const Src *s, float out[3]) {
  const Ent *e = &d->ent[s->ent];
  if (e->aparent < 0 || e->aparent >= d->nent) { memcpy(out, e->pos, 12); return; }
  const Ent *p = &d->ent[e->aparent];
  float sx = p->scale[0] ? p->scale[0] : 1, sy = p->scale[1] ? p->scale[1] : 1, sz = p->scale[2] ? p->scale[2] : 1;
  float x = e->aoff[0] * sx, y = e->aoff[1] * sy, z = e->aoff[2] * sz;
  const float *q = p->rot;                                           /* q * v */
  float tx = 2 * (q[1] * z - q[2] * y), ty = 2 * (q[2] * x - q[0] * z), tz = 2 * (q[0] * y - q[1] * x);
  out[0] = p->pos[0] + x + q[3] * tx + (q[1] * tz - q[2] * ty);
  out[1] = p->pos[1] + y + q[3] * ty + (q[2] * tx - q[0] * tz);
  out[2] = p->pos[2] + z + q[3] * tz + (q[0] * ty - q[1] * tx);
}

static void voice_release(Src *s) { if (s->voice >= 0) { audio_voice_stop(AUDIO_DA0 + s->voice); voice_src[s->voice] = -1; s->voice = -1; } }
static void sound_start(Src *s, int loop) { s->snd = 1; s->snd_t = 0; s->snd_loop = loop; voice_release(s); }   /* restarts from 0 */
static void sound_stop(Src *s) { s->snd = 0; voice_release(s); }

static void start_playing(Src *s, const AudioDef *a) {
  if (s->co) return;                                                 /* the coroutine is already running */
  if (a->loop || a->oneshot) { s->pvol = clamp01(s->vol); s->ppitch = s->pitch; sound_start(s, a->loop); return; }
  s->co = 1; s->t = 0;
  if (!a->waitaudible || a->scripted) s->audible = 1;
  s->phase = 0;
}
static void stop_playing(Src *s) { sound_stop(s); s->co = 0; }

void da_play(int ent) {
  Src *s = src_of(ent); if (!s || !cur) return;
  if (g_foot_log) lsd_log("DreamAudio.Play %s%s", cur->ent[ent].name, src_live(cur, s) ? "" : " (inactive: ignored)");
  if (!src_live(cur, s)) return;                                     /* Unity refuses Play on an inactive object */
  start_playing(s, cur->ent[ent].aud);
}
void da_stop(int ent) { Src *s = src_of(ent); if (s) stop_playing(s); }
float da_get(int ent, int which) { Src *s = src_of(ent); if (!s) return 1; return which ? s->pitch : s->vol; }
void da_set(int ent, int which, float v) {
  Src *s = src_of(ent); if (!s) return;
  if (which) s->pitch = v; else s->vol = v;
  if (g_foot_log) lsd_log("DreamAudio.%s %s = %.3f (plays at %.3f; sounding at %.3f)", which ? "Pitch" : "Volume", cur ? cur->ent[ent].name : "?", v, which ? v : clamp01(v), s->pvol);
}

void da_frame(const Dream *d, const Player *pl, const Cam *cam, float dt) {
  if (!nsrc || d != cur) return;
  float rx = cosf(cam->yaw), rz = -sinf(cam->yaw);                  /* the camera's right vector (xz) */
  for (int i = 0; i < nsrc; i++) {
    Src *s = &src[i]; const AudioDef *a = d->ent[s->ent].aud;
    int live = src_live(d, s);
    if (live && !s->was_live && !a->scripted) start_playing(s, a);  /* OnEnable */
    if (!live && s->was_live) stop_playing(s);                       /* OnDisable (`audible` stays) */
    s->was_live = live;
    float p[3]; src_pos(d, s, p);
    if (live && !s->audible && !a->scripted) {                       /* Update: every 0.5 s, from the player's feet */
      if ((s->think += dt) >= 0.5f) { s->think = 0;
        float fx = p[0] - pl->x, fy = p[1] - pl->y, fz = p[2] - pl->z;
        if (fx * fx + fy * fy + fz * fz < a->range * a->range) s->audible = 1; }
    }
    if (s->co) {                                                     /* the audioPlayer coroutine */
      s->t += dt;
      if (s->phase == 0 && s->audible) { s->phase = 1; s->t = 0; }
      if (s->phase == 1 && s->t >= a->delaybefore) { s->pvol = clamp01(s->vol); s->ppitch = s->pitch; sound_start(s, 0); s->phase = 2; s->t = 0; }
      if (s->phase == 2 && (a->playfor <= 0 || s->t >= a->playfor)) { if (a->playfor > 0) sound_stop(s); s->phase = 3; s->t = 0; }
      if (s->phase == 3 && s->t >= fmaxf(a->delay, 0.0001f)) { s->phase = 1; s->t = 0; }
    }
    float len = s->clip >= 0 ? clips[s->clip].secs : 0;
    if (s->snd) {
      s->snd_t += dt;
      float pp = s->ppitch > 0.01f ? s->ppitch : 0.01f;
      if (!s->snd_loop && s->snd_t * pp >= len) sound_stop(s);      /* a clip lasts frames / rate / pitch */
    }
    s->gain = 0;
    if (s->snd && s->clip >= 0) {
      float dx = p[0] - cam->x, dy = p[1] - cam->y, dz = p[2] - cam->z, dist = sqrtf(dx * dx + dy * dy + dz * dz);
      float g = s->pvol, pan = 0;
      if (a->spatial) {
        float mn = a->full, mx = a->range > mn ? a->range : mn + 0.001f;
        g *= dist <= mn ? 1.0f : dist >= mx ? 0.0f : (mx - dist) / (mx - mn);
        float h = sqrtf(dx * dx + dz * dz);
        if (h > 1e-4f) pan = (dx * rx + dz * rz) / h * fminf(1.0f, dist / 0.5f);   /* centred when it is on top of us */
      }
      s->gain = g;
      s->gl = g * fminf(1.0f, sqrtf(1.0f - pan)); s->gr = g * fminf(1.0f, sqrtf(1.0f + pan));   /* equal power, 1 at centre */
    }
  }
  /* voices: keep the ones still audible, give free voices to the loudest waiting sounds */
  for (int v = 0; v < AUDIO_DA_N; v++) {
    int k = voice_src[v]; if (k < 0) continue;
    Src *s = &src[k];
    if (s->gain <= 0.001f || !audio_voice_on(AUDIO_DA0 + v)) {
      if (!audio_voice_on(AUDIO_DA0 + v) && s->snd && !s->snd_loop) s->snd = 0;
      audio_voice_stop(AUDIO_DA0 + v); voice_src[v] = -1; s->voice = -1;
    } else audio_voice_gain(AUDIO_DA0 + v, s->gl, s->gr);
  }
  for (;;) {
    int fv = -1; for (int v = 0; v < AUDIO_DA_N; v++) if (voice_src[v] < 0) { fv = v; break; }
    if (fv < 0) break;
    int best = -1; float bg = 0.001f;
    for (int i = 0; i < nsrc; i++) if (src[i].voice < 0 && src[i].gain > bg) { bg = src[i].gain; best = i; }
    if (best < 0) break;
    Src *s = &src[best];
    audio_voice_play(AUDIO_DA0 + fv, clips[s->clip].use, s->gl, s->gr, s->snd_loop, s->snd_t * s->ppitch, s->ppitch);
    voice_src[fv] = best; s->voice = fv;
  }
}

int da_voices(void) { int n = 0; for (int v = 0; v < AUDIO_DA_N; v++) n += voice_src[v] >= 0 && audio_voice_on(AUDIO_DA0 + v); return n; }
int da_sounding(void) { int n = 0; for (int i = 0; i < nsrc; i++) n += src[i].snd; return n; }
void da_silence(void) { for (int v = 0; v < AUDIO_DA_N; v++) if (voice_src[v] >= 0) audio_voice_gain(AUDIO_DA0 + v, 0, 0); }
