import re, sys, collections
path = sys.argv[1]
mode = sys.argv[2] if len(sys.argv) > 2 else 'S'
secs = []
for line in open(path):
    if line.startswith('device'):
        secs.append({'drv': int(line.split('driver=')[1]), 'iv': []}); continue
    if line.startswith('interval'):
        m = re.match(r'interval t=([\d.]+) secs=([\d.]+) lists=(\d+) passes=(\d+) total_ms=([\d.]+)', line)
        secs[-1]['iv'].append({'t': float(m[1]), 'secs': float(m[2]), 'rows': {}})
        continue
    m = re.match(r'pass ([SF]) (.*) n=(\d+) avg_us=([\d.]+) max_us=([\d.]+) total_ms=([\d.]+) draws=(\d+)', line)
    if m and m[1] == mode:
        secs[-1]['iv'][-1]['rows'][m[2]] = (int(m[3]), float(m[6]), int(m[7]))
new = secs[-2:]  # newest run: vendor then ours (or the reverse)
res = {}
for s in new:
    name = 'ours' if s['drv'] == 109457507 else 'vendor'
    tot = collections.defaultdict(lambda: [0, 0.0, 0])
    nint = 0
    for i in s['iv']:
        if i['t'] < 20 or not i['rows']:
            continue
        nint += 1
        for k, (n, ms, dr) in i['rows'].items():
            t = tot[k]; t[0] += n; t[1] += ms; t[2] += dr
    res[name] = tot
    print('%s intervals(%s)=%d passes=%d total_ms=%.0f' % (name, mode, nint, sum(v[0] for v in tot.values()), sum(v[1] for v in tot.values())))
o, v = res['ours'], res['vendor']
print('%-40s | %6s %9s | %6s %9s | %5s' % ('pass', 'o_n', 'o_avg_us', 'v_n', 'v_avg_us', 'o/v'))
rows = []
for k in set(o) | set(v):
    a, b = o.get(k), v.get(k)
    if a and b and a[0] >= 20 and b[0] >= 20:
        rows.append((a[1] / a[0] * a[0] - b[1] / b[0] * a[0], k, a, b))  # excess ms if ours had ours count
for ex, k, a, b in sorted(rows, reverse=True)[:22]:
    print('%-40s | %6d %9.0f | %6d %9.0f | %5.2f  excess_ms/ours_pass_count=%.0f' % (k[:40], a[0], a[1] * 1000 / a[0], b[0], b[1] * 1000 / b[0], (a[1] / a[0]) / (b[1] / b[0]), ex))
