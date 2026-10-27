import argparse
import json
from pathlib import Path

from isa_reference import ISA


class Generator:
    def __init__(self):
        self.old = ISA(14)
        self.new = ISA(15)

    def literal(self, value):
        return f'UINT64_C({value & ((1 << 64) - 1):#x})'

    def expression(self, expr, resolver, isa):
        if expr.get('identifier') is not None:
            return resolver(expr.get('identifier'))
        if expr.get('literal') is not None:
            return self.literal(isa.number(expr.get('literal'),
                                           expr.get('type', expr.get('enum'))))
        op = expr.get('operator')
        args = [self.expression(e, resolver, isa) for e in expr]
        binary = {'bitwise_or': '|', 'bitwise_and': '&', 'bitwise_xor': '^',
                  'lshift': '<<', 'rshift': '>>', 'add': '+', 'sub': '-',
                  'equal': '==', 'not_equal': '!=', 'greater_equal': '>=',
                  'greater_than': '>', 'less_than': '<', 'logical_and': '&&'}
        if op in binary:
            return f'({args[0]} {binary[op]} {args[1]})'
        if op == 'if':
            return f'({args[0]} ? {args[1]} : {args[2]})'
        if op == 'slice':
            high, low = [self.new.number(e.get('literal')) for e in list(expr)[1:]]
            return f'(({args[0]} >> {low}) & {self.literal((1 << (high - low + 1)) - 1)})'
        if op == 'inside':
            return '(' + ' || '.join(f'({args[0]} == {a})' for a in args[1:]) + ')'
        if op == 'countones':
            return f'((uint64_t)__builtin_popcountll({args[0]}))'
        if op == 'mask':
            return f'((UINT64_C(1) << {args[0]}) - 1)'
        raise ValueError(('unknown expression', op))

    def mapping(self, source, target):
        old_defs = {f.get('name'): f for f in source.findall('field') + source.findall('virtual')}
        new_defs = {f.get('name'): f for f in target.findall('field') + target.findall('virtual')}
        cache = {}

        def raw_old(key):
            f = old_defs[key]
            if f.tag == 'virtual':
                expr = f.find('expression')
                if expr is None:
                    raise ValueError(('missing old virtual expression', key))
                return self.expression(expr, raw_old, self.old)
            pos, width = int(f.get('pos')), int(f.get('width'))
            value = f'((w >> {pos}) & {self.literal((1 << width) - 1)})'
            if f.get('type') in ('int', 'pc_rel_label_signed'):
                value = f'((uint64_t)(((int64_t)({value} << {64 - width})) >> {64 - width}))'
            if f.get('modifier', '').startswith('shr('):
                value = f'({value} << {int(f.get("modifier")[4:-1])})'
            return value

        def mapped(key):
            if key in cache:
                return cache[key]
            nf = new_defs[key]
            old_key = key
            if key not in old_defs and key in ('dest_width_narrow', 'dest_width_wide'):
                old_key = 'dest_width'
            if nf.get('exact') is not None:
                value = self.literal(self.new.number(nf.get('exact'), nf.get('type')))
            elif old_key in old_defs:
                of = old_defs[old_key]
                value = raw_old(old_key)
                if of.get('type', '').startswith('SourceEncoding'):
                    value = (f'(({value} & UINT64_C(63)) | '
                             f'(({value} & UINT64_C(128)) ? '
                             f'(({value} & UINT64_C(64)) ? UINT64_C(448) : UINT64_C(256)) : '
                             f'(({value} & UINT64_C(64)) ? UINT64_C(128) : UINT64_C(0))))')
                oe = self.old.enums.get(of.get('type'))
                ne = self.new.enums.get(nf.get('type'))
                if oe is not None and ne is not None and any(ne.get(k) != n for k, n in oe.items()):
                    branches = []
                    for name, old_n in oe.items():
                        if name in ne:
                            branches.append(f'({value} == {self.literal(old_n)}) ? {self.literal(ne[name])} : ')
                    value = '(' + ''.join(branches) + 'UINT64_MAX)'
            elif nf.find('expression') is not None:
                value = self.expression(nf.find('expression'), mapped, self.new)
            elif nf.get('default') is not None:
                value = self.literal(self.new.number(nf.get('default'), nf.get('type')))
            else:
                raise ValueError(('unmapped field', key))
            cache[key] = value
            return value

        return mapped

    def generate(self):
        lines = ['#include <stdbool.h>', '#include <stdint.h>',
                 'bool va_v15_encode_from_valhall(uint64_t w, uint64_t *out);',
                 'bool va_v15_encode_from_valhall(uint64_t w, uint64_t *out)', '{']
        supported = []
        unsupported = []
        for mask, value, source in self.old.patterns:
            key = source.get('name'), source.get('variant', '')
            lines.append(f'   if ((w & {self.literal(mask)}) == {self.literal(value)}) {{')
            try:
                target = self.new.instructions[key]
                resolver = self.mapping(source, target)
                body = ['      uint64_t encoded = 0;']
                for idx, f in enumerate(target.findall('field')):
                    name = f.get('name')
                    expression = resolver(name)
                    width, pos = int(f.get('width')), int(f.get('pos'))
                    var = f'field_{idx}'
                    body.append(f'      uint64_t {var} = {expression};')
                    if f.get('modifier', '').startswith('shr('):
                        shift = int(f.get('modifier')[4:-1])
                        body.append(f'      if ({var} & {self.literal((1 << shift) - 1)}) return false;')
                        body.append(f'      {var} >>= {shift};')
                    if f.get('type') not in ('int', 'pc_rel_label_signed'):
                        body.append(f'      if ({var} > {self.literal((1 << width) - 1)}) return false;')
                    if f.get('max') is not None:
                        body.append(f'      if ({var} > {f.get("max")}) return false;')
                    if f.get('restrict'):
                        allowed = [self.new.number(n, f.get('type')) for n in f.get('restrict').split()]
                        test = ' && '.join(f'{var} != {v}' for v in allowed)
                        body.append(f'      if ({test}) return false;')
                    body.append(f'      encoded |= ({var} & {self.literal((1 << width) - 1)}) << {pos};')
                body.extend(['      *out = encoded;', '      return true;'])
                lines += body
                supported.append(key)
            except (KeyError, ValueError) as error:
                lines.append('      return false;')
                unsupported.append({'instruction': key, 'reason': str(error)})
            lines.append('   }')
        lines += ['   return false;', '}']
        return '\n'.join(lines) + '\n', {'supported': supported, 'unsupported': unsupported}


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('output', type=Path)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    code, report = Generator().generate()
    args.output.write_text(code)
    if args.report:
        args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(f"Supported {len(report['supported'])}, unsupported {len(report['unsupported'])}")
