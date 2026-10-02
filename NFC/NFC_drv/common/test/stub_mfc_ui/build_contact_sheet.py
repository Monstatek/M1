#!/usr/bin/env python3
"""
build_contact_sheet.py

Builds artifacts/nfc2-005-review/NFC2-005-screen-review.png: a labeled
contact sheet of the 9 required NFC2-005 MFC dictionary-UI renders, each
already 4x nearest-neighbor upscaled (512x256) by
nfc_mfc_dict_ui_render_test.c's own dump_ppm(). This script does NOT re-scale
or resample those pixels at all -- it reads each raw/*.ppm byte-for-byte and
blits it verbatim into the contact-sheet canvas, so the hard pixel edges from
the original 4x nearest-neighbor upscale are exactly preserved (no blur, no
smoothing introduced by this step).

Labels are placed in a title strip ABOVE each tile (never overlaid on the
screen pixels), rendered with a small hand-encoded 3x5 monospace bitmap font
defined below -- a test-tooling utility for the review sheet's own labels,
not a substitute for any firmware screen content (the "no hand-drawn
approximations" rule in the task brief is about the SCREEN CONTENT itself,
which comes entirely from the real, unmodified production draw code; this
font never touches that).

No PIL/Pillow (confirmed not installed) and no ImageMagick (`convert`/
`magick`, confirmed not on PATH) -- stdlib only: parses P6 PPM by hand and
writes a raw, uncompressed-per-row-filter PNG via zlib + manual chunk/CRC32
framing.

Run from the repository root:
    python3 NFC/NFC_drv/common/test/stub_mfc_ui/build_contact_sheet.py
"""
import os
import struct
import zlib
from pathlib import Path

REPO = Path(__file__).resolve().parents[5]
RAW_DIR = str(REPO / "artifacts/nfc2-005-review/raw")
OUT_PATH = str(REPO / "artifacts/nfc2-005-review/NFC2-005-screen-review.png")

# (ppm basename, title strip text) -- order defines the 3x3 grid layout.
TILES = [
    ("mfc-user-dictionary",     "USER DICTIONARY"),
    ("mfc-builtin-keys",        "BUILTIN KEYS"),
    ("mfc-system-dictionary",   "SYSTEM DICTIONARY 843/2511"),
    ("mfc-lost-tag",            "LOST TAG"),
    ("mfc-exit-confirm",        "EXIT CONFIRM"),
    ("mfc-partial-result",      "PARTIAL RESULT"),
    ("mfc-partial-actions",     "PARTIAL ACTIONS"),
    ("mfc-partial-info",        "PARTIAL INFO"),
    ("mfc-find-missing-system", "FIND MISSING KEYS (SYSTEM)"),
]
GRID_COLS = 3

# ---------------------------------------------------------------------------
# Hand-encoded 3x5 monospace bitmap font (original to this script -- see the
# header comment). Each glyph is 5 rows of a 3-char '1'/'0' string.
# ---------------------------------------------------------------------------
FONT_3X5 = {
    'A': ["010", "101", "111", "101", "101"],
    'B': ["110", "101", "110", "101", "110"],
    'C': ["011", "100", "100", "100", "011"],
    'D': ["110", "101", "101", "101", "110"],
    'E': ["111", "100", "110", "100", "111"],
    'F': ["111", "100", "110", "100", "100"],
    'G': ["011", "100", "101", "101", "011"],
    'I': ["111", "010", "010", "010", "111"],
    'K': ["101", "101", "110", "101", "101"],
    'L': ["100", "100", "100", "100", "111"],
    'M': ["101", "111", "111", "101", "101"],
    'N': ["101", "111", "111", "111", "101"],
    'O': ["010", "101", "101", "101", "010"],
    'P': ["110", "101", "110", "100", "100"],
    'R': ["110", "101", "110", "101", "101"],
    'S': ["011", "100", "010", "001", "110"],
    'T': ["111", "010", "010", "010", "010"],
    'U': ["101", "101", "101", "101", "011"],
    'X': ["101", "101", "010", "101", "101"],
    'Y': ["101", "101", "010", "010", "010"],
    '0': ["010", "101", "101", "101", "010"],
    '1': ["010", "110", "010", "010", "111"],
    '2': ["110", "001", "010", "100", "111"],
    '3': ["110", "001", "010", "001", "110"],
    '4': ["101", "101", "111", "001", "001"],
    '5': ["111", "100", "110", "001", "110"],
    '8': ["010", "101", "010", "101", "010"],
    ' ': ["000", "000", "000", "000", "000"],
    '-': ["000", "000", "111", "000", "000"],
    '/': ["001", "001", "010", "100", "100"],
    '(': ["010", "100", "100", "100", "010"],
    ')': ["010", "001", "001", "001", "010"],
}
GLYPH_W, GLYPH_H = 3, 5


def draw_text(canvas, cw, x0, y0, text, scale, color):
    """Blits `text` into `canvas` (flat RGB bytearray, width cw) at (x0,y0),
    each font pixel drawn as a scale x scale block, 1 glyph-column of spacing
    between characters (also x`scale`)."""
    x = x0
    for ch in text:
        glyph = FONT_3X5.get(ch, FONT_3X5[' '])
        for gy in range(GLYPH_H):
            row = glyph[gy]
            for gx in range(GLYPH_W):
                if row[gx] == '1':
                    for sy in range(scale):
                        py = y0 + gy * scale + sy
                        for sx in range(scale):
                            px = x + gx * scale + sx
                            off = (py * cw + px) * 3
                            canvas[off + 0] = color[0]
                            canvas[off + 1] = color[1]
                            canvas[off + 2] = color[2]
        x += (GLYPH_W + 1) * scale


def text_width(text, scale):
    return len(text) * (GLYPH_W + 1) * scale


# ---------------------------------------------------------------------------
# Minimal P6 (binary PPM) reader.
# ---------------------------------------------------------------------------
def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    assert data[:2] == b"P6", f"{path}: not a P6 PPM"
    idx = 2

    def read_token(data, idx):
        while data[idx] in b" \t\r\n":
            idx += 1
        if data[idx:idx + 1] == b'#':
            while data[idx] not in b"\r\n":
                idx += 1
            return read_token(data, idx)
        start = idx
        while data[idx] not in b" \t\r\n":
            idx += 1
        return data[start:idx], idx

    w_tok, idx = read_token(data, idx); w = int(w_tok)
    h_tok, idx = read_token(data, idx); h = int(h_tok)
    maxv_tok, idx = read_token(data, idx); maxv = int(maxv_tok)
    assert maxv == 255
    idx += 1  # single whitespace byte separating header from pixel data
    pixels = data[idx:idx + w * h * 3]
    assert len(pixels) == w * h * 3, f"{path}: truncated pixel data"
    return w, h, bytearray(pixels)


# ---------------------------------------------------------------------------
# Minimal PNG writer (8-bit RGB, no interlace, filter type 0 per row).
# ---------------------------------------------------------------------------
def write_png(path, width, height, rgb):
    def chunk(tag, payload):
        return (struct.pack(">I", len(payload)) + tag + payload +
                struct.pack(">I", zlib.crc32(tag + payload) & 0xffffffff))

    sig = b"\x89PNG\r\n\x1a\n"
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)  # color type 2 = RGB

    raw = bytearray()
    stride = width * 3
    for y in range(height):
        raw.append(0)  # filter type 0 (None) for every scanline
        raw.extend(rgb[y * stride:(y + 1) * stride])
    idat = zlib.compress(bytes(raw), 9)

    with open(path, "wb") as f:
        f.write(sig)
        f.write(chunk(b"IHDR", ihdr))
        f.write(chunk(b"IDAT", idat))
        f.write(chunk(b"IEND", b""))


def main():
    tiles = []
    for base, label in TILES:
        w, h, px = read_ppm(os.path.join(RAW_DIR, base + ".ppm"))
        tiles.append((base, label, w, h, px))

    tile_w = tiles[0][2]
    tile_h = tiles[0][3]
    for base, _label, w, h, _px in tiles:
        assert (w, h) == (tile_w, tile_h), f"{base}: unexpected size {w}x{h}"

    rows = (len(tiles) + GRID_COLS - 1) // GRID_COLS
    label_scale = 3
    title_h = GLYPH_H * label_scale + 16   # strip above each tile, text vertically padded
    margin = 16
    gap = 24

    canvas_w = margin * 2 + GRID_COLS * tile_w + (GRID_COLS - 1) * gap
    canvas_h = margin * 2 + rows * (title_h + tile_h) + (rows - 1) * gap

    canvas = bytearray(b"\xff" * (canvas_w * canvas_h * 3))  # white background

    for i, (base, label, w, h, px) in enumerate(tiles):
        col = i % GRID_COLS
        row = i // GRID_COLS
        x0 = margin + col * (tile_w + gap)
        y0 = margin + row * (title_h + tile_h + gap)

        # Title strip text, left-aligned to the tile (outside the 512x256
        # frame -- never overlaid on the screen pixels below it).
        tw = text_width(label, label_scale)
        tx = x0 + max(0, (tile_w - tw) // 2)
        ty = y0 + 8
        draw_text(canvas, canvas_w, tx, ty, label, label_scale, (0, 0, 0))

        # Thin border around the image tile for visual separation.
        img_y0 = y0 + title_h
        for bx in range(tile_w):
            for by in (0, tile_h - 1):
                off = ((img_y0 + by) * canvas_w + (x0 + bx)) * 3
                canvas[off:off + 3] = b"\x00\x00\x00"
        for by in range(tile_h):
            for bx in (0, tile_w - 1):
                off = ((img_y0 + by) * canvas_w + (x0 + bx)) * 3
                canvas[off:off + 3] = b"\x00\x00\x00"

        # Verbatim blit of the already-4x-upscaled PPM pixels -- no resampling.
        for y in range(h):
            src_off = y * w * 3
            dst_off = ((img_y0 + y) * canvas_w + x0) * 3
            canvas[dst_off:dst_off + w * 3] = px[src_off:src_off + w * 3]

    write_png(OUT_PATH, canvas_w, canvas_h, canvas)
    print(f"wrote {OUT_PATH} ({canvas_w}x{canvas_h})")


if __name__ == "__main__":
    main()
