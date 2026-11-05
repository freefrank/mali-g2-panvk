import re, sys, collections
path = sys.argv[1]
sess = []
kv = re.compile(r'"([^"]+)"=(-?\d+)')
for line in open(path):
    if line.startswith('device'):
        sess.append(collections.defaultdict(dict)); continue
    m = re.match(r'pipe vs=(\w+) fs=(\w+) rts=(\d+) samples=(\d+) rt0=(\d+) exec=(\d+) stages=(\S+) name="([^"]+)"', line)
    if not m: continue
    vs, fs, rts, smp, rt0, ex, st, name = m.groups()
    sess[-1][(vs, fs, rts, smp, rt0)][name] = {k: int(v) for k, v in kv.findall(line[m.end():])}
V, P = sess
# map vendor stat -> panvk stat
MAP = [('Arithmetic', 'ALU'), ('Arithmetic FMA (Valhall only)', 'FMA'), ('Arithmetic CVT (Valhall only)', 'CVT'),
       ('Arithmetic SFU (Valhall only)', 'SFU'), ('Load Store', 'Load/store'), ('Varying', 'Varying'), ('Texture', 'Texture')]
common = sorted(set(V) & set(P))
rows = []
for k in common:
    a, b = V[k].get('Fragment shader'), P[k].get('fragment')
    if a and b: rows.append((k, a, b))
# dedupe by fs hash only (rt formats multiply the same shader)
by_fs = {}
for k, a, b in rows: by_fs.setdefault(k[1], (k, a, b))
print('pipelines compared %d, unique fragment shaders %d' % (len(rows), len(by_fs)))
tv = collections.Counter(); tp = collections.Counter()
for k, a, b in by_fs.values():
    for va, pa in MAP:
        tv[va] += a.get(va, 0); tp[va] += b.get(pa, 0)
print('%-32s %9s %9s %6s' % ('stat (sum over unique fs)', 'vendor', 'panvk', 'ratio'))
for va, pa in MAP:
    print('%-32s %9d %9d %6.2f' % (va, tv[va], tp[va], tp[va] / tv[va] if tv[va] else 0))
r = collections.Counter()
for k, a, b in by_fs.values():
    r['vendor regs>32'] += a.get('Registers allocated', 0) > 32
    r['panvk threads=1'] += b.get('Threads', 2) == 1
    r['panvk spills>0'] += b.get('Spills', 0) > 0
    r['vendor early'] += a.get('Early fragment test', 0) > 0
    r['vendor side effects'] += a.get('Side effects', 0) > 0
    r['vendor depth write'] += a.get('Depth write', 0) > 0
    r['vendor coverage'] += a.get('Coverage', 0) > 0
    r['panvk loops>0'] += b.get('Loops', 0) > 0
print(dict(r))
rat = sorted(((b['ALU'] / a['Arithmetic'] if a.get('Arithmetic') else 0, k, a, b) for k, a, b in by_fs.values()), reverse=True)
print('\nratio distribution (panvk ALU / vendor Arithmetic):')
vals = sorted(x[0] for x in rat if x[0])
for q in (0.1, 0.25, 0.5, 0.75, 0.9, 1.0):
    print('  p%d %.2f' % (q * 100, vals[min(len(vals) - 1, int(q * len(vals)))]))
print('\nworst 15 by absolute ALU gap:')
for k, a, b in sorted(by_fs.values(), key=lambda x: x[2]['ALU'] - x[1].get('Arithmetic', 0), reverse=True)[:15]:
    print(' fs=%s  ALU v=%4d p=%4d | FMA %4d/%4d CVT %4d/%4d SFU %3d/%3d | LS %3d/%3d TEX %2d/%2d | regs v=%d p=%d thr=%d loops=%d spills=%d | v:early=%d side=%d dw=%d' % (
        k[1], a.get('Arithmetic', 0), b['ALU'], a.get('Arithmetic FMA (Valhall only)', 0), b['FMA'],
        a.get('Arithmetic CVT (Valhall only)', 0), b['CVT'], a.get('Arithmetic SFU (Valhall only)', 0), b['SFU'],
        a.get('Load Store', 0), b['Load/store'], a.get('Texture', 0), b['Texture'],
        a.get('Registers allocated', 0), b['Registers used'], b['Threads'], b['Loops'], b['Spills'],
        a.get('Early fragment test', 0), a.get('Side effects', 0), a.get('Depth write', 0)))
# panvk-only / vendor-only pipelines
print('\nvendor pipelines without panvk match:', len(set(V) - set(P)), ' panvk without vendor:', len(set(P) - set(V)))
# usage-weighted proxy is not available; list vendor early-ZS = 0 but no side effects etc.
