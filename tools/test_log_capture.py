import argparse
import json
import os
from pathlib import Path
import subprocess
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('driver', type=Path)
    parser.add_argument('--monitor', type=Path, required=True)
    parser.add_argument('--tag', required=True)
    parser.add_argument('--fopen-hook', type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent
    work = root / 'work' / args.tag
    work.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env.pop('LD_PRELOAD', None)
    if args.fopen_hook:
        env['LD_PRELOAD'] = str(args.fopen_hook.resolve())
    env['LD_LIBRARY_PATH'] = '/vendor/lib64:/vendor/lib64/egl:/system/lib64'
    env['PANVK_DEBUG'] = 'sync'
    env['PANVK_G2_DIAGNOSTICS'] = '1'
    for option in ['WHITE', 'BLEND', 'SCISSOR', 'LOAD_DRAW', 'PARTIAL_RENDER']:
        env['MALI_G2_TILES_' + option] = '1'
    env['MALI_G2_TILES_DEPTH_FORMAT'] = '129'
    env['MALI_G2_TILES_INDEX_BITS'] = '32'
    env['MALI_G2_TILES_VERTEX_OFFSET'] = '-30000'
    command = [str(root / 'build/bin/vulkan_tiles'), str(args.driver.resolve()),
               str(root / 'build/shaders/tiles_sample.comp.spv'), '257', '129', '4', '37',
               str(root / 'build/shaders/tiles_depth.vert.spv'),
               str(root / 'build/shaders/tiles.frag.spv')]
    cases = []
    for name in ['eden_log.txt', 'yuzu_log.txt']:
        log = work / name
        with log.open('w') as opened:
            opened.write('Render.Vulkan <Info> G2_FD_CAPTURE_RENDER_CHECK\n'
                         'Video.NVDEC <Info> G2_FD_CAPTURE_VIDEO_CHECK\n'
                         'Core <Critical> G2_FD_CAPTURE_CRITICAL_CHECK\n'
                         'Core <Info> G2_FD_CAPTURE_IGNORE_CHECK\n')
            opened.flush()
            process = subprocess.Popen(command, env=env, pass_fds=(opened.fileno(),),
                                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                       text=True)
            output = process.communicate(timeout=30)[0]
        (work / (name + '.draw.txt')).write_text(output)
        time.sleep(0.3)
        events = []
        for line in args.monitor.open():
            try:
                item = json.loads(line)
            except ValueError:
                continue
            event = item.get('event', '')
            if f'pid={process.pid} ' in event:
                events.append(event)
        captured = '\n'.join(events)
        (work / (name + '.events.txt')).write_text(captured + '\n')
        checks = {
            'draw_pass': process.returncode == 0 and 'TILES_PASS' in output,
            'fd_source_detected': 'source=' in captured,
            'renderer_log_captured': 'G2_FD_CAPTURE_RENDER_CHECK' in captured,
            'video_log_captured': 'G2_FD_CAPTURE_VIDEO_CHECK' in captured,
            'critical_log_captured': 'G2_FD_CAPTURE_CRITICAL_CHECK' in captured,
            'unrelated_info_excluded': 'G2_FD_CAPTURE_IGNORE_CHECK' not in captured,
        }
        case = dict(name=name, pid=process.pid, returncode=process.returncode,
                    checks=checks, passed=all(checks.values()))
        cases.append(case)
        print(json.dumps(case), flush=True)
    result = dict(driver=str(args.driver.resolve()), cases=cases,
                  fopen_hook=str(args.fopen_hook.resolve()) if args.fopen_hook else None,
                  passed=all(case['passed'] for case in cases))
    (root / 'fixtures' / (args.tag + '.json')).write_text(json.dumps(result, indent=2) + '\n')
    raise SystemExit(not result['passed'])


if __name__ == '__main__':
    main()
