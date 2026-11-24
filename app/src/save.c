/* save.c - see save.h. Format (one item per line, unknown keys ignored so later versions can add fields):
 *   lsdr-save 1
 *   day 12
 *   graph -3 4
 *   last 2
 *   game 1
 *   visited 1 0 1 ...          (one flag per dream index: 0-13 the originals, 14-27 the Additional Dreams)
 *   addl 1                     (option ADDITIONAL DREAMS; older saves without it read as on)
 *   hist -3,4 0,1 ...          (graph position per finished day, oldest first)
 *   sens 5 / invert 0 / bob 1 / run 0 / music 6 / sfx 7 / threads 3
 *   scheme 0 / deadzone 3 / foot 1 / bind 1 0 8 2 3 6 7 10   (controls: FPS/TANK, deadzone x5 %, footsteps, buttons per action)
 *   ec <dayidx> <d> <u> <x> <y> <z> <yawdeg> <dreamidx> <entityID...>   (entity graph contributions, SPECIAL_FORMAT.md 10)
 * The file is written to save.tmp first and then renamed over save.txt, so a power cut never leaves half a save. */
#include "save.h"
#include "input.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

void lsd_log(const char *fmt, ...);

#ifndef LSDR_SAVE_NAME
#define LSDR_SAVE_NAME "save"            /* test builds may pass -DLSDR_SAVE_NAME="\"save_test\"" to keep away from the real journal */
#endif

SaveData g_save;
static char s_dir[256], s_file[280];
static int s_on;

void save_defaults(SaveData *s) {
  memset(s, 0, sizeof *s);
  s->version = 1; s->day = 1; s->last_dream = -1;
  s->opt.look_sens = 5; s->opt.invert_y = 0; s->opt.head_bob = 1; s->opt.run_mode = 0;
  s->opt.music_vol = 6; s->opt.sfx_vol = 7; s->opt.threads = 3;
  s->opt.view = VIEW_AUTO; s->opt.vcap = VIEW_160; s->opt.vtarget = 0; s->opt.fogmode = FOG_SCALED;
  s->opt.res = 0; s->opt.aspect = 0; s->opt.scaling = 0;
  s->opt.skyfx = 1; s->opt.snap = 1; s->opt.models = 1; s->opt.show_fps = 0; s->opt.addl = 1; s->opt.affine = 2;
  s->opt.scheme = 0; s->opt.deadzone = 3; s->opt.footsteps = 1; input_default_bindings(s->opt.bind);
  s->opt.specialdays = 1; s->opt.intro = 1;
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static const int s_view_tiles[VIEW_N] = { 15, 20, 30, 40, 60, 80, 120, 160, 256, 0 };
int view_tiles(int choice) { return choice >= 0 && choice < VIEW_N ? s_view_tiles[choice] : 20; }

int save_serialize(const SaveData *s, char *out, int cap) {
  int n = 0;
#define PUT(...) do { int k = snprintf(out + n, (size_t)(cap - n), __VA_ARGS__); if (k < 0 || k >= cap - n) return -1; n += k; } while (0)
  PUT("lsdr-save 1\n");
  PUT("day %d\n", s->day);
  PUT("graph %d %d\n", s->graph_x, s->graph_y);
  PUT("last %d\n", s->last_dream);
  PUT("game %d\n", s->has_game);
  PUT("visited");
  for (int i = 0; i < SAVE_MAX_DREAMS; i++) PUT(" %d", s->visited[i] ? 1 : 0);
  PUT("\nhist");
  for (int i = 0; i < s->nhist; i++) PUT(" %d,%d", s->hist[i][0], s->hist[i][1]);
  PUT("\nsens %d\ninvert %d\nbob %d\nrun %d\nmusic %d\nsfx %d\nthreads %d\n", s->opt.look_sens, s->opt.invert_y, s->opt.head_bob,
      s->opt.run_mode, s->opt.music_vol, s->opt.sfx_vol, s->opt.threads);
  PUT("view %d\nvcap %d\nvtarget %d\nfogmode %d\nres %d\naspect %d\nscaling %d\nskyfx %d\nsnap %d\nmodels %d\nfps %d\n", s->opt.view, s->opt.vcap,
      s->opt.vtarget, s->opt.fogmode, s->opt.res, s->opt.aspect, s->opt.scaling, s->opt.skyfx, s->opt.snap, s->opt.models, s->opt.show_fps);
  PUT("addl %d\naffine %d\n", s->opt.addl, s->opt.affine);
  PUT("scheme %d\ndeadzone %d\nfoot %d\nbind", s->opt.scheme, s->opt.deadzone, s->opt.footsteps);
  for (int i = 0; i < SAVE_NBIND; i++) PUT(" %d", s->opt.bind[i]);
  PUT("\n");
  for (int i = 0; i < s->nec; i++) { const SaveEC *e = &s->ec[i];
    PUT("ec %d %d %d %.3f %.3f %.3f %.3f %d %s\n", e->day, e->d, e->u, e->x, e->y, e->z, e->yaw, e->dream, e->id[0] ? e->id : "-"); }
  PUT("year %d\nseenface %d\nopt_specialdays %d\nopt_intro %d\n", s->year, s->seenface, s->opt.specialdays, s->opt.intro);
  for (int i = 0; i < s->nsp; i++) PUT("spday %d %d %c %s\n", s->sp[i].year, s->sp[i].day, s->sp[i].kind, s->sp[i].asset[0] ? s->sp[i].asset : "-");
#undef PUT
  return n;
}

int save_parse(SaveData *s, const char *text) {
  save_defaults(s);
  if (strncmp(text, "lsdr-save", 9) != 0) return -1;
  const char *p = text; int old_dd = -1, old_fog = -1, has_view = 0, has_fogmode = 0;
  while (*p) {
    const char *eol = strchr(p, '\n'); size_t L = eol ? (size_t)(eol - p) : strlen(p);
    char line[4096]; if (L >= sizeof line) L = sizeof line - 1;
    memcpy(line, p, L); line[L] = 0;
    char *cr = strchr(line, '\r'); if (cr) *cr = 0;
    char key[16] = ""; int off = 0;
    if (sscanf(line, "%15s %n", key, &off) >= 1) {
      const char *v = line + off; int a = 0, b = 0;
      if (!strcmp(key, "day") && sscanf(v, "%d", &a) == 1) s->day = clampi(a, 1, 365);
      else if (!strcmp(key, "graph") && sscanf(v, "%d %d", &a, &b) == 2) { s->graph_x = clampi(a, -9, 9); s->graph_y = clampi(b, -9, 9); }
      else if (!strcmp(key, "last") && sscanf(v, "%d", &a) == 1) s->last_dream = clampi(a, -1, SAVE_MAX_DREAMS - 1);
      else if (!strcmp(key, "game") && sscanf(v, "%d", &a) == 1) s->has_game = a != 0;
      else if (!strcmp(key, "visited")) {
        int i = 0, k; while (i < SAVE_MAX_DREAMS && sscanf(v, "%d%n", &a, &k) == 1) { s->visited[i++] = a != 0; v += k; }
      } else if (!strcmp(key, "hist")) {
        int k; s->nhist = 0;
        while (s->nhist < SAVE_MAX_HISTORY && sscanf(v, " %d,%d%n", &a, &b, &k) == 2) {
          s->hist[s->nhist][0] = (signed char)clampi(a, -9, 9); s->hist[s->nhist][1] = (signed char)clampi(b, -9, 9); s->nhist++; v += k;
        }
      }
      else if (!strcmp(key, "sens") && sscanf(v, "%d", &a) == 1) s->opt.look_sens = clampi(a, 1, 10);
      else if (!strcmp(key, "invert") && sscanf(v, "%d", &a) == 1) s->opt.invert_y = a != 0;
      else if (!strcmp(key, "bob") && sscanf(v, "%d", &a) == 1) s->opt.head_bob = a != 0;
      else if (!strcmp(key, "run") && sscanf(v, "%d", &a) == 1) s->opt.run_mode = clampi(a, 0, 1);
      else if (!strcmp(key, "music") && sscanf(v, "%d", &a) == 1) s->opt.music_vol = clampi(a, 0, 10);
      else if (!strcmp(key, "sfx") && sscanf(v, "%d", &a) == 1) s->opt.sfx_vol = clampi(a, 0, 10);
      else if (!strcmp(key, "threads") && sscanf(v, "%d", &a) == 1) s->opt.threads = clampi(a, 1, 4);
      else if (!strcmp(key, "drawdist") && sscanf(v, "%d", &a) == 1) old_dd = a;     /* before M11: tiles 10..60, see below */
      else if (!strcmp(key, "view") && sscanf(v, "%d", &a) == 1) { s->opt.view = clampi(a, 0, VIEW_N - 1); has_view = 1; }
      else if (!strcmp(key, "vcap") && sscanf(v, "%d", &a) == 1) s->opt.vcap = clampi(a, VIEW_CAP_MIN, VIEW_MAX);
      else if (!strcmp(key, "vtarget") && sscanf(v, "%d", &a) == 1) s->opt.vtarget = clampi(a, 0, 1);
      else if (!strcmp(key, "fogmode") && sscanf(v, "%d", &a) == 1) { s->opt.fogmode = clampi(a, 0, 2); has_fogmode = 1; }
      else if (!strcmp(key, "res") && sscanf(v, "%d", &a) == 1) s->opt.res = clampi(a, 0, 3);
      else if (!strcmp(key, "aspect") && sscanf(v, "%d", &a) == 1) s->opt.aspect = clampi(a, 0, 1);
      else if (!strcmp(key, "scaling") && sscanf(v, "%d", &a) == 1) s->opt.scaling = clampi(a, 0, 2);
      else if (!strcmp(key, "fog") && sscanf(v, "%d", &a) == 1) old_fog = a != 0;     /* before M11: the FOG on/off toggle */
      else if (!strcmp(key, "skyfx") && sscanf(v, "%d", &a) == 1) s->opt.skyfx = a != 0;
      else if (!strcmp(key, "snap") && sscanf(v, "%d", &a) == 1) s->opt.snap = a != 0;
      else if (!strcmp(key, "models") && sscanf(v, "%d", &a) == 1) s->opt.models = a != 0;
      else if (!strcmp(key, "fps") && sscanf(v, "%d", &a) == 1) s->opt.show_fps = a != 0;
      else if (!strcmp(key, "addl") && sscanf(v, "%d", &a) == 1) s->opt.addl = a != 0;
      else if (!strcmp(key, "affine") && sscanf(v, "%d", &a) == 1) s->opt.affine = clampi(a, 0, 4);
      else if (!strcmp(key, "scheme") && sscanf(v, "%d", &a) == 1) s->opt.scheme = clampi(a, 0, 1);
      else if (!strcmp(key, "deadzone") && sscanf(v, "%d", &a) == 1) s->opt.deadzone = clampi(a, 0, 8);
      else if (!strcmp(key, "foot") && sscanf(v, "%d", &a) == 1) s->opt.footsteps = a != 0;
      else if (!strcmp(key, "bind")) {
        int i = 0, k; while (i < SAVE_NBIND && sscanf(v, "%d%n", &a, &k) == 1) { s->opt.bind[i++] = clampi(a, -1, 15); v += k; }
      } else if (!strcmp(key, "ec") && s->nec < SAVE_MAX_EC) {
        SaveEC *e = &s->ec[s->nec]; int dd, uu, dr, k = 0; float x, y, z, yw;
        if (sscanf(v, "%d %d %d %f %f %f %f %d %n", &a, &dd, &uu, &x, &y, &z, &yw, &dr, &k) >= 8 && k > 0) {
          memset(e, 0, sizeof *e); e->day = (short)a; e->d = (signed char)clampi(dd, -128, 127); e->u = (signed char)clampi(uu, -128, 127);
          e->x = x; e->y = y; e->z = z; e->yaw = yw; e->dream = (short)dr; snprintf(e->id, sizeof e->id, "%s", v + k);
          if (!strcmp(e->id, "-")) e->id[0] = 0;
          s->nec++;
        }
      }
      else if (!strcmp(key, "year") && sscanf(v, "%d", &a) == 1) s->year = clampi(a, 0, 1000000);
      else if (!strcmp(key, "seenface") && sscanf(v, "%d", &a) == 1) s->seenface = a != 0;
      else if (!strcmp(key, "opt_specialdays") && sscanf(v, "%d", &a) == 1) s->opt.specialdays = a != 0;
      else if (!strcmp(key, "opt_intro") && sscanf(v, "%d", &a) == 1) s->opt.intro = a != 0;
      else if (!strcmp(key, "spday") && s->nsp < SAVE_MAX_SP) {
        char k = 0, as[8] = ""; if (sscanf(v, "%d %d %c %7s", &a, &b, &k, as) == 4) {
          SaveSP *e = &s->sp[s->nsp++]; e->year = (short)clampi(a, 0, 30000); e->day = (short)clampi(b, 1, 365); e->kind = k;
          snprintf(e->asset, sizeof e->asset, "%s", strcmp(as, "-") ? as : ""); }
      }
    }
    if (!eol) break;
    p = eol + 1;
  }
  /* saves from before M11: an explicitly chosen render distance (anything but the old default 20) becomes the nearest fixed
   * choice; the old default gets the new default (AUTO). FOG OFF stays off; FOG ON gets the new default (SCALED) */
  if (!has_view && old_dd >= 0 && old_dd != 20) {
    int best = VIEW_15; for (int c = VIEW_15; c <= VIEW_MAX; c++) if (abs(view_tiles(c) - old_dd) < abs(view_tiles(best) - old_dd)) best = c;
    s->opt.view = best;
  }
  if (!has_fogmode && old_fog == 0) s->opt.fogmode = FOG_OFF;
  return 0;
}

void save_add_history(int gx, int gy) {
  if (g_save.nhist == SAVE_MAX_HISTORY) {
    memmove(g_save.hist[0], g_save.hist[1], (SAVE_MAX_HISTORY - 1) * 2); g_save.nhist--;
    int k = 0;                                   /* the oldest day's entity contributions go with it; renumber the rest */
    for (int i = 0; i < g_save.nec; i++) if (g_save.ec[i].day > 0) { g_save.ec[k] = g_save.ec[i]; g_save.ec[k].day--; k++; }
    g_save.nec = k;
  }
  g_save.hist[g_save.nhist][0] = (signed char)clampi(gx, -9, 9); g_save.hist[g_save.nhist][1] = (signed char)clampi(gy, -9, 9);
  g_save.nhist++;
}

void save_add_ec(int d, int u, float x, float y, float z, float yawdeg, int dream, const char *id) {
  if (g_save.nec == SAVE_MAX_EC) { memmove(&g_save.ec[0], &g_save.ec[1], sizeof(SaveEC) * (SAVE_MAX_EC - 1)); g_save.nec--; }
  SaveEC *e = &g_save.ec[g_save.nec++]; memset(e, 0, sizeof *e);
  e->day = (short)(g_save.nhist > 0 ? g_save.nhist - 1 : 0); e->d = (signed char)clampi(d, -128, 127); e->u = (signed char)clampi(u, -128, 127);
  e->x = x; e->y = y; e->z = z; e->yaw = yawdeg; e->dream = (short)dream;
  snprintf(e->id, sizeof e->id, "%s", id ? id : ""); for (char *q = e->id; *q; q++) if (*q == 31 || *q == '\n' || *q == '\r') *q = ' ';
}

void save_add_sp(int year, int day, char kind, const char *asset) {
  if (g_save.nsp == SAVE_MAX_SP) { memmove(&g_save.sp[0], &g_save.sp[1], sizeof(SaveSP) * (SAVE_MAX_SP - 1)); g_save.nsp--; }
  SaveSP *e = &g_save.sp[g_save.nsp++]; memset(e, 0, sizeof *e);
  e->year = (short)year; e->day = (short)day; e->kind = kind; snprintf(e->asset, sizeof e->asset, "%s", asset ? asset : "");
}

static void mkdirs(const char *path) {           /* create every directory along path (ignores "already exists") */
  char t[256]; snprintf(t, sizeof t, "%s", path);
  for (char *q = t + 1; *q; q++) {
    if (*q != '/') continue;
    if (q > t && q[-1] == ':') continue;         /* "fs:/" device prefix */
    *q = 0; mkdir(t, 0777); *q = '/';
  }
  mkdir(t, 0777);
}

const char *save_path(void) { return s_on ? s_file : NULL; }
void save_disable(void) { s_on = 0; }

int save_init(const char *base) {
  save_defaults(&g_save);
  s_on = 0;
  if (!base) { lsd_log("save: no SD card, saving is off"); return -1; }
  snprintf(s_dir, sizeof s_dir, "%s/wiiu/apps/lsdr", base);
  snprintf(s_file, sizeof s_file, "%s/" LSDR_SAVE_NAME ".txt", s_dir);
  s_on = 1;
  mkdirs(s_dir);
  FILE *f = fopen(s_file, "rb");
  if (!f) { char tmp[290]; snprintf(tmp, sizeof tmp, "%s/" LSDR_SAVE_NAME ".tmp", s_dir); f = fopen(tmp, "rb"); }   /* cut off between remove and rename */
  if (!f) { lsd_log("save: %s not found (new journal)", s_file); return -1; }
  fseek(f, 0, SEEK_END); long fl = ftell(f); fseek(f, 0, SEEK_SET); if (fl < 0) fl = 0;
  char *buf = (char *)malloc((size_t)fl + 1); size_t n = buf ? fread(buf, 1, (size_t)fl, f) : 0; fclose(f);
  if (!buf) return -1;
  buf[n] = 0;
  int rc = save_parse(&g_save, buf); free(buf);
  if (rc) { save_defaults(&g_save); lsd_log("save: %s unreadable, starting fresh", s_file); return -1; }
  lsd_log("save: loaded day %d graph %d,%d, %d days in the chart, %d entity contributions", g_save.day, g_save.graph_x, g_save.graph_y, g_save.nhist, g_save.nec);
  return 0;
}

int save_write(void) {
  if (!s_on) return -1;
  int cap = 16384 + g_save.nec * 128; char *buf = (char *)malloc((size_t)cap);
  if (!buf) return -1;
  int n = save_serialize(&g_save, buf, cap);
  if (n < 0) { lsd_log("save: too big"); free(buf); return -1; }
  mkdirs(s_dir);
  char tmp[290]; snprintf(tmp, sizeof tmp, "%s/" LSDR_SAVE_NAME ".tmp", s_dir);
  FILE *f = fopen(tmp, "wb");
  if (!f) { lsd_log("save: cannot write %s (errno %d)", tmp, errno); free(buf); return -1; }
  int ok = fwrite(buf, 1, (size_t)n, f) == (size_t)n; ok &= fclose(f) == 0;
  if (!ok) { lsd_log("save: write failed"); remove(tmp); free(buf); return -1; }
  remove(s_file);                                /* rename() does not replace on every devoptab */
  if (rename(tmp, s_file) != 0) {                /* fall back to writing in place */
    f = fopen(s_file, "wb"); if (!f) { free(buf); return -1; }
    fwrite(buf, 1, (size_t)n, f); fclose(f); remove(tmp);
  }
  free(buf);
  lsd_log("save: wrote day %d graph %d,%d (%d bytes)", g_save.day, g_save.graph_x, g_save.graph_y, n);
  return 0;
}
