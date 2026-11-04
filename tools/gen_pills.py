#!/usr/bin/env python3
"""Writes the README's feature pills: docs/img/pills/<slug>.svg (rounded, GitHub-label style: a tinted fill, a
coloured border and text). Dark text on GitHub's light theme, the bright colour on its dark theme (CSS in the SVG
follows the viewer's colour scheme). Usage: tools/gen_pills.py"""
import os

# (label, colour): the app's accent palette
PILLS = [
    ("Instant on huge folders", "#e0af68"),
    ("Keyboard-first", "#7aa2f7"),
    ("Split panels + tabs", "#bb9af7"),
    ("Terminal tabs", "#9ece6a"),
    ("Disk usage map", "#ff9e64"),
    ("Git status", "#f7768e"),
    ("Compare + sync", "#7dcfff"),
    ("iPhone + Android", "#73daca"),
    ("Archives as folders", "#2ac3de"),
    ("Snapshots", "#c099ff"),
    ("Made with AI", "#ff79c6"),
]

def text_width(s, size=12.0):
    w = 0.0
    for c in s:
        if c in "il.,:;'|!": w += 0.28
        elif c in "fjrt() ": w += 0.36
        elif c in "mwMW": w += 0.86
        elif c.isupper() or c in "+&": w += 0.66
        else: w += 0.56
    return w * size

def darken(hex_, k):
    r, g, b = (int(hex_[i:i + 2], 16) for i in (1, 3, 5))
    return "#%02x%02x%02x" % (int(r * k), int(g * k), int(b * k))

def pill(label, col):
    tw = round(text_width(label))
    h, pad, dot = 26, 12, 8
    w = pad + dot + 7 + tw + pad
    dark_text = darken(col, 0.55)
    return f'''<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}" role="img" aria-label="{label}">
<style>
  .bg{{fill:{col};fill-opacity:.14;stroke:{col};stroke-opacity:.6}} .t{{fill:{dark_text}}} .d{{fill:{col}}}
  @media (prefers-color-scheme: dark){{ .bg{{fill-opacity:.16;stroke-opacity:.55}} .t{{fill:{col}}} }}
</style>
<rect class="bg" x=".5" y=".5" width="{w - 1}" height="{h - 1}" rx="{(h - 1) / 2}"/>
<circle class="d" cx="{pad + dot / 2}" cy="{h / 2}" r="{dot / 2}"/>
<text class="t" x="{pad + dot + 7}" y="{h / 2 + 4.2:.1f}" textLength="{tw}" lengthAdjust="spacingAndGlyphs"
 font-family="-apple-system,BlinkMacSystemFont,'Segoe UI','Noto Sans',Helvetica,Arial,sans-serif" font-size="12" font-weight="600">{label}</text>
</svg>
'''

def slug(label):
    return "-".join("".join(c if c.isalnum() else " " for c in label.lower()).split())

out = os.path.join(os.path.dirname(__file__), "..", "docs", "img", "pills")
os.makedirs(out, exist_ok=True)
for label, col in PILLS:
    with open(os.path.join(out, slug(label) + ".svg"), "w") as f:
        f.write(pill(label, col))
    print(slug(label))
