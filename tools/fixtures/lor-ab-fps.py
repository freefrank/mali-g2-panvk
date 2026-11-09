import re, sys, glob, os, statistics
for f in sorted(glob.glob(os.path.join(sys.argv[1], '[0-9]-*.log'))):
    pts = []
    for l in open(f, errors='replace'):
        m = re.match(r'\[\s*([0-9.]+) \S+\] \[info\]\s+renderer: texinfo spec frame=(\d+) map=(\S+) battle=(\d)', l)
        if m:
            pts.append((float(m[1]), int(m[2]), m[3]))
    scene = [p for p in pts if p[2] != '-']
    segs = []
    for a, b in zip(scene, scene[1:]):
        if b[0] - a[0] > 1:
            segs.append((b[1] - a[1]) / (b[0] - a[0]))
    allscene = (scene[-1][1] - scene[0][1]) / (scene[-1][0] - scene[0][0]) if len(scene) > 1 else 0
    print('%-24s scene_points=%d span=%.0fs avg=%.1f fps  per-10s: %s' % (
        os.path.basename(f), len(scene), (scene[-1][0] - scene[0][0]) if scene else 0, allscene,
        ' '.join('%.1f' % s for s in segs)))
