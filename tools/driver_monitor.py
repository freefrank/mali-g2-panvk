import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import select
import socket
import struct
import time


def create_receivers(port, count):
    class Filter(ctypes.Structure):
        _fields_ = [('code', ctypes.c_ushort), ('jt', ctypes.c_ubyte),
                    ('jf', ctypes.c_ubyte), ('k', ctypes.c_uint)]

    class Program(ctypes.Structure):
        _fields_ = [('length', ctypes.c_ushort), ('filters', ctypes.POINTER(Filter))]

    receivers = []
    try:
        for _ in range(count):
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            receivers.append(sock)
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 33554432)
            sock.setsockopt(socket.SOL_SOCKET, getattr(socket, 'SO_RXQ_OVFL', 40), 1)
            sock.bind(('127.0.0.1', port))
            port = sock.getsockname()[1]
        if count > 1:
            filters = (Filter * 3)(Filter(0x20, 0, 0, 0xfffff038),
                                   Filter(0x94, 0, 0, count), Filter(0x16, 0, 0, 0))
            program = Program(3, filters)
            libc = ctypes.CDLL(None, use_errno=True)
            if libc.setsockopt(receivers[0].fileno(), socket.SOL_SOCKET, 51,
                               ctypes.byref(program), ctypes.sizeof(program)):
                raise OSError(ctypes.get_errno(), 'SO_ATTACH_REUSEPORT_CBPF')
        return receivers
    except BaseException:
        for sock in receivers:
            sock.close()
        raise


def receive(receivers, deadline, overflow_option):
    indices = {sock: index for index, sock in enumerate(receivers)}
    next_stats = 0
    while time.monotonic() < deadline:
        ready, _, _ = select.select(receivers, [], [], 1)
        if not ready or time.monotonic() >= next_stats:
            for index, sock in enumerate(receivers):
                memory = struct.unpack('9I', sock.getsockopt(socket.SOL_SOCKET, 55, 36))
                yield index, b'', [(socket.SOL_SOCKET, overflow_option,
                                    struct.pack('I', memory[8]))], 0, ('127.0.0.1', 0)
            next_stats = time.monotonic() + 1
        for sock in ready:
            yield (indices[sock], *sock.recvmsg(8192, 64))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path,
                        default=Path('/storage/emulated/0/Download/panvk-g2-test3-driver.log'))
    parser.add_argument('--seconds', type=int, default=3600)
    parser.add_argument('--capture-dir', type=Path)
    parser.add_argument('--sockets', type=int, default=8)
    args = parser.parse_args()
    if not 1 <= args.sockets <= 16:
        parser.error('--sockets must be between 1 and 16')
    receivers = create_receivers(28643, args.sockets)
    overflow_option = getattr(socket, 'SO_RXQ_OVFL', 40)
    deadline = time.monotonic() + args.seconds
    written = 0
    captures = {}
    capture_bytes = 0
    dropped = [0] * len(receivers)
    if args.capture_dir:
        args.capture_dir.mkdir(parents=True, exist_ok=True)
    with args.output.open('a', buffering=1) as output:
        output.write(json.dumps({'monitor_started': time.time(), 'pid': os.getpid(),
                                 'receive_sockets': len(receivers),
                                 'receive_buffer_bytes': receivers[0].getsockopt(
                                     socket.SOL_SOCKET, socket.SO_RCVBUF),
                                 'total_receive_buffer_bytes': sum(s.getsockopt(
                                     socket.SOL_SOCKET, socket.SO_RCVBUF) for s in receivers)}) + '\n')
        print('Monitoring G2 driver:', args.output, flush=True)
        for index, data, control, flags, source in receive(receivers, deadline, overflow_option):
            if written >= 268435456:
                break
            for level, kind, value in control:
                if level == socket.SOL_SOCKET and kind == overflow_option:
                    count, = struct.unpack('I', value)
                    if count != dropped[index]:
                        dropped[index] = count
                        output.write(json.dumps({'kernel_dropped_datagrams': sum(dropped),
                                                 'receiver_socket': index}) + '\n')
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
                    if capture_bytes + size > 1073741824:
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
                                 'kernel_dropped_datagrams': sum(dropped),
                                 'incomplete_shader_dumps': len(captures)}) + '\n')
    for sock in receivers:
        sock.close()


if __name__ == '__main__':
    main()
