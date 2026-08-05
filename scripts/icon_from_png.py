"""Convert an AI-generated square PNG into LogCater's multi-resolution .ico.

The source is expected to be a rounded-square app-icon design on a white
canvas. White regions connected to the border are made transparent so the
icon gets clean rounded corners, then the result is downscaled into
16/32/48/64/128/256 .ico entries plus the README logo.

Usage:
    python scripts/icon_from_png.py <source.png> [--dewatermark]

--dewatermark removes a bottom-right corner watermark (e.g. Doubao) by
mirroring the clean bottom-left corner over it.
"""
import io
import os
import struct
import sys
from collections import deque

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ICO_OUT = os.path.join(ROOT, "src", "logcater.ico")
LOGO_OUT = os.path.join(ROOT, "assets", "logo.png")
PREVIEW_OUT = os.path.join(ROOT, "scripts", "icon_ai_preview.png")
SIZES = [16, 32, 48, 64, 128, 256]
WHITE_THRESHOLD = 238


def strip_white_corners(img):
    """Make border-connected near-white pixels transparent (rounded corners)."""
    w, h = img.size
    px = img.load()

    def iswhite(p):
        return p[0] >= WHITE_THRESHOLD and p[1] >= WHITE_THRESHOLD and p[2] >= WHITE_THRESHOLD

    alpha = bytearray(b"\xff") * (w * h)
    q = deque()
    seen = set()

    for x in range(w):
        for y in (0, h - 1):
            if iswhite(px[x, y]):
                q.append((x, y))
    for y in range(h):
        for x in (0, w - 1):
            if iswhite(px[x, y]):
                q.append((x, y))

    while q:
        x, y = q.popleft()
        if (x, y) in seen:
            continue
        seen.add((x, y))
        alpha[y * w + x] = 0
        for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
            if 0 <= nx < w and 0 <= ny < h and (nx, ny) not in seen and iswhite(px[nx, ny]):
                q.append((nx, ny))

    out = img.convert("RGBA")
    out.putalpha(Image.frombytes("L", (w, h), bytes(alpha)))
    return out


def remove_corner_watermark(img, margin=200, top=150):
    """Overwrite the bottom-right corner with the mirrored bottom-left corner.

    Both corners share the same vertical background gradient and rounded-edge
    geometry, and the left corner is watermark-free, so this removes a
    bottom-right watermark seamlessly.
    """
    w, h = img.size
    src = img.crop((0, h - top, margin, h))
    src = src.transpose(Image.FLIP_LEFT_RIGHT)
    img.paste(src, (w - margin, h - top))
    return img


def write_ico(images, path):
    images.sort(key=lambda x: x[0])
    entries = []
    for size, img in images:
        if size >= 256:
            buf = io.BytesIO()
            img.save(buf, format="PNG")
            entries.append((size, buf.getvalue()))
        else:
            w = h = size
            pixels = []
            for y in range(h - 1, -1, -1):
                for x in range(w):
                    r, g, b, a = img.getpixel((x, y))
                    pixels.append(struct.pack("BBBB", b, g, r, a))
            pixel_data = b"".join(pixels)
            and_row_bytes = (w + 7) // 8
            and_row_padded = (and_row_bytes + 3) & ~3
            and_mask = b"\x00" * (and_row_padded * h)
            dib = struct.pack("<IiiHHIIiiII", 40, w, h * 2, 1, 32, 0,
                              len(pixel_data), 0, 0, 0, 0)
            entries.append((size, dib + pixel_data + and_mask))

    header = struct.pack("<HHH", 0, 1, len(entries))
    dir_entries = b""
    image_data = b""
    offset = 6 + 16 * len(entries)
    for size, data in entries:
        dim = 0 if size >= 256 else size
        dir_entries += struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32,
                                   len(data), offset)
        offset += len(data)
        image_data += data
    with open(path, "wb") as f:
        f.write(header + dir_entries + image_data)
    print(f"Written {path} ({len(entries)} sizes: {[s for s, _ in images]})")


def main():
    if len(sys.argv) < 2:
        raise SystemExit("Usage: python scripts/icon_from_png.py <source.png>")
    source = sys.argv[1]
    base = Image.open(source).convert("RGB")
    if "--dewatermark" in sys.argv:
        base = remove_corner_watermark(base)
        print("Applied bottom-right watermark removal.")
    base = strip_white_corners(base)
    print(f"Source: {source} ({base.size[0]}x{base.size[1]})")

    images = []
    for size in SIZES:
        img = base.resize((size, size), Image.LANCZOS)
        images.append((size, img))
    write_ico(images, ICO_OUT)

    base.resize((256, 256), Image.LANCZOS).save(LOGO_OUT)
    base.resize((512, 512), Image.LANCZOS).save(PREVIEW_OUT)
    print(f"Logo -> {LOGO_OUT}, preview -> {PREVIEW_OUT}")


if __name__ == "__main__":
    main()
