import argparse
import json
import os
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parent
BIN = ROOT / 'build' / 'bin'
BUILD = ROOT / 'build' / 'backend'
MODES = ['integer', 'float', 'branch', 'loop', 'global_x', 'global_y', 'global_z',
         'local_x', 'local_y', 'local_z', 'group_x', 'group_y', 'group_z']


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, default=ROOT / 'fixtures/mesa-backend-suite.json')
    args = parser.parse_args()
    environment = {k: v for k, v in os.environ.items()
                   if k not in ['LD_LIBRARY_PATH', 'LD_PRELOAD']}
    BUILD.mkdir(parents=True, exist_ok=True)
    results = []
    failed = False
    for mode in MODES:
        binary = BUILD / f'mesa-{mode}.bin'
        compiled = subprocess.run([str(BIN / 'compile_nir'), str(binary), '15', mode],
                                  capture_output=True, text=True, timeout=20)
        entry = {'mode': mode, 'compile_exit': compiled.returncode,
                 'compile_log': compiled.stdout + compiled.stderr}
        if not compiled.returncode:
            metadata = json.loads(Path(str(binary) + '.json').read_text())
            allocation = (metadata['work_registers'] + 7) // 8 - 1
            command = [str(BIN / 'kmd_probe'), '21', '0', '0', str(binary),
                       str(allocation), str(metadata['preload'])]
            if '_' in mode:
                command.append(mode)
            gpu = subprocess.run(command, env=environment, capture_output=True, text=True,
                                 timeout=15)
            entry.update({'gpu_exit': gpu.returncode, 'metadata': metadata,
                          'gpu_log': gpu.stdout + gpu.stderr})
        passed = entry['compile_exit'] == 0 and entry.get('gpu_exit') == 0
        failed |= not passed
        print(mode, 'PASS' if passed else 'FAIL', flush=True)
        results.append(entry)
    args.output.write_text(json.dumps(results, indent=2) + '\n')
    return int(failed)


if __name__ == '__main__':
    raise SystemExit(main())
