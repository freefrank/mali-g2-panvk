import argparse
import json
import os
from pathlib import Path
import socket
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path,
                        default=Path('/storage/emulated/0/Download/panvk-g2-test3-driver.log'))
    parser.add_argument('--seconds', type=int, default=3600)
    args = parser.parse_args()
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 33554432)
    sock.bind(('127.0.0.1', 28643))
    sock.settimeout(1)
    deadline = time.monotonic() + args.seconds
    written = 0
    with args.output.open('a', buffering=1) as output:
        output.write(json.dumps({'monitor_started': time.time(), 'pid': os.getpid(),
                                 'receive_buffer_bytes': sock.getsockopt(socket.SOL_SOCKET,
                                                                         socket.SO_RCVBUF)}) + '\n')
        print('Monitoring G2 driver:', args.output, flush=True)
        while time.monotonic() < deadline and written < 8388608:
            try:
                data, source = sock.recvfrom(2048)
            except socket.timeout:
                continue
            if source[0] != '127.0.0.1' or not data.startswith(b'PANVK_G2_V1 '):
                continue
            item = {'received': time.time(), 'event': data.decode('utf-8', errors='replace')}
            line = json.dumps(item, ensure_ascii=True) + '\n'
            output.write(line)
            written += len(line)
        output.write(json.dumps({'monitor_stopped': time.time(), 'bytes': written}) + '\n')
    sock.close()


if __name__ == '__main__':
    main()
