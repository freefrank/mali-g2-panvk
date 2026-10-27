import argparse
import json
from pathlib import Path
import struct


def inspect(directory):
    captures = []
    for metadata in sorted(directory.glob('kick-*-maps.jsonl')):
        kick = int(metadata.name.split('-')[1])
        mappings = [json.loads(l) for l in metadata.read_text().splitlines()]

        def read(address, size):
            for m in mappings:
                start = int(m['offset'], 16)
                offset = address - start
                if 0 <= offset and offset + size <= m['copied']:
                    data = (directory / f"kick-{kick}-map-{m['index']}.bin").read_bytes()
                    return data[offset:offset + size]
            raise ValueError(f'uncaptured range {address:#x}+{size:#x}')

        events = [json.loads(l) for l in (directory / 'ioctls.jsonl').read_text().splitlines()]
        queues = [struct.unpack('<Q', bytes.fromhex(e['before']))[0]
                  for e in events if e['nr'] == 37]
        if kick >= len(queues):
            continue
        registers = [0] * 256
        visited = set()

        def walk(address, size, depth=0):
            if depth > 8 or (address, size) in visited:
                return
            visited.add((address, size))
            data = read(address, size)
            for i, (word,) in enumerate(struct.iter_unpack('<Q', data)):
                op = word >> 56
                reg = (word >> 48) & 255
                if op == 1:
                    value = word & ((1 << 48) - 1)
                    registers[reg], registers[reg + 1] = value & 0xffffffff, value >> 32
                elif op == 2:
                    registers[reg] = word & 0xffffffff
                elif op == 32:
                    a, n = (word >> 40) & 255, (word >> 32) & 255
                    ptr = registers[a] | (registers[a + 1] << 32)
                    walk(ptr, registers[n], depth + 1)
                elif op == 4:
                    ptr = registers[16] | (registers[17] << 32)
                    spd = ptr & ~63
                    descriptor = read(spd, 32)
                    code = struct.unpack_from('<Q', descriptor, 8)[0]
                    capture = {'kick': kick, 'cs_address': hex(address + i * 8),
                               'shader_pointer': hex(ptr),
                               'shader_descriptor': descriptor.hex(),
                               'shader_binary': hex(code),
                               'workgroup_size_register': hex(registers[33]),
                               'resources': [hex(registers[r] | registers[r + 1] << 32)
                                             for r in [0, 8, 16, 24]]}
                    captures.append(capture)
        walk(queues[kick], 128)
    return captures


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    print(json.dumps(inspect(args.directory), indent=2))
