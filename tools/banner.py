#!/usr/bin/env python3
"""banner.py frame.ans out.svg - draw a VT screen (raw bytes, as tools/banner_frame
writes them) as an SVG, in the colours the terminal shows (tools/vtscreen.py).
As vms-vlmf's banner.py."""
import sys, html, os
sys.path.insert(0, os.path.dirname(__file__))
import vtscreen

src, out = sys.argv[1:3]
s = vtscreen.Screen(24, 80)
s.feed(open(src, encoding='latin-1').read())
FG = {'.': '#c0c0c0', 'w': '#ffffff', 'y': '#ffff55', 'c': '#00aaaa', 'C': '#55ffff', '#': '#000000', '=': '#000000', '!': '#ffffff', '?': '#c0c0c0', ' ': '#c0c0c0'}
BG = {'#': '#00aaaa', '=': '#c0c0c0', '!': '#aa0000'}
cw, ch = 9.6, 19
W, H = int(80 * cw) + 24, int(24 * ch) + 24
o = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" font-family="DejaVu Sans Mono, Menlo, Consolas, monospace" font-size="16">',
     f'<rect width="{W}" height="{H}" rx="8" fill="#0000aa"/>']
for r in range(24):
    for c in range(80):
        b = BG.get(s.col[r][c])
        if b:
            o.append(f'<rect x="{12 + c * cw:.1f}" y="{12 + r * ch}" width="{cw + 0.4:.1f}" height="{ch}" fill="{b}"/>')
for r in range(24):
    c = 0
    while c < 80:
        k = s.col[r][c]
        e = c
        while e < 80 and s.col[r][e] == k:
            e += 1
        seg = ''.join(s.ch[r][c:e])
        if seg.strip():
            o.append(f'<text x="{12 + c * cw:.1f}" y="{12 + r * ch + 15}" fill="{FG.get(k, "#c0c0c0")}" xml:space="preserve" textLength="{(e - c) * cw:.1f}" lengthAdjust="spacingAndGlyphs">{html.escape(seg)}</text>')
        c = e
o.append('</svg>')
open(out, 'w').write('\n'.join(o))
print(out)
