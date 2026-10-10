#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Make the Mocky site for GitHub Pages (https://charlesvestal.github.io/fm1-mocky/):

  index.html                    what Mocky is, how to use it, and the ways in: send, install, download, source
  img/                          screenshots (docs/img, from tools/gen_screens.sh)
  app/                          the page that sends pictures (web/index.html, web/mocky.js)
  install/index.html            the web installer (web/mocky_installer.html, with fm1pkg.js, fm1ota.js and
                                the package's metadata inlined; Chrome or Edge, Web MIDI)
  firmware/mocky-VERSION.fwsc   the package the installer writes; also the download

  tools/make_pages.py build/mocky-0.9.fwsc 0.9 OUT_DIR

The package must be a release build (./build.sh --release X.Y): its identity, FM-1_1XXYYZZ, is what
the installer checks the download against and what the FM-1 reports after the install."""
import html
import json
import re
import shutil
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SRC / "web"))
from make_site import product_of, strip_module  # noqa: E402  (Felucca's: the package format)

REPO = "https://github.com/charlesvestal/fm1-mocky"

LANDING = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Mocky</title>
<meta name="description" content="Mocky shows your pictures on the M-VAVE FM-1's screen, sent from the browser: for checking screen mockups on the real hardware.">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Barlow:wght@400;600&family=Barlow+Semi+Condensed:wght@600;700&display=swap">
<style>
:root { --ground: #faf4e8; --ink: #48392f; --muted: #8a7764; --rule: #e6dccb; --accent: #e0683a; --teal: #26a69a; --card: #fffaf1; color-scheme: light; }
@media (prefers-color-scheme: dark) {
  :root:not([data-theme="light"]) { --ground: #1c1814; --ink: #f2e8d8; --muted: #b3a28e; --rule: #3a322a; --accent: #f28a5a; --teal: #4cc3b6; --card: #26201b; color-scheme: dark; }
}
:root[data-theme="dark"] { --ground: #1c1814; --ink: #f2e8d8; --muted: #b3a28e; --rule: #3a322a; --accent: #f28a5a; --teal: #4cc3b6; --card: #26201b; color-scheme: dark; }
body { background: var(--ground); color: var(--ink); margin: 0; padding-inline: 16px; padding-block: 48px 64px;
       font: 400 17px/1.6 "Barlow", "Helvetica Neue", Arial, sans-serif; }
main { max-width: 42rem; margin: 0 auto; }
h1 { font: 700 3rem/1 "Barlow Semi Condensed", "Barlow", sans-serif; margin: 0 0 .5rem; color: var(--accent); }
h2 { font: 700 1.4rem "Barlow Semi Condensed", "Barlow", sans-serif; margin: 2.2rem 0 .6rem; }
.lede { font-size: 1.15rem; margin: 0 0 1.6rem; max-width: 36rem; }
.shots { display: grid; grid-template-columns: repeat(auto-fit, minmax(150px, 1fr)); gap: 12px; margin: 0 0 1.8rem; }
.shots img { width: 100%; height: auto; border-radius: 14px; image-rendering: pixelated; box-shadow: 0 1px 0 var(--rule), 0 6px 18px rgba(72, 57, 47, .12); }
.status { border: 1px solid var(--rule); background: var(--card); border-radius: 14px; padding: 12px 16px; margin: 0 0 1.6rem; }
.ways { display: grid; gap: 12px; margin: 0 0 1rem; }
.ways a { display: block; text-decoration: none; color: var(--ink); background: var(--card); border: 1px solid var(--rule);
          border-radius: 14px; padding: 14px 18px; }
.ways a:hover, .ways a:focus-visible { border-color: var(--accent); outline: none; }
.ways strong { font: 600 1.25rem "Barlow Semi Condensed", "Barlow", sans-serif; color: var(--accent); display: block; }
.ways span { color: var(--muted); font-size: .95rem; }
table { border-collapse: collapse; width: 100%; font-size: .95rem; }
td { border-top: 1px solid var(--rule); padding: 8px 10px 8px 0; vertical-align: top; }
td:first-child { font-weight: 600; white-space: nowrap; color: var(--teal); }
p.small { color: var(--muted); font-size: .9rem; }
a { color: var(--accent); }
code { font-size: .9em; }
ul.pages { padding-left: 1.2rem; margin: 0 0 1rem; }
ul.pages li { margin: 0 0 .4rem; }
ul.pages b { color: var(--teal); }
.video { position: relative; aspect-ratio: 16 / 9; max-width: 100%; margin: 0 0 1.4rem; border-radius: 14px; overflow: hidden; background: #000; }
.video iframe { position: absolute; inset: 0; width: 100%; height: 100%; border: 0; }
</style>
</head>
<body>
<main>
<h1>Mocky</h1>
<p class="lede">Pictures on the FM-1's screen, sent from your browser. Drop a screen mockup on the page and
it's on the FM-1 a moment later, exactly as the screen will show it: check the colours, the sizes, and how the
text reads at arm's length, on the real thing.</p>
<div class="shots">
  <img src="img/welcome.png" width="240" height="240" alt="Mocky's welcome screen on the FM-1">
  <img src="img/step.png" width="240" height="240" alt="A FoMni-1 screen shown by Mocky, the note 1 / 3 on its bottom edge">
  <img src="img/slideshow.png" width="240" height="240" alt="An AMB-1 screen shown by Mocky in a slideshow">
</div>
<div class="status"><strong>Version __VERSION__.</strong> It installs and
uninstalls the way Felucca and X0X do, and the installer can put M-VAVE's own firmware back. Installing is at your own risk.</div>
<nav class="ways" aria-label="Get Mocky">
  <a href="app/"><strong>Send pictures</strong><span>Chrome or Edge, with the FM-1 on USB running Mocky. Drop, choose or paste a picture.</span></a>
  <a href="install/"><strong>Install</strong><span>From Chrome or Edge, with the FM-1 connected by USB. Nothing to install on the computer.</span></a>
  <a href="firmware/__PKG__"><strong>Download __PKG__</strong><span>For the command-line installer: <code>python3 tools/fm1_install.py __PKG__</code></span></a>
  <a href="__REPO__"><strong>Source</strong><span>GitHub, GPL-3.0. Built on Felucca by Hügelton Instruments.</span></a>
</nav>

<h2>Sending</h2>
<table>
<tr><td>Any size</td><td>Pictures are scaled to the screen's 240 × 240: Fit (with a colour behind), Fill (cropped), Stretch, or 1:1. A picture made at 240 × 240 arrives pixel for pixel.</td></tr>
<tr><td>True colours</td><td>The page shows what the screen will show (the screen's 16-bit colour). Dither for photos and gradients, if you like.</td></tr>
<tr><td>Keep</td><td>Kept pictures stay on the FM-1 when it's switched off: 276 KB of room, up to 64 pictures. A flat mockup takes a few KB, a photo about 116 KB.</td></tr>
<tr><td>Several</td><td>Drop several pictures at once and they're sent in order: a whole flow, ready to step through.</td></tr>
<tr><td>Watch a file</td><td>Pick a file and the page sends it again each time it changes. Export from your design tool over the same file and the FM-1 follows.</td></tr>
</table>

<h2>On the FM-1</h2>
<table>
<tr><td>White keys</td><td>Pictures 1–16. Their lights show which are kept; the one on the screen blinks.</td></tr>
<tr><td>OCT− · OCT+</td><td>Previous, next. SELECT, ALGORITHM, PRESETS and KNOB 1–3 step too.</td></tr>
<tr><td>PLAY</td><td>Slideshow on and off. KNOB 4 sets the pace, 1 to 30 seconds a picture.</td></tr>
<tr><td>SAVE</td><td>Keeps a picture sent without "Keep" (SAVE blinks while there is one).</td></tr>
<tr><td>HOME</td><td>Which picture this is, and how much room is left.</td></tr>
</table>
<p>Mocky starts on the picture you looked at last.</p>

<p class="small">To go back to the official
firmware, use the installer's "Back to the stock firmware", or M-VAVE's updater, M-UPGRADE, from
<a href="https://www.m-vave.com/download">m-vave.com/download</a>. M-VAVE and FM-1 are
trademarks of their owners. Mocky is not affiliated with them.</p>
</main>
</body>
</html>
"""


def main(pkg, version, out):
    pkg, out = Path(pkg), Path(out)
    raw = pkg.read_bytes()
    product = product_of(raw)
    if not re.fullmatch(r"FM-1_1\d{6}", product):
        raise SystemExit(f"{pkg}: identity {product!r}: make a release build (./build.sh --release X.Y)")
    if b"FELUCCA-LOADER-1" not in raw:
        raise SystemExit(f"{pkg}: no update loader in it")
    name = f"mocky-{re.sub(r'[^A-Za-z0-9.-]', '-', version)}.fwsc"
    if out.exists():
        shutil.rmtree(out)
    for d in ("install", "firmware"):
        (out / d).mkdir(parents=True)
    shutil.copy(pkg, out / "firmware" / name)
    page = (SRC / "web" / "mocky_installer.html").read_text(encoding="utf-8")
    lib = strip_module((SRC / "web" / "fm1pkg.js").read_text(encoding="utf-8")) + "\n" + \
        strip_module((SRC / "web" / "fm1ota.js").read_text(encoding="utf-8"))
    meta = json.dumps({"version": version, "product": product, "pkg": "../firmware/" + name})
    for mark in ("/*LIB*/", "/*META*/"):
        if page.count(mark) != 1:
            raise SystemExit(f"mocky_installer.html must contain {mark} once")
    (out / "install" / "index.html").write_text(page.replace("/*LIB*/", lib).replace("/*META*/", meta), encoding="utf-8")
    shutil.copytree(SRC / "docs" / "img", out / "img")
    (out / "index.html").write_text(LANDING.replace("__VERSION__", html.escape(version)).replace("__PKG__", name)
                                    .replace("__REPO__", REPO), encoding="utf-8")
    (out / "app").mkdir()
    for f in ("index.html", "mocky.js"):
        shutil.copy(SRC / "web" / f, out / "app" / f)
    (out / ".nojekyll").write_text("")
    print(f"site: {out}: index.html, app/, install/ ({product}), firmware/{name} ({len(raw)} B)")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    main(*sys.argv[1:4])
