import argparse
import hashlib
import json
import os
from pathlib import Path
import random
import subprocess


ROOT = Path(__file__).resolve().parent
SHADERS = ['astc_decoder', 'vulkan_quad_indexed', 'vulkan_uint8',
           'queries_prefix_scan_sum', 'queries_prefix_scan_sum_nosubgroups',
           'resolve_conditional_render', 'block_linear_unswizzle_3d_bcn']


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('driver', type=Path)
    parser.add_argument('--tag', default='eden-compute-suite')
    args = parser.parse_args()
    env = dict(os.environ)
    for key in ['LD_PRELOAD', 'MALI_G2_COMPILE_ROBUST', 'MALI_G2_RUN_ASTC',
                'MALI_G2_ASTC_INPUT', 'MALI_G2_ASTC_OUTPUT', 'MALI_G2_ASTC_REFERENCE']:
        env.pop(key, None)
    env['LD_LIBRARY_PATH'] = '/vendor/lib64:/vendor/lib64/egl:/system/lib64'
    env['MESA_LOG'] = 'file'
    for name in SHADERS:
        source = Path('/root/eden-source-review/src/video_core/host_shaders') / (name + '.comp')
        subprocess.run(['glslangValidator', '-V', '-DUseUniformBlock=1',
                        '--target-env', 'spirv1.3', str(source),
                        '-o', str(ROOT / (name + '.comp.spv'))], check=True,
                       stdout=subprocess.DEVNULL)
    results = []

    def run(label, driver, names, case_env):
        call = [str(ROOT / 'vulkan_compile'), str(driver)]
        call += [str(ROOT / (name + '.comp.spv')) for name in names]
        process = subprocess.run(call, env=case_env, stdout=subprocess.PIPE,
                                 stderr=subprocess.STDOUT, text=True, timeout=90)
        (ROOT / 'fixtures' / (args.tag + '-' + label + '.txt')).write_text(process.stdout)
        results.append({'case': label, 'returncode': process.returncode,
                        'compiled': process.stdout.count('COMPILE_PASS')})
        print(label, 'PASS' if process.returncode == 0 else 'FAIL', flush=True)
        if process.returncode:
            print(process.stdout, flush=True)
            raise RuntimeError(label)

    try:
        for robust in [False, True]:
            case = dict(env)
            if robust:
                case['MALI_G2_COMPILE_ROBUST'] = '1'
            run('compile-robust-' + str(int(robust)), args.driver, SHADERS, case)
        astc_env = dict(env, MALI_G2_RUN_ASTC='1')
        run('astc-void-extent', args.driver, ['astc_decoder'], astc_env)
        for seed in [401, 402, 403]:
            rng = random.Random(seed)
            blocks = bytearray()
            for _ in range(16):
                word = (rng.getrandbits(128) & ~((1 << 17) - 1)) | 0x42 | (12 << 13)
                blocks += word.to_bytes(16, 'little')
            source = ROOT / 'fixtures' / ('astc-normal-' + str(seed) + '.blocks')
            reference = ROOT / 'fixtures' / ('astc-normal-' + str(seed) + '-stock.rgba')
            output = ROOT / 'fixtures' / (args.tag + '-normal-' + str(seed) + '.rgba')
            source.write_bytes(blocks)
            case = dict(astc_env, MALI_G2_ASTC_INPUT=str(source),
                        MALI_G2_ASTC_OUTPUT=str(reference))
            run('stock-normal-' + str(seed), '/vendor/lib64/hw/vulkan.mali.so',
                ['astc_decoder'], case)
            case.update(MALI_G2_ASTC_REFERENCE=str(reference), MALI_G2_ASTC_OUTPUT=str(output))
            run('panvk-normal-' + str(seed), args.driver, ['astc_decoder'], case)
            if reference.read_bytes() != output.read_bytes():
                raise RuntimeError('pixel mismatch for seed ' + str(seed))
            results[-1]['pixel_bytes_sha256'] = hashlib.sha256(output.read_bytes()).hexdigest()
    except (RuntimeError, subprocess.TimeoutExpired) as error:
        results.append({'error': str(error)})
        returncode = 1
    else:
        returncode = 0
    report = {'driver': str(args.driver.resolve()), 'returncode': returncode, 'results': results}
    (ROOT / 'fixtures' / (args.tag + '.json')).write_text(json.dumps(report, indent=2) + '\n')
    return returncode


if __name__ == '__main__':
    raise SystemExit(main())
