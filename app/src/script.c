#include "script.h"
#include "lsd.h"
#include "model.h"
#include "special.h"
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

ScriptCtl g_sctl = { 0, 0, 0, 0, 0, 1, 1, 0 };
int g_link_request = -2; char g_link_spawn[48]; int g_link_lock, g_link_sound; int g_trans_sounds = 1;      /* -2 none, -1 random wall link, >=0 dream */

static lua_State *L;
static const ScriptHost *H;
static Dream *cur;
static const DreamList *dlist;
static const CWorld *world;
static Player s_noplayer, *plp = &s_noplayer;   /* the real player is known from the first script_frame */
static int ready;

#define ENT(i) (&cur->ent[(i) - 1])

static int l_ents(lua_State *Ls) {
  lua_newtable(Ls);
  for (int i = 0; i < cur->nent; i++) {
    const Ent *e = &cur->ent[i];
    lua_newtable(Ls);
    { char nb[64]; snprintf(nb, sizeof nb, "%s", e->name); for (char *q = nb; *q; q++) if (*q == 31) *q = 32; lua_pushstring(Ls, nb); lua_setfield(Ls, -2, "name"); }
    char k[2] = { e->kind, 0 }; lua_pushstring(Ls, k); lua_setfield(Ls, -2, "kind");
    lua_pushnumber(Ls, e->pos[0]); lua_setfield(Ls, -2, "x"); lua_pushnumber(Ls, e->pos[1]); lua_setfield(Ls, -2, "y"); lua_pushnumber(Ls, e->pos[2]); lua_setfield(Ls, -2, "z");
    lua_pushnumber(Ls, e->yaw); lua_setfield(Ls, -2, "yaw"); lua_pushinteger(Ls, e->active); lua_setfield(Ls, -2, "active");
    lua_pushstring(Ls, e->script); lua_setfield(Ls, -2, "script"); lua_pushstring(Ls, e->fn); lua_setfield(Ls, -2, "fn"); lua_pushstring(Ls, e->fnexit); lua_setfield(Ls, -2, "fnexit");
    lua_pushnumber(Ls, e->idist); lua_setfield(Ls, -2, "dist");
    lua_pushnumber(Ls, e->scale[0] ? e->scale[0] : 1); lua_setfield(Ls, -2, "sx"); lua_pushnumber(Ls, e->scale[1] ? e->scale[1] : 1); lua_setfield(Ls, -2, "sy");
    lua_pushnumber(Ls, e->scale[2] ? e->scale[2] : 1); lua_setfield(Ls, -2, "sz");
    if (e->model[0]) { lua_pushstring(Ls, e->model); lua_setfield(Ls, -2, "model"); }
    if (e->parent >= 0) { lua_pushinteger(Ls, e->parent + 1); lua_setfield(Ls, -2, "parent"); lua_pushstring(Ls, e->epath); lua_setfield(Ls, -2, "epath"); }
    if (e->startonenable) { lua_pushboolean(Ls, 1); lua_setfield(Ls, -2, "startonenable"); }
    if (e->aud) { lua_pushboolean(Ls, 1); lua_setfield(Ls, -2, "audio"); }
    lua_rawseti(Ls, -2, i + 1);
  }
  return 1;
}
static int l_player_pos(lua_State *Ls) {
  lua_createtable(Ls, 0, 3); lua_pushnumber(Ls, plp->x); lua_setfield(Ls, -2, "x"); lua_pushnumber(Ls, plp->y); lua_setfield(Ls, -2, "y"); lua_pushnumber(Ls, plp->z); lua_setfield(Ls, -2, "z");
  return 1;
}
static int l_player_yaw(lua_State *Ls) { lua_pushnumber(Ls, plp->yaw); return 1; }
static int l_set_active(lua_State *Ls) { int i = (int)luaL_checkinteger(Ls, 1); if (i >= 1 && i <= cur->nent) ENT(i)->active = lua_toboolean(Ls, 2); return 0; }
static int l_p_override(lua_State *Ls) { g_sctl.override = lua_toboolean(Ls, 1); if (!g_sctl.override) { g_sctl.forward = 0; g_sctl.has_rot = 0; } return 0; }
static int l_p_rotate_to(lua_State *Ls) { g_sctl.has_rot = 1; g_sctl.rot_x = (float)luaL_checknumber(Ls, 1); g_sctl.rot_z = (float)luaL_checknumber(Ls, 2); return 0; }
static int l_p_forward(lua_State *Ls) { g_sctl.forward = lua_toboolean(Ls, 1); return 0; }
static int l_p_stop(lua_State *Ls) { (void)Ls; g_sctl.forward = 0; g_sctl.has_rot = 0; return 0; }
static int l_transition(lua_State *Ls) { if (H) H->transition((int)luaL_optinteger(Ls, 1, -1)); return 0; }
static int l_set_next(lua_State *Ls) { if (H) H->set_next((int)luaL_optinteger(Ls, 1, -1)); return 0; }
static int l_end_day(lua_State *Ls) { (void)Ls; if (H) H->end_day(); return 0; }
static int l_log_graph(lua_State *Ls) { if (H) H->log_graph((int)luaL_checkinteger(Ls, 1), (int)luaL_checkinteger(Ls, 2), luaL_optstring(Ls, 3, "")); return 0; }
static int l_day(lua_State *Ls) { lua_pushinteger(Ls, H ? H->day() : 1); return 1; }
static int l_zone(lua_State *Ls) { lua_pushstring(Ls, H ? H->zone() : "white"); return 1; }
static int l_arm(lua_State *Ls) { (void)Ls; g_sctl.teleports_armed = 1; return 0; }
static int l_ground(lua_State *Ls) {
  float g, x = (float)luaL_checknumber(Ls, 1), z = (float)luaL_checknumber(Ls, 2), y = (float)luaL_optnumber(Ls, 3, 1e6);
  if (world && cw_ground(world, x, z, y, &g)) lua_pushnumber(Ls, g); else lua_pushnil(Ls);
  return 1;
}
static void lower(char *d, const char *s) { size_t n = 0; for (; *s && n < 63; s++) if (!isspace((unsigned char)*s)) d[n++] = (char)tolower((unsigned char)*s); d[n] = 0; }
/* GetDreamByName: a script finds the dreams of its own journal first (the mod's dreams.lua / the original LuaInclude),
 * exact names before substrings */
static int l_dream_index(lua_State *Ls) {
  char want[64], have[64]; lower(want, luaL_checkstring(Ls, 1));
  int set = cur ? cur->set : 0;
  for (int pass = 0; pass < 4; pass++)
    for (int i = 0; i < dlist->n; i++) {
      if ((pass & 1) == 0 && dlist->d[i].set != set) continue;
      lower(have, dlist->d[i].name);
      if (pass < 2 ? !strcmp(want, have) : strstr(have, want) != NULL) { lua_pushinteger(Ls, i); return 1; }
    }
  lua_pushinteger(Ls, -1); return 1;
}
char g_next_spawn[48]; float g_next_rgb[3]; int g_has_next_rgb; float g_flash_rgb[3], g_flash_secs; int g_flash_req;
static int l_set_next_spawn(lua_State *Ls) { snprintf(g_next_spawn, sizeof g_next_spawn, "%s", luaL_optstring(Ls, 1, "")); return 0; }
static int l_set_next_color(lua_State *Ls) { for (int k = 0; k < 3; k++) g_next_rgb[k] = (float)luaL_optnumber(Ls, k + 1, 0); g_has_next_rgb = 1; return 0; }
static int l_flash(lua_State *Ls) { for (int k = 0; k < 3; k++) g_flash_rgb[k] = (float)luaL_optnumber(Ls, k + 1, 1); g_flash_secs = (float)luaL_optnumber(Ls, 4, 1); g_flash_req = 1; return 0; }
static int l_audio_play(lua_State *Ls) { int i = (int)luaL_checkinteger(Ls, 1); da_play(i - 1); return 0; }
static int l_audio_stop(lua_State *Ls) { int i = (int)luaL_checkinteger(Ls, 1); da_stop(i - 1); return 0; }
static int l_audio_get(lua_State *Ls) { int i = (int)luaL_checkinteger(Ls, 1); lua_pushnumber(Ls, da_get(i - 1, lua_toboolean(Ls, 2))); return 1; }
static int l_audio_set(lua_State *Ls) { int i = (int)luaL_checkinteger(Ls, 1); da_set(i - 1, lua_toboolean(Ls, 2), (float)luaL_checknumber(Ls, 3)); return 0; }
static int l_trans_sounds(lua_State *Ls) { g_trans_sounds = lua_toboolean(Ls, 1); return 0; }
/* GetEntity(id).VideoClip.Play(colour) -> DreamSystem.PlayVideo (special.c: movieclip table, main.c: fade + movie) */
static int l_video_play(lua_State *Ls) {
  sp_request_event(cur ? cur->folder : "", luaL_checkstring(Ls, 1), (float)luaL_optnumber(Ls, 2, 0), (float)luaL_optnumber(Ls, 3, 0), (float)luaL_optnumber(Ls, 4, 0));
  return 0;
}
static int l_read(lua_State *Ls) {
  size_t n; uint8_t *b = lsd_read_file(luaL_checkstring(Ls, 1), &n);
  if (!b) { lua_pushnil(Ls); return 1; }
  lua_pushlstring(Ls, (const char *)b, n); free(b); return 1;
}
static int l_log(lua_State *Ls) { lsd_log("lua: %s", luaL_tolstring(Ls, 1, NULL)); return 0; }
static int l_teleport(lua_State *Ls) { plp->x = (float)luaL_checknumber(Ls, 1); plp->y = (float)luaL_checknumber(Ls, 2); plp->z = (float)luaL_checknumber(Ls, 3); plp->vy = 0; return 0; }
static int l_set_flag(lua_State *Ls) {
  const char *n = luaL_checkstring(Ls, 1); int v = lua_toboolean(Ls, 2);
  if (!strcmp(n, "control")) g_sctl.can_control = v; else if (!strcmp(n, "gravity")) g_sctl.gravity = v;
  return 0;
}

/* ---- entity transforms and models (api.lua owns the transforms and pushes changes here; model.c draws them) ---- */
static int ent_arg(lua_State *Ls) { int i = (int)luaL_checkinteger(Ls, 1); return i >= 1 && i <= cur->nent ? i : 0; }
static int l_set_xform(lua_State *Ls) {
  int i = ent_arg(Ls); if (!i) return 0;
  float p[3], q[4], s[3];
  for (int k = 0; k < 3; k++) p[k] = (float)luaL_checknumber(Ls, 2 + k);
  for (int k = 0; k < 4; k++) q[k] = (float)luaL_checknumber(Ls, 5 + k);
  for (int k = 0; k < 3; k++) s[k] = (float)luaL_checknumber(Ls, 9 + k);
  Ent *e = ENT(i);
  memcpy(e->pos, p, sizeof p); memcpy(e->scale, s, sizeof s); memcpy(e->rot, q, sizeof q);
  float fx = 2 * (q[0] * q[2] + q[3] * q[1]), fz = 1 - 2 * (q[0] * q[0] + q[1] * q[1]);   /* forward = q * (0,0,1) */
  if (fx * fx + fz * fz > 1e-8f) e->yaw = atan2f(fx, fz) * 57.2957795f;
  model_set_xform(i - 1, p, q, s);
  return 0;
}
static int l_anim_play(lua_State *Ls) { int i = ent_arg(Ls); if (i) model_play(i - 1, luaL_optstring(Ls, 3, NULL), (int)luaL_optinteger(Ls, 2, 0)); return 0; }
static int l_anim_stop(lua_State *Ls) { int i = ent_arg(Ls); if (i) model_stop(i - 1, luaL_optstring(Ls, 2, NULL)); return 0; }
static int l_anim_resume(lua_State *Ls) { int i = ent_arg(Ls); if (i) model_resume(i - 1, luaL_optstring(Ls, 2, NULL)); return 0; }
static int l_anim_len(lua_State *Ls) { int i = ent_arg(Ls); lua_pushnumber(Ls, i ? model_clip_seconds(i - 1, luaL_optstring(Ls, 3, NULL), (int)luaL_optinteger(Ls, 2, 0)) : -1); return 1; }
static int l_has_model(lua_State *Ls) { int i = ent_arg(Ls); lua_pushboolean(Ls, i && model_has(i - 1)); return 1; }
static int l_child_find(lua_State *Ls) { int i = ent_arg(Ls); lua_pushboolean(Ls, i && model_find(i - 1, luaL_checkstring(Ls, 2))); return 1; }
static int l_child_active(lua_State *Ls) { int i = ent_arg(Ls); if (i) model_set_active(i - 1, luaL_optstring(Ls, 2, NULL), lua_toboolean(Ls, 3)); return 0; }
static int l_renderers(lua_State *Ls) { int i = ent_arg(Ls); if (i) model_set_renderers(i - 1, lua_toboolean(Ls, 2)); return 0; }
static int l_child_pos(lua_State *Ls) {
  int i = ent_arg(Ls); if (!i) return 0;
  float p[3] = { (float)luaL_checknumber(Ls, 3), (float)luaL_checknumber(Ls, 4), (float)luaL_checknumber(Ls, 5) };
  model_set_local_pos(i - 1, luaL_checkstring(Ls, 2), p); return 0;
}
static int l_player_set_yaw(lua_State *Ls) { plp->yaw = (float)luaL_checknumber(Ls, 1); return 0; }

static const luaL_Reg funcs[] = {
  {"_ents", l_ents}, {"_player_pos", l_player_pos}, {"_player_yaw", l_player_yaw}, {"_set_active", l_set_active}, {"_p_override", l_p_override},
  {"_p_rotate_to", l_p_rotate_to}, {"_p_forward", l_p_forward}, {"_p_stop", l_p_stop}, {"_transition", l_transition}, {"_set_next", l_set_next},
  {"_end_day", l_end_day}, {"_log_graph", l_log_graph}, {"_day", l_day}, {"_zone", l_zone}, {"_arm_teleports", l_arm}, {"_ground", l_ground},
  {"_dream_index", l_dream_index}, {"_read", l_read}, {"_log", l_log}, {"_teleport", l_teleport}, {"_set_flag", l_set_flag},
  {"_set_xform", l_set_xform}, {"_anim_play", l_anim_play}, {"_anim_stop", l_anim_stop}, {"_anim_resume", l_anim_resume}, {"_anim_len", l_anim_len},
  {"_has_model", l_has_model}, {"_child_find", l_child_find}, {"_child_active", l_child_active}, {"_renderers", l_renderers}, {"_child_pos", l_child_pos}, {"_player_set_yaw", l_player_set_yaw},
  {"_set_next_spawn", l_set_next_spawn}, {"_set_next_color", l_set_next_color}, {"_flash", l_flash}, {"_audio_play", l_audio_play}, {"_audio_stop", l_audio_stop},
  {"_audio_get", l_audio_get}, {"_audio_set", l_audio_set}, {"_trans_sounds", l_trans_sounds}, {"_video_play", l_video_play}, {NULL, NULL}
};

void script_init(const ScriptHost *h) { H = h; }

void script_shutdown(void) { if (L) { lua_close(L); L = NULL; } ready = 0; }

int script_level(Dream *d, const DreamList *dl, const CWorld *w) {
  script_shutdown();
  cur = d; dlist = dl; world = w;
  memset(&g_sctl, 0, sizeof g_sctl); g_sctl.can_control = 1; g_sctl.gravity = 1;
  g_link_request = -2;
  g_next_spawn[0] = 0; g_has_next_rgb = 0;
  for (int i = 0; i < d->nent; i++) { d->ent[i].fired = d->ent[i].inside = d->ent[i].interacted = d->ent[i].gc_done = 0; d->ent[i].active = d->ent[i].active0 && d->ent[i].gate_ok;
    memcpy(d->ent[i].pos, d->ent[i].pos0, sizeof d->ent[i].pos); d->ent[i].yaw = d->ent[i].yaw0; memcpy(d->ent[i].scale, d->ent[i].scale0, sizeof d->ent[i].scale);
    float hy = d->ent[i].yaw0 * 0.00872664626f; d->ent[i].rot[0] = 0; d->ent[i].rot[1] = sinf(hy); d->ent[i].rot[2] = 0; d->ent[i].rot[3] = cosf(hy); }
  g_trans_sounds = 1;
  L = luaL_newstate();
  if (!L) return -1;
  luaL_requiref(L, "_G", luaopen_base, 1); lua_pop(L, 1);
  luaL_requiref(L, "package", luaopen_package, 1); lua_pop(L, 1);
  luaL_requiref(L, "table", luaopen_table, 1); lua_pop(L, 1);
  luaL_requiref(L, "string", luaopen_string, 1); lua_pop(L, 1);
  luaL_requiref(L, "math", luaopen_math, 1); lua_pop(L, 1);
  luaL_requiref(L, "coroutine", luaopen_coroutine, 1); lua_pop(L, 1);
  luaL_requiref(L, "bit32", luaopen_bit32, 1); lua_pop(L, 1);
  for (const luaL_Reg *r = funcs; r->name; r++) lua_register(L, r->name, r->func);
  lua_pushstring(L, dl->luainc[d->set & 1]); lua_setglobal(L, "_INCDIR");     /* require "x" reads <dir>/x.lua */
  lua_pushboolean(L, d->set != 0); lua_setglobal(L, "_EAGER_START");
  { extern unsigned lsd_ticks(void); lua_pushinteger(L, (lua_Integer)(lsd_ticks() & 0x7FFFFFF)); lua_setglobal(L, "_SEED"); }          /* mod dreams: Start() of every active object at load */
  size_t n; uint8_t *api = lsd_read_file("api.lua", &n);
  if (!api) { lsd_log("lua: api.lua missing"); script_shutdown(); return -2; }
  int rc = luaL_loadbuffer(L, (const char *)api, n, "=api.lua") || lua_pcall(L, 0, 0, 0);
  free(api);
  if (rc) { lsd_log("lua: api.lua failed: %s", lua_tostring(L, -1)); script_shutdown(); return -3; }
  lua_getglobal(L, "__setup"); l_ents(L);
  if (lua_pcall(L, 1, 0, 0)) { lsd_log("lua: setup failed: %s", lua_tostring(L, -1)); lua_pop(L, 1); }
  lua_getglobal(L, "__start_all");
  if (lua_pcall(L, 0, 0, 0)) { lsd_log("lua: start: %s", lua_tostring(L, -1)); lua_pop(L, 1); }
  ready = 1;
  lsd_log("lua: %d entities, %.0f KB in use", d->nent, lua_gc(L, LUA_GCCOUNT, 0) + 0.0);
  return 0;
}

static int inside_box(const Ent *e, float px, float py, float pz) {
  float dx = px - e->pos[0], dy = py - e->pos[1], dz = pz - e->pos[2];
  float c = cosf(e->yaw * 0.0174532925f), s = sinf(e->yaw * 0.0174532925f);
  float lx = dx * c - dz * s, lz = dx * s + dz * c;                       /* inverse of Unity's yaw rotation */
  float sx = e->scale[0] ? e->scale[0] : 1, sy = e->scale[1] ? e->scale[1] : 1, sz = e->scale[2] ? e->scale[2] : 1;
  lx = lx / sx - e->box[0]; dy = dy / sy - e->box[1]; lz = lz / sz - e->box[2];
  float pad = 0.08f;
  return fabsf(lx) * sx <= e->box[3] * sx * 0.5f + pad && fabsf(dy) * sy <= e->box[4] * sy * 0.5f + pad + 0.2f && fabsf(lz) * sz <= e->box[5] * sz * 0.5f + pad;
}

static void call_trigger(int i, int exit) {
  lua_getglobal(L, "__trigger"); lua_pushinteger(L, i); lua_pushboolean(L, exit);
  if (lua_pcall(L, 2, 0, 0)) { lsd_log("lua: trigger: %s", lua_tostring(L, -1)); lua_pop(L, 1); }
}

void script_frame(float dt, Dream *d, Player *pl, const CWorld *w) {
  if (!ready) return;
  cur = d; plp = pl; world = w;
  float cy = pl->y + 0.2f;
  int near_idx[512], nn = 0;
  for (int i = 1; i <= d->nent; i++) {
    Ent *e = ENT(i);
    if (e->has_gc && !e->gc_done && e->live) {                     /* GraphContributor: once within its radius (3D) */
      float gx = e->pos[0] - pl->x, gy = e->pos[1] - pl->y, gz = e->pos[2] - pl->z;
      if (gx * gx + gy * gy + gz * gz < e->gc[2] * e->gc[2]) { e->gc_done = 1; if (H) H->log_graph((int)e->gc[0], (int)e->gc[1], e->name); lsd_log("graph contribution %s %.0f,%.0f", e->name, e->gc[0], e->gc[1]); }
    }
    if (e->kind == 'T') continue;
    float dx = e->pos[0] - pl->x, dz = e->pos[2] - pl->z, dd = dx * dx + dz * dz;
    if ((e->kind == 'L' || e->kind == 'I') && e->script[0] && e->script[0] != '-' && dd < 30.0f * 30.0f && nn < 512) near_idx[nn++] = i;
    if (!e->live) { e->inside = 0; continue; }
    if (e->kind == 'L' || e->kind == 'K' || e->kind == 'P') {
      int in = dd < 40.0f * 40.0f && inside_box(e, pl->x, cy, pl->z);
      if (in && !e->inside) {                                             /* enter */
        e->inside = 1;
        if (!(e->onlyonce && e->fired)) {
          e->fired = 1;
          if (e->kind == 'L') call_trigger(i, 0);
          else if (e->kind == 'K') { if (e->affects) { if (H) H->set_next(e->dream); } else { g_link_request = e->dream; snprintf(g_link_spawn, sizeof g_link_spawn, "%s", e->spawn); g_link_lock = e->lockinput; g_link_sound = e->linksound; lsd_log("link trigger %s -> %d", e->name, e->dream); } }
          else if (e->kind == 'P' && (e->alwayson || g_sctl.teleports_armed)) { pl->x = e->dest[0]; pl->y = e->dest[1] + 0.01f; pl->z = e->dest[2]; pl->vy = 0; }
        }
      } else if (!in && e->inside) {                                      /* exit */
        e->inside = 0;
        if (e->kind == 'L') call_trigger(i, 1); else if (e->kind == 'K' && e->affects && H) H->set_next(-1);
      }
    } else if (e->kind == 'I' && e->script[0] && e->script[0] != '-' && e->ikind != 2) {
      float r = e->idist > 0 ? e->idist : 3, dy = e->pos[1] - pl->y, d3 = dd + dy * dy;   /* Vector3.Distance (floors of the cottage!) */
      int in = d3 < r * r;
      if (in && e->ikind == 1) {                  /* InteractionType.View: only while the object is on screen (processViewInteraction) */
        float vx = e->pos[0] - pl->x, vy = e->pos[1] - (pl->y + EYE_HEIGHT), vz = e->pos[2] - pl->z;
        float cy = cosf(pl->yaw), sy = sinf(pl->yaw), cp = cosf(pl->pitch), sp = sinf(pl->pitch);
        float cz = vx * sy * cp + vy * sp + vz * cy * cp, cx = vx * cy - vz * sy, cyy = -vx * sy * sp + vy * cp - vz * cy * sp;
        in = cz > 0.01f && fabsf(cx) < cz * 0.80f && fabsf(cyy) < cz * 0.60f;   /* the 320x240 / focal 207 view frustum */
      }
      if (in && !e->interacted) { e->interacted = 1; lsd_log("interact %s", e->name); lua_getglobal(L, "__interact"); lua_pushinteger(L, i); if (lua_pcall(L, 1, 0, 0)) { lsd_log("lua: interact: %s", lua_tostring(L, -1)); lua_pop(L, 1); } }
      else if (!in && d3 > r * r * 2.25f) e->interacted = 0;
    }
  }
  lua_getglobal(L, "__tick"); lua_pushnumber(L, dt);
  lua_createtable(L, nn, 0); for (int k = 0; k < nn; k++) { lua_pushinteger(L, near_idx[k]); lua_rawseti(L, -2, k + 1); }
  lua_pushboolean(L, g_sctl.interact_pressed); g_sctl.interact_pressed = 0;
  if (lua_pcall(L, 3, 0, 0)) { lsd_log("lua: tick: %s", lua_tostring(L, -1)); lua_pop(L, 1); }
  /* scripted turning towards a point (PlayerMovement.RotateTowards) */
  if (g_sctl.has_rot) {
    float want = atan2f(g_sctl.rot_x - pl->x, g_sctl.rot_z - pl->z), diff = fmodf(want - pl->yaw + 9.42477796f, 6.2831853f) - 3.14159265f, step = 3.0f * dt;
    if (fabsf(diff) <= step) pl->yaw = want; else pl->yaw += diff > 0 ? step : -step;
  }
}
