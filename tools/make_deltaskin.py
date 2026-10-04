#!/usr/bin/env python3
"""Makes the Smash&Clash controller skins for Delta (.deltaskin: a zip of info.json and art).

  python3 tools/make_deltaskin.py OUT_DIR

  * smashandclash-ds.deltaskin: the DS touch screen ALONE, big, like a GBA SP. The DS
    ROM keeps the whole game on its touch screen, so nothing is lost; taps still work.
  * smashandclash-gba.deltaskin: the same look for the GBA ROM's 240 x 160 screen.

The art is drawn here (Pillow) in the Arena Pop look of the web edition: the blue
world, candy buttons with a hard lip, Luckiest Guy and Lilita One. iPhone (notched and
classic) and iPad, portrait and landscape. Needs Python 3, Pillow; uses tools/cache/
(run make_assets.py first, it downloads the fonts and the logo).
"""
import io
import json
import os
import sys
import zipfile

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
CACHE = os.path.join(HERE, "cache")
S = 3  # the art is drawn at 3x the mapping size

WORLD_TOP, WORLD_BOT = (10, 190, 255), (1, 109, 203)
INK, INK_DEEP = (13, 26, 74), (6, 16, 47)
SUGAR, SUGAR2, SUGAR_EDGE = (255, 255, 255), (230, 240, 255), (180, 208, 240)
SUN1, SUN2, SUN_LIP = (255, 232, 114), (255, 194, 26), (217, 138, 0)
MINT1, MINT2, MINT_LIP = (139, 247, 207), (34, 211, 154), (14, 154, 108)
SKY1, SKY2, SKY_LIP = (94, 211, 255), (19, 152, 240), (10, 98, 184)
CHERRY1, CHERRY2, CHERRY_LIP = (255, 90, 90), (224, 32, 48), (158, 15, 24)

SYSTEMS = {
    "ds": {"id": "com.rileytestut.delta.game.ds", "input": {"x": 0, "y": 192, "width": 256, "height": 192}, "aspect": 4 / 3, "touch": True,
           "buttons": ["a", "b", "x", "y"], "labels": {"a": "A", "b": "B", "x": "X", "y": "Y"}},
    "gba": {"id": "com.rileytestut.delta.game.gba", "input": {"x": 0, "y": 0, "width": 240, "height": 160}, "aspect": 3 / 2, "touch": False,
            "buttons": ["a", "b"], "labels": {"a": "A", "b": "B"}},
}


def font(name, px):
    return ImageFont.truetype(os.path.join(CACHE, name), int(px))


def world(w, h):
    img = Image.new("RGB", (w, h))
    d = ImageDraw.Draw(img)
    for y in range(h):
        t = y / max(1, h - 1)
        d.line([(0, y), (w, y)], fill=tuple(round(a + (b - a) * t) for a, b in zip(WORLD_TOP, WORLD_BOT)))
    glow = Image.new("L", (w, h), 0)
    ImageDraw.Draw(glow).ellipse([-w * 0.3, -h * 0.45, w * 1.3, h * 0.35], fill=40)
    img.paste((255, 255, 255), (0, 0), glow)
    return img.convert("RGBA")


def candy(d, box, hi, base, lip, radius, label=None, fnt=None, on=INK):
    x0, y0, x1, y1 = box
    l = max(2 * S, int((y1 - y0) * 0.09))
    d.rounded_rectangle([x0, y0 + l, x1, y1], radius, fill=lip)
    d.rounded_rectangle([x0, y0, x1, y1 - l], radius, fill=base)
    d.rounded_rectangle([x0 + S, y0 + S, x1 - S, y0 + (y1 - l - y0) * 0.5], max(1, radius - S), fill=hi)
    if label:
        cx, cy = (x0 + x1) / 2, y0 + (y1 - l - y0) / 2
        d.text((cx, cy), label, font=fnt, fill=on, anchor="mm")


def disc(d, box, hi, base, lip, label, fnt, on=INK):
    x0, y0, x1, y1 = box
    l = max(2 * S, int((y1 - y0) * 0.08))
    d.ellipse([x0, y0 + l, x1, y1], fill=lip)
    d.ellipse([x0, y0, x1, y1 - l], fill=base)
    inset = (x1 - x0) * 0.12
    d.ellipse([x0 + inset, y0 + inset * 0.6, x1 - inset, y0 + (y1 - y0) * 0.5], fill=hi)
    d.text(((x0 + x1) / 2, y0 + (y1 - l - y0) / 2), label, font=fnt, fill=on, anchor="mm")


def dpad(d, box):
    x0, y0, x1, y1 = box
    w = x1 - x0
    a = w / 3
    l = int(w * 0.04)
    r = int(a * 0.28)
    for (fill, dy) in ((SUGAR_EDGE, l), (SUGAR, 0)):
        d.rounded_rectangle([x0 + a, y0 + dy, x0 + 2 * a, y1 - l + dy], r, fill=fill)
        d.rounded_rectangle([x0, y0 + a + dy, x1, y0 + 2 * a - l + dy], r, fill=fill)
    c = (x0 + w / 2, y0 + w / 2 - l / 2)
    t = a * 0.22
    for ang in range(4):  # little arrows
        pts = {0: [(c[0], y0 + a * 0.3), (c[0] - t, y0 + a * 0.3 + t), (c[0] + t, y0 + a * 0.3 + t)],
               1: [(x1 - a * 0.3, c[1]), (x1 - a * 0.3 - t, c[1] - t), (x1 - a * 0.3 - t, c[1] + t)],
               2: [(c[0], y1 - l - a * 0.3), (c[0] - t, y1 - l - a * 0.3 - t), (c[0] + t, y1 - l - a * 0.3 - t)],
               3: [(x0 + a * 0.3, c[1]), (x0 + a * 0.3 + t, c[1] - t), (x0 + a * 0.3 + t, c[1] + t)]}[ang]
        d.polygon(pts, fill=(74, 94, 156))


def bezel(img, d, box):
    """The ink frame around the game screen."""
    x0, y0, x1, y1 = box
    p = 7 * S
    d.rounded_rectangle([x0 - p, y0 - p + 3 * S, x1 + p, y1 + p + 3 * S], 10 * S, fill=INK_DEEP)
    d.rounded_rectangle([x0 - p, y0 - p, x1 + p, y1 + p], 10 * S, fill=INK)
    d.rectangle([x0, y0, x1, y1], fill=(0, 0, 0))


def logo(img, cx, cy, maxw, maxh):
    lg = Image.open(os.path.join(CACHE, "logo.png")).convert("RGBA")
    lg = lg.crop(lg.getbbox())
    s = min(maxw / lg.width, maxh / lg.height)
    lg = lg.resize((max(1, int(lg.width * s)), max(1, int(lg.height * s))), Image.LANCZOS)
    img.alpha_composite(lg, (int(cx - lg.width / 2), int(cy - lg.height / 2)))


def frame(x, y, w, h):
    return {"x": round(x), "y": round(y), "width": round(w), "height": round(h)}


def layout(system, mw, mh, landscape, ipad=False):
    """One representation: the art (at 3x) and the info.json part."""
    sysd = SYSTEMS[system]
    img = world(mw * S, mh * S)
    d = ImageDraw.Draw(img)
    items, edges = [], 10 if not ipad else 20
    big = font("LilitaOne-Regular.ttf", (26 if not ipad else 34) * S)
    small = font("LilitaOne-Regular.ttf", (13 if not ipad else 16) * S)

    if not landscape:
        side = 14 if not ipad else 64
        top = 52 if mh >= 800 and not ipad else 22 if not ipad else 60
        sw = mw - 2 * side
        sh = sw / sysd["aspect"]
        screen = (side, top, sw, sh)
        cy = top + sh + 16
        logo(img, mw / 2 * S, (cy + 22) * S, 150 * S if not ipad else 220 * S, 40 * S if not ipad else 56 * S)
        ctrl_top = cy + 54
        room = mh - ctrl_top - (30 if mh >= 800 else 14)
        u = min(room / 300, mw / 400) * (1.0 if not ipad else 1.15)
        ctrl_top += max(0, (room - 300 * u) / 2)  # the controls sit in the middle of what is left
        pad = 172 * u
        lr_y = ctrl_top
        lw, lh = 112 * u, 36 * u
        items.append(("l", (side, lr_y, lw, lh)))
        items.append(("r", (mw - side - lw, lr_y, lw, lh)))
        dp = (side + 6 * u, lr_y + lh + 24 * u, pad, pad)
        bc = (mw - side - 92 * u, dp[1] + pad / 2)
        bs = 66 * u
        pills_y = dp[1] + pad + 22 * u
    else:
        top = 10 if not ipad else 40
        sh = mh - 2 * top
        sw = sh * sysd["aspect"]
        if sw > mw * 0.62:
            sw = mw * 0.62
            sh = sw / sysd["aspect"]
        side_w = (mw - sw) / 2
        screen = ((mw - sw) / 2, (mh - sh) / 2, sw, sh)
        u = min(side_w / 200, mh / 330) * (1.0 if not ipad else 1.1)
        pad = 160 * u
        lw, lh = 112 * u, 36 * u
        items.append(("l", ((side_w - lw) / 2, 16 * u, lw, lh)))
        items.append(("r", (mw - side_w + (side_w - lw) / 2, 16 * u, lw, lh)))
        dp = ((side_w - pad) / 2, mh / 2 - pad / 2 + 10 * u, pad, pad)
        bc = (mw - side_w / 2, mh / 2 + 10 * u)
        bs = 62 * u
        pills_y = mh - 44 * u
        logo(img, side_w / 2 * S, (mh - 34 * u) * S, (side_w - 30) * S, 34 * u * S)

    # the screen
    x, y, w, h = screen
    bezel(img, d, (x * S, y * S, (x + w) * S, (y + h) * S))
    screens = [{"inputFrame": sysd["input"], "outputFrame": frame(x, y, w, h)}]
    if sysd["touch"]:
        items.append(({"x": "touchScreenX", "y": "touchScreenY"}, (x, y, w, h)))

    # L and R
    for key, (bx, by, bw, bh) in [it for it in items if it[0] in ("l", "r")]:
        candy(d, (bx * S, by * S, (bx + bw) * S, (by + bh) * S), SUGAR, SUGAR2, SUGAR_EDGE, 12 * S, key.upper(), small)

    # the D-pad
    dx, dy, dw, dh = dp
    dpad(d, (dx * S, dy * S, (dx + dw) * S, (dy + dh) * S))
    items.append(({"up": "up", "down": "down", "left": "left", "right": "right"}, (dx, dy, dw, dh)))

    # the face buttons: A sun (the one primary), B sugar, X sky, Y mint
    styles = {"a": (SUN1, SUN2, SUN_LIP), "b": (SUGAR, SUGAR2, SUGAR_EDGE), "x": (SKY1, SKY2, SKY_LIP), "y": (MINT1, MINT2, MINT_LIP)}
    cx, cy2 = bc
    off = bs * 0.78
    if system == "ds":
        spots = {"x": (cx, cy2 - off), "a": (cx + off, cy2), "b": (cx, cy2 + off), "y": (cx - off, cy2)}
    else:
        spots = {"a": (cx + off * 0.62, cy2 - off * 0.45), "b": (cx - off * 0.62, cy2 + off * 0.45)}
        bs *= 1.15
    for key in sysd["buttons"]:
        px, py = spots[key]
        box = (px - bs / 2, py - bs / 2, bs, bs)
        hi, base, lip = styles[key]
        disc(d, ((px - bs / 2) * S, (py - bs / 2) * S, (px + bs / 2) * S, (py + bs / 2) * S), hi, base, lip, sysd["labels"][key], big)
        items.append(([key], box))

    # SELECT, START and Delta's menu, as candy pills
    pw, ph = 74 * u, 26 * u
    if not landscape:
        row = [("select", "SELECT"), ("start", "START"), ("menu", "MENU")]
        total = 3 * pw + 2 * 14 * u
        px0 = (mw - total) / 2
        for i, (key, label) in enumerate(row):
            bx = px0 + i * (pw + 14 * u)
            fill = (SKY1, SKY2, SKY_LIP) if key == "menu" else (SUGAR, SUGAR2, SUGAR_EDGE)
            candy(d, (bx * S, pills_y * S, (bx + pw) * S, (pills_y + ph) * S), *fill, 13 * S, label, small)
            items.append(([key], (bx, pills_y, pw, ph)))
    else:
        left = [("menu", "MENU"), ("select", "SELECT")]
        for i, (key, label) in enumerate(left):
            bx = (side_w - pw) / 2
            by = 16 * u + lh + 16 * u + i * (ph + 10 * u)
            fill = (SKY1, SKY2, SKY_LIP) if key == "menu" else (SUGAR, SUGAR2, SUGAR_EDGE)
            candy(d, (bx * S, by * S, (bx + pw) * S, (by + ph) * S), *fill, 13 * S, label, small)
            items.append(([key], (bx, by, pw, ph)))
        bx = mw - side_w + (side_w - pw) / 2
        by = 16 * u + lh + 16 * u
        candy(d, (bx * S, by * S, (bx + pw) * S, (by + ph) * S), SUGAR, SUGAR2, SUGAR_EDGE, 13 * S, "START", small)
        items.append((["start"], (bx, by, pw, ph)))

    rep = {
        "assets": {},
        "items": [{"inputs": k, "frame": frame(*f), **({} if isinstance(k, dict) and "x" in k else {})} for k, f in items],
        "screens": screens,
        "mappingSize": {"width": mw, "height": mh},
        "extendedEdges": {"top": edges, "bottom": edges, "left": edges, "right": edges},
    }
    return img.convert("RGB"), rep


def build(system, out):
    reps = {"iphone": {}, "ipad": {}}
    files = {}
    plan = [
        ("iphone", "edgeToEdge", "portrait", 414, 896, False),
        ("iphone", "edgeToEdge", "landscape", 896, 414, True),
        ("iphone", "standard", "portrait", 375, 667, False),
        ("iphone", "standard", "landscape", 667, 375, True),
        ("ipad", "standard", "portrait", 768, 1024, False),
        ("ipad", "standard", "landscape", 1024, 768, True),
    ]
    for device, kind, orient, mw, mh, land in plan:
        img, rep = layout(system, mw, mh, land, ipad=device == "ipad")
        name = f"{device}_{kind.lower()}_{orient}.png"
        buf = io.BytesIO()
        img.save(buf, "PNG", optimize=True)
        files[name] = buf.getvalue()
        rep["assets"] = {"large": name} if device == "iphone" else {"small": name, "medium": name, "large": name}
        reps[device].setdefault(kind, {})[orient] = rep
    info = {
        "name": "Smash&Clash" + (" (one screen)" if system == "ds" else ""),
        "identifier": f"in.smashandclash.delta.{system}",
        "gameTypeIdentifier": SYSTEMS[system]["id"],
        "debug": False,
        "representations": reps,
    }
    path = os.path.join(out, f"smashandclash-{system}.deltaskin")
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("info.json", json.dumps(info, indent=2))
        for name, data in files.items():
            z.writestr(name, data)
    # previews for the README
    for name in ("iphone_edgetoedge_portrait.png", "iphone_edgetoedge_landscape.png"):
        with open(os.path.join(out, f"preview-{system}-{name.split('_')[-1]}"), "wb") as f:
            f.write(files[name])
    print("wrote", path)


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "build/delta"
    os.makedirs(out, exist_ok=True)
    for system in ("ds", "gba"):
        build(system, out)
