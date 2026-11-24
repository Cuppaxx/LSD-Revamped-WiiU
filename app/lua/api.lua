-- api.lua: the LSD Revamped Lua API (a re-implementation of the subset the original dream scripts use). Runs on Lua 5.2.
-- C provides: _ents(), _player_pos(), _player_yaw(), _set_active(i, b), _p_override(b), _p_rotate_to(x, z), _p_forward(b), _p_stop(),
--             _transition(dream), _set_next(dream), _end_day(), _log_graph(d, u, entityID), _day(), _zone(), _arm_teleports(), _ground(x, z),
--             _audio_play(i), _audio_stop(i), _audio_get(i, isPitch), _audio_set(i, isPitch, v), _trans_sounds(b),
--             _dream_index(name), _read(path), _log(text), _teleport(x, y, z), _set_flag(name, value), _player_set_yaw(radians)
-- models (src/model.c; i = entity index, path = GameObject path below the entity like Transform.Find):
--             _set_xform(i, x, y, z, qx, qy, qz, qw, sx, sy, sz), _anim_play(i, clip), _anim_stop(i), _anim_resume(i),
--             _anim_len(i, clip) (seconds, -1 none), _has_model(i), _child_find(i, path) (bool), _child_active(i, path, b)
--             (path nil: the first child, SetChildVisible), _renderers(i, b), _child_pos(i, path, x, y, z). The anim functions
--             take an optional path of a child model (default: the entity's own AnimatedObject).

local sqrt, floor = math.sqrt, math.floor
local sin, cos, atan2, asin, acos, rad, deg = math.sin, math.cos, math.atan2, math.asin, math.acos, math.rad, math.deg

-- Vector3 values (LuaEngine.makeVec3Table): plain {x, y, z} tables; methods are called with a dot (v.length(), v.dot(b)),
-- so they are bound on lookup instead of stored in every vector
local VecMT = {}
local function vec(x, y, z) return setmetatable({ x = x or 0, y = y or 0, z = z or 0 }, VecMT) end
local vmethods = {
  length = function(v) return sqrt(v.x * v.x + v.y * v.y + v.z * v.z) end,
  normalise = function(v) local l = sqrt(v.x * v.x + v.y * v.y + v.z * v.z); if l < 1e-5 then return vec(0, 0, 0) end; return vec(v.x / l, v.y / l, v.z / l) end,
  negated = function(v) return vec(-v.x, -v.y, -v.z) end,
  dot = function(v, b) if b == nil then return 0 end; return v.x * b.x + v.y * b.y + v.z * b.z end,
  project = function(v, b) if b == nil then return vec(0, 0, 0) end; local d = b.x * b.x + b.y * b.y + b.z * b.z; if d < 1e-12 then return vec(0, 0, 0) end; local k = (v.x * b.x + v.y * b.y + v.z * b.z) / d; return vec(b.x * k, b.y * k, b.z * k) end,
}
vmethods.normalized = vmethods.normalise
VecMT.__index = function(v, k) local f = vmethods[k]; if f then return function(...) return f(v, ...) end end end
VecMT.__add = function(a, b) return vec(a.x + b.x, a.y + b.y, a.z + b.z) end
VecMT.__sub = function(a, b) return vec(a.x - b.x, a.y - b.y, a.z - b.z) end
VecMT.__unm = function(a) return vec(-a.x, -a.y, -a.z) end
VecMT.__mul = function(a, b)
  if type(a) == "number" then return vec(a * b.x, a * b.y, a * b.z) end
  if type(b) == "number" then return vec(a.x * b, a.y * b, a.z * b) end
  return vec(a.x * b.x, a.y * b.y, a.z * b.z)
end
VecMT.__div = function(a, b) return vec(a.x / b, a.y / b, a.z / b) end
VecMT.__eq = function(a, b) return a.x == b.x and a.y == b.y and a.z == b.z end
VecMT.__tostring = function(a) return "(" .. a.x .. ", " .. a.y .. ", " .. a.z .. ")" end

-- ---------------------------------------------------------------- quaternions (Unity conventions: {x, y, z, w}, left-handed,
-- Quaternion.Euler applies Z, then X, then Y; eulerAngles are in 0..360)
local function quat(x, y, z, w) return { x = x, y = y, z = z, w = w } end
local function qmul(a, b)
  return quat(a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
              a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z)
end
local function qrot(q, x, y, z)        -- q * v
  local tx, ty, tz = 2 * (q.y * z - q.z * y), 2 * (q.z * x - q.x * z), 2 * (q.x * y - q.y * x)
  return x + q.w * tx + (q.y * tz - q.z * ty), y + q.w * ty + (q.z * tx - q.x * tz), z + q.w * tz + (q.x * ty - q.y * tx)
end
local function qconj(q) return quat(-q.x, -q.y, -q.z, q.w) end
local function qnorm(q) local l = sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w); if l < 1e-9 then return quat(0, 0, 0, 1) end; return quat(q.x / l, q.y / l, q.z / l, q.w / l) end
local function qeuler(x, y, z)         -- degrees
  local hx, hy, hz = rad(x) * 0.5, rad(y) * 0.5, rad(z) * 0.5
  return qmul(qmul(quat(0, sin(hy), 0, cos(hy)), quat(sin(hx), 0, 0, cos(hx))), quat(0, 0, sin(hz), cos(hz)))
end
local function wrap360(a) a = a % 360; if a < 0 then a = a + 360 end; return a end
local function qto_euler(q)            -- Transform.eulerAngles
  local x, y, z, w = q.x, q.y, q.z, q.w
  local sx = 2 * (w * x - y * z); if sx > 1 then sx = 1 elseif sx < -1 then sx = -1 end
  local ex = asin(sx)
  local ey, ez
  if math.abs(sx) < 0.9999 then
    ey = atan2(2 * (x * z + w * y), 1 - 2 * (x * x + y * y)); ez = atan2(2 * (x * y + w * z), 1 - 2 * (x * x + z * z))
  else                                  -- gimbal lock: put everything in y
    ey = atan2(-2 * (x * z - w * y), 1 - 2 * (y * y + z * z)); ez = 0
  end
  return wrap360(deg(ex)), wrap360(deg(ey)), wrap360(deg(ez))
end
local function qfrom_basis(rx, ry, rz, ux, uy, uz, fx, fy, fz)   -- matrix columns right, up, forward
  local m00, m01, m02, m10, m11, m12, m20, m21, m22 = rx, ux, fx, ry, uy, fy, rz, uz, fz
  local tr = m00 + m11 + m22
  if tr > 0 then local s = sqrt(tr + 1) * 2; return quat((m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25 * s)
  elseif m00 > m11 and m00 > m22 then local s = sqrt(1 + m00 - m11 - m22) * 2; return quat(0.25 * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s)
  elseif m11 > m22 then local s = sqrt(1 + m11 - m00 - m22) * 2; return quat((m01 + m10) / s, 0.25 * s, (m12 + m21) / s, (m02 - m20) / s)
  else local s = sqrt(1 + m22 - m00 - m11) * 2; return quat((m02 + m20) / s, (m12 + m21) / s, 0.25 * s, (m10 - m01) / s) end
end
local function qlook(fx, fy, fz, ux, uy, uz)   -- Quaternion.LookRotation(forward, up); nil for a zero forward
  local l = sqrt(fx * fx + fy * fy + fz * fz); if l < 1e-6 then return nil end
  fx, fy, fz = fx / l, fy / l, fz / l
  local rx, ry, rz = uy * fz - uz * fy, uz * fx - ux * fz, ux * fy - uy * fx   -- up x forward
  local rl = sqrt(rx * rx + ry * ry + rz * rz)
  if rl < 1e-6 then rx, ry, rz = 1, 0, 0; if math.abs(fx) > 0.9 then rx, rz = 0, 1 end; rl = 1 end
  rx, ry, rz = rx / rl, ry / rl, rz / rl
  local vx, vy, vz = fy * rz - fz * ry, fz * rx - fx * rz, fx * ry - fy * rx    -- forward x right
  return qnorm(qfrom_basis(rx, ry, rz, vx, vy, vz, fx, fy, fz))
end
local function qdot(a, b) return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w end
local function qrotate_towards(a, b, maxdeg)   -- Quaternion.RotateTowards
  local d = qdot(a, b)
  if d < 0 then b = quat(-b.x, -b.y, -b.z, -b.w); d = -d end
  if d > 0.999999 then return b end
  local theta = acos(d < 1 and d or 1)
  local angle = deg(2 * theta)
  if angle <= maxdeg then return b end
  local t = maxdeg / angle
  local st = sin(theta); local w1, w2 = sin((1 - t) * theta) / st, sin(t * theta) / st
  return qnorm(quat(a.x * w1 + b.x * w2, a.y * w1 + b.y * w2, a.z * w1 + b.z * w2, a.w * w1 + b.w * w2))
end

-- a stand-in for something the port does not have: every unknown member is a do-nothing function (calls never error);
-- members that are objects themselves (AnimatedObject, DreamAudio, GameObject, Action...) are stand-ins too
-- NOP: callable (does nothing) and indexable (every member is NOP again), so x.Foo(), x.Foo.Play() and x.Foo.Bar.Baz() all
-- work on things the port does not have
local NOP
NOP = setmetatable({}, { __call = function() return nil end, __index = function() return NOP end, __newindex = function() end })
local new_runner
nop_obj = function(t)
  t = t or {}
  return setmetatable(t, { __index = function(tt, k)
    if k == "WorldPosition" or k == "LocalPosition" or k == "Forward" or k == "ForwardDirection" then return vec(0, 0, 0) end
    if k == "AnimatedObject" or k == "DreamAudio" or k == "GameObject" or k == "InteractiveObject" then local o = nop_obj({ Name = rawget(tt, "Name") }); rawset(tt, k, o); return o end
    if k == "Action" then local r = new_runner(); rawset(tt, k, r); return r end
    if k == "Active" then return false end
    return NOP
  end })
end

-- an API member the port does not implement, looked up on an entity: logged once, then a do-nothing function
local missing_seen = {}
missing = function(k)
  if type(k) ~= "string" or not k:match("^%u") then return nil end
  if not missing_seen[k] then missing_seen[k] = true; _log("api: no '" .. k .. "' (ignored)") end
  return NOP
end

local function dist(a, b) local dx, dy, dz = a.x - b.x, a.y - b.y, a.z - b.z; return sqrt(dx * dx + dy * dy + dz * dz) end

-- ---------------------------------------------------------------- time / action runner (LuaAsyncActionRunner)
local now = 0
local runners = {}

new_runner = function(owner)
  local r = { running = false, looping = false, began = false, owner = owner }   -- owner: entity whose GameObject must be active
  local function node(f) return { f = f } end
  local function wrap(n)
    local b = {}
    b.Until = function(p) n.pred = p; return b end
    b.Then = function(f) local nn = node(f); n.next = nn; return wrap(nn) end
    b.ThenWaitUntil = function(p) local nn = node(function() end); nn.pred = p; n.next = nn; return wrap(nn) end
    b.ThenFinish = function() r.running = true end
    b.ThenLoop = function() r.running = true; r.looping = true end
    return b
  end
  r.Do = function(f) r.root = node(f); r.cur = r.root; return wrap(r.root) end
  r.WaitUntil = function(p) local n = node(function() end); n.pred = p; r.root = n; r.cur = n; return wrap(n) end
  r.Stop = function() r.running = false; r.began = false end
  runners[#runners + 1] = r
  return r
end

local function step_runner(r)
  if not r.running or not r.root then return end
  if not r.began then r.began = true; r.cur = r.root; if r.root.pred then r.root.pred.begin() end end
  local guard = 0
  while r.running and guard < 200 do
    guard = guard + 1
    if r.cur == nil then
      if r.looping then r.cur = r.root; if r.root.pred then r.root.pred.begin() end
      else r.running = false; r.began = false; return end
    end
    local ok, err = pcall(r.cur.f)
    if not ok then _log("action error: " .. tostring(err)) end
    if r.cur.pred then
      local ok2, res = pcall(r.cur.pred.test)
      if not (ok2 and res) then return end
    end
    local nxt = r.cur.next; r.cur = nxt
    if nxt and nxt.pred then nxt.pred.begin() end
  end
end

local function pred(begin, test) return { begin = begin or function() end, test = test } end

Condition = {
  WaitForSeconds = function(sec) local t0 = 0; return pred(function() t0 = now end, function() return now - t0 >= sec end) end,
  Custom = function(f) return pred(nil, f) end,
  PointingAt = function(obj, pos)
    return pred(nil, function()
      local p = _player_pos(); local want = math.atan2(pos.x - p.x, pos.z - p.z); local d = (want - _player_yaw() + math.pi) % (2 * math.pi) - math.pi
      return math.abs(d) < 0.06
    end)
  end,
  WaitForLinearMove = function(obj, pos)
    local start
    return pred(function() local c = obj.WorldPosition; start = vec(c.x, c.y, c.z) end, function()
      local c = obj.WorldPosition
      local dx, dy, dz = pos.x - start.x, pos.y - start.y, pos.z - start.z
      local den = dx * dx + dy * dy + dz * dz; if den < 1e-9 then return true end
      return ((c.x - start.x) * dx + (c.y - start.y) * dy + (c.z - start.z) * dz) / den >= 1
    end)
  end,
}

-- ---------------------------------------------------------------- entities
-- Each entity owns a transform T (position px/py/pz, rotation quaternion q, scale sx/sy/sz). The Lua side is the source of
-- truth; when a script changes it the entity is marked dirty and __tick pushes it to C with _set_xform (which moves the
-- model instance and the entity's position used for interaction). Reads return copies, like Unity's Vector3 structs.
local ents, by_name, EntProxy = {}, {}, {}
local player
local dirty = {}                        -- entity index -> entity, pushed to C at the end of the tick
local cur_dt = 1 / 60

local function mark(e) dirty[e.index] = e end

-- transform properties shared by the InteractiveObject (this) and its GameObject (this.GameObject)
local getters = {
  WorldPosition = function(T) return vec(T.px, T.py, T.pz) end,
  WorldRotation = function(T) return vec(qto_euler(T.q)) end,
  Scale = function(T) return vec(T.sx, T.sy, T.sz) end,
  Forward = function(T) return vec(qrot(T.q, 0, 0, 1)) end,
  Right = function(T) return vec(qrot(T.q, 1, 0, 0)) end,
  Up = function(T) return vec(qrot(T.q, 0, 1, 0)) end,
}
getters.LocalPosition = getters.WorldPosition       -- entities hang off the dream root at the origin
getters.LocalRotation = getters.WorldRotation
getters.ForwardDirection = getters.Forward; getters.RightDirection = getters.Right; getters.UpDirection = getters.Up
local setters = {
  WorldPosition = function(T, v) T.px, T.py, T.pz = v.x, v.y, v.z end,
  WorldRotation = function(T, v) T.q = qeuler(v.x, v.y, v.z) end,
  Scale = function(T, v) T.sx, T.sy, T.sz = v.x, v.y, v.z end,
}
setters.LocalPosition = setters.WorldPosition; setters.LocalRotation = setters.WorldRotation

-- metatable for a proxy table t holding rawfield _T: transform properties through getters/setters, anything else via extra
local function transform_mt(owner_of, extra)
  return {
    __index = function(t, k)
      local g = getters[k]; if g then return g(rawget(t, "_T")) end
      if extra then return extra(t, k) end
    end,
    __newindex = function(t, k, v)
      local f = setters[k]
      if f then f(rawget(t, "_T"), v); mark(owner_of(t)) else rawset(t, k, v) end
    end,
  }
end

local function make_entity(i, info)
  local e = { ID = info.name, Name = info.name, kind = info.kind, index = i }
  local yaw = rad(info.yaw)
  local T = { px = info.x, py = info.y, pz = info.z, q = quat(0, sin(yaw / 2), 0, cos(yaw / 2)),
              sx = info.sx or 1, sy = info.sy or 1, sz = info.sz or 1 }
  e._T = T
  local go = { Name = info.name, active = info.active ~= 0, _T = T, _e = e }
  local function setactive(b) go.active = b and true or false; e.active = go.active; _set_active(i, go.active) end
  local function look_at(x, y, z)    -- Transform.LookAt(target, transform.up)
    local ux, uy, uz = qrot(T.q, 0, 1, 0)
    local q = qlook(x - T.px, y - T.py, z - T.pz, ux, uy, uz)
    if q then T.q = q; mark(e) end
  end
  local function anim_of(path)       -- AnimatedObject (path nil: the entity's own, else a child model's)
    return {
      Play = function(n) _anim_play(i, n or 0, path) end,
      Stop = function() _anim_stop(i, path) end,
      Resume = function() _anim_resume(i, path) end,
    }
  end
  local animated = anim_of(nil)
  -- DreamAudio (DreamAudioProxy): entities with an `audio` line play through src/dreamaudio.c; others are silent.
  -- Volume / Pitch read and write the DreamAudio fields; a write takes effect at the next Play (clamped 0..1 there)
  local audio
  if info.audio then
    audio = setmetatable({ Play = function() _audio_play(i) end, Stop = function() _audio_stop(i) end,
                           SetVolume = function(v) if type(v) == "number" then _audio_set(i, false, v) end end }, {
      __index = function(t, k) if k == "Volume" then return _audio_get(i, false) elseif k == "Pitch" then return _audio_get(i, true) end end,
      __newindex = function(t, k, v)
        if k == "Volume" and type(v) == "number" then _audio_set(i, false, v)
        elseif k == "Pitch" and type(v) == "number" then _audio_set(i, true, v)
        else rawset(t, k, v) end
      end })
  else
    audio = { Play = function() end, Stop = function() end, SetVolume = function() end, Volume = 1, Pitch = 1 }
  end

  -- GameObject proxy (GameObjectProxy.cs)
  go.SetActive = setactive
  go.IsActive = function() return go.active end
  go.DestroySelf = function() setactive(false) end
  go.LookAt = function(p) look_at(p.x, p.y, p.z) end
  go.LookAtPlane = function(p) look_at(p.x, T.py, p.z) end
  go.PositionToWorld = function(v) local x, y, z = qrot(T.q, v.x * T.sx, v.y * T.sy, v.z * T.sz); return vec(T.px + x, T.py + y, T.pz + z) end
  go.DirectionToWorld = function(v) return vec(qrot(T.q, v.x, v.y, v.z)) end
  go.PositionFromWorld = function(v) local x, y, z = qrot(qconj(T.q), v.x - T.px, v.y - T.py, v.z - T.pz); return vec(x / T.sx, y / T.sy, z / T.sz) end
  go.DirectionFromWorld = function(v) return vec(qrot(qconj(T.q), v.x, v.y, v.z)) end
  go.AnimatedObject = animated
  go.DreamAudio = audio
  go.InteractiveObject = e
  -- Transform.Find: a named object of this entity's model (its parts), else another entity of that name (child entities)
  local children = {}
  go.GetChildByName = function(n)
    if children[n] then return children[n] end
    local c
    n = tostring(n)
    local kid = e._kids and e._kids[n]                -- an entity nested below this one (epath), e.g. the "Audio" DreamAudio
    if kid then c = kid.GameObject
    elseif _child_find(i, n) then
      local cactive = true
      c = setmetatable({ Name = n:match("[^/]*$"),
        SetActive = function(b) cactive = b and true or false; _child_active(i, n, cactive) end,
        AnimatedObject = anim_of(n), DreamAudio = audio }, {
        __index = function(t, k)
          if k == "Active" then return cactive and go.active end
          if k == "LocalPosition" then return vec(0, 0, 0) end
          local g = getters[k]; if g then return g(T) end
        end,
        __newindex = function(t, k, v)
          if k == "LocalPosition" then _child_pos(i, n, v.x, v.y, v.z) else rawset(t, k, v) end
        end })
    elseif by_name[n] then c = by_name[n].GameObject
    else
      _log("GetChildByName: '" .. tostring(n) .. "' not found under " .. info.name)
      c = nop_obj({ Name = n, WorldPosition = vec(T.px, T.py, T.pz) })
    end
    children[n] = c
    return c
  end
  go.SetChildVisible = function(b) _child_active(i, nil, b and true or false) end
  go.SetRenderersActive = function(b) _renderers(i, b and true or false) end
  setmetatable(go, transform_mt(function(t) return rawget(t, "_e") end, function(t, k)
    if k == "Active" then return rawget(t, "active") end
    if k == "Action" then return e.Action end
    return missing(k)
  end))
  e.GameObject = go; e.active = go.active
  e.SetActive = setactive

  -- InteractiveObject proxy (InteractiveObjectProxy.cs)
  e.InteractionDistance = info.dist or 3
  e.LogGraphContribution = function(d, u) _log_graph(d, u, info.name) end   -- an ENTITY contribution (with this entity's ID)
  e.SetUpdateIntervalSeconds = function() end
  e.SetChildVisible = go.SetChildVisible
  e.SetRenderersActive = go.SetRenderersActive
  e.GetChildByName = go.GetChildByName
  e.AnimatedObject = animated
  e.DreamAudio = audio
  -- MovieClip entities (DreamSystem.PlayVideo): the movie of this entity ID (specials.txt movieclip lines), faded in with c
  e.VideoClip = nop_obj({ Play = function(c) c = c or {}; _video_play(info.name, c.r or 0, c.g or 0, c.b or 0) end })
  go.VideoClip = e.VideoClip
  e.InteractiveObject = e
  e.Init = function() end
  e.PlayAnimation = animated.Play
  e.StopAnimation = animated.Stop
  e.ResumeAnimation = animated.Resume
  e.GetAnimationLengthSeconds = function(n)
    local l = _anim_len(i, n or 0)
    if l < 0 then if _has_model(i) then return 0 end; return 1.0 end   -- no such clip / no model converted yet
    return l
  end
  e.WaitForAnimation = function(n, off)                       -- a WaitForSecondsPredicate of the clip length
    local len = e.GetAnimationLengthSeconds(n) + (off or 0); local t0 = 0
    return pred(function() t0 = now end, function() return now - t0 >= len end)
  end
  e.MoveInDirection = function(dir, speed)
    local s = speed * cur_dt
    T.px, T.py, T.pz = T.px + dir.x * s, T.py + dir.y * s, T.pz + dir.z * s; mark(e)
  end
  e.MoveTowards = function(target, speed)                     -- true (and snapped) when within 0.1
    local dx, dy, dz = target.x - T.px, target.y - T.py, target.z - T.pz
    local l = sqrt(dx * dx + dy * dy + dz * dz)
    if l < 0.1 then T.px, T.py, T.pz = target.x, target.y, target.z; mark(e); return true end
    e.MoveInDirection(vec(dx / l, dy / l, dz / l), speed)
    return false
  end
  e.LookAt = go.LookAt
  e.LookAtPlane = go.LookAtPlane
  e.LookInDirection = function(d) look_at(T.px + d.x, T.py + d.y, T.pz + d.z) end
  e.LookTowards = function(p, speed)                          -- turn at speed deg/s; true when facing p (in the XZ plane)
    local dx, dy, dz = p.x - T.px, p.y - T.py, p.z - T.pz
    local l = sqrt(dx * dx + dy * dy + dz * dz); if l < 1e-6 then return true end
    dx, dz = dx / l, dz / l                                   -- normalised, then flattened (like the C#)
    if dx * dx + dz * dz < 0.01 then return true end
    local ux, uy, uz = qrot(T.q, 0, 1, 0)
    local want = qlook(dx, 0, dz, ux, uy, uz); if not want then return true end
    if math.abs(qdot(T.q, want)) > 0.99 then T.q = want; mark(e); return true end
    T.q = qrotate_towards(T.q, want, speed * cur_dt); mark(e)
    return false
  end
  e.Rotate = function(v)                                      -- Transform.Rotate(degreesPerSecond * dt, Space.Self)
    T.q = qnorm(qmul(T.q, qeuler(v.x * cur_dt, v.y * cur_dt, v.z * cur_dt))); mark(e)
  end
  e.SnapToFloor = function(check)                             -- raycast down from `check` units above (default 10)
    local g = _ground(T.px, T.pz, T.py + (check or 10))
    if g and g ~= T.py then T.py = g; mark(e) end
  end
  e.StretchShrink = function(f) T.sx, T.sy, T.sz = 1, f, 1; mark(e) end
  return setmetatable(e, transform_mt(function(t) return t end, function(t, k)
    if k == "Action" then local r = rawget(t, "_runner"); if not r then r = new_runner(t); rawset(t, "_runner", r) end; return r end
    if k == "Active" then return rawget(t, "active") ~= false end
    return missing(k)
  end))
end

-- push the transforms scripts changed this tick to C (model instances, interaction positions)
local function follow_parent(e)                -- move nested entities along with their parent (recursively)
  local kids = rawget(e, "_children"); if not kids then return end
  local P = e._T
  for _, c in ipairs(kids) do
    local r, C = rawget(c, "_rel"), c._T
    if r then
      local x, y, z = qrot(P.q, r.px * P.sx, r.py * P.sy, r.pz * P.sz)
      C.px, C.py, C.pz = P.px + x, P.py + y, P.pz + z; C.q = qmul(P.q, r.q)
      _set_xform(c.index, C.px, C.py, C.pz, C.q.x, C.q.y, C.q.z, C.q.w, C.sx, C.sy, C.sz)
      follow_parent(c)
    end
  end
end
local function flush_dirty()
  for i, e in pairs(dirty) do
    local T = e._T
    _set_xform(i, T.px, T.py, T.pz, T.q.x, T.q.y, T.q.z, T.q.w, T.sx, T.sy, T.sz)
    follow_parent(e)
    dirty[i] = nil
  end
end

local function make_player()
  local p = { ID = "__player", Name = "__player", kind = "player" }
  p.GameObject = { Name = "__player", SetActive = function() end }
  local mt = { __index = function(t, k)
    if k == "WorldPosition" then local q = _player_pos(); return vec(q.x, q.y, q.z) end
    if k == "Action" then local r = rawget(t, "_runner"); if not r then r = new_runner(); rawset(t, "_runner", r) end; return r end
    if k == "Forward" or k == "ForwardDirection" then local y = _player_yaw(); return vec(math.sin(y), 0, math.cos(y)) end
    if k == "Right" or k == "RightDirection" then local y = _player_yaw(); return vec(math.cos(y), 0, -math.sin(y)) end
    if k == "Up" or k == "UpDirection" then return vec(0, 1, 0) end
    if k == "WorldRotation" or k == "LocalRotation" then return vec(0, wrap360(deg(_player_yaw())), 0) end
    return rawget(t, k)
  end, __newindex = function(t, k, v)
    if k == "WorldPosition" then _teleport(v.x, v.y, v.z)
    elseif k == "WorldRotation" or k == "LocalRotation" then _player_set_yaw(rad(v.y))   -- only the yaw: the player stays upright
    else rawset(t, k, v) end
  end }
  p.GameObject.WorldPosition = nil
  setmetatable(p.GameObject, { __index = function(t, k) if k == "WorldPosition" then return p.WorldPosition end end })
  p.SetActive = function() end
  local function face(pt) local q = _player_pos(); local dx, dz = pt.x - q.x, pt.z - q.z; if dx * dx + dz * dz > 1e-8 then _player_set_yaw(math.atan2(dx, dz)) end end
  p.LookAtPlane = face; p.LookAt = face; p.LookInDirection = function(d) local q = _player_pos(); face(vec(q.x + d.x, 0, q.z + d.z)) end
  p.GameObject.LookAtPlane = face; p.GameObject.LookAt = face
  p.PlayerMovement = nop_obj {
    OverrideMovement = function() _p_override(true) end,
    RotateTowards = function(pos) _p_rotate_to(pos.x, pos.z) end,
    StartMovingForward = function() _p_forward(true) end,
    StopMovementAndRotation = function() _p_stop() end,
    SetInput = function() end, UseExternalInput = function() end, StopExternalInput = function() _p_override(false) end,
    SetInputLock = function() end, ClearInputLock = function() _p_override(false) end,
  }
  return setmetatable(p, mt)
end

GetEntity = function(name)
  if name == "__player" then return player end
  local e = by_name[name]
  if not e then _log("GetEntity: no entity '" .. tostring(name) .. "'")
    local d = nop_obj({ Name = tostring(name), ID = tostring(name), WorldPosition = vec(0, 0, 0) })
    d.InteractiveObject = d; d.GameObject = nop_obj({ Name = tostring(name), WorldPosition = vec(0, 0, 0) })
    by_name[name] = d
    return d end
  return e
end

-- dream objects: scripts compare/pass them back to DreamSystem
local dream_cache = {}
GetDreamByName = function(name) local i = _dream_index(name); if i < 0 then return nil end; dream_cache[i] = dream_cache[i] or { index = i, Name = name }; return dream_cache[i] end

-- ---------------------------------------------------------------- global API
local next_dream
DreamSystem = {
  SetNextTransitionDream = function(d) next_dream = d and d.index or nil; _set_next(next_dream or -1) end,
  TransitionToDream = function() _transition(next_dream or -1); next_dream = nil end,
  EndDream = function() _end_day() end,
  ArmTeleports = function() _arm_teleports() end,
  SetPlayerGravity = function(b) _set_flag("gravity", b) end,
  SetNextTransitionColor = function(c) if c then _set_next_color(c.r or 0, c.g or 0, c.b or 0) end end,
  SetNextTransitionSpawnID = function(id) _set_next_spawn(id and tostring(id) or "") end,
  SetNextTransitionLockControls = function() end, SetTransitionSounds = function(b) _trans_sounds(b and true or false) end,
  LogGraphContributionFromEntity = function(d, u, ent) _log_graph(d, u, ent and (rawget(ent, "ID") or ent.Name) or "") end,
  FadeFromBlack = function() end, FadeToBlack = function() end,
  Flash = function(c, secs) c = c or Colors.White; _flash(c.r or 1, c.g or 1, c.b or 1, secs or 1) end,
  StretchDream = function() end, OnDreamTimeout = function() end,
  StretchShrink = function() end,
}
local function rgba(r, g, b, a) return { r = r, g = g, b = b, a = a or 1 } end
Colors = { Red = rgba(1, 0, 0), Black = rgba(0, 0, 0), Blue = rgba(0, 0, 1), Clear = rgba(0, 0, 0, 0), Cyan = rgba(0, 1, 1),
  Gray = rgba(0.5, 0.5, 0.5), Grey = rgba(0.5, 0.5, 0.5), Magenta = rgba(1, 0, 1), White = rgba(1, 1, 1), Yellow = rgba(1, 0.92, 0.016),
  Green = rgba(0, 1, 0) }
Input = { Interact = { WasPressed = false, IsPressed = false } }
setmetatable(DreamSystem, { __index = function(t, k)
  if k == "DayNumber" or k == "CurrentDay" then return _day() end
  if k == "InFlashback" or k == "InDream" then return k == "InDream" end
  if k == "EnvironmentGroup" then return -1 end
end })
IsDayEven = function() return _day() % 2 == 0 end
IsDayLinear = function(slope, constant) if not slope or slope == 0 then return false end; return (_day() - (constant or 0)) % slope == 0 end
ChartZone = function() return _zone() end
SetCanControlPlayer = function(b) _set_flag("control", b) end
Random = {
  OneIn = function(n) return math.random(1, n) == 1 end,
  IntMax = function(n) return math.random(0, n - 1) end,
  FloatMinMax = function(a, b) return a + math.random() * (b - a) end,
  Float = function() return math.random() end,
  IntMinMax = function(a, b) if b <= a then return a end; return math.random(a, b - 1) end,   -- max exclusive (System.Random.Next)
  Int = function(n) if not n or n <= 0 then return 0 end; return math.random(0, n - 1) end,
}
Unity = {
  DeltaTime = function() return cur_dt end,
  CurrentTime = function() return now end,
  Vector3 = function(x, y, z) return vec(x, y, z) end,
  ColorRGB = function(r, g, b) return { r = r, g = g, b = b, a = 1 } end,
  ColorRGBA = function(r, g, b, a) return { r = r, g = g, b = b, a = a } end,
}
Vector3 = { New = vec, Zero = vec(0, 0, 0) }

-- ---------------------------------------------------------------- script instances
local function env_for(e)
  local env = setmetatable({ this = e }, { __index = _G })
  return env
end

local instances = {}
local function load_script(e, path)
  local src = _read(path)
  if not src then _log("missing script " .. tostring(path)); return nil end
  local env = env_for(e)
  local chunk, err = load(src, "=" .. path, "t", env)
  if not chunk then _log("script " .. path .. ": " .. tostring(err)); return nil end
  local ok, err2 = pcall(chunk)
  if not ok then _log("script " .. path .. " run: " .. tostring(err2)); return nil end
  return env
end

local loaded_inc = {}
require = function(name)
  if loaded_inc[name] ~= nil then return loaded_inc[name] end
  local src = _read((_INCDIR or "LuaInclude") .. "/" .. name .. ".lua")
  if not src then error("module '" .. tostring(name) .. "' not found", 2) end
  local f, err = load(src, "=" .. name .. ".lua", "t", _G)
  if not f then error(err, 2) end
  local r = f(); if r == nil then r = true end
  loaded_inc[name] = r; return r
end
local _unused_preload = function()
  local t = {}
  local src = _read("LuaInclude/dreams.lua")
  if src then local f = load(src, "=dreams.lua", "t", setmetatable({}, { __index = _G })); if f then local ok, r = pcall(f); if ok and r then return r end end end
  return t
end

__setup = function(list)
  ents, by_name, instances, dirty = {}, {}, {}, {}
  runners = {}; loaded_inc = {}
  player = make_player()
  if _SEED then math.randomseed(_SEED) end
  for i, info in ipairs(list) do
    local e = make_entity(i, info)
    ents[i] = e; by_name[info.name] = e
  end
  -- nested entities (epath lines of the mesh dreams): GetChildByName finds them below their parent, and they follow its
  -- transform: child = parent * inverse(parent at load) * child at load
  for i, info in ipairs(list) do
    local p = info.parent and ents[info.parent]
    if p then
      local e = ents[i]
      p._kids = p._kids or {}; p._kids[info.epath or e.Name] = e
      p._children = p._children or {}; p._children[#p._children + 1] = e
      local P, C = p._T, e._T
      local iq = qconj(P.q)
      local rx, ry, rz = qrot(iq, C.px - P.px, C.py - P.py, C.pz - P.pz)
      e._rel = { px = rx / P.sx, py = ry / P.sy, pz = rz / P.sz, q = qmul(iq, C.q) }
    end
  end
  for i, info in ipairs(list) do
    local e = ents[i]
    if info.script and info.script ~= "-" and (info.kind == "L" or info.kind == "I") then
      local env = load_script(e, info.script)
      if env then
        instances[i] = { env = env, e = e, info = info, started = false, was_active = info.active ~= 0 }
      end
    end
  end
  eager_pending = _EAGER_START and #list or nil   -- mesh dreams: Unity's Start() of every active object, on the first tick
end

local function call(env, name, ...)
  local f = rawget(env, name)
  if type(f) == "function" then local ok, err = pcall(f, ...); if not ok then _log(name .. ": " .. tostring(err)) end end
end

-- InteractiveObject.Init runs IntervalUpdate once right away, so scripts that measure distances there (tv.lua, bmcBear.lua)
-- have real values before their first update
local function begin(inst)
  inst.started = true; call(inst.env, "start"); call(inst.env, "intervalUpdate")
end

__trigger = function(i, exit)
  local inst = instances[i]; if not inst then return end
  call(inst.env, exit and inst.info.fnexit or inst.info.fn)
end

__interact = function(i)
  local inst = instances[i]; if not inst then return end
  if not inst.started then begin(inst) end
  if inst.e.active == false then return end      -- start() may have switched it off (e.g. doublefaceRun on odd days)
  call(inst.env, "interact")
end

-- mesh dreams: Unity's Start() of every active object at load (called by script_level right after __setup, before the
-- first trigger/interaction check, so objects have already moved/hidden themselves)
__start_all = function()
  local n = eager_pending; eager_pending = nil
  if not n then return end
  for i = 1, n do local inst = instances[i]; if inst and not inst.started and inst.e.active ~= false then begin(inst) end end
  local ok, err = pcall(flush_dirty); if not ok then _log("flush: " .. tostring(err)) end
end

local itimer = 0
__tick = function(dt, active_near, pressed)
  now = now + dt; cur_dt = dt
  Input.Interact.WasPressed = pressed and true or false; Input.Interact.IsPressed = Input.Interact.WasPressed
  itimer = itimer + dt
  local interval = itimer >= 0.25; if interval then itimer = 0 end
  if eager_pending then __start_all() end
  for _, i in ipairs(active_near) do
    local inst = instances[i]
    if inst and inst.e.active ~= false then
      inst.e.dt = dt
      if inst.started and inst.info.startonenable and not inst.was_active then begin(inst) end   -- CallStartOnEnable
      inst.was_active = true
      if not inst.started then begin(inst) end
      if inst.e.active ~= false then
        call(inst.env, "update")
        if interval then call(inst.env, "intervalUpdate") end
      end
    elseif inst then inst.was_active = false
    end
  end
  for _, r in ipairs(runners) do if not r.owner or r.owner.active ~= false then local ok, err = pcall(step_runner, r); if not ok then _log("runner: " .. tostring(err)) end end end   -- a MonoBehaviour: no Update while inactive
  local ok, err = pcall(flush_dirty); if not ok then _log("flush: " .. tostring(err)) end
end
