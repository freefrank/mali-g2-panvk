import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess


ROOT = Path(__file__).resolve().parent
PROJECT = ROOT.parent
DRIVER = PROJECT / 'driver'
REFERENCES = PROJECT / 'references'
BUILD = ROOT / 'build'
BIN = BUILD / 'bin'
OBJ = BUILD / 'obj'
BACKEND = BUILD / 'backend'
SHADERS = BUILD / 'shaders'


def prepare_output():
    BIN.mkdir(parents=True, exist_ok=True)
    OBJ.mkdir(parents=True, exist_ok=True)
    BACKEND.mkdir(parents=True, exist_ok=True)
    SHADERS.mkdir(parents=True, exist_ok=True)


def build_nir(build, source='compile_nir'):
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
    args[index + 1] = str(OBJ / (source + '.o'))
    args[-1] = str(ROOT / (source + '.c'))
    subprocess.run(args, cwd=command['directory'], env=env, check=True)
    all_link = subprocess.run(['ninja', '-C', str(build), '-t', 'commands',
                               'src/panfrost/clc/panfrost_compile'],
                              check=True, capture_output=True, text=True).stdout
    link = shlex.split(all_link.splitlines()[-1])
    link[link.index('-o') + 1] = str(BIN / source)
    link = [str(OBJ / (source + '.o')) if x.endswith('/pan_compile.c.o') else x
            for x in link]
    if source == 'replay_nir':
        link.insert(1, '-rdynamic')
    subprocess.run(link, cwd=command['directory'], env=env, check=True)


def build_android():
    env = {k: v for k, v in os.environ.items()
           if k not in ['LD_LIBRARY_PATH', 'LD_PRELOAD']}
    base = ['/data/data/com.termux/files/usr/bin/clang', '-no-canonical-prefixes',
            '-std=c11', '-Wall', '-Wextra', '-Werror', '-O2']
    for source in ['kmd_probe', 'probe_binaries']:
        subprocess.run(base + [str(ROOT / (source + '.c')), '-o', str(BIN / source), '-ldl'],
                       env=env, check=True)
    includes = [f'-I{REFERENCES / "malibridge/third_party/Vulkan-Headers-1.4.341/include"}',
                f'-I{DRIVER / "include"}', f'-I{DRIVER / "include/android_stub"}',
                f'-I{ROOT / "legacy/diagnostics"}']
    for source in ['vulkan_android', 'vulkan_draw', 'vulkan_compile', 'vulkan_tiles', 'vulkan_msaa', 'vulkan_submit', 'vulkan_acquire', 'vulkan_completion']:
        subprocess.run(base + includes + [str(ROOT / (source + '.c')), '-o',
                                         str(BIN / source), '-ldl'],
                       env=env, check=True)
    subprocess.run(base + ['-fPIC', '-shared', str(ROOT / 'trace_ioctl.c'),
                           '-o', str(BIN / 'libtrace_ioctl.so'), '-ldl', '-pthread'],
                   env=env, check=True)


def build_android_replay(build):
    env = dict(os.environ, LC_ALL='C')
    commands = json.loads((build / 'compile_commands.json').read_text())
    command = next(c for c in commands if c['file'].endswith('/panvk_g2_trace.c'))
    args = shlex.split(command['command'])
    for flag in ['-MF', '-MQ']:
        if flag in args:
            index = args.index(flag)
            del args[index:index + 2]
    if '-MD' in args:
        args.remove('-MD')
    args[args.index('-o') + 1] = str(OBJ / 'replay_nir_android.o')
    args[-1] = str(ROOT / 'replay_nir.c')
    subprocess.run(args, cwd=command['directory'], env=env, check=True)
    lines = subprocess.check_output(['ninja', '-C', str(build), '-t', 'commands',
                                     'src/panfrost/vulkan/libvulkan_panfrost.so'],
                                    text=True)
    original = shlex.split(lines.splitlines()[-1])
    archives = [x for x in original if x.endswith('.a')]
    link = [original[0], '-nostdlib++', '-pie', '-rdynamic', '-o',
            str(BIN / 'replay_nir_android'), str(OBJ / 'replay_nir_android.o'),
            '-Wl,--start-group'] + archives + ['-Wl,--end-group', '-ldl', '-llog', '-lm', '-pthread']
    subprocess.run(link, cwd=build, env=env, check=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--mesa-build', type=Path, default=DRIVER / 'build-host')
    parser.add_argument('--android', action='store_true')
    parser.add_argument('--android-replay', action='store_true')
    args = parser.parse_args()
    prepare_output()
    if args.android_replay:
        build_android_replay(DRIVER / 'build-android-arm')
    else:
        build_nir(args.mesa_build)
        build_nir(args.mesa_build, 'replay_nir')
    if args.android:
        build_android()
    print('Helpers built')
