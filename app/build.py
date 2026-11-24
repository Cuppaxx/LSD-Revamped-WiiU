#!/usr/bin/env python3
"""Build LSD: Revamped for Wii U: app/src/*.c + Lua 5.2.3 -> app.elf -> app.rpx -> .wuhb (no make needed).

Usage (from anywhere; Windows, Linux and macOS):
  python app/build.py                      app-only build -> build/lsdr-app.wuhb (NO game data inside: distributable)
  python app/build.py --data PATH/data     personal build -> build/lsdr.wuhb with your converted data folder bundled
                                           (contains converted game data, like the release)
Options:
  --out DIR      output directory (default: <repo>/build)
  --name NAME    output file name without extension (default: lsdr-app / lsdr)
  --jobs N       parallel compiler processes (default: CPU count)
  --clean        delete the object files first

Environment:
  DEVKITPRO          devkitPro root (default C:\\devkitPro on Windows, /opt/devkitpro elsewhere); needs devkitPPC, wut,
                     wiiu-sdl2 and ppc-libjpeg-turbo (see docs/BUILDING.md)
  LUA_DIR            Lua 5.2.3 sources (default <repo>/third_party/lua)
  LSDR_EXTRA_CFLAGS  extra compiler flags (e.g. -DLSDR_START_FILE=\\"t.txt\\" for test builds)

The app reads its data from SD:/wiiu/apps/lsdr/data/ first and from the wuhb's content/data/ second, so the app-only
wuhb only carries lua/api.lua (the port's own Lua API layer) and works with a data folder on the SD card.
"""
import argparse, glob, os, shutil, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
EXE = '.exe' if os.name == 'nt' else ''


def die(msg):
    sys.exit('build.py: ' + msg)


def main():
    ap = argparse.ArgumentParser(description='Build LSD: Revamped for Wii U (.wuhb)')
    ap.add_argument('--data', help='a converted data folder (named "data") to bundle into the wuhb')
    ap.add_argument('--out', default=os.path.join(ROOT, 'build'))
    ap.add_argument('--name')
    ap.add_argument('--jobs', type=int, default=os.cpu_count() or 4)
    ap.add_argument('--clean', action='store_true')
    a = ap.parse_args()

    dkp = os.environ.get('DEVKITPRO') or (r'C:\devkitPro' if os.name == 'nt' else '/opt/devkitpro')
    if os.name == 'nt' and dkp.startswith('/opt/'):        # msys-style value set by the devkitPro installer
        dkp = r'C:\devkitPro'
    ppc_bin = os.path.join(dkp, 'devkitPPC', 'bin'); tools_bin = os.path.join(dkp, 'tools', 'bin')
    gcc = os.path.join(ppc_bin, 'powerpc-eabi-gcc' + EXE)
    elf2rpl = os.path.join(tools_bin, 'elf2rpl' + EXE); wuhbtool = os.path.join(tools_bin, 'wuhbtool' + EXE)
    for t in (gcc, elf2rpl, wuhbtool):
        if not os.path.exists(t): die('not found: %s (set DEVKITPRO; see docs/BUILDING.md)' % t)
    lua = os.path.abspath(os.environ.get('LUA_DIR') or os.path.join(ROOT, 'third_party', 'lua'))
    if not os.path.exists(os.path.join(lua, 'lua.h')): die('Lua 5.2.3 sources not found in %s (set LUA_DIR)' % lua)

    env = dict(os.environ, DEVKITPRO=dkp, DEVKITPPC=os.path.join(dkp, 'devkitPPC'),
               PATH=os.pathsep.join([ppc_bin, tools_bin, os.environ.get('PATH', '')]))

    def run(cmd):
        r = subprocess.run(cmd, env=env, capture_output=True, text=True)
        out = (r.stdout + r.stderr).strip()
        if r.returncode: die('FAILED: %s\n%s' % (' '.join(cmd[:4]), out))
        return out

    out = os.path.abspath(a.out); obj_dir = os.path.join(out, 'obj')
    if a.clean and os.path.isdir(obj_dir): shutil.rmtree(obj_dir)
    os.makedirs(obj_dir, exist_ok=True)
    pl = lambda *p: os.path.join(dkp, 'portlibs', *p)
    inc = ['-I' + lua, '-I' + os.path.join(dkp, 'wut', 'include'), '-I' + pl('wiiu', 'include'), '-I' + pl('ppc', 'include'),
           '-I' + pl('wiiu', 'include', 'SDL2'), '-I' + os.path.join(HERE, 'src')]
    arch = ['-mcpu=750', '-meabi', '-mhard-float']
    cflags = ['-O2', '-Wall', '-Wno-unused-function', '-ffunction-sections', '-fdata-sections', '-DESPRESSO', '-D__WIIU__',
              '-D__WUT__'] + arch + os.environ.get('LSDR_EXTRA_CFLAGS', '').split()

    hdrs = glob.glob(os.path.join(HERE, 'src', '*.h'))
    newest_h = max([os.path.getmtime(h) for h in hdrs] + [0])
    skip = ('lua.c', 'luac.c', 'liolib.c', 'loslib.c', 'ldblib.c', 'linit.c')   # no io/os/debug libraries in the game
    units = [(s, 'app_') for s in sorted(glob.glob(os.path.join(HERE, 'src', '*.c')))]
    units += [(s, 'lua_') for s in sorted(glob.glob(os.path.join(lua, '*.c'))) if os.path.basename(s) not in skip]
    jobs, objs = [], []
    for src, pre in units:
        o = os.path.join(obj_dir, pre + os.path.basename(src) + '.o'); objs.append(o)
        if os.path.exists(o) and os.path.getmtime(o) > max(os.path.getmtime(src), newest_h): continue
        jobs.append([gcc, '-c', src, '-o', o] + cflags + inc)
    print('compiling %d of %d files with %d jobs' % (len(jobs), len(objs), a.jobs))
    with ThreadPoolExecutor(max(1, a.jobs)) as ex:
        for msg in ex.map(run, jobs):
            if msg: print(msg)

    elf = os.path.join(out, 'app.elf'); rpx = os.path.join(out, 'app.rpx')
    libdirs = ['-L' + os.path.join(dkp, 'wut', 'lib'), '-L' + pl('wiiu', 'lib'), '-L' + pl('ppc', 'lib')]
    libs = ['-lturbojpeg', '-lSDL2', '-lwut', '-lm']      # turbojpeg (portlibs/ppc): the movies' MJPEG frames (video.c)
    run([gcc, '-specs=' + os.path.join(dkp, 'wut', 'share', 'wut.specs')] + objs + libdirs + libs + ['-o', elf] + arch)
    run([elf2rpl, elf, rpx])

    # content: the wuhb mounts it at /vol/content; the app reads /vol/content/data/<file>
    if a.data:
        data = os.path.abspath(a.data)
        if os.path.basename(data.rstrip('\\/')) != 'data' or not os.path.isdir(data):
            die('--data must be a folder named "data" (the output of tools/make_data.py)')
        content = os.path.dirname(data)
        if os.path.isdir(os.path.join(data, 'MOVIE')):
            print('note: %s is bundled too, but the app reads movies from the SD card only' % os.path.join(data, 'MOVIE'))
        shutil.copy2(os.path.join(HERE, 'lua', 'api.lua'), os.path.join(data, 'api.lua'))
        print('note: copied api.lua into %s; delete it before copying that folder to the SD card' % data)
        name = a.name or 'lsdr'
    else:                                                # app only: just the port's own files, no game data
        content = os.path.join(out, 'content_app')
        if os.path.isdir(content): shutil.rmtree(content)
        os.makedirs(os.path.join(content, 'data'))
        shutil.copy2(os.path.join(HERE, 'lua', 'api.lua'), os.path.join(content, 'data', 'api.lua'))
        name = a.name or 'lsdr-app'
    wuhb = os.path.join(out, name + '.wuhb')
    cmd = [wuhbtool, rpx, wuhb, '--name=LSD Revamped (Wii U)', '--short-name=LSDR', '--author=Cuppax', '--content=' + content]
    for k, f in (('icon', '--icon='), ('tv', '--tv-image='), ('drc', '--drc-image=')):
        p = os.path.join(HERE, 'assets', k + '.png')
        if os.path.exists(p): cmd.append(f + p)
    run(cmd)
    print('built %s (%.2f MB)%s' % (wuhb, os.path.getsize(wuhb) / 1048576.0,
                                     '' if a.data else ' - app only: put the data folder on the SD card'))


if __name__ == '__main__':
    main()
