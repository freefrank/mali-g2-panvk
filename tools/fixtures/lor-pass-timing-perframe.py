import re, sys, collections
secs = []
for line in open(sys.argv[1]):
    if line.startswith('device'):
        secs.append({'drv': int(line.split('driver=')[1]), 'iv': []}); continue
    m = re.match(r'interval t=([\d.]+) secs=([\d.]+) lists=(\d+) passes=(\d+) total_ms=([\d.]+)', line)
    if m:
        secs[-1]['iv'].append({'t': float(m[1]), 'secs': float(m[2]), 'lists': int(m[3]), 'passes': int(m[4]), 'ms': float(m[5]), 'mode': None, 'rows': 0})
        continue
    m = re.match(r'pass ([SF]) .* n=(\d+) .* total_ms=([\d.]+) draws=(\d+)', line)
    if m:
        iv = secs[-1]['iv'][-1]; iv['mode'] = m[1]
for s in secs[-2:]:
    name = 'ours' if s['drv'] == 109457507 else 'vendor'
    for mode in 'FS':
        ivs = [i for i in s['iv'] if i['mode'] == mode and i['t'] >= 20]
        if not ivs: continue
        secs_ = sum(i['secs'] for i in ivs); lists = sum(i['lists'] for i in ivs); passes = sum(i['passes'] for i in ivs); ms = sum(i['ms'] for i in ivs)
        print('%-6s mode=%s intervals=%d lists/s=%.1f passes/list=%.1f pass_ms/list=%.2f wall_ms/list=%.2f  pass_time_share_of_wall=%.0f%%' % (
            name, mode, len(ivs), lists / secs_, passes / lists, ms / lists, secs_ * 1000 / lists, 100 * ms / (secs_ * 1000)))
