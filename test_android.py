import argparse
import json
import os
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('driver', type=Path)
    parser.add_argument('--tag', default='android-suite')
    args = parser.parse_args()
    env = dict(os.environ)
    env.pop('LD_PRELOAD', None)
    env.pop('MALI_G2_TEST_CLOCKWISE', None)
    env['LD_LIBRARY_PATH'] = '/vendor/lib64:/vendor/lib64/egl:/system/lib64'
    env['MESA_LOG'] = 'file'
    env['MALI_G2_TEST_FRAMES'] = '30'
    for shader in ['triangle.vert', 'triangle.frag', 'texture.vert',
                   'texture.frag', 'vertex_store.vert', 'fragcoord.frag',
                   'drawid.vert', 'drawid.frag']:
        subprocess.run(['glslangValidator', '-V', str(ROOT / 'fixtures' / shader),
                        '-o', str(ROOT / (shader + '.spv'))], check=True,
                       stdout=subprocess.DEVNULL)
    tests = [('import', 'vulkan_android', [], 'FAILURES 0')]
    for name, vert, frag, mode in [
        ('triangle-native', 'triangle.vert', 'triangle.frag', 'native'),
        ('texture-native', 'texture.vert', 'texture.frag', 'texture_native'),
        ('texture-ahb', 'texture.vert', 'texture.frag', 'texture_ahb'),
        ('texture-offscreen', 'texture.vert', 'texture.frag', 'texture'),
        ('vertex-store', 'vertex_store.vert', 'triangle.frag', 'vertex_store'),
        ('fragcoord', 'triangle.vert', 'fragcoord.frag', 'fragcoord'),
        ('fragcoord-clockwise', 'triangle.vert', 'fragcoord.frag', 'fragcoord'),
        ('drawid', 'drawid.vert', 'drawid.frag', 'drawid'),
    ]:
        tests.append((name, 'vulkan_draw',
                      [str(ROOT / (vert + '.spv')), str(ROOT / (frag + '.spv')), mode],
                      'VULKAN_DRAW_PASS frames=30/30 correct_pixels=256/256'))
    results = []
    for name, binary, extra, expected in tests:
        case_env = dict(env)
        if name == 'fragcoord-clockwise':
            case_env['MALI_G2_TEST_CLOCKWISE'] = '1'
        run = subprocess.run([str(ROOT / binary), str(args.driver.resolve())] + extra,
                             env=case_env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                             text=True, timeout=45)
        path = ROOT / 'fixtures' / (args.tag + '-' + name + '.txt')
        path.write_text(run.stdout)
        passed = run.returncode == 0 and expected in run.stdout
        results.append({'case': name, 'returncode': run.returncode, 'passed': passed})
        print(name, 'PASS' if passed else 'FAIL', flush=True)
        if not passed:
            print(run.stdout, flush=True)
            break
    report = {'driver': str(args.driver.resolve()), 'results': results}
    (ROOT / 'fixtures' / (args.tag + '.json')).write_text(json.dumps(report, indent=2) + '\n')
    return 0 if len(results) == len(tests) and all(x['passed'] for x in results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
