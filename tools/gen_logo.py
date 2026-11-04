#!/usr/bin/env python3
"""App logo: writes packaging/dev.fileexplorer.svg and prints the two SVG strings for src/icons.c.
Geometry traced from the original artwork (624x456)."""
import sys

def shadow(dx):  # front's right edge shifted by dx, clipped to the back folder
    return (f"M205 115H{288.1+dx:.1f}L{385.6+dx:.1f} 239.3Q{400.4+dx:.1f} 258.2 {385+dx:.1f} 276.6"
            f"L{269+dx:.1f} 415.6Q{256.2+dx:.1f} 431 {236+dx:.1f} 431H205Z")

FRONT = "M31 55Q31 25 61 25H196Q217.6 25 231.2 42.3L385.6 239.3Q400.4 258.2 385 276.6L269 415.6Q256.2 431 236 431H61Q31 431 31 401Z"
CHEVRON = "M248 215H285L319 262L285 309H248L282 262Z"          # as drawn in the original
CHEVRON_SMALL = "M223 187H281L330 258L281 329H223L272 258Z"     # ~1.5x, bolder, centred on the tip (small sizes)

def svg(chev, view="0 0 624 456", size='width="624" height="456"'):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" {size} viewBox="{view}"><defs>'
            '<linearGradient id="b" x1="205" y1="115" x2="592" y2="431" gradientUnits="userSpaceOnUse">'
            '<stop offset="0" stop-color="#2566DE"/><stop offset="1" stop-color="#193C96"/></linearGradient>'
            '<linearGradient id="f" x1="31" y1="25" x2="380" y2="431" gradientUnits="userSpaceOnUse">'
            '<stop offset="0" stop-color="#4CC8FF"/><stop offset="1" stop-color="#2D7BE8"/></linearGradient></defs>'
            '<rect x="205" y="115" width="387" height="316" rx="28" fill="url(#b)"/>'
            + "".join(f'<path d="{shadow(dx)}" fill="#0A1E5C" opacity="{op}"/>' for dx, op in ((13, .08), (8, .12), (4, .18)))
            + f'<path d="{FRONT}" fill="url(#f)"/><path d="{chev}" fill="#FFFFFF"/></svg>')

if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--files":
        open("/tmp/logo/logo.svg", "w").write(svg(CHEVRON))
        open("/tmp/logo/logo_small.svg", "w").write(svg(CHEVRON_SMALL))
    else:
        open("packaging/dev.fileexplorer.svg", "w").write(svg(CHEVRON, "0 -84 624 624", 'width="256" height="256"') + "\n")
        for name, chev in (("g_logo_svg", CHEVRON), ("g_logo_small_svg", CHEVRON_SMALL)):
            print(f'{name}\t{svg(chev)}')
