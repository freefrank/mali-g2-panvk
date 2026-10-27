import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess


ROOT = Path(__file__).resolve().parent


def build_nir(build):
    env = dict(os.environ, LC_ALL='C')
    commands = json.loads((build / 'compile_commands.json').read_text())
    command = next(c for c in commands if c['file'].endswith('/pan_compile.c'))
    args = shlex.split(command['command'])
    for flag in ['-MF', '-MQ']:
        if flag in args:
            index = args.index(flag)
            del args[index:index + 2]
    if '-MD' in args:
        args.remove('-MD')
    index = args.index('-o')
    args[index + 1] = str(ROOT / 'compile_nir.o')
    args[-1] = str(ROOT / 'compile_nir.c')
    subprocess.run(args, cwd=command['directory'], env=env, check=True)
    all_link = subprocess.run(['ninja', '-C', str(build), '-t', 'commands',
                               'src/panfrost/clc/panfrost_compile'],
                              check=True, capture_output=True, text=True).stdout
    link = shlex.split(all_link.splitlines()[-1])
    link[link.index('-o') + 1] = str(ROOT / 'compile_nir')
    link = [str(ROOT / 'compile_nir.o') if x.endswith('/pan_compile.c.o') else x
            for x in link]
    subprocess.run(link, cwd=command['directory'], env=env, check=True)


def build_android():
    env = {k: v for k, v in os.environ.items()
           if k not in ['LD_LIBRARY_PATH', 'LD_PRELOAD']}
    base = ['/data/data/com.termux/files/usr/bin/clang', '-no-canonical-prefixes',
            '-std=c11', '-Wall', '-Wextra', '-Werror', '-O2']
    for source in ['kmd_probe', 'probe_binaries']:
        subprocess.run(base + [str(ROOT / (source + '.c')), '-o', str(ROOT / source), '-ldl'],
                       env=env, check=True)
    subprocess.run(base + ['-fPIC', '-shared', str(ROOT / 'trace_ioctl.c'),
                           '-o', str(ROOT / 'libtrace_ioctl.so'), '-ldl', '-pthread'],
                   env=env, check=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--mesa-build', type=Path, default=Path('/root/mali-panvk/build-host'))
    parser.add_argument('--android', action='store_true')
    args = parser.parse_args()
    build_nir(args.mesa_build)
    if args.android:
        build_android()
    print('Helpers built')
