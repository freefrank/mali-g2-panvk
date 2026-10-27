import argparse
import json
from pathlib import Path
import struct
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parent
REFERENCE = ROOT / 'isa'


def active(element, arch):
    return int(element.get('since', '0')) <= arch <= int(element.get('until', '255'))


class ISA:
    def __init__(self, arch):
        self.arch = arch
        name = 'isa-v15.xml' if arch == 15 else 'isa-v9-v14.xml'
        self.root = ET.parse(REFERENCE / name).getroot()
        self.enums = {
            e.get('name'): {v.get('name'): int(v.get('value'), 0)
                            for v in e.findall('value') if active(v, arch)}
            for e in self.root.findall('enum') if active(e, arch)
        }
        self.instructions = {}
        self.patterns = []
        for e in self.root.findall('instruction'):
            if not active(e, arch):
                continue
            key = e.get('name'), e.get('variant', '')
            if key in self.instructions:
                raise ValueError(('duplicate instruction', key))
            self.instructions[key] = e
            mask = value = 0
            for f in e:
                if f.tag not in ('field', 'reserved'):
                    continue
                exact = f.get('exact')
                if f.tag == 'reserved' and f.get('type') == 'zero':
                    exact = '0'
                if exact is None:
                    continue
                n = self.number(exact, f.get('type'))
                pos, width = int(f.get('pos')), int(f.get('width'))
                mask |= ((1 << width) - 1) << pos
                value |= n << pos
            self.patterns.append((mask, value, e))
        self.patterns.sort(key=lambda p: p[0].bit_count(), reverse=True)

    def number(self, value, kind=None):
        if isinstance(value, (bool, int)):
            return int(value)
        try:
            return int(value, 0)
        except ValueError:
            return self.enums[kind][value]

    def expression(self, e, values):
        if 'identifier' in e.attrib:
            return values[e.get('identifier')]
        if 'literal' in e.attrib:
            return self.number(e.get('literal'), e.get('type'))
        op = e.get('operator')
        children = list(e)
        if op == 'if':
            branch = 1 if self.expression(children[0], values) else 2
            return self.expression(children[branch], values)
        args = [self.expression(c, values) for c in children]
        if op == 'slice':
            x, high, low = args
            return (x >> low) & ((1 << (high - low + 1)) - 1)
        if op == 'mask':
            return (1 << args[0]) - 1
        if op == 'countones':
            return args[0].bit_count()
        a, b = args
        operations = {
            'bitwise_or': lambda: a | b,
            'bitwise_and': lambda: a & b,
            'bitwise_xor': lambda: a ^ b,
            'lshift': lambda: a << b,
            'rshift': lambda: a >> b,
            'add': lambda: a + b,
            'sub': lambda: a - b,
            'equal': lambda: a == b,
            'not_equal': lambda: a != b,
            'greater_equal': lambda: a >= b,
            'greater_than': lambda: a > b,
            'less_than': lambda: a < b,
            'logical_and': lambda: bool(a and b),
        }
        if op not in operations:
            raise ValueError(('unsupported expression', ET.tostring(e).decode()))
        return operations[op]()

    def decode(self, word):
        for mask, value, e in self.patterns:
            if word & mask == value:
                break
        else:
            raise ValueError(f'unknown instruction {word:016x}')
        values = {}
        for f in e.findall('field'):
            width = int(f.get('width'))
            n = (word >> int(f.get('pos'))) & ((1 << width) - 1)
            if f.get('type') in ('int', 'pc_rel_label_signed'):
                if n & (1 << (width - 1)):
                    n -= 1 << width
            modifier = f.get('modifier', '')
            if modifier.startswith('shr('):
                n <<= int(modifier[4:-1])
            values[f.get('name')] = n
        pending = list(e.findall('virtual'))
        while pending:
            progress = False
            for f in list(pending):
                expr = f.find('expression')
                if expr is None:
                    continue
                try:
                    values[f.get('name')] = self.expression(expr, values)
                except KeyError:
                    continue
                pending.remove(f)
                progress = True
            if not progress:
                raise ValueError(('unresolved virtuals', e.attrib, [x.attrib for x in pending]))
        return e, values

    def encode(self, name, variant='', **provided):
        e = self.instructions[name, variant]
        values = dict(provided)
        fields = list(e.findall('field'))
        definitions = {f.get('name'): f for f in fields + e.findall('virtual')}

        def resolve(key):
            if key in values:
                value = self.number(values[key], definitions[key].get('type'))
                values[key] = value
                return value
            f = definitions[key]
            if f.get('exact') is not None:
                value = self.number(f.get('exact'), f.get('type'))
            elif f.find('expression') is not None:
                expr = f.find('expression')
                for node in expr.iter('expression'):
                    dep = node.get('identifier')
                    if dep and dep not in values:
                        resolve(dep)
                value = self.expression(expr, values)
            elif f.get('default') is not None:
                value = self.number(f.get('default'), f.get('type'))
            else:
                raise ValueError(('missing field', name, variant, key))
            values[key] = value
            return value

        word = 0
        for f in fields:
            key = f.get('name')
            n = resolve(key)
            modifier = f.get('modifier', '')
            if modifier.startswith('shr('):
                shift = int(modifier[4:-1])
                if n & ((1 << shift) - 1):
                    raise ValueError(('unaligned field', name, key, n))
                n >>= shift
            width = int(f.get('width'))
            if not -(1 << (width - 1)) <= n < (1 << width):
                raise ValueError(('field range', name, key, n, width))
            if f.get('max') is not None and n > int(f.get('max')):
                raise ValueError(('field maximum', name, key, n))
            if f.get('restrict'):
                allowed = [self.number(s, f.get('type')) for s in f.get('restrict').split()]
                if n not in allowed:
                    raise ValueError(('restricted field', name, key, n))
            word |= (n & ((1 << width) - 1)) << int(f.get('pos'))
        return word

    def describe(self, word):
        e, values = self.decode(word)
        definitions = {f.get('name'): f for f in e.findall('field') + e.findall('virtual')}
        result = {'word': f'{word:016x}', 'name': e.get('name'), 'variant': e.get('variant', '')}
        fields = {}
        for key, n in values.items():
            f = definitions[key]
            if f.get('exact') is not None:
                continue
            enum = self.enums.get(f.get('type'), {})
            names = [k for k, v in enum.items() if v == n]
            fields[key] = names[0] if names else n
        result['fields'] = fields
        return result


def transcode_word(old, new, word, register_map=None):
    e, values = old.decode(word)
    target = new.instructions[e.get('name'), e.get('variant', '')]
    old_definitions = {f.get('name'): f for f in e.findall('field') + e.findall('virtual')}
    new_definitions = {f.get('name'): f for f in target.findall('field') + target.findall('virtual')}
    provided = {}
    for key, n in values.items():
        f = old_definitions[key]
        if f.get('exact') is not None:
            continue
        if key not in new_definitions:
            continue
        nf = new_definitions[key]
        if nf.get('exact') is not None:
            continue
        if f.get('type', '').startswith('SourceEncoding'):
            kind, index = n >> 6, n & 63
            if kind < 2 and register_map is not None:
                index = register_map[index]
            n = ((0, 128, 256, 448)[kind]) + index
        elif 'register_index' in key and register_map is not None:
            n = register_map[n]
        oe = old.enums.get(f.get('type'))
        ne = new.enums.get(nf.get('type'))
        if oe is not None and ne is not None:
            matches = [k for k, v in oe.items() if v == n and k in ne]
            if not matches:
                raise ValueError(('unmapped enum', key, n))
            n = ne[matches[0]]
        provided[key] = n
    for key in ('dest_width_narrow', 'dest_width_wide'):
        if key in new_definitions and key not in provided and 'dest_width' in values:
            old_enum = old.enums[old_definitions['dest_width'].get('type')]
            name = next(k for k, v in old_enum.items() if v == values['dest_width'])
            provided[key] = new.enums[new_definitions[key].get('type')][name]
    return new.encode(e.get('name'), e.get('variant', ''), **provided)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('command', choices=('decode', 'transcode', 'check'))
    parser.add_argument('binary', type=Path)
    parser.add_argument('--arch', type=int, default=15)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    data = args.binary.read_bytes()
    words = [w for w, in struct.iter_unpack('<Q', data)]
    isa = ISA(args.arch)
    if args.command == 'decode':
        print(json.dumps([isa.describe(w) for w in words if w], indent=2))
    elif args.command == 'check':
        for w in words:
            if not w:
                continue
            e, v = isa.decode(w)
            actual = isa.encode(e.get('name'), e.get('variant', ''), **v)
            if actual != w:
                raise ValueError(('roundtrip mismatch', hex(w), hex(actual)))
        print(f'PASS {len(words)} instruction words')
    else:
        old = ISA(14)
        output = [transcode_word(old, isa, w) if w else 0 for w in words]
        args.output.write_bytes(struct.pack('<' + 'Q' * len(output), *output))
        print(f'Transcoded {len(words)} instruction words')


if __name__ == '__main__':
    main()
