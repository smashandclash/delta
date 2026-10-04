#!/usr/bin/env python3
"""Shows a game frame inside a Delta skin, the way Delta lays it out (for the README).

  python3 tools/skin_mockup.py SKIN.deltaskin FRAME.png OUT.png [iphone/edgeToEdge/portrait]

FRAME is an emulator screenshot: 256 x 384 (both DS screens) or 240 x 160 (GBA), any
integer scale. The skin's info.json says which part of it shows where.
"""
import io
import json
import sys
import zipfile

from PIL import Image


def main():
    skin, frame_path, out = sys.argv[1:4]
    device, kind, orient = (sys.argv[4] if len(sys.argv) > 4 else "iphone/edgeToEdge/portrait").split("/")
    z = zipfile.ZipFile(skin)
    info = json.loads(z.read("info.json"))
    rep = info["representations"][device][kind][orient]
    art = Image.open(io.BytesIO(z.read(rep["assets"]["large"]))).convert("RGB")
    mw, mh = rep["mappingSize"]["width"], rep["mappingSize"]["height"]
    s = art.width / mw
    frame = Image.open(frame_path).convert("RGB")
    native_w = 256 if "ds" in info["gameTypeIdentifier"] else 240
    k = frame.width // native_w
    for sc in rep["screens"]:
        i, o = sc["inputFrame"], sc["outputFrame"]
        part = frame.crop((i["x"] * k, i["y"] * k, (i["x"] + i["width"]) * k, (i["y"] + i["height"]) * k))
        part = part.resize((round(o["width"] * s), round(o["height"] * s)), Image.NEAREST)
        art.paste(part, (round(o["x"] * s), round(o["y"] * s)))
    art.save(out)
    print("wrote", out)


if __name__ == "__main__":
    main()
