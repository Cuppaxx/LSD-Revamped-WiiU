#include "dream.h"
#include "lsd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Env defenv = { {1, 1, 1}, 0, 10, 30, 0.24f, 0.24f, {0, 1, 1}, {1, 1, 1} };
const Env *default_env(void) { return &defenv; }

int g_addl_enabled = 1;
int dream_enabled(const DreamList *l, int i) { return i >= 0 && i < l->n && (i < l->norig || g_addl_enabled); }

static void sp31(char *s) { for (; *s; s++) if (*s == 31) *s = ' '; }

/* one dream table file; set 0 = dreams.txt (indices as written), set 1 = dreams_addl.txt (appended: its dream indices in
 * K link entities and the graph are local to the file and get the offset of its first dream) */
static int parse_file(DreamList *l, const char *file, int set) {
  size_t len; uint8_t *buf = lsd_read_file(file, &len);
  if (!buf) return -1;
  char *text = (char *)realloc(buf, len + 1); text[len] = 0;
  Dream *cur = NULL; int curgroup = 0, base = l->n, n0 = l->n;
  char *save = NULL;
  for (char *line = strtok_r(text, "\r\n", &save); line; line = strtok_r(NULL, "\r\n", &save)) {
    if (!strncmp(line, "dream ", 6)) {
      l->d = (Dream *)realloc(l->d, (size_t)(l->n + 1) * sizeof(Dream));
      cur = &l->d[l->n++]; memset(cur, 0, sizeof *cur);
      int idx, off = 0; char folder[16];
      if (sscanf(line + 6, "%d %15s %f %d %f %d %d %d %d %n", &idx, folder, &cur->walk, &cur->linkable, &cur->weight, &cur->firstday,
                 &cur->layout, &cur->tilew, &cur->envoffset, &off) < 9) { l->n--; cur = NULL; continue; }
      snprintf(cur->folder, sizeof cur->folder, "%s", folder); snprintf(cur->name, sizeof cur->name, "%s", line + 6 + off);
      cur->set = set;
    } else if (!strncmp(line, "luainclude ", 11)) { snprintf(l->luainc[set], sizeof l->luainc[set], "%s", line + 11);
    } else if (!strncmp(line, "graph ", 6) && set) {
      char *p = line + 6;
      for (int i = 0; i < GRAPH_SIZE * GRAPH_SIZE; i++) { int v = (int)strtol(p, &p, 10); l->graph2[i] = v >= 0 && base + v < l->n ? base + v : -1; }
      l->has_graph2 = 1;
    } else if (!cur) continue;
    else if (!strncmp(line, "world ", 6)) { snprintf(cur->world, sizeof cur->world, "%s", line + 6);
    } else if (!strncmp(line, "models ", 7)) { snprintf(cur->models, sizeof cur->models, "%s", line + 7);
    } else if (!strncmp(line, "gate ", 5)) {
      int id, one, mask; if (sscanf(line + 5, "%d %d %d", &id, &one, &mask) == 3 && id >= 0 && id < 4096) {
        if (id >= cur->ngate) { cur->gate = (Gate *)realloc(cur->gate, (size_t)(id + 1) * sizeof(Gate)); for (int k = cur->ngate; k <= id; k++) { cur->gate[k].one_in = 1; cur->gate[k].daymask = -1; } cur->ngate = id + 1; }
        cur->gate[id].one_in = one < 1 ? 1 : one; cur->gate[id].daymask = mask; }
    } else if (!strncmp(line, "gates ", 6) && cur->nent) {
      Ent *e = &cur->ent[cur->nent - 1]; char *p = line + 6; int n = (int)strtol(p, &p, 10);
      for (int k = 0; k < n && e->ngates < 8; k++) e->gates[e->ngates++] = (short)strtol(p, &p, 10);
    } else if (!strncmp(line, "epath ", 6) && cur->nent) {
      Ent *e = &cur->ent[cur->nent - 1]; char pn[48], path[48];
      if (sscanf(line + 6, "%47s %47s", pn, path) == 2) {
        sp31(pn); sp31(path); snprintf(e->epath, sizeof e->epath, "%s", path);
        for (int k = cur->nent - 2; k >= 0; k--) { char nb[48]; snprintf(nb, sizeof nb, "%s", cur->ent[k].name); sp31(nb); if (!strcmp(nb, pn)) { e->parent = k; break; } }
      }
    } else if (!strncmp(line, "startonenable ", 14) && cur->nent) { cur->ent[cur->nent - 1].startonenable = atoi(line + 14);
    } else if (!strncmp(line, "aobj ", 5) && cur->nent) { Ent *e = &cur->ent[cur->nent - 1]; snprintf(e->aobj, sizeof e->aobj, "%s", line + 5); sp31(e->aobj);
    } else if (!strncmp(line, "gcontrib ", 9) && cur->nent) {
      Ent *e = &cur->ent[cur->nent - 1]; if (sscanf(line + 9, "%f %f %f", &e->gc[0], &e->gc[1], &e->gc[2]) == 3) e->has_gc = 1;
    } else if (!strncmp(line, "audio ", 6) && cur->nent) {
      Ent *e = &cur->ent[cur->nent - 1]; AudioDef a; memset(&a, 0, sizeof a);
      if (sscanf(line + 6, "%47s %d %f %f %f %f %f %f %f %d %d %d %d", a.clip, &a.spatial, &a.full, &a.range, &a.pitch, &a.vol, &a.delay, &a.playfor,
                 &a.delaybefore, &a.waitaudible, &a.loop, &a.oneshot, &a.scripted) == 13 && strcmp(a.clip, "-")) {
        free(e->aud); e->aud = (AudioDef *)malloc(sizeof a); if (e->aud) *e->aud = a; }
    } else if (!strncmp(line, "aparent ", 8) && cur->nent) {
      Ent *e = &cur->ent[cur->nent - 1];
      if (sscanf(line + 8, "%47s %f %f %f", e->aparent_name, &e->aoff[0], &e->aoff[1], &e->aoff[2]) != 4) e->aparent_name[0] = 0;
    } else if (!strncmp(line, "linksound ", 10) && cur->nent) { cur->ent[cur->nent - 1].linksound = atoi(line + 10);
    } else if (!strncmp(line, "ecol ", 5) && cur->nent) {
      Ent *e = &cur->ent[cur->nent - 1]; ECol c; memset(&c, 0, sizeof c); char ty[12]; int ok = 0;
      if (sscanf(line + 5, "%11s", ty) == 1) {
        const char *p = line + 5 + strlen(ty);
        if (!strcmp(ty, "box")) { c.type = 0; ok = sscanf(p, "%d %f %f %f %f %f %f", &c.enabled, &c.c[0], &c.c[1], &c.c[2], &c.s[0], &c.s[1], &c.s[2]) == 7; }
        else if (!strcmp(ty, "capsule")) { c.type = 1; ok = sscanf(p, "%d %f %f %f %f %f %d", &c.enabled, &c.c[0], &c.c[1], &c.c[2], &c.r, &c.h, &c.dir) == 7; }
        else if (!strcmp(ty, "sphere")) { c.type = 2; ok = sscanf(p, "%d %f %f %f %f", &c.enabled, &c.c[0], &c.c[1], &c.c[2], &c.r) == 5; }
      }
      if (ok) { e->ecol = (ECol *)realloc(e->ecol, (size_t)(e->necol + 1) * sizeof(ECol)); e->ecol[e->necol++] = c; }
    } else if (!strncmp(line, "mclips ", 7) && cur->nent && cur->ent[cur->nent - 1].nparts) {
      EntPart *pt = &cur->ent[cur->nent - 1].parts[cur->ent[cur->nent - 1].nparts - 1]; char *p = line + 7; int n = (int)strtol(p, &p, 10);
      if (n > 0 && n < 256) { free(pt->clipmap); pt->clipmap = (short *)malloc(sizeof(short) * (size_t)n); pt->nclipmap = n;
        for (int k = 0; k < n; k++) pt->clipmap[k] = (short)strtol(p, &p, 10); }
    }
    else if (!strncmp(line, "lbd ", 4)) {
      cur->lbd = (char (*)[24])realloc(cur->lbd, (size_t)(cur->nlbd + 1) * 24);
      snprintf(cur->lbd[cur->nlbd++], 24, "%s", line + 4);
    } else if (!strncmp(line, "song ", 5)) {
      int si = atoi(line + 5); const char *p = strchr(line + 5, ' ');
      if (si >= 0 && si < 7 && p) { cur->song[si] = (char (*)[64])realloc(cur->song[si], (size_t)(cur->nsong[si] + 1) * 64); snprintf(cur->song[si][cur->nsong[si]++], 64, "%s", p + 1); }
    } else if (!strncmp(line, "len ", 4)) { cur->length = (float)atof(line + 4);
    } else if (!strncmp(line, "lgraph ", 7)) {
      char *p = line + 7; int n = (int)strtol(p, &p, 10);
      cur->lgraph = (signed char (*)[2])calloc((size_t)n + 1, 2); cur->nlgraph = n;
      for (int i = 0; i < n; i++) { cur->lgraph[i][0] = (signed char)strtol(p, &p, 10); cur->lgraph[i][1] = (signed char)strtol(p, &p, 10); }
    } else if (!strncmp(line, "graph ", 6)) {
      char *p = line + 6; for (int i = 0; i < GRAPH_SIZE * GRAPH_SIZE; i++) l->graph[i] = (int)strtol(p, &p, 10); l->has_graph = 1;
    } else if (!strncmp(line, "group ", 6)) {
      int g, n; if (sscanf(line + 6, "%d %d", &g, &n) == 2) { cur->grouped = g >= 0; curgroup = g < 0 ? 0 : g & 3; cur->nenv[curgroup] = 0; }
    } else if (!strncmp(line, "fx ", 3)) { cur->fx = atoi(line + 3);
    } else if (!strncmp(line, "grey ", 5)) { cur->grey = atoi(line + 5);
    } else if (!strncmp(line, "env ", 4)) {
      float v[48]; int nv = 0; char *p = line + 4;
      while (nv < 48) { char *e; float x = strtof(p, &e); if (e == p) break; v[nv++] = x; p = e; }
      if (nv >= 14) {
        Env e; memset(&e, 0, sizeof e);
        memcpy(e.fog, v, 12); e.sub = (int)v[3]; e.start = v[4]; e.end = v[5]; e.fogheight = v[6]; e.foggrad = v[7];
        memcpy(e.sky, v + 8, 12); memcpy(e.skyfog, v + 11, 12);
        if (nv >= 42) {
          memcpy(e.sun, v + 14, 12); e.sun_chance = v[17]; e.sun_height = v[18]; e.sun_size = v[19];
          memcpy(e.sun2, v + 20, 12); e.sun2_size = v[23]; e.sun2_chance = v[24]; e.sun2_offset = v[25]; e.sun2_height = v[26];
          memcpy(e.burst, v + 27, 12); e.burst_size = v[30]; e.burst_chance = v[31];
          memcpy(e.cloud, v + 32, 12); e.nclouds = v[35]; e.cloud_chance = v[36];
          memcpy(e.star, v + 37, 12); e.nstars = v[40]; e.star_chance = v[41];
        }
        cur->env[curgroup] = (Env *)realloc(cur->env[curgroup], (size_t)(cur->nenv[curgroup] + 1) * sizeof(Env));
        cur->env[curgroup][cur->nenv[curgroup]++] = e;
      }
    } else if (!strncmp(line, "ent ", 4)) {
      Ent e; memset(&e, 0, sizeof e); char kind, nm[48], rest[400] = ""; int act; float yaw;
      int got = sscanf(line + 4, "%c %47s %d %f %f %f %f %f %f %f %f %f %f %f %f %f %399[^\n]", &kind, nm, &act, &e.pos[0], &e.pos[1], &e.pos[2], &yaw,
                       &e.scale[0], &e.scale[1], &e.scale[2], &e.box[0], &e.box[1], &e.box[2], &e.box[3], &e.box[4], &e.box[5], rest);
      if (got >= 16) {
        e.kind = kind; snprintf(e.name, sizeof e.name, "%s", nm); e.active = e.active0 = act; e.yaw = yaw; e.dream = -1; e.parent = -1; e.gate_ok = 1; e.aparent = -1;
        int off = -1;                                       /* end of the kind's own fields: the model suffix follows */
        if (kind == 'L') sscanf(rest, "%55s %39s %39s %d %n", e.script, e.fn, e.fnexit, &e.onlyonce, &off);
        else if (kind == 'K') { sscanf(rest, "%d %47s %d %d %d %n", &e.dream, e.spawn, &e.lockinput, &e.affects, &e.onlyonce, &off);
          if (set && e.dream >= 0) e.dream += base; }      /* the mod's links index its own dream list */
        else if (kind == 'P') sscanf(rest, "%f %f %f %d %d %n", &e.dest[0], &e.dest[1], &e.dest[2], &e.alwayson, &e.onlyonce, &off);
        else if (kind == 'I') sscanf(rest, "%55s %d %f %n", e.script, &e.ikind, &e.idist, &off);
        else off = 0;
        /* visuals suffix (tools/models.py): <first model key or -> <nmodels> <nmeshes> <first child name or -> */
        int nmod = 0, nmesh = 0;
        if (off >= 0 && off < (int)sizeof rest && sscanf(rest + off, "%63s %d %d %47s", e.model, &nmod, &nmesh, e.child0) == 4) {
          if (!strcmp(e.model, "-")) e.model[0] = 0;
          if (!strcmp(e.child0, "-")) e.child0[0] = 0;
          for (char *q = e.child0; *q; q++) if (*q == 31) *q = ' ';
        } else { e.model[0] = 0; e.child0[0] = 0; }
        memcpy(e.pos0, e.pos, sizeof e.pos); e.yaw0 = e.yaw; memcpy(e.scale0, e.scale, sizeof e.scale);
        cur->ent = (Ent *)realloc(cur->ent, (size_t)(cur->nent + 1) * sizeof(Ent)); cur->ent[cur->nent++] = e;
      }
    } else if ((!strncmp(line, "model ", 6) || !strncmp(line, "mmesh ", 6)) && cur->nent) {
      Ent *e = &cur->ent[cur->nent - 1]; EntPart pt; memset(&pt, 0, sizeof pt); float *t = pt.t; int n;
      pt.is_mesh = line[1] == 'm'; pt.autoplay = -1;
      if (!pt.is_mesh) n = sscanf(line + 6, "%63s %95s %d %d %f %f %f %f %f %f %f %f %f %f", pt.key, pt.path, &pt.autoplay, &pt.active,
                                  &t[0], &t[1], &t[2], &t[3], &t[4], &t[5], &t[6], &t[7], &t[8], &t[9]);
      else n = sscanf(line + 6, "%63s %d %95s %d %f %f %f %f %f %f %f %f %f %f", pt.key, &pt.tmd, pt.path, &pt.active,
                      &t[0], &t[1], &t[2], &t[3], &t[4], &t[5], &t[6], &t[7], &t[8], &t[9]);
      if (n == 14) {
        for (char *q = pt.path; *q; q++) if (*q == 31) *q = ' ';    /* names with spaces are written with char 31 */
        e->parts = (EntPart *)realloc(e->parts, (size_t)(e->nparts + 1) * sizeof(EntPart)); e->parts[e->nparts++] = pt;
      }
    } else if (!strncmp(line, "mpose ", 6) && cur->nent && cur->ent[cur->nent - 1].nparts) {
      Ent *e = &cur->ent[cur->nent - 1]; EntPart *pt = &e->parts[e->nparts - 1]; EntPose po; memset(&po, 0, sizeof po); float *t = po.t;
      if (!pt->is_mesh && sscanf(line + 6, "%31s %d %f %f %f %f %f %f %f %f %f %f", po.name, &po.active,
                                 &t[0], &t[1], &t[2], &t[3], &t[4], &t[5], &t[6], &t[7], &t[8], &t[9]) == 12) {
        for (char *q = po.name; *q; q++) if (*q == 31) *q = ' ';
        pt->pose = (EntPose *)realloc(pt->pose, (size_t)(pt->npose + 1) * sizeof(EntPose)); pt->pose[pt->npose++] = po;
      }
    } else if (!strncmp(line, "spawn ", 6)) {
      Spawn s; memset(&s, 0, sizeof s); if (sscanf(line + 6, "%f %f %f %f %d %d %47s", &s.x, &s.y, &s.z, &s.yaw, &s.dayone, &s.tunnel, s.name) >= 6) {
        cur->spawn = (Spawn *)realloc(cur->spawn, (size_t)(cur->nspawn + 1) * sizeof(Spawn)); cur->spawn[cur->nspawn++] = s;
      }
    }
  }
  free(text);
  /* aparent: the parent is another entity of the dream (anywhere in the file); names can repeat (STG03 WindAudio_$$), so
   * take the one whose position plus the offset lands nearest to the source */
  for (int i = n0; i < l->n; i++) { Dream *d = &l->d[i];
    for (int k = 0; k < d->nent; k++) { Ent *e = &d->ent[k]; if (!e->aparent_name[0]) continue;
      float best = 1e30f;
      for (int j = 0; j < d->nent; j++) { if (j == k || strcmp(d->ent[j].name, e->aparent_name)) continue;
        float dx = d->ent[j].pos0[0] + e->aoff[0] - e->pos0[0], dy = d->ent[j].pos0[1] + e->aoff[1] - e->pos0[1], dz = d->ent[j].pos0[2] + e->aoff[2] - e->pos0[2];
        float dd = dx * dx + dy * dy + dz * dz; if (dd < best) { best = dd; e->aparent = j; } }
      if (e->aparent < 0) lsd_log("%s: aparent %s of %s not found", d->folder, e->aparent_name, e->name);
    } }
  /* K links of the mod point into its own list: dreams that are not there (a partial bake) would be out of range */
  for (int i = n0; i < l->n; i++) for (int k = 0; k < l->d[i].nent; k++) { Ent *e = &l->d[i].ent[k]; if (e->kind == 'K' && e->dream >= l->n) e->dream = -1; }
  lsd_log("%s: %d dreams", file, l->n - n0);
  return l->n > n0 ? 0 : -2;
}

int dreams_load(DreamList *l) {
  memset(l, 0, sizeof *l);
  for (int i = 0; i < GRAPH_SIZE * GRAPH_SIZE; i++) l->graph[i] = l->graph2[i] = -1;
  snprintf(l->luainc[0], sizeof l->luainc[0], "LuaInclude"); snprintf(l->luainc[1], sizeof l->luainc[1], "ADDLUA/include");
  int rc = parse_file(l, "dreams.txt", 0);
  if (rc) return rc;
  l->norig = l->n;
  parse_file(l, "dreams_addl.txt", 1);                    /* optional: the Additional Dreams mod (tools/addl.py) */
  return 0;
}

/* AppearOnDay.checkDayKind */
static int day_kind(int day, int mask) {
  if (mask == -1) return 1;
  if (!mask) return 0;
  int r = 0;
  if (mask & 4) r |= day % 2 == 0;
  if (mask & 2) r |= day % 2 != 0;
  for (int n = 1; n <= 7; n++) if (mask & (4 << n)) r |= ((day - 1) % 7) + 1 == n;
  return r;
}

void dream_roll_gates(Dream *d, int day, unsigned *rng) {
  extern unsigned rng_next(unsigned *s);
  if (!d->ngate) return;
  char *ok = (char *)malloc((size_t)d->ngate);
  for (int g = 0; g < d->ngate; g++) {
    int pass = d->gate[g].one_in <= 1 || rng_next(rng) % (unsigned)d->gate[g].one_in == 0;   /* RandUtil.OneIn */
    ok[g] = (char)(pass && day_kind(day, d->gate[g].daymask));
  }
  for (int i = 0; i < d->nent; i++) {
    Ent *e = &d->ent[i]; e->gate_ok = 1;
    for (int k = 0; k < e->ngates; k++) if (e->gates[k] >= 0 && e->gates[k] < d->ngate && !ok[e->gates[k]]) e->gate_ok = 0;
  }
  free(ok);
}

const Env *dream_env(const Dream *d, int dreamIndex, int day, int *group) {
  *group = -1;
  if (d->grouped) {
    int n = day + dreamIndex, g = n % 4;
    if (d->nenv[g] > 0) { *group = g; return &d->env[g][n % d->nenv[g]]; }
  }
  if (d->nenv[0] > 0) return &d->env[0][((day - 1 + d->envoffset) % d->nenv[0] + d->nenv[0]) % d->nenv[0]];
  return &defenv;
}
