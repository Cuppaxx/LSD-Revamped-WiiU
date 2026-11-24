#!/usr/bin/env python3
"""Build the game data folder for the Wii U port from YOUR OWN clone of LSD: Revamped (one command).

  python tools/make_data.py --revamped PATH/LSDRevamped [--addl PATH/LSDR-Additional-Dreams] [--out build/data]
                            [--movies] [--music-all] [--no-audio] [--no-special] [--stage DIR]

  --revamped DIR  clone of https://github.com/Figglewatts/LSDRevamped WITH git-lfs content (the folder that contains LSDR/)
  --addl DIR      optional clone of https://github.com/Figglewatts/LSDR-Additional-Dreams (14 more dreams)
  --out DIR       output data folder (default: <repo>/build/data); copy it to SD:/wiiu/apps/lsdr/data/
  --movies        also transcode the 38 FMV movies (intro, special days, ending; ~250 MB) into <out>/MOVIE
  --music-all     copy every music style (~800 MB) instead of the standard style only (~100 MB)
  --no-audio      skip sound effects / footsteps / DreamAudio (audio.txt, SFX/)
  --no-special    skip special days, text days and credits (specials.txt, SPDAY/, CREDITS/)
  --texts-en FILE optional English text-day file, installed as SPDAY/texts_en.txt (without it the text days show
                  the original Japanese pages)
  --stage DIR     scratch folder for intermediate files (default: <repo>/build/stage)

Needs Python 3.8+, Pillow (pip install pillow) and ffmpeg on PATH (menu music, mod sounds, movies).
Everything written to --out is derived from the original game data: keep it for yourself, never redistribute it.
"""
import argparse, glob, os, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)


def check_repo(repo):
    orig = os.path.join(repo, 'LSDR', 'Assets', 'Resources', 'Original')
    if not os.path.isdir(orig):
        sys.exit('make_data: %s does not look like an LSDRevamped clone (no LSDR/Assets/Resources/Original)' % repo)
    lbds = glob.glob(os.path.join(orig, 'STG00', '*.LBD'))
    if not lbds:
        sys.exit('make_data: no STG00/*.LBD in %s' % orig)
    with open(lbds[0], 'rb') as f:
        if f.read(40).startswith(b'version https://git-lfs'):
            sys.exit('make_data: the LBD files are git-lfs pointers. Run "git lfs install" and "git lfs pull" in the clone.')


def main():
    ap = argparse.ArgumentParser(description='Build the Wii U data folder from your own LSD: Revamped clone')
    ap.add_argument('--revamped', required=True)
    ap.add_argument('--addl')
    ap.add_argument('--out', default=os.path.join(ROOT, 'build', 'data'))
    ap.add_argument('--stage', default=os.path.join(ROOT, 'build', 'stage'))
    ap.add_argument('--movies', action='store_true')
    ap.add_argument('--music-all', action='store_true')
    ap.add_argument('--no-audio', action='store_true')
    ap.add_argument('--no-special', action='store_true')
    ap.add_argument('--texts-en', help='optional English text-day file to install as SPDAY/texts_en.txt')
    a = ap.parse_args()
    if a.texts_en and not os.path.isfile(a.texts_en): sys.exit('make_data: --texts-en file not found: %s' % a.texts_en)

    repo = os.path.abspath(a.revamped); out = os.path.abspath(a.out); stage = os.path.abspath(a.stage)
    check_repo(repo)
    try:
        import PIL  # noqa: F401
    except ImportError:
        sys.exit('make_data: Pillow is missing (pip install pillow)')
    if not shutil.which('ffmpeg'):
        print('make_data: WARNING: ffmpeg not on PATH - title music, Additional Dreams sounds and movies will be skipped')
    os.makedirs(out, exist_ok=True); os.makedirs(stage, exist_ok=True)

    cmd = [sys.executable, os.path.join(HERE, 'convert.py'), repo, out, '--copy', '--music-all' if a.music_all else '--music',
           '--stage', stage]
    if a.addl:
        if not os.path.isdir(a.addl): sys.exit('make_data: --addl folder not found: %s' % a.addl)
        cmd += ['--mod', os.path.abspath(a.addl)]
    else:
        cmd += ['--noaddl']
    if not a.no_audio: cmd += ['--audio']
    if not a.no_special: cmd += ['--special'] + (['--movies'] if a.movies else [])
    print('make_data:', ' '.join(cmd))
    r = subprocess.run(cmd)
    if r.returncode: sys.exit('make_data: convert.py failed (exit %d)' % r.returncode)

    if not a.no_special:
        # text days: the original Japanese pages (SPDAY/SPDAYnnX.TIM) and the rendered text pages (TXT_*.TIM) are the
        # fallback the app shows when no English text file is present (~7 MB)
        spd = os.path.join(stage, 'out_special', 'SPDAY')
        if os.path.isdir(spd):
            dst = os.path.join(out, 'SPDAY'); os.makedirs(dst, exist_ok=True)
            for f in glob.glob(os.path.join(spd, '*.TIM')): shutil.copy2(f, dst)
        if a.texts_en:                              # optional English text-day file (format: docs/formats/SPECIAL_FORMAT.md)
            dst = os.path.join(out, 'SPDAY'); os.makedirs(dst, exist_ok=True)
            shutil.copy2(a.texts_en, os.path.join(dst, 'texts_en.txt'))
    if a.movies and not a.no_special:            # the movies are read from SD:/wiiu/apps/lsdr/data/MOVIE only
        src = os.path.join(stage, 'out_special', 'MOVIE')
        if os.path.isdir(src):
            dst = os.path.join(out, 'MOVIE'); os.makedirs(dst, exist_ok=True)
            for f in glob.glob(os.path.join(src, '*.mjv')) + glob.glob(os.path.join(src, '*.wav')):
                shutil.copy2(f, dst)
            print('make_data: movies copied to', dst)
    stale = os.path.join(out, 'api.lua')
    if os.path.exists(stale): os.remove(stale)   # api.lua ships inside the app; a copy on the SD card would override it
    size = sum(os.path.getsize(os.path.join(d, f)) for d, _, fs in os.walk(out) for f in fs)
    print('make_data: done, %s (%.0f MB). Copy this folder to SD:/wiiu/apps/lsdr/data/' % (out, size / 1048576.0))


if __name__ == '__main__':
    main()
