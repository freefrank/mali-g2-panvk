import argparse
import hashlib
import json
import os
from pathlib import Path
import socket
import struct
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path,
                        default=Path('/storage/emulated/0/Download/panvk-g2-test3-driver.log'))
    parser.add_argument('--seconds', type=int, default=3600)
    parser.add_argument('--capture-dir', type=Path)
    args = parser.parse_args()
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 33554432)
    overflow_option = getattr(socket, 'SO_RXQ_OVFL', 40)
    sock.setsockopt(socket.SOL_SOCKET, overflow_option, 1)
    sock.bind(('127.0.0.1', 28643))
    sock.settimeout(1)
    deadline = time.monotonic() + args.seconds
    written = 0
    captures = {}
    capture_bytes = 0
    dropped = 0
    if args.capture_dir:
        args.capture_dir.mkdir(parents=True, exist_ok=True)
    with args.output.open('a', buffering=1) as output:
        output.write(json.dumps({'monitor_started': time.time(), 'pid': os.getpid(),
                                 'receive_buffer_bytes': sock.getsockopt(socket.SOL_SOCKET,
                                                                         socket.SO_RCVBUF)}) + '\n')
        print('Monitoring G2 driver:', args.output, flush=True)
        while time.monotonic() < deadline and written < 8388608:
            try:
                data, control, flags, source = sock.recvmsg(8192, 64)
            except socket.timeout:
                continue
            for level, kind, value in control:
                if level == socket.SOL_SOCKET and kind == overflow_option:
                    count, = struct.unpack('I', value)
                    if count != dropped:
                        output.write(json.dumps({'kernel_dropped_datagrams': count}) + '\n')
                        dropped = count
            if source[0] != '127.0.0.1' or flags & socket.MSG_TRUNC:
                continue
            if data.startswith(b'PANVK_G2_BLOB_V1 ') and args.capture_dir:
                header, separator, payload = data.partition(b'\n')
                try:
                    pid, compile_id, stage, point, size, offset = map(int, header.split()[1:])
                except (ValueError, TypeError):
                    continue
                if (not separator or min(pid, compile_id, stage, point, size, offset) < 0 or
                        not 0 < size <= 2097152 or offset >= size or offset % 4096 or
                        len(payload) != min(4096, size - offset)):
                    continue
                key = (pid, compile_id, stage, point)
                if key not in captures:
                    if capture_bytes + size > 16777216:
                        continue
                    captures[key] = {'data': bytearray(size), 'parts': set(), 'size': size}
                    capture_bytes += size
                capture = captures[key]
                if capture['size'] != size:
                    continue
                capture['data'][offset:offset + len(payload)] = payload
                capture['parts'].add(offset)
                if len(capture['parts']) == (size + 4095) // 4096:
                    path = args.capture_dir / ('shader-%d-%d-%d-%d.bin' % key)
                    path.write_bytes(capture['data'])
                    output.write(json.dumps({'shader_dump_complete': str(path), 'pid': pid,
                                             'compile_id': compile_id, 'stage': stage,
                                             'point': point, 'bytes': size,
                                             'sha256': hashlib.sha256(capture['data']).hexdigest()}) + '\n')
                    capture_bytes -= size
                    del captures[key]
                continue
            if not data.startswith(b'PANVK_G2_V1 '):
                continue
            item = {'received': time.time(), 'event': data.decode('utf-8', errors='replace')}
            line = json.dumps(item, ensure_ascii=True) + '\n'
            output.write(line)
            written += len(line)
        output.write(json.dumps({'monitor_stopped': time.time(), 'bytes': written,
                                 'kernel_dropped_datagrams': dropped,
                                 'incomplete_shader_dumps': len(captures)}) + '\n')
    sock.close()


if __name__ == '__main__':
    main()
