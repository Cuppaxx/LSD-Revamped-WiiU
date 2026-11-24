"""Special days, flashback/ending/intro movies and credits for the Wii U port (format: tools/SPECIAL_FORMAT.md).

usage: python -I tools/special_assets.py <repo> <outdir> [--q N] [--no-movies] [--force] [--only NAME[,NAME..]] [--jobs N]
       python -I tools/special_assets.py --bundle <outdir> <app data dir>   (copies specials.txt, SPDAY/texts_en.txt, CREDITS/)

  <repo>    the LSDRevamped clone (folder that contains LSDR/)
  <outdir>  e.g. tools/out_special (staging; copy into data/ when the app supports it)
  --q N     MJPEG quantiser for ffmpeg -q:v (2 best .. 31 worst), default 8
  --no-movies   skip the ffmpeg transcodes (tables, texts and images only)
  --force   re-encode movies that already exist
  --only    only these movies (names like SPDAY01A,OPENINGA)
  --jobs N  parallel ffmpeg processes (default 4)

Writes:
  specials.txt                    tables: special days, ending, chart face, intro, MovieClip entities, constants
  SPDAY/<SPDAYnnX>.TIM            the original text-day images (copied from IMG1/IMG2, 320x240 16-bit TIM)
  SPDAY/TXT_<id>.TIM              the text-day text rendered like Revamped's text_dream scene (4-bit TIM, 320x240)
  SPDAY/texts.txt                 the UTF-8 text of every text day (reference)
  MOVIE/<NAME>.mjv + <NAME>.wav   every ETC/FILM movie: MJPEG frames in a trivial container + 22050 Hz mono s16 WAV
  CREDITS/credits.txt + *.TIM     the credits roll (StreamingAssets/credits.json) and its section images
  preview/                        PNG previews (decoded back from the outputs) for checking, not for shipping
"""
import os, re, sys, glob, json, struct, shutil, subprocess, tempfile
from concurrent.futures import ThreadPoolExecutor
from PIL import Image, ImageDraw, ImageFont

MAGIC_MJV = b'LMJV'

def read(p): return open(p, encoding='utf8', errors='ignore').read()

def guid_map(roots):
    g = {}
    for root in roots:
        for m in glob.glob(os.path.join(root, '**', '*.meta'), recursive=True):
            t = open(m, encoding='utf8', errors='ignore').read(400)
            k = re.search(r'guid: (\w+)', t)
            if k: g[k.group(1)] = m[:-5]
    return g

def guid_of(s):
    m = re.search(r'guid: (\w+)', s or ''); return m.group(1) if m else None

def field(t, name):
    m = re.search(r'^\s*' + name + r':[ \t]*(.*)$', t, re.M); return m.group(1).strip() if m else None

def ref_list(t, name):
    """the `- {fileID.., guid..}` items of a YAML list field"""
    m = re.search(r'^(\s*)' + name + r':\s*\n((?:\1- .*\n)*)', t, re.M)
    return [guid_of(l) for l in m.group(2).splitlines()] if m else []

def movie_name(path):  # .../FILM/SPDAY01A.STR[0].webm -> SPDAY01A
    return os.path.basename(path).split('.')[0]

def unity_string(raw):
    """a YAML scalar as Unity writes it: "..." with \\uXXXX / \\r\\n escapes, or plain"""
    if raw is None: return ''
    raw = raw.strip()
    if raw.startswith('"'):
        body = raw[1:raw.rindex('"')]
        return json.loads('"' + body.replace('\\\n', '') + '"')
    return raw

def text_field(t, name):
    """multi-line quoted scalar (Unity folds long strings over several indented lines)"""
    m = re.search(r'^  ' + name + r': (".*?(?<!\\)"|[^\n]*)', t, re.M | re.S)
    if not m: return ''
    s = m.group(1)
    if s.startswith('"'):
        # YAML double-quoted folding: newline + indentation -> one space; escaped line breaks join directly
        s = re.sub(r'\\\n\s*', '', s)
        s = re.sub(r'\n\s*', ' ', s)
    return unity_string(s)

# ------------------------------------------------------------------ TIM writing
def tim4(img_l, levels):
    """4-bit CLUT TIM from an 'L' image whose values are indices 0..15; levels = 16 grey values (0..255)"""
    w, h = img_l.size
    assert w % 4 == 0
    clut = b''.join(struct.pack('<H', ((v * 31 // 255) | ((v * 31 // 255) << 5) | ((v * 31 // 255) << 10))) for v in levels)
    clut_block = struct.pack('<IHHHH', 12 + 32, 0, 480, 16, 1) + clut
    px = img_l.tobytes(); data = bytearray()
    for i in range(0, len(px), 2): data.append((px[i] & 15) | ((px[i + 1] & 15) << 4))
    img_block = struct.pack('<IHHHH', 12 + len(data), 640, 0, w // 4, h) + bytes(data)
    return struct.pack('<II', 0x10, 0x08 | 0) + clut_block + img_block

def tim16(img_rgb):
    w, h = img_rgb.size
    data = bytearray()
    raw = img_rgb.tobytes()
    for i in range(0, len(raw), 3):
        r, g, b = raw[i], raw[i + 1], raw[i + 2]
        v = (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10)
        if v == 0 and (r or g or b): v = 0x8000   # keep near-black opaque on real PS1 semantics (STP)
        data += struct.pack('<H', v)
    return struct.pack('<II', 0x10, 2) + struct.pack('<IHHHH', 12 + len(data), 640, 0, w, h) + bytes(data)

def tim_to_image(b):
    """decode a TIM (4/8/16-bit) to RGB, like app/src/lsd.c lsd_decode_tim (for previews/verification)"""
    rd32 = lambda o: struct.unpack_from('<I', b, o)[0]; rd16 = lambda o: struct.unpack_from('<H', b, o)[0]
    flags = rd32(4); mode = flags & 7; p = 8; clut = None
    if flags & 8: clen = rd32(p); clut = p + 12; p += clen
    w, h = rd16(p + 8), rd16(p + 10); data = p + 12
    imgw = w * 4 if mode == 0 else w * 2 if mode == 1 else w
    im = Image.new('RGB', (imgw, h)); out = []
    for r in range(h):
        row = data + r * w * 2
        for x in range(imgw):
            if mode == 2: c = rd16(row + x * 2)
            elif mode == 1: c = rd16(clut + b[row + x] * 2)
            else:
                by = b[row + (x >> 1)]; c = rd16(clut + ((by >> 4) if x & 1 else (by & 15)) * 2)
            out.append(((c & 31) * 255 // 31, ((c >> 5) & 31) * 255 // 31, ((c >> 10) & 31) * 255 // 31))
    im.putdata(out); return im

# ------------------------------------------------------------------ text-day page (Revamped text_dream scene)
# a Japanese TrueType font for the (optional) rendered text pages: LSDR_JP_FONT, else common Windows / Linux / macOS fonts
_WINFONTS = os.path.join(os.environ.get('WINDIR', r'C:\Windows'), 'Fonts')
FONT_CANDIDATES = ([os.environ['LSDR_JP_FONT']] if os.environ.get('LSDR_JP_FONT') else []) + [
    os.path.join(_WINFONTS, 'msgothic.ttc'), os.path.join(_WINFONTS, 'YuGothM.ttc'), os.path.join(_WINFONTS, 'meiryo.ttc'),
    '/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc', '/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc',
    '/usr/share/fonts/truetype/fonts-japanese-gothic.ttf', '/System/Library/Fonts/Hiragino Sans GB.ttc']

def load_font(px):
    for f in FONT_CANDIDATES:
        if os.path.exists(f):
            try: return ImageFont.truetype(f, px), f
            except OSError: pass
    return None, None

def wrap(draw, font, line, width):
    out, cur = [], ''
    for ch in line:
        if draw.textlength(cur + ch, font=font) > width and cur: out.append(cur); cur = ch
        else: cur += ch
    out.append(cur); return out

def render_page(text, W=320, H=240):
    """white text, centred both ways, in the Text rect of text_dream.unity (Canvas 1280 wide reference, rect inset
    100 px left/right and 60 px top/bottom of the reference = 25 / 20 px at 320x240); best fit shrinks the font"""
    rx, ry = 25, 20; rw, rh = W - 2 * rx, H - 2 * ry
    for px in (12, 11, 10):
        font, fname = load_font(px)
        if font is None: return None, None
        im = Image.new('L', (W, H), 0); d = ImageDraw.Draw(im); d.fontmode = '1'
        lines = []
        for l in text.replace('\r\n', '\n').replace('\r', '\n').split('\n'): lines += wrap(d, font, l, rw)
        lh = px + 2
        if len(lines) * lh <= rh or px == 10: break
    y = ry + (rh - len(lines) * lh) // 2
    for l in lines:
        x = rx + (rw - int(d.textlength(l, font=font))) // 2
        d.text((x, y), l, font=font, fill=1)
        y += lh
    return im, (os.path.basename(fname), px, len(lines))

# ------------------------------------------------------------------ movies
def ffmpeg_exe():
    ff = shutil.which('ffmpeg')
    if not ff: sys.exit('ffmpeg not found on PATH')
    return ff

def write_mjv(frames_dir, out_path, fps, audio_rate, audio_samples, w, h):
    files = sorted(glob.glob(os.path.join(frames_dir, '*.jpg')))
    blobs = [open(f, 'rb').read() for f in files]
    n = len(blobs); hdr = 32; table = 4 * (n + 1)
    offs = [hdr + table]
    for b_ in blobs: offs.append(offs[-1] + len(b_))
    with open(out_path, 'wb') as f:
        f.write(MAGIC_MJV + struct.pack('>HHHHHHIIII', 1, w, h, fps, 1, 1, n, max(len(b_) for b_ in blobs) if blobs else 0,
                                        audio_rate, audio_samples))
        f.write(struct.pack('>%dI' % (n + 1), *offs))
        for b_ in blobs: f.write(b_)
    return n, offs[-1]

def wav_samples(path):
    b = open(path, 'rb').read(); i = 12
    while i + 8 <= len(b):
        cid, sz = b[i:i + 4], struct.unpack_from('<I', b, i + 4)[0]
        if cid == b'data': return min(sz, len(b) - i - 8) // 2
        i += 8 + sz + (sz & 1)
    return 0

def transcode(ff, src, out_dir, name, q, force):
    mjv = os.path.join(out_dir, name + '.mjv'); wav = os.path.join(out_dir, name + '.wav')
    if not force and os.path.exists(mjv) and os.path.exists(wav): return name, 'kept'
    subprocess.run([ff, '-v', 'error', '-y', '-i', src, '-vn', '-ar', '22050', '-ac', '1', '-acodec', 'pcm_s16le', wav], check=True)
    with tempfile.TemporaryDirectory() as td:
        subprocess.run([ff, '-v', 'error', '-y', '-i', src, '-an', '-vf', 'fps=15,scale=320:240:flags=bicubic',
                        '-c:v', 'mjpeg', '-q:v', str(q), '-pix_fmt', 'yuvj420p', '-huffman', 'optimal',
                        os.path.join(td, '%05d.jpg')], check=True)
        write_mjv(td, mjv, 15, 22050, wav_samples(wav), 320, 240)
    return name, 'encoded'

def read_mjv(path):
    b = open(path, 'rb').read()
    assert b[:4] == MAGIC_MJV, path
    ver, w, h, fn, fd, flags, n, mx, ar, asmp = struct.unpack_from('>HHHHHHIIII', b, 4)
    offs = struct.unpack_from('>%dI' % (n + 1), b, 32)
    return dict(ver=ver, w=w, h=h, fps=fn / fd, n=n, max=mx, rate=ar, samples=asmp, offs=offs, data=b)

# ------------------------------------------------------------------ main
BUNDLE_FILES = ['specials.txt', 'SPDAY/texts_en.txt', 'CREDITS/*']   # what the wuhb carries (the movies stay on the SD card)

def bundle(stage, data):
    """copy the small special data the app needs from the staging dir into the app's data dir (no movies, no TIM pages:
    the English texts cover every text day; the original SPDAY TIMs are only a fallback, put them on the SD card if wanted)"""
    n = 0
    for pat in BUNDLE_FILES:
        for s in glob.glob(os.path.join(stage, pat)):
            rel = os.path.relpath(s, stage); d = os.path.join(data, rel)
            os.makedirs(os.path.dirname(d), exist_ok=True); shutil.copy2(s, d); n += 1
    if not os.path.exists(os.path.join(stage, 'SPDAY', 'texts_en.txt')): print('note: SPDAY/texts_en.txt missing from', stage)
    print('special data bundled: %d files -> %s' % (n, data))

def main():
    a = [x for x in sys.argv[1:]]
    if a and a[0] == '--bundle':
        if len(a) < 3: sys.exit('usage: special_assets.py --bundle <staging dir> <app data dir>')
        return bundle(a[1], a[2])
    if len(a) < 2: sys.exit(__doc__)
    repo, out = a[0], a[1]
    def opt(k, d=None):
        return a[a.index(k) + 1] if k in a and a.index(k) + 1 < len(a) else d
    q = int(opt('--q', 8)); jobs = int(opt('--jobs', 4)); only = opt('--only'); force = '--force' in a
    only = set(only.split(',')) if only else None
    assets = os.path.join(repo, 'LSDR', 'Assets'); orig = os.path.join(assets, 'Resources', 'Original')
    sdk = os.path.join(repo, 'LSDR', 'Packages', 'com.figglewatts.lsdr.sdk', 'Runtime')
    os.makedirs(out, exist_ok=True)
    G = guid_map([orig])
    sg = {k: guid_of(read(os.path.join(sdk, 'Data', k + '.cs.meta'))) for k in ('RandomSpecialDay', 'TextSpecialDay', 'VideoSpecialDay')}
    kind_of = {sg['RandomSpecialDay']: 'random', sg['TextSpecialDay']: 'text', sg['VideoSpecialDay']: 'video'}
    report = []

    journal = read(os.path.join(orig, 'OriginalDreams.asset'))
    keys_hex = re.search(r'_keys: ([0-9a-f]*)', journal).group(1)
    days = [struct.unpack_from('<i', bytes.fromhex(keys_hex), i)[0] for i in range(0, len(keys_hex) // 2, 4)]
    vals = re.search(r'SpecialDays:\s*\n\s*_keys: [0-9a-f]*\n\s*_values:\s*\n((?:\s*- .*\n)*)', journal).group(1)
    vals = [guid_of(l) for l in vals.splitlines() if l.strip()]
    assert len(days) == len(vals), (len(days), len(vals))

    movies_used = {}           # name -> source path
    def movie(guid):
        p = G.get(guid)
        if not p: report.append('unresolved video guid ' + str(guid)); return '-'
        movies_used[movie_name(p)] = p; return movie_name(p)

    texts = {}; pages = {}; tims = {}
    os.makedirs(os.path.join(out, 'SPDAY'), exist_ok=True); os.makedirs(os.path.join(out, 'preview'), exist_ok=True)

    def special(guid, depth=0):
        p = G[guid]; t = read(p); name = os.path.splitext(os.path.basename(p))[0]
        kind = kind_of.get(guid_of(field(t, 'm_Script')), '?')
        vx = re.search(r'_vector: \{x: (-?\d+), y: (-?\d+)\}', t)
        d = dict(name=name, kind=kind, always=int(field(t, 'Always') or 0), dyn=int(vx.group(1)), up=int(vx.group(2)))
        if kind == 'random': d['choices'] = [special(g, depth + 1) for g in ref_list(t, 'SpecialDayChoices')]
        elif kind == 'video': d['movie'] = movie(guid_of(field(t, 'VideoClip')))
        elif kind == 'text':
            ja, en = text_field(t, 'TextJa'), text_field(t, 'TextEn')
            d['text'] = ja if ja.strip() else en; d['lang'] = 'ja' if ja.strip() else 'en'
            tp = G.get(guid_of(field(t, 'OriginalTexture')))
            if tp and os.path.exists(tp):
                tn = os.path.basename(tp); shutil.copy2(tp, os.path.join(out, 'SPDAY', tn)); d['tim'] = 'SPDAY/' + tn
                tims[name] = tp
            else: d['tim'] = '-'; report.append('text day %s: original TIM missing' % name)
            if name not in pages:
                im, info = render_page(d['text'])
                if im is None: d['page'] = '-'; report.append('no Japanese font for ' + name)
                else:
                    levels = [0, 255] + [0] * 14
                    open(os.path.join(out, 'SPDAY', 'TXT_%s.TIM' % name), 'wb').write(tim4(im, levels))
                    pages[name] = info
            d['page'] = 'SPDAY/TXT_%s.TIM' % name if name in pages else '-'
            texts[name] = d['text']
        return d

    L = ['lsdr-specials 1',
         '# generated by tools/special_assets.py; format and semantics: tools/SPECIAL_FORMAT.md']
    L.append('# day <day> <asset> <kind> <always> <dyn> <upper> <nchoices>   then nchoices x choice lines (random) or one choice line')
    L.append('# choice <kind> <asset> <always> <dyn> <upper> <movie|-> <tim|-> <page|->')
    seen_sets = {}
    for day, g in sorted(zip(days, vals)):
        d = seen_sets.get(g) or special(g); seen_sets[g] = d
        ch = d['choices'] if d['kind'] == 'random' else [d]
        L.append('day %d %s %s %d %d %d %d' % (day, d['name'], d['kind'], d['always'], d['dyn'], d['up'], len(ch)))
        for c in ch:
            L.append('choice %s %s %d %d %d %s %s %s' % (c['kind'], c['name'], c['always'], c['dyn'], c['up'],
                     c.get('movie', '-'), c.get('tim', '-'), c.get('page', '-')))
    L.append('# EndingVideo: plays after day 365 has ended (DreamSystem.endDream / playEnding)')
    L.append('ending ' + movie(guid_of(field(journal, 'EndingVideo'))))
    cf = [movie(x) for x in ref_list(journal, 'ChartFaceVideos')]
    L.append('# ChartFaceVideos: played back to back (each skippable) the first time the chart shows the face (UIGraph)')
    L.append('chartface %d %s' % (len(cf), ' '.join(cf)))
    intro_scene = read(os.path.join(assets, 'UnityEditor', 'Scenes', 'intro.unity'))
    iv = [movie(x) for x in ref_list(intro_scene, 'IntroVideos')]
    L.append('# IntroVideos: one of these at random on every boot, before the title (IntroVideoController)')
    L.append('intro %d %s' % (len(iv), ' '.join(iv)))
    # MovieClip entities (SDK Entities/MovieClip.cs): Lua calls GetEntity(id).VideoClip.Play(colour)
    mc_guid = guid_of(read(os.path.join(sdk, 'Entities', 'MovieClip.cs.meta')))
    L.append('# movieclip <dream folder> <entity ID> <movie>   (Lua: GetEntity(ID).VideoClip.Play(Unity.ColorRGB(r,g,b)))')
    for pf in sorted(glob.glob(os.path.join(orig, 'STG*', 'STG*.prefab'))):
        t = read(pf)
        for m in re.finditer(r'm_Script: \{fileID: 11500000, guid: ' + mc_guid + r'.*?\n((?:  .*\n)*)', t):
            blk = m.group(1); eid = field(blk, 'ID'); clip = movie(guid_of(field(blk, 'Clip')))
            L.append('movieclip %s %s %s' % (os.path.basename(os.path.dirname(pf)), eid, clip))
    # every other movie on disk (not referenced: EVENT*, or extra OPENINGs) still gets transcoded, listed here
    all_movies = {movie_name(p): p for p in glob.glob(os.path.join(orig, 'ETC', '*.webm')) + glob.glob(os.path.join(orig, 'FILM', '*.webm'))}
    unref = sorted(set(all_movies) - set(movies_used))
    L.append('# movies on disk that nothing references')
    L.append('unreferenced %d %s' % (len(unref), ' '.join(unref)))
    L.append('# constants (DreamSystem.cs, GraphContributor.cs, UIGraph.cs, Greyman.cs, GameSaveData.cs)')
    for k, v in (('MIN_SECONDS_IN_DREAM', 60), ('MAX_SECONDS_IN_DREAM', 360), ('MIN_SECONDS_IN_FLASHBACK', 8),
                 ('MAX_SECONDS_IN_FLASHBACK', 16), ('FADE_OUT_SECS_REGULAR', 3), ('FADE_OUT_SECS_FALL', 2),
                 ('FADE_OUT_SECS_FORCE', 1), ('FADE_OUT_SECS_TIMEOUT', 1.25), ('FADE_SECS_BETWEEN_VIDEOS', 0.5),
                 ('FADE_SECS_CUTSCENE', 1), ('FADE_IN_SECS_DEFAULT', 1), ('FADE_IN_SECS_DAY_START', 1.25),
                 ('FLASHBACK_MIN_ENTITY_CONTRIBUTIONS', 101), ('FLASHBACK_TINT_ADD', 0.4),
                 ('GREYMAN_DISABLES_FLASHBACK_DAYS', 10), ('GRAPH_CONTRIBUTOR_UPDATE_INTERVAL', 0.25),
                 ('CHART_FACE_POINTS', '-1,1 1,1 0,0 0,-3')):
        L.append('const %s %s' % (k, v))
    open(os.path.join(out, 'specials.txt'), 'w', encoding='utf8', newline='\n').write('\n'.join(L) + '\n')
    with open(os.path.join(out, 'SPDAY', 'texts.txt'), 'w', encoding='utf8', newline='\n') as f:
        for k in sorted(texts):
            f.write('text %s\n' % k)
            for l in texts[k].replace('\r\n', '\n').split('\n'): f.write('| ' + l + '\n')
            f.write('end\n')
    print('specials.txt: %d special days, %d text pages, %d original TIMs, %d movies referenced, %d unreferenced'
          % (len(days), len(pages), len(tims), len(movies_used), len(unref)))
    # previews: rendered page next to the original TIM, for a few text days
    for k in sorted(pages)[:48:6]:
        page = tim_to_image(open(os.path.join(out, 'SPDAY', 'TXT_%s.TIM' % k), 'rb').read())
        w = Image.new('RGB', (640, 240)); w.paste(page, (0, 0))
        if k in tims: w.paste(tim_to_image(open(tims[k], 'rb').read()).resize((320, 240)), (320, 0))
        w.save(os.path.join(out, 'preview', 'text_%s.png' % k))

    credits(assets, out, report)

    if '--no-movies' not in a:
        ff = ffmpeg_exe(); md = os.path.join(out, 'MOVIE'); os.makedirs(md, exist_ok=True)
        todo = sorted(all_movies.items())
        if only: todo = [x for x in todo if x[0] in only]
        with ThreadPoolExecutor(max_workers=jobs) as ex:
            for name, st in ex.map(lambda x: transcode(ff, x[1], md, x[0], q, force), todo): print('movie', name, st)
        verify_movies(md, out, [n for n, _ in todo], all_movies)
    for r in report: print('note:', r)

def verify_movies(md, out, names, srcs):
    """decode the outputs back: every frame must be a baseline JPEG PIL can open; one mid frame per movie into a contact
    sheet; sizes and durations into MOVIE/movies.txt"""
    import io
    rows = []; sheet = []
    tot_v = tot_a = 0
    for n in sorted(names):
        m = read_mjv(os.path.join(md, n + '.mjv')); b = m['data']; bad = 0
        for i in range(m['n']):
            fr = b[m['offs'][i]:m['offs'][i + 1]]
            if fr[:2] != b'\xff\xd8' or fr[-2:] != b'\xff\xd9': bad += 1
        mid = m['n'] // 2
        im = Image.open(io.BytesIO(b[m['offs'][mid]:m['offs'][mid + 1]])); im.load()
        sof0 = b'\xff\xc0' in b[m['offs'][mid]:m['offs'][mid + 1]]
        sheet.append((n, im.convert('RGB')))
        vs = os.path.getsize(os.path.join(md, n + '.mjv')); as_ = os.path.getsize(os.path.join(md, n + '.wav'))
        tot_v += vs; tot_a += as_
        rows.append('%-10s frames %5d  %6.1f s video  %6.1f s audio  mjv %8d B (%5.0f KB/s, max frame %6d B)  wav %8d B  %s%s'
                    % (n, m['n'], m['n'] / m['fps'], m['samples'] / m['rate'], vs, vs / 1024 / max(0.001, m['n'] / m['fps']),
                       m['max'], as_, 'baseline' if sof0 else 'NOT-BASELINE', '' if not bad else '  BAD FRAMES %d' % bad))
    rows.append('total mjv %.1f MB, wav %.1f MB' % (tot_v / 1048576, tot_a / 1048576))
    open(os.path.join(md, 'movies.txt'), 'w', newline='\n').write('\n'.join(rows) + '\n')
    for r in rows: print(r)
    cols = 6; W = 160; H = 120
    sh = Image.new('RGB', (cols * W, ((len(sheet) + cols - 1) // cols) * (H + 10)))
    d = ImageDraw.Draw(sh)
    for i, (n, im) in enumerate(sheet):
        x, y = (i % cols) * W, (i // cols) * (H + 10)
        sh.paste(im.resize((W, H)), (x, y + 10)); d.text((x + 2, y), n, fill=(255, 255, 0))
    sh.save(os.path.join(out, 'preview', 'movies_mid_frames.png'))

def credits(assets, out, report):
    src = os.path.join(assets, 'StreamingAssets', 'credits.json')
    if not os.path.exists(src): report.append('credits.json missing'); return
    secs = json.load(open(src, encoding='utf8'))
    cd = os.path.join(out, 'CREDITS'); os.makedirs(cd, exist_ok=True)
    tex = os.path.join(assets, 'UnityEditor', 'Resources')
    L = ['lsdr-credits 1', '# section <image TIM|-> <sort 0|1> <title> ; name <text> (rich text tags <i> <b> stripped, flags: i=italic b=bold)']
    for s in secs:
        img = s.get('SectionImagePath') or ''
        tim = '-'
        if img:
            p = os.path.join(tex, img + '.png')
            if os.path.exists(p):
                im = Image.open(p).convert('RGBA')
                # Section image LayoutElement minWidth 90 on an 800x600 reference canvas -> 36 px at 320x240;
                # baked at 72 px wide (2x) so the 480-line modes stay sharp, the app halves it at 240 lines
                w = 72; h = max(1, round(im.height * w / im.width)); w += (-w) % 4
                bg = Image.new('RGBA', im.size, (0, 0, 0, 255)); bg.alpha_composite(im)
                small = bg.convert('RGB').resize((w, h), Image.LANCZOS)
                name = re.sub(r'[^A-Za-z0-9]', '', os.path.basename(img)).upper()[:8] + '.TIM'
                open(os.path.join(cd, name), 'wb').write(tim16(small)); tim = 'CREDITS/' + name
            else: report.append('credits image missing: ' + img)
        sort = 1 if s.get('SortNames', True) else 0
        L.append('section %s %d %s' % (tim, sort, s['SectionTitle']))
        names = sorted(s['Names']) if sort else s['Names']
        for n in names:
            fl = ('i' if '<i>' in n else '') + ('b' if '<b>' in n else '') or '-'
            L.append('name %s %s' % (fl, re.sub(r'</?[ib]>', '', n)))
    open(os.path.join(cd, 'credits.txt'), 'w', encoding='utf8', newline='\n').write('\n'.join(L) + '\n')
    print('credits: %d sections' % len(secs))

if __name__ == '__main__':
    main()
