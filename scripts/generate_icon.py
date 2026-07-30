"""Generate LogCater app icon: multi-resolution .ico file."""
from PIL import Image, ImageDraw
import math
import struct
import os

OUTPUT = os.path.join(os.path.dirname(__file__), "logcater.ico")
SIZES = [16, 32, 48, 64, 128, 256]

# Colors
BG_DARK   = (30,  30,  30,  255)  # #1E1E1E
GREEN     = (61,  220, 132, 255)  # #3DDC84 (Android green)
WHITE     = (220, 220, 225, 255)
GRAY      = (120, 120, 130, 255)
DIM_GRAY  = (70,  70,  78,  255)

def draw_icon(size):
    """Draw the LogCater icon at a given size. Returns a PIL Image (RGBA)."""
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    s = size  # shorthand

    # --- Background: rounded rectangle ---
    radius = max(2, int(s * 0.18))
    # Draw rounded rect corners manually with pies + rects
    draw.rounded_rectangle([0, 0, s - 1, s - 1], radius=radius, fill=BG_DARK)

    # Margins
    margin = int(s * 0.15)
    inner_s = s - 2 * margin

    if size >= 48:
        # --- Large icon: detailed design ---

        # Left vertical green accent bar
        bar_x0 = margin + int(inner_s * 0.08)
        bar_x1 = bar_x0 + max(3, int(s * 0.06))
        bar_y0 = margin
        bar_y1 = s - margin
        draw.rounded_rectangle([bar_x0, bar_y0, bar_x1, bar_y1],
                               radius=max(2, int(s * 0.03)), fill=GREEN)

        # Log lines to the right of the green bar
        line_start_x = bar_x1 + max(4, int(s * 0.07))
        line_end_area = s - margin - max(2, int(s * 0.04))
        line_height = max(1, int(s * 0.028))
        line_gap = max(1, int(s * 0.045))
        line_radius = max(1, line_height // 2)

        # Line 1: shorter (dim gray)
        l1_w = int(inner_s * 0.33)
        l1_y = margin + int(inner_s * 0.18)
        draw.rounded_rectangle(
            [line_start_x, l1_y, line_start_x + l1_w, l1_y + line_height],
            radius=line_radius, fill=GRAY)

        # Line 2: longest (white, main log entry)
        l2_w = int(inner_s * 0.55)
        l2_y = l1_y + line_gap + line_height
        draw.rounded_rectangle(
            [line_start_x, l2_y, line_start_x + l2_w, l2_y + line_height],
            radius=line_radius, fill=WHITE)

        # Line 3: medium (dim gray)
        l3_w = int(inner_s * 0.44)
        l3_y = l2_y + line_gap + line_height
        draw.rounded_rectangle(
            [line_start_x, l3_y, line_start_x + l3_w, l3_y + line_height],
            radius=line_radius, fill=GRAY)

        # Line 4: short (green, like a prompt / cursor)
        l4_w = int(inner_s * 0.2)
        l4_y = l3_y + line_gap + line_height
        draw.rounded_rectangle(
            [line_start_x, l4_y, line_start_x + l4_w, l4_y + line_height],
            radius=line_radius, fill=GREEN)

        # Small green dot at top-right (status indicator)
        dot_r = max(2, int(s * 0.045))
        dot_cx = s - margin - dot_r
        dot_cy = margin + dot_r
        draw.ellipse(
            [dot_cx - dot_r, dot_cy - dot_r, dot_cx + dot_r, dot_cy + dot_r],
            fill=GREEN)

        # Subtle bracket / prompt chevron near green bar
        chevron_x = bar_x0
        chevron_y = l2_y + line_height // 2
        chevron_size = max(2, int(s * 0.035))
        # Override: draw a tiny triangle on the green bar
        # Actually skip this for cleanliness — the bar + lines are enough

    elif size >= 32:
        # --- Medium icon: simplified ---
        # Green bar + 3 lines
        bar_x0 = margin + int(inner_s * 0.08)
        bar_x1 = bar_x0 + max(2, int(s * 0.07))
        draw.rounded_rectangle([bar_x0, margin, bar_x1, s - margin],
                               radius=max(1, int(s * 0.03)), fill=GREEN)

        lx = bar_x1 + max(3, int(s * 0.07))
        lw1 = int(inner_s * 0.38)
        lw2 = int(inner_s * 0.58)
        lw3 = int(inner_s * 0.46)
        lh = max(1, int(s * 0.04))
        lg = max(2, int(s * 0.06))
        lr = max(1, lh // 2)

        ly0 = margin + int(inner_s * 0.18)
        draw.rounded_rectangle([lx, ly0, lx + lw1, ly0 + lh], radius=lr, fill=GRAY)
        ly1 = ly0 + lg + lh
        draw.rounded_rectangle([lx, ly1, lx + lw2, ly1 + lh], radius=lr, fill=WHITE)
        ly2 = ly1 + lg + lh
        draw.rounded_rectangle([lx, ly2, lx + lw3, ly2 + lh], radius=lr, fill=GRAY)

        # Green dot
        dot_r = max(1, int(s * 0.06))
        draw.ellipse([s - margin - dot_r*2, margin, s - margin, margin + dot_r*2], fill=GREEN)

    else:
        # --- 16x16: ultra-minimal ---
        # Just a green vertical stripe + 2 horizontal bars
        bar_x = int(s * 0.22)
        bar_w = max(1, int(s * 0.12))
        draw.rectangle([bar_x, int(s * 0.18), bar_x + bar_w, s - int(s * 0.18)], fill=GREEN)

        lx = bar_x + bar_w + int(s * 0.1)
        lw = s - lx - int(s * 0.12)
        lh = max(1, int(s * 0.1))

        draw.rectangle([lx, int(s * 0.3), lx + lw, int(s * 0.3) + lh], fill=WHITE)
        draw.rectangle([lx, int(s * 0.55), lx + int(lw * 0.65), int(s * 0.55) + lh], fill=GRAY)

    return img


# --- Write multi-resolution .ico ---
# ICO format: header (6 bytes) + directory (16 bytes per image) + image data

def write_ico(images, path):
    """Write a multi-resolution .ico file.
    images: list of (size, PIL Image in RGBA or RGB)
    """
    # Sort by size ascending
    images.sort(key=lambda x: x[0])

    # We'll store PNG for 256x256, BMP for smaller sizes
    entries = []  # (size, raw_data)
    for size, img in images:
        if size >= 256:
            # PNG format for 256x256 (required by Windows for large icons)
            import io
            buf = io.BytesIO()
            img.save(buf, format="PNG")
            entries.append((size, buf.getvalue()))
        else:
            # BMP format with AND mask
            # Convert to RGBA then to BGRA for BMP
            if img.mode != "RGBA":
                img = img.convert("RGBA")
            w, h = img.size

            # DIB header (40 bytes) + pixel data (BGRA, bottom-up)
            # We use 32-bit BGRA
            pixels = []
            for y in range(h - 1, -1, -1):  # bottom-up
                for x in range(w):
                    r, g, b, a = img.getpixel((x, y))
                    pixels.append(struct.pack("BBBB", b, g, r, a))

            pixel_data = b"".join(pixels)

            # AND mask: 1 bit per pixel, rows padded to 4-byte boundary
            and_row_bytes = (w + 7) // 8
            and_row_padded = (and_row_bytes + 3) & ~3  # pad to 4 bytes
            and_mask = b"\x00" * (and_row_padded * h)  # all transparent (0 = opaque in AND mask)

            # XOR bitmap: DIB header (40 bytes) + pixel data
            dib_header = struct.pack(
                "<IiiHHIIiiII",
                40,        # biSize
                w,         # biWidth
                h * 2,     # biHeight (double for XOR + AND)
                1,         # biPlanes
                32,        # biBitCount
                0,         # biCompression (BI_RGB)
                len(pixel_data),
                0, 0, 0, 0
            )
            bmp_data = dib_header + pixel_data + and_mask
            entries.append((size, bmp_data))

    # ICO header
    header = struct.pack("<HHH", 0, 1, len(entries))  # reserved, type=ICO, count

    # Directory entries + image data
    dir_entries = b""
    image_data = b""
    offset = 6 + 16 * len(entries)  # header + directory

    for size, data in entries:
        w = size if size < 256 else 0  # 0 means 256
        h = size if size < 256 else 0
        bpp = 32
        img_size = len(data)
        dir_entries += struct.pack("<BBBBHHII",
            w, h, 0, 0,     # width, height, palette, reserved
            1,              # color planes
            bpp,            # bits per pixel
            img_size,       # image size
            offset          # offset to image data
        )
        offset += img_size
        image_data += data

    with open(path, "wb") as f:
        f.write(header + dir_entries + image_data)

    print(f"Written {path} ({len(entries)} sizes: {[s for s,_ in images]})")


# --- Generate ---
images = []
for size in SIZES:
    img = draw_icon(size)
    images.append((size, img))
    # Debug: save individual PNGs
    png_path = os.path.join(os.path.dirname(__file__), f"icon_{size}.png")
    img.save(png_path)
    print(f"  Generated {size}x{size}")

write_ico(images, OUTPUT)
print(f"\nDone! Icon saved to: {OUTPUT}")
