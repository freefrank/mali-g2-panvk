import re, sys, collections
kv = re.compile(r'"([^"]+)"=(-?\d+)')
sess = []
for line in open(sys.argv[1]):
    if line.startswith('device'):
        sess.append(collections.defaultdict(list)); continue
    m = re.match(r'pipe vs=(\w+) fs=(\w+) rts=(\d+) samples=(\d+) rt0=(\d+) exec=(\d+) stages=(\S+) name="([^"]+)"', line)
    if not m: continue
    key = m.groups()[:5]
    ex = int(m.group(6))
    lst = sess[-1][key]
    if ex == 0 or not lst:
        lst.append({})
    lst[-1][m.group(8)] = {k: int(v) for k, v in kv.findall(line[m.end():])}
V, P = sess
print('keys vendor %d panvk %d; variants vendor %d panvk %d' % (len(V), len(P), sum(map(len, V.values())), sum(map(len, P.values()))))
multi = [(k, len(V[k]), len(P[k])) for k in V if k in P and (len(V[k]) > 1 or len(P[k]) > 1)]
print('keys with >1 variant: %d; variant-count mismatches: %d' % (len(multi), sum(1 for k, a, b in multi if a != b)))
# pair by occurrence, fragment only
tv = tp = tvp = 0
rows = []
for k in V:
    if k not in P: continue
    for a, b in zip(V[k], P[k]):
        if 'Fragment shader' in a and 'fragment' in b:
            va = a['Fragment shader'].get('Arithmetic', 0)
            vp = a.get('Fragment pilot shader', {}).get('Arithmetic', 0)
            pa = b['fragment']['ALU']
            rows.append((pa - va - vp, k, va, vp, pa))
            tv += va; tvp += vp; tp += pa
print('paired fragment variants %d: vendor main %d + pilot %d = %d ; panvk %d ; ratio vs main %.2f, vs main+pilot %.2f' % (
    len(rows), tv, tvp, tv + tvp, tp, tp / tv, tp / (tv + tvp)))
import statistics
rat = sorted(r[4] / (r[2] + r[3]) for r in rows if r[2] + r[3])
print('ratio panvk/(main+pilot): p10 %.2f p50 %.2f p90 %.2f max %.2f' % (rat[len(rat)//10], rat[len(rat)//2], rat[len(rat)*9//10], rat[-1]))
print('worst:')
for d, k, va, vp, pa in sorted(rows, reverse=True)[:8]:
    print('  fs=%s rt0=%s vendor main %d pilot %d | panvk %d' % (k[1], k[4], va, vp, pa))
