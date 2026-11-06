import re, sys, collections
secs = []
for line in open(sys.argv[1]):
    if line.startswith('device'):
        secs.append({'drv': int(line.split('driver=')[1]), 'iv': []}); continue
    if line.startswith('interval'):
        m = re.match(r'interval t=([\d.]+) secs=([\d.]+) lists=(\d+) passes=(\d+) total_ms=([\d.]+)', line)
        secs[-1]['iv'].append({'t': float(m[1]), 'secs': float(m[2]), 'lists': int(m[3]), 'passes': int(m[4]), 'ms': float(m[5]), 'rows': {}})
        continue
    m = re.match(r'pass (.*) n=(\d+) avg_us=([\d.]+) max_us=([\d.]+) total_ms=([\d.]+) draws=(\d+)', line)
    if m:
        secs[-1]['iv'][-1]['rows'][m[1]] = (int(m[2]), float(m[5]), int(m[6]))
name = {109457507: 'ours', 234881024: 'vendor'}
agg = {}
for s in secs:
    d = name.get(s['drv'], str(s['drv']))
    ivs = [i for i in s['iv'] if i['t'] >= 20]  # skip warm-up
    tot = collections.defaultdict(lambda: [0, 0.0, 0])
    secs_sum = sum(i['secs'] for i in ivs)
    lists = sum(i['lists'] for i in ivs)
    for i in ivs:
        for k, (n, ms, dr) in i['rows'].items():
            t = tot[k]; t[0] += n; t[1] += ms; t[2] += dr
    agg[d] = (tot, secs_sum, lists, len(ivs))
    print('%s: intervals=%d secs=%.0f lists=%d passes=%d pass_ms_total=%.0f (%.1f ms/s, %.2f ms per list)' % (
        d, len(ivs), secs_sum, lists, sum(v[0] for v in tot.values()), sum(v[1] for v in tot.values()),
        sum(v[1] for v in tot.values()) / secs_sum, sum(v[1] for v in tot.values()) / max(lists, 1)))
o, v = agg['ours'], agg['vendor']
print()
print('%-46s | %8s %8s %6s | %8s %8s %6s | %5s' % ('pass key', 'o_n/s', 'o_avg_us', 'o_dr', 'v_n/s', 'v_avg_us', 'v_dr', 'ratio'))
keys = sorted(set(o[0]) | set(v[0]), key=lambda k: -(o[0][k][1] if k in o[0] else 0))
for k in keys[:25]:
    a = o[0].get(k); b = v[0].get(k)
    def f(x, s):
        if not x: return ('-', '-', '-')
        return ('%.1f' % (x[0] / s), '%.0f' % (x[1] * 1000 / x[0]), '%.0f' % (x[2] / x[0]))
    fa, fb = f(a, o[1]), f(b, v[1])
    ratio = '%.2f' % ((a[1] / a[0]) / (b[1] / b[0])) if a and b else '-'
    print('%-46s | %8s %8s %6s | %8s %8s %6s | %5s' % (k[:46], *fa, *fb, ratio))
