#!/usr/bin/env python
"""Bake LSD: Revamped's sound effects for the Wii U port (everything except music and the title/menu sounds).

Usage:
  python -I tools\\audio_assets.py <LSDRevamped repo> <outdir> [--mod <Additional Dreams clone>] [--rate 22050]
                                   [--all-samples] [--merge <data dir>] [--verify]

  <outdir>        staging dir (normally tools\\out_audio). Writes:
                    audio.txt       footstep tables, link/fall/sound-test sounds, music notes and the DreamAudio records of
                                    the 14 original dreams (format: AUDIO_FORMAT.md, same folder as this file)
                    SFX/*.wav       the sounds those tables use, <rate> Hz mono signed 16-bit PCM WAV
                    ADDSND/*.wav    the Additional Dreams' footstep sounds (same folder/naming as tools/addl.py's DreamAudio
                                    clips, so both land in one folder in the data dir)
  --mod DIR       Additional Dreams clone (default: <this folder>/../ref/LSDR-Additional-Dreams); skipped when missing
  --rate N        output sample rate (default 22050; the PS1 sources are 11025 Hz, so 11025 halves the size losslessly)
  --all-samples   also convert every sample of the PS1 banks (SND/LoadedSamples/*, ETC/LoadedSamples/ETCSE) to SFX/bank/
                  (nothing in Revamped plays them; only for experiments)
  --merge DIR     after baking, copy audio.txt, SFX/ and ADDSND/ into data dir DIR and insert the `audio` / `asrc` lines
                  after the matching `ent` lines of DIR/dreams.txt (idempotent). convert.py calls this with --audio.
  --verify        decode every written WAV and print counts/sizes (also done at the end of a normal run)

Nothing here imports convert.py (it is a script); the few Unity-YAML helpers and the prefab Transform walk are copied
from it so entity names/positions come out character-identical to its `ent` lines.
"""
import os, re, sys, glob, math, shutil, subprocess, wave

HERE = os.path.dirname(os.path.abspath(__file__))
DREAMAUDIO_GUID = 'cc8a685b01684a7bb48bf5ff0bbd6e80'          # Packages/com.figglewatts.lsdr.sdk/Runtime/Entities/DreamAudio.cs
TRIG = {'eb87da8162174fe3be019fb7462fe1cf': 'L', 'c2064ad0257e4cb478058e31e87d2613': 'K', 'a6756dc41dbcffeadb913dcedce78d6f': 'P',
        'c5b0923921724b18b17afa40a2d01bf7': 'I', 'aadec67881284206abea7f2dc299e32e': 'T'}   # as convert.py
TRIG_LINK = 'c2064ad0257e4cb478058e31e87d2613'           # TriggerLink.cs

def read(p): return open(p, encoding='utf8', errors='ignore').read()
def field(t, name, default=None):
    m = re.search(r'^\s*' + name + r': (.*)$', t, re.M); return m.group(1).strip() if m else default

def guid_map(*roots):
    g = {}
    for r in roots:
        for d in ('Assets', 'Packages'):
            for m in glob.glob(os.path.join(r, d, '**', '*.meta'), recursive=True):
                if os.sep + 'Library' + os.sep in m or 'PackageCache' in m: continue
                try: t = open(m, encoding='utf8', errors='ignore').read(600)
                except OSError: continue
                x = re.search(r'guid: (\w+)', t)
                if x: g.setdefault(x.group(1), m[:-5])
    return g

def qmul(a, b):
    ax, ay, az, aw = a; bx, by, bz, bw = b
    return (aw*bx + ax*bw + ay*bz - az*by, aw*by - ax*bz + ay*bw + az*bx, aw*bz + ax*by - ay*bx + az*bw, aw*bw - ax*bx - ay*by - az*bz)
def qrot(q, v):
    x, y, z, w = q
    p = qmul(qmul(q, (v[0], v[1], v[2], 0.0)), (-x, -y, -z, w))
    return p[:3]

class Ctx:
    def __init__(self, repo, out, rate, mod):
        self.repo = repo; self.out = out; self.rate = rate; self.mod = mod
        self.base = os.path.join(repo, 'LSDR', 'Assets', 'Resources'); self.orig = os.path.join(self.base, 'Original')
        print('indexing .meta files...')
        self.guid = guid_map(os.path.join(repo, 'LSDR'))
        self.mguid = guid_map(mod) if mod else {}
        self.clips = {}           # out rel path -> source path
        self.lines = []
        self.notes = []
    def ref(self, s, mod=False):
        m = re.search(r'guid: (\w+)', s or '');
        if not m: return None
        return (self.mguid.get(m.group(1)) if mod else None) or self.guid.get(m.group(1))
    def refs_after(self, t, name):
        m = re.search(r'^\s*' + name + r':\n((?:\s+- \{.*\}\n?)+)', t, re.M)
        return [self.ref(l) for l in m.group(1).strip().split('\n')] if m else []

    def clip(self, src, folder='SFX'):
        """register a source clip, return its runtime path (data-dir relative) or '-'"""
        if not src: return '-'
        src = os.path.normpath(src)
        rel = os.path.relpath(src, self.orig).replace(os.sep, '/') if src.startswith(os.path.normpath(self.orig)) else None
        if folder == 'ADDSND':
            se = os.path.join(self.orig, 'SND', 'LoadedSamples', 'SE', os.path.basename(src))   # the mod ships byte-identical
            if re.match(r'Sample\d+\.wav$', os.path.basename(src)) and os.path.exists(se) and \
               open(se, 'rb').read() == open(src, 'rb').read():                                 # copies of the SE bank: share them
                return self.clip(se)
            name ='ADDSND/' + os.path.splitext(os.path.basename(src))[0] + '.wav'   # addl.py's naming
        else:
            m = re.match(r'(?:SND|ETC)/LoadedSamples/(\w+)/Sample(\d+)\.wav$', rel or '', re.I)
            if m: name = 'SFX/%s_%s.wav' % (m.group(1).upper(), m.group(2))
            else: name = 'SFX/' + re.sub(r'[^A-Za-z0-9_.-]', '_', os.path.splitext(os.path.basename(src))[0]).lower() + '.wav'
        self.clips[name] = src
        return name

# ---------------------------------------------------------------- footsteps
def footstep_tables(c):
    """LBDFootstepIndex of the original journal and the UVFootstepIndex of the Additional Dreams journal"""
    t = read(os.path.join(c.orig, 'OriginalDreams.asset'))
    idx_path = c.ref(field(t, 'FootstepIndex'))
    L = c.lines
    L.append('# footsteps, original journal (dreams.txt): LBDFootstepIndex %s' % os.path.relpath(idx_path, c.base).replace(os.sep, '/'))
    ft = read(idx_path)
    fb = re.search(r'Fallback:\n\s+Sound: (\{.*\})\n\s+Pitch: ([-\d.e]+)', ft)
    L.append('foot lbdfallback %s %s' % (c.clip(c.ref(fb.group(1))), fb.group(2)))
    keys_hex = re.search(r'_keys: (\w+)', ft).group(1)
    keys = [int.from_bytes(bytes.fromhex(keys_hex[i:i + 8]), 'little', signed=True) for i in range(0, len(keys_hex), 8)]
    vals = re.findall(r'- Sound: (\{.*\})\n\s+Pitch: ([-\d.e]+)', ft.split('_values:')[1].split('_tempKey')[0])
    assert len(keys) == len(vals), (len(keys), len(vals))
    for k, (s, p) in sorted(zip(keys, vals)):
        L.append('foot lbd %d %s %s' % (k, c.clip(c.ref(s)), p))
    if not c.mod: c.notes.append('Additional Dreams clone not found: no uv footstep table'); return
    at = read(os.path.join(c.mod, 'Assets', 'AdditionalDreams.asset'))
    up = c.ref(field(at, 'FootstepIndex'), mod=True)
    if not up: c.notes.append('mod FootstepIndex not resolved'); return
    ut = read(up)
    L.append('# footsteps, Additional Dreams journal (dreams_addl.txt): UVFootstepIndex %s' % os.path.relpath(up, c.mod).replace(os.sep, '/'))
    ad = re.search(r'AtlasDimensions: \{x: (\d+), y: (\d+)\}', ut)
    L.append('foot uvatlas %s %s' % ad.groups())
    fb = re.search(r'Fallback:\n\s+Sound: (\{.*\})\n\s+Pitch: ([-\d.e]+)', ut)
    L.append('foot uvfallback %s %s' % (c.clip(c.ref(fb.group(1), mod=True), 'ADDSND'), fb.group(2)))
    ks = re.findall(r'- x: (-?\d+)\n\s+y: (-?\d+)\n\s+width: (\d+)\n\s+height: (\d+)', ut.split('_keys:')[1].split('_values:')[0])
    vs = re.findall(r'- Sound: (\{.*\})\n\s+Pitch: ([-\d.e]+)', ut.split('_values:')[1].split('_tempKey')[0])
    assert len(ks) == len(vs), (len(ks), len(vs))
    for (x, y, w, h), (s, p) in zip(ks, vs):        # keep the serialized order: first containing rect wins
        L.append('foot uv %s %s %s %s %s %s' % (x, y, w, h, c.clip(c.ref(s, mod=True), 'ADDSND'), p))

# ---------------------------------------------------------------- global one-shot sounds
def global_sounds(c):
    L = c.lines
    ds = read(os.path.join(c.repo, 'LSDR', 'Assets', 'UnityEditor', 'Systems', 'DreamSystem.asset'))
    link = c.clip(c.ref(field(ds, 'LinkSound')))
    L.append('# DreamSystem.LinkSound: Transition() (link triggers, scripted transitions, wall links) and EndDream(fromFall)')
    L.append('link %s 4 0.25 0.5 1 2' % link)
    L.append('fall %s 2 0.25 0.5' % link)
    # UIAudioSettings.SoundTestClip (the SFX volume slider test sound)
    st = glob.glob(os.path.join(c.repo, 'LSDR', 'Assets', 'UnityEditor', 'Resources', 'Prefabs', 'UI', 'Settings', 'AudioSettings.prefab'))
    if st:
        s = c.ref(field(read(st[0]), 'SoundTestClip'))
        if s: L.append('soundtest %s' % c.clip(s))

# ---------------------------------------------------------------- music notes (no files: convert.py copies the songs)
def music_notes(c):
    L = c.lines
    L.append('# music: convert.py writes the `song <style> <path>` lines; Revamped picks per dream load (DreamSystem.cs):')
    L.append('#   songNumber = day == 1 ? 2 : random(0..4); song = list[style][songNumber % len(list)]  (UseOriginalSoundtrack, default on)')
    L.append('musicrule original 5 2')
    if c.mod:
        L.append('# Additional Dreams have SongLibrary = null; with UseOriginalSoundtrack the OriginalSongLibrary is used with')
        L.append('# DreamNumber = (index in the mod journal) % 14, i.e. ADDnn plays the song lists of original dream nn:')
        for n in range(14): L.append('modsongs ADD%02d %d' % (n, n))
    rl = os.path.join(c.repo, 'LSDR', 'Assets', 'UnityEditor', 'Resources', 'Sound', 'Music', 'RevampedSongList.asset')
    if os.path.exists(rl):
        songs = [p for p in c.refs_after(read(rl), 'Songs') if p]
        n = sum(1 for p in songs if c.ref(field(read(p), 'Clip')))
        L.append('# RevampedSongLibrary (only when the UseOriginalSoundtrack option is OFF): %d community .ogg songs, random pick,' % n)
        L.append('# 1 in 8 loads play no music (revampedSongLibraryScript.lua). Not baked (see AUDIO_FORMAT.md).')

# ---------------------------------------------------------------- DreamAudio entities in the original dream prefabs
def prefab_audio(c, path):
    t = read(path)
    docs = re.split(r'^--- !u!(\d+) &(-?\d+)', t, flags=re.M)
    byid = {}
    for i in range(1, len(docs), 3): byid[docs[i + 1]] = (int(docs[i]), docs[i + 2])
    go = {}; trs = {}; go_tr = {}
    for id_, (cls, body) in byid.items():
        if cls == 1:
            nm = re.search(r'm_Name: (.*)', body); act = re.search(r'm_IsActive: (\d)', body)
            go[id_] = dict(name=nm.group(1).strip() if nm else '', active=int(act.group(1)) if act else 1,
                           comps=re.findall(r'component: \{fileID: (-?\d+)\}', body))
        elif cls == 4:
            g = re.search(r'm_GameObject: \{fileID: (-?\d+)\}', body)
            if not g or 'm_LocalPosition' not in body: continue
            def vec(n_):
                m = re.search(n_ + r': \{x: ([-\d.e]+), y: ([-\d.e]+), z: ([-\d.e]+)(?:, w: ([-\d.e]+))?', body); return tuple(float(v) for v in m.groups() if v is not None)
            fa = re.search(r'm_Father: \{fileID: (-?\d+)\}', body)
            trs[id_] = dict(pos=vec('m_LocalPosition'), rot=vec('m_LocalRotation'), scale=vec('m_LocalScale'), father=fa.group(1) if fa else '0')
            go_tr[g.group(1)] = id_
    def world(tid):
        t_ = trs.get(tid)
        if not t_: return (0, 0, 0), (0, 0, 0, 1), (1, 1, 1)
        if t_['father'] != '0': pp, pr, ps = world(t_['father'])
        else: pp, pr, ps = (0, 0, 0), (0, 0, 0, 1), (1, 1, 1)
        lp = qrot(pr, tuple(a * b for a, b in zip(t_['pos'], ps)))
        return (pp[0] + lp[0], pp[1] + lp[1], pp[2] + lp[2]), qmul(pr, t_['rot']), tuple(a * b for a, b in zip(t_['scale'], ps))
    tr_go = {v: k for k, v in go_tr.items()}
    def active_in_hierarchy(gid):
        tid = go_tr.get(gid)
        while tid and tid != '0':
            g_ = tr_go.get(tid)
            if g_ and not go[g_]['active']: return 0
            tid = trs[tid]['father'] if tid in trs else '0'
        return 1
    def ent_sel(gid):
        """(kind, body) the way convert.py picks the entity component of a GameObject (last match wins), or (None, None)"""
        comps = [byid[cid] for cid in go[gid]['comps'] if cid in byid]
        kind = None; body_ = None
        for cls, b in comps:
            if cls == 114:
                for guid_, k in TRIG.items():
                    if guid_ in b.split('m_EditorClassIdentifier')[0]: kind = k; body_ = b
        if not kind:
            for cls, b in comps:
                if cls == 114 and re.search(r'^  ID: ', b, re.M): kind = 'T'; body_ = b
        return kind, body_
    def is_entity(gid): return ent_sel(gid)[0] is not None
    def ent_body(gid): return ent_sel(gid)[1]
    recs = []; n_src = 0; plain_src = []; links = []
    for gid, g in go.items():
        comps = [(cid,) + byid[cid] for cid in g['comps'] if cid in byid]
        for _, cls, b in comps:      # TriggerLink.PlayLinkSound (convert.py's K lines do not carry it)
            if cls == 114 and TRIG_LINK in b.split('m_EditorClassIdentifier')[0] and go_tr.get(gid):
                lp, _, _ = world(go_tr[gid])
                links.append(((field(b, 'ID', '') or g['name']).replace(' ', chr(31)) or '_', lp, field(b, 'PlayLinkSound', '0')))
        das = [b for _, cls, b in comps if cls == 114 and DREAMAUDIO_GUID in b.split('m_EditorClassIdentifier')[0]]
        srcs = [b for _, cls, b in comps if cls == 82]
        n_src += len(srcs)
        if srcs and not das: plain_src.append(g['name'])
        if not das: continue
        # the entity name convert.py gives this GameObject (same component scan, last match wins)
        kind, body_ = ent_sel(gid)
        tid = go_tr.get(gid)
        if not tid: c.notes.append('%s: DreamAudio on %s has no Transform' % (os.path.basename(path), g['name'])); continue
        idv = field(body_, 'ID', '') if body_ else ''
        nm = ((idv or g['name']).replace(' ', chr(31))) or '_'
        p_, q_, s_ = world(tid)
        for d in das:
            daid = field(d, 'ID', '') or g['name']
            clip_src = c.ref(field(d, 'Clip', ''))
            clip = c.clip(clip_src)
            a = 'audio %s %s %s %s %s %s %s %s %s %s %s %s %s' % (clip, field(d, 'Spatial', '1'), field(d, 'FullVolumeRangeUnits', '3'),
                 field(d, 'RangeUnits', '10'), field(d, 'Pitch', '1'), field(d, 'Volume', '1'), field(d, 'DelayBetweenPlaysSeconds', '1'),
                 field(d, 'PlayClipForSeconds', '0'), field(d, 'DelayBeforePlayingSeconds', '0'), field(d, 'WaitUntilAudibleBeforePlaying', '1'),
                 field(d, 'Loop', '0'), field(d, 'OneShot', '0'), field(d, 'ControlledWithScript', '0'))
            asrc = None
            if srcs:
                s = srcs[0]
                pan = re.search(r'panLevelCustomCurve:.*?value: ([-\d.e]+)', s, re.S)
                asrc = 'asrc %s %s %s %s %s %s %s %s %s %s %s %s' % (field(s, 'rolloffMode', '0'), field(s, 'MinDistance', '1'), field(s, 'MaxDistance', '500'),
                       pan.group(1) if pan else '0', field(s, 'm_Volume', '1'), field(s, 'm_Pitch', '1'), field(s, 'Loop', '0'),
                       field(s, 'm_PlayOnAwake', '1'), field(s, 'DopplerLevel', '1'), field(s, 'Priority', '128'), field(s, 'Mute', '0'),
                       field(s, 'm_Enabled', '1'))
                sclip = c.ref(field(s, 'm_audioClip', ''))
                if sclip != clip_src: c.notes.append('%s %s: AudioSource clip differs from DreamAudio.Clip' % (os.path.basename(path), daid))
            else:
                c.notes.append('%s %s: DreamAudio without AudioSource' % (os.path.basename(path), daid))
            # nearest ancestor GameObject that is an entity itself (the sound moves with it, e.g. the pterodactyl's audio)
            par = '-'; off = (0.0, 0.0, 0.0)
            ptid = trs[tid]['father'] if tid in trs else '0'
            while ptid and ptid != '0':
                pg = tr_go.get(ptid)
                if pg and is_entity(pg):
                    pb = ent_body(pg); par = ((field(pb, 'ID', '') or go[pg]['name']).replace(' ', chr(31))) or '_'
                    pp_, pq_, ps_ = world(ptid)
                    d_ = qrot((-pq_[0], -pq_[1], -pq_[2], pq_[3]), (p_[0] - pp_[0], p_[1] - pp_[1], p_[2] - pp_[2]))
                    off = tuple(d_[k] / (ps_[k] or 1) for k in range(3)); break
                ptid = trs[ptid]['father'] if ptid in trs else '0'
            recs.append(dict(name=nm, kind=kind, id=daid.replace(' ', chr(31)), pos=p_, active=g['active'], live=active_in_hierarchy(gid),
                             audio=a, asrc=asrc, clip=clip, parent=par, off=off))
    return recs, n_src, plain_src, links

def dream_audio(c):
    dl = c.refs_after(read(os.path.join(c.orig, 'OriginalDreams.asset')), 'Dreams')
    tot = 0; summary = []
    for idx, dpath in enumerate(dl):
        folder = os.path.basename(os.path.dirname(dpath))
        prefab = os.path.join(c.base, field(read(dpath), 'DreamPrefabPath') + '.prefab')
        recs, n_src, plain, links = prefab_audio(c, prefab)
        c.lines.append('dream %d %s %d' % (idx, folder, len(recs)))
        for nm, lp, pls in links: c.lines.append('klink %s %.4f %.4f %.4f %s' % (nm, *lp, pls))
        for r in recs:
            c.lines.append('aent %s %s %.4f %.4f %.4f %d %d %s %s %.4f %.4f %.4f' % (r['name'], r['kind'], *r['pos'], r['active'], r['live'],
                           r['id'], r['parent'], *r['off']))
            c.lines.append(r['audio'])
            if r['asrc']: c.lines.append(r['asrc'])
        tot += len(recs)
        nscr = sum(1 for r in recs if r['audio'].split()[13] == '1')
        nloop = sum(1 for r in recs if r['audio'].split()[11] == '1')
        summary.append('%s: %d DreamAudio (%d scripted, %d loop), %d AudioSource, clips %s' % (folder, len(recs), nscr, nloop, n_src,
                       ' '.join(sorted(set(r['clip'].split('/')[-1][:-4] for r in recs)))))
        if plain: c.notes.append('%s: AudioSource without DreamAudio on %s' % (folder, ', '.join(plain)))
        dup = {}
        for r in recs: dup[r['name']] = dup.get(r['name'], 0) + 1
        d2 = [k for k, v in dup.items() if v > 1]
        if d2: c.notes.append('%s: entity names used by more than one DreamAudio (matched by position): %s' % (folder, ' '.join(sorted(d2))[:300]))
        for r in recs:
            if r['name'] != r['id']: c.notes.append('%s: DreamAudio %s sits on entity %s (%s)' % (folder, r['id'], r['name'], r['kind']))
    return tot, summary

def mod_links(c):
    """TriggerLink.PlayLinkSound of the Additional Dreams (addl.py's K lines do not carry it); keyed by trigger ID"""
    if not c.mod: return
    at = read(os.path.join(c.mod, 'Assets', 'AdditionalDreams.asset'))
    m = re.search(r'^\s*Dreams:\n((?:\s+- \{.*\}\n?)+)', at, re.M)
    c.lines.append('# Additional Dreams TriggerLinks (dreams_addl.txt `ent K` names): modlink <ADDnn> <id> <PlayLinkSound>')
    for n, l in enumerate(m.group(1).strip().split('\n') if m else []):
        dp = c.ref(l, mod=True)
        pp = field(read(dp), 'DreamPrefabPath') if dp else None
        pf = os.path.join(c.mod, pp) if pp else None
        if not pf or not os.path.exists(pf): c.notes.append('mod dream %d: prefab not found' % n); continue
        for b in re.split(r'^--- !u!', read(pf), flags=re.M):
            if b.startswith('114 ') and TRIG_LINK in b.split('m_EditorClassIdentifier')[0]:
                c.lines.append('modlink ADD%02d %s %s' % (n, (field(b, 'ID', '') or '_').replace(' ', chr(31)), field(b, 'PlayLinkSound', '0')))

# ---------------------------------------------------------------- conversion
def convert_all(c, all_samples=False):
    ff = shutil.which('ffmpeg')
    if not ff: sys.exit('ffmpeg not found on PATH')
    if all_samples:
        for f in sorted(glob.glob(os.path.join(c.orig, 'SND', 'LoadedSamples', '*', '*.wav')) + glob.glob(os.path.join(c.orig, 'ETC', 'LoadedSamples', '*', '*.wav'))):
            b = os.path.basename(os.path.dirname(f)).upper(); n = re.search(r'(\d+)', os.path.basename(f)).group(1)
            c.clips.setdefault('SFX/bank/%s_%s.wav' % (b, n), f)
    made = 0
    for name, src in sorted(c.clips.items()):
        dst = os.path.join(c.out, name.replace('/', os.sep)); os.makedirs(os.path.dirname(dst), exist_ok=True)
        if os.path.exists(dst) and os.path.getmtime(dst) >= os.path.getmtime(src) and wav_info(dst)[0] == c.rate: continue
        subprocess.run([ff, '-v', 'error', '-y', '-i', src, '-ar', str(c.rate), '-ac', '1', '-acodec', 'pcm_s16le',
                        '-map_metadata', '-1', '-fflags', '+bitexact', '-flags:a', '+bitexact', dst], check=True)
        made += 1
    return made

def wav_info(p):
    w = wave.open(p); r = (w.getframerate(), w.getnchannels(), w.getsampwidth(), w.getnframes()); w.close(); return r

def verify(out):
    tot = 0; n = 0; bad = 0; secs = 0
    for p in sorted(glob.glob(os.path.join(out, '**', '*.wav'), recursive=True)):
        try:
            rate, ch, sw, fr = wav_info(p)
            if ch != 1 or sw != 2: bad += 1; print('not mono s16:', p)
            w = wave.open(p); data = w.readframes(fr); w.close()
            if len(data) != fr * 2: bad += 1; print('short data:', p)
            secs += fr / rate
        except Exception as e:
            bad += 1; print('bad wav', p, e)
        tot += os.path.getsize(p); n += 1
    print('verify: %d wav files, %.1f s, %d bytes (%.1f KB), %d bad' % (n, secs, tot, tot / 1024, bad))
    # every clip path named in audio.txt exists
    at = os.path.join(out, 'audio.txt'); miss = 0
    if os.path.exists(at):
        for l in open(at, encoding='utf8'):
            for tok in l.split():
                if tok.endswith('.wav') and not os.path.exists(os.path.join(out, tok)): miss += 1; print('missing', tok)
    print('verify: %d referenced clips missing' % miss)
    return bad + miss

# ---------------------------------------------------------------- merge into a data dir (convert.py --audio)
def merge(staged, data):
    """copy the baked files into data dir and add `audio`/`asrc` lines after the matching `ent` lines of dreams.txt"""
    for sub in ('SFX', 'ADDSND'):
        s = os.path.join(staged, sub)
        if os.path.isdir(s):
            for dp, _, fn in os.walk(s):
                rel = os.path.relpath(dp, staged)
                if 'bank' in rel.split(os.sep): continue
                os.makedirs(os.path.join(data, rel), exist_ok=True)
                for f in fn: shutil.copy2(os.path.join(dp, f), os.path.join(data, rel, f))
    shutil.copy2(os.path.join(staged, 'audio.txt'), os.path.join(data, 'audio.txt'))
    want = {}; folder = None; cur = None; modk = {}
    for l in open(os.path.join(staged, 'audio.txt'), encoding='utf8'):
        t = l.split()
        if not t: continue
        if t[0] == 'dream': folder = t[2]
        elif t[0] == 'aent':
            cur = (folder, t[1], t[3], t[4], t[5]); want.setdefault(cur, [])
            if t[9] != '-': want[cur].append('aparent %s %s %s %s' % tuple(t[9:13]))
        elif t[0] in ('audio', 'asrc') and cur: want[cur].append(l.rstrip('\n'))
        elif t[0] == 'klink': want.setdefault((folder, t[1], t[2], t[3], t[4]), []).append('linksound ' + t[5])
        elif t[0] == 'modlink': modk[(t[1], t[2])] = ['linksound ' + t[3]]
    ours = (['audio'], ['asrc'], ['linksound'], ['aparent'])
    def patch(path, table, key_of, once=True):
        src = open(path, encoding='utf8', errors='ignore').read().split('\n')
        out = []; folder = None; hit = set(); i = 0
        while i < len(src):
            l = src[i]; out.append(l); t = l.split()
            if t and t[0] == 'dream' and len(t) > 2: folder = t[2]
            if t and t[0] == 'ent' and len(t) > 6:
                k = key_of(folder, t)
                if k in table and (k not in hit or not once):
                    hit.add(k)
                    while i + 1 < len(src) and src[i + 1].split()[:1] in ours: i += 1   # drop a previous merge
                    out.extend(table[k])
            i += 1
        open(path, 'w', encoding='utf8', newline='\n').write('\n'.join(out))
        missed = [k for k in table if k not in hit]
        print('merge: %d of %d records attached to ent lines in %s' % (len(hit), len(table), path))
        for k in missed: print('merge: no ent line for', k)
        return missed
    missed = patch(os.path.join(data, 'dreams.txt'), want, lambda f, t: (f, t[2], t[4], t[5], t[6]))
    da = os.path.join(data, 'dreams_addl.txt')
    if modk and os.path.exists(da):
        m2 = patch(da, modk, lambda f, t: (f, t[2]) if t[1] == 'K' else None, once=False)   # every same-named K trigger of a dream
        missed += m2
    return len(missed)

# ---------------------------------------------------------------- main
def run(repo, out, mod=None, rate=22050, all_samples=False):
    if mod is None:
        mod = os.path.normpath(os.path.join(HERE, '..', 'ref', 'LSDR-Additional-Dreams'))
    if not os.path.isdir(mod): mod = None
    c = Ctx(repo, out, rate, mod)
    os.makedirs(out, exist_ok=True)
    c.lines.append('audiofmt 1 %d' % rate)
    footstep_tables(c)
    global_sounds(c)
    music_notes(c)
    tot, summary = dream_audio(c)
    mod_links(c)
    c.lines.append('end')
    clipdecl = []
    made = convert_all(c, all_samples)
    for name in sorted(n for n in c.clips if not n.startswith('SFX/bank/')):
        r, ch, sw, fr = wav_info(os.path.join(out, name.replace('/', os.sep)))
        clipdecl.append('clip %s %d %d %.3f' % (name, fr, r, fr / r))
    # clip table first, so a loader can preload everything in one pass
    lines = c.lines[:1] + ['# every clip this file names: clip <path> <frames> <rate> <seconds>'] + clipdecl + c.lines[1:]
    open(os.path.join(out, 'audio.txt'), 'w', encoding='utf8', newline='\n').write('\n'.join(lines) + '\n')
    for s in summary: print(s)
    for n in c.notes: print('note:', n)
    print('audio.txt: %d lines, %d DreamAudio records, %d clips (%d converted now)' % (len(lines), tot, len(clipdecl), made))
    return verify(out)

if __name__ == '__main__':
    a = sys.argv[1:]
    if len(a) < 2: sys.exit(__doc__)
    def opt(k, d=None): return a[a.index(k) + 1] if k in a and a.index(k) + 1 < len(a) else d
    err = run(a[0], a[1], opt('--mod'), int(opt('--rate', '22050')), '--all-samples' in a)
    if '--merge' in a: err += merge(a[1], opt('--merge'))
    sys.exit(1 if err else 0)
