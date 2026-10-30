import argparse
import itertools
import json
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('driver', type=Path)
    parser.add_argument('--tag', required=True)
    parser.add_argument('--baseline', type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent
    work = root / 'work' / args.tag
    work.mkdir(parents=True, exist_ok=True)
    shader_dir = root / 'build/shaders'
    for array in [False, True]:
        command = ['glslangValidator', '-V']
        if array:
            command.append('-DARRAY_IMAGE=1')
        command += [str(root / 'fixtures/msaa_sample.comp'), '-o',
                    str(shader_dir / ('msaa_sample-array.comp.spv' if array else 'msaa_sample.comp.spv'))]
        subprocess.run(command, check=True, stdout=subprocess.DEVNULL)
    env = dict(os.environ)
    env.pop('LD_PRELOAD', None)
    env.pop('MALI_G2_MSAA_PROBE', None)
    env['LD_LIBRARY_PATH'] = '/vendor/lib64:/vendor/lib64/egl:/system/lib64'
    env['MALI_G2_MSAA_CLEAR'] = '1'
    drivers = [('vendor', Path('/vendor/lib64/hw/vulkan.mali.so')),
               ('current', args.driver.resolve())]
    if args.baseline:
        drivers.append(('baseline', args.baseline.resolve()))
    results = []
    for name, driver in drivers:
        counts = [4] if name == 'baseline' else [2, 4, 8, 16]
        for fmt, samples, layer in itertools.product([37, 97, 122], counts, [0, 2]):
            current_env = dict(env)
            current_env.pop('MALI_G2_MSAA_LAYER', None)
            if layer:
                current_env['MALI_G2_MSAA_LAYER'] = '1'
            case = f'{name}-fmt{fmt}-samples{samples}-layer{layer}'
            command = [str(root / 'build/bin/vulkan_msaa'), str(driver), '-',
                       str(shader_dir / ('msaa_sample-array.comp.spv' if layer else 'msaa_sample.comp.spv')),
                       str(fmt), str(samples)]
            run = subprocess.run(command, env=current_env, stdout=subprocess.PIPE,
                                 stderr=subprocess.STDOUT, text=True, timeout=30)
            (work / (case + '.txt')).write_text(run.stdout)
            passed = run.returncode == 0 and 'MSAA_PASS' in run.stdout
            expected_failure = name == 'baseline'
            result = dict(case=case, returncode=run.returncode, passed=passed,
                          expected_failure=expected_failure,
                          expected_result=not passed if expected_failure else passed)
            results.append(result)
            print(case, 'PASS' if passed else 'FAIL', flush=True)
    report = dict(driver=str(args.driver.resolve()),
                  purpose='Valid MSAA clear, per-sample texture read and resolve across formats, sample counts and distinct array layers',
                  results=results, passed=all(result['expected_result'] for result in results))
    (root / 'fixtures' / (args.tag + '.json')).write_text(json.dumps(report, indent=2) + '\n')
    raise SystemExit(not report['passed'])


if __name__ == '__main__':
    main()
