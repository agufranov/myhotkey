# Generates assets/myhotkey.ico. The icon is code, not a binary blob: change a
# colour here, rerun, rebuild.
#
# The picture: a keycap with two swap arrows - remapping in one glyph. It is
# drawn on a 1024-unit grid and rendered per size, because one design does not
# survive 16 px: small renders drop the keycap lip, fatten the arrows and let
# the cap fill the canvas, otherwise the tray icon turns to mush.
#
#   python assets/make_icon.py [preview]
#
# "preview" also writes preview.png next to the .ico - every size on a light
# and a dark strip, which is how the tray and the taskbar will actually show it.

import io
import os
import struct
import sys

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))

# palette
TOP_A = (0x63, 0x7D, 0xFF)   # top face gradient, upper
TOP_B = (0x37, 0x51, 0xE6)   # top face gradient, lower
LIP   = (0x22, 0x34, 0xA6)   # keycap side, visible at the bottom
GLYPH = (0xFF, 0xFF, 0xFF)

SIZES = [16, 20, 24, 32, 48, 64, 128, 256]


def vgrad(w, h, top, bot):
    strip = Image.new("RGB", (1, h))
    for y in range(h):
        t = y / max(1, h - 1)
        strip.putpixel((0, y), tuple(round(top[i] + (bot[i] - top[i]) * t) for i in range(3)))
    return strip.resize((w, h))


def arrow(draw, cx, cy, length, thick, head_w, head_len, to_right):
    x0, x1 = cx - length / 2, cx + length / 2
    if to_right:
        shaft = (x0, cy - thick / 2, x1 - head_len, cy + thick / 2)
        head = [(x1, cy), (x1 - head_len, cy - head_w / 2), (x1 - head_len, cy + head_w / 2)]
    else:
        shaft = (x0 + head_len, cy - thick / 2, x1, cy + thick / 2)
        head = [(x0, cy), (x0 + head_len, cy - head_w / 2), (x0 + head_len, cy + head_w / 2)]
    draw.rounded_rectangle(shaft, radius=thick / 2, fill=GLYPH)
    draw.polygon(head, fill=GLYPH)


def render(size):
    C = 1024
    small = size <= 24
    mid = size <= 48

    margin = 16 if small else (56 if mid else 84)
    radius = 300 if small else 230
    lip = 0 if small else (72 if mid else 88)

    img = Image.new("RGBA", (C, C), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    # keycap: full body in the side colour, then the top face laid over it,
    # stopping short of the bottom - the strip left showing is the 3D lip
    d.rounded_rectangle((margin, margin, C - margin, C - margin), radius=radius, fill=LIP)

    face_box = (margin, margin, C - margin, C - margin - lip)
    face_h = face_box[3] - face_box[1]
    mask = Image.new("L", (C, C), 0)
    ImageDraw.Draw(mask).rounded_rectangle(face_box, radius=radius, fill=255)
    grad = Image.new("RGBA", (C, C))
    grad.paste(vgrad(C, face_h, TOP_A, TOP_B), (0, face_box[1]))
    img.paste(grad, (0, 0), mask)

    d = ImageDraw.Draw(img)
    cx = C / 2
    cy = (face_box[1] + face_box[3]) / 2

    if small:
        thick, length, head_w, head_len, row = 150, 640, 330, 250, 130
    elif mid:
        thick, length, head_w, head_len, row = 118, 560, 260, 205, 105
    else:
        thick, length, head_w, head_len, row = 96, 520, 225, 185, 95

    arrow(d, cx, cy - row, length, thick, head_w, head_len, to_right=True)
    arrow(d, cx, cy + row, length, thick, head_w, head_len, to_right=False)

    return img.resize((size, size), Image.LANCZOS)


# ---------------------------------------------------------------- ico file --

def bmp_entry(img):
    """Classic BGRA entry. Sizes up to 64 are stored this way because every
    icon consumer alive understands it; PNG entries stay for the big ones."""
    w, h = img.size
    px = img.convert("RGBA").load()
    rows = bytearray()
    for y in range(h - 1, -1, -1):
        for x in range(w):
            r, g, b, a = px[x, y]
            rows += struct.pack("<BBBB", b, g, r, a)
    mask_stride = ((w + 31) // 32) * 4
    mask = b"\x00" * (mask_stride * h)
    header = struct.pack("<IiiHHIIiiII", 40, w, h * 2, 1, 32, 0,
                         len(rows) + len(mask), 0, 0, 0, 0)
    return header + bytes(rows) + mask


def png_entry(img):
    buf = io.BytesIO()
    img.save(buf, "PNG")
    return buf.getvalue()


def write_ico(path, images):
    entries = []
    for img in images:
        data = bmp_entry(img) if img.width <= 64 else png_entry(img)
        entries.append((img.width, data))

    out = bytearray(struct.pack("<HHH", 0, 1, len(entries)))
    offset = 6 + 16 * len(entries)
    for w, data in entries:
        b = 0 if w >= 256 else w
        out += struct.pack("<BBBBHHII", b, b, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
    for _, data in entries:
        out += data
    with open(path, "wb") as f:
        f.write(out)


def write_preview(path, images):
    pad = 12
    W = sum(i.width + pad for i in images) + pad
    H = max(i.width for i in images) + 2 * pad
    canvas = Image.new("RGBA", (W, H * 2))
    ImageDraw.Draw(canvas).rectangle((0, 0, W, H), fill=(0xF2, 0xF2, 0xF2))
    ImageDraw.Draw(canvas).rectangle((0, H, W, H * 2), fill=(0x1E, 0x1E, 0x1E))
    x = pad
    for img in images:
        for half in (0, 1):
            y = half * H + (H - img.height) // 2
            canvas.paste(img, (x, y), img)
        x += img.width + pad
    canvas.save(path)


def main():
    images = [render(s) for s in sorted(SIZES, reverse=True)]
    ico = os.path.join(HERE, "myhotkey.ico")
    write_ico(ico, images)
    print("written", ico, os.path.getsize(ico), "bytes")
    if "preview" in sys.argv:
        p = os.path.join(HERE, "preview.png")
        write_preview(p, images)
        print("written", p)


if __name__ == "__main__":
    main()
