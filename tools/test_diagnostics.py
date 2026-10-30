import argparse
import json
import os
from pathlib import Path
import re
import socket
import subprocess
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('driver', type=Path)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--tag', required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent
    work = root / 'work' / args.tag
    work.mkdir(parents=True, exist_ok=True)
    receiver = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    receiver.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4194304)
    receiver.bind(('127.0.0.1', 28643))
    receiver.setblocking(False)
    results = []
    cases = [('baseline', args.baseline.resolve(), None, True),
             ('default-off', args.driver.resolve(), None, False),
             ('explicit-off', args.driver.resolve(), '0', False),
             ('explicit-on', args.driver.resolve(), '1', True)]
    for name, driver, enabled, expected in cases:
        env = dict(os.environ)
        env.pop('LD_PRELOAD', None)
        env['LD_LIBRARY_PATH'] = '/vendor/lib64:/vendor/lib64/egl:/system/lib64'
        env.pop('PANVK_G2_DIAGNOSTICS', None)
        if enabled is not None:
            env['PANVK_G2_DIAGNOSTICS'] = enabled
        env['PANVK_DEBUG'] = 'sync' if expected else 'sync,trace,dump,nir,cs,startup'
        env['PANVK_G2_TRACE'] = '1'
        env['MALI_G2_TILES_AUDIT_DIAGNOSTICS'] = '1'
        trace_file = work / (name + '.trace.txt')
        trace_file.write_text('')
        env['PANVK_G2_TRACE_FILE'] = str(trace_file)
        for key in ['WHITE', 'BLEND', 'SCISSOR', 'LOAD_DRAW', 'PARTIAL_RENDER']:
            env['MALI_G2_TILES_' + key] = '1'
        command = [str(root / 'build/bin/vulkan_tiles'), str(driver),
                   str(root / 'build/shaders/tiles_sample.comp.spv'), '257', '129', '4', '37',
                   str(root / 'build/shaders/tiles.vert.spv'), str(root / 'build/shaders/tiles.frag.spv')]
        process = subprocess.Popen(command, env=env, stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, text=True)
        datagrams = 0
        deadline = time.monotonic() + 30
        while process.poll() is None and time.monotonic() < deadline:
            try:
                receiver.recv(8192)
                datagrams += 1
            except BlockingIOError:
                time.sleep(0.002)
        if process.poll() is None:
            process.kill()
        output = process.communicate()[0]
        while True:
            try:
                receiver.recv(8192)
                datagrams += 1
            except BlockingIOError:
                break
        (work / (name + '.txt')).write_text(output)
        match = re.search(r'DIAG_AUDIT sockets=(\d+) signals=(\d+)', output)
        counts = dict(zip(['sockets', 'signals'], map(int, match.groups()))) if match else {}
        trace_bytes = trace_file.stat().st_size
        draw_pass = process.returncode == 0 and 'TILES_PASS' in output
        if expected:
            diagnostic_pass = bool(counts) and all(v > 0 for v in counts.values()) and datagrams > 0 and trace_bytes > 0
        else:
            diagnostic_pass = counts == dict(sockets=0, signals=0) and datagrams == 0 and trace_bytes == 0
        case = dict(case=name, draw_pass=draw_pass, counts=counts, datagrams=datagrams,
                    trace_file_bytes=trace_bytes, passed=draw_pass and diagnostic_pass)
        results.append(case)
        print(json.dumps(case), flush=True)
    receiver.close()
    report = dict(driver=str(args.driver.resolve()), results=results,
                  passed=all(case['passed'] for case in results))
    (root / 'fixtures' / (args.tag + '.json')).write_text(json.dumps(report, indent=2) + '\n')
    raise SystemExit(not report['passed'])


if __name__ == '__main__':
    main()
