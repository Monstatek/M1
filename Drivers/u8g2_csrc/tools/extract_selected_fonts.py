#!/usr/bin/env python3
"""
extract_selected_fonts.py

Generates Drivers/u8g2_csrc/u8g2_fonts_selected.c as a BYTE-EXACT, VERBATIM
extraction of specific font array definitions copied out of the upstream
u8g2 project's full font catalog (u8g2_fonts.c, ~38 MB / ~1987 fonts). No
retyping, no editing of the extracted byte data -- same "extract, don't
transcribe" technique already established in this repo for
NFC/NFC_drv/common/test/stub_desfire_ui/extract_desfire_ui.py and
stub_mfc_ui/extract_mfc_dict_ui.py.

WHY this exists: u8g2_fonts.c is never modified or deleted by this script --
it stays in the tree, compiled by nothing, as the CMakeLists.txt no longer
lists it as a build source. This preserves upstream licensing/provenance
(the full catalog, with its original file structure, is still present for
audit or for adding a new font later) while removing it from the normal
build graph: compiling the full file alone measured ~58 seconds and a
~13.5 MB single-TU object file on this toolchain, dwarfing the rest of the
firmware's build time, even though --gc-sections already discards every
unused font at link time (U8G2_FONT_SECTION places each font in its own
named section) -- so this change is a build-time / repository-tooling
improvement, not a FLASH size optimization; FLASH usage is expected to be
unchanged (confirmed by the render-equivalence + size measurement this
script's caller performs after generating the output).

Every font actually linked into the current firmware was identified from
the real linked ELF (arm-none-eabi-nm on a fresh release build), not
guessed from source references -- see the parent audit's own report for
the full cross-check (54 font names referenced in source; 41 of those are
comment-only alternatives/notes in m1_display.h, never compiled; the 13
below are the only ones a real build ever links).

Provenance: the upstream u8g2 project is "Universal 8bit Graphics Library"
(https://github.com/olikraus/u8g2/), Copyright (c) 2016, olikraus@gmail.com,
BSD-2-Clause-style license (full text embedded below and reproduced in the
generated output's banner). No U8G2_VERSION macro, CHANGELOG, or other
version/commit marker exists anywhere in this repository's vendored
Drivers/u8g2_csrc/ tree (checked: grep for VERSION-like macros across every
vendored .h, and a directory listing for CHANGELOG/*.md/*.txt -- none
found), so the precise upstream commit this catalog was vendored from is
NOT discoverable from in-tree evidence; only the copyright year (2016) and
the project URL are available. This is reported honestly rather than
guessed.

Regenerate with (from the repository root):
    python3 Drivers/u8g2_csrc/tools/extract_selected_fonts.py \\
        --src Drivers/u8g2_csrc/u8g2_fonts.c \\
        --out Drivers/u8g2_csrc/u8g2_fonts_selected.c

--src/--out default to the paths above (relative to the repo root this
script lives under) if omitted, so a plain `python3 .../extract_selected_fonts.py`
with no arguments still works and is what CI/build docs reference; --src
is accepted explicitly so the exact input is never implicit when the
command is quoted in an audit report.
"""
import argparse
import hashlib
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_REPO_ROOT = _HERE.rsplit(os.sep + os.path.join("Drivers", "u8g2_csrc", "tools"), 1)[0]
DEFAULT_SRC = os.path.join(_REPO_ROOT, "Drivers", "u8g2_csrc", "u8g2_fonts.c")
DEFAULT_OUT = os.path.join(_REPO_ROOT, "Drivers", "u8g2_csrc", "u8g2_fonts_selected.c")

# The exact 13 fonts linked into the current firmware (verified via
# arm-none-eabi-nm on a fresh clean release build -- see the cleanup
# report for the exact command and byte sizes of each).
SELECTED_FONTS = [
    "u8g2_font_10x20_mr",
    "u8g2_font_Pixellari_tu",
    "u8g2_font_VCR_OSD_tu",
    "u8g2_font_courB08_tf",
    "u8g2_font_courB14_tf",
    "u8g2_font_helvB08_tf",
    "u8g2_font_profont17_tr",
    "u8g2_font_resoledmedium_tr",
    "u8g2_font_resoledbold_tr",
    "u8g2_font_spleen5x8_mf",
    "u8g2_font_squeezed_b7_tr",
    "u8g2_font_tiny5_tf",
    "u8g2_font_u8glib_4_tr",
    "u8g2_font_victoriabold8_8r",
]

U8G2_LICENSE_BANNER = """\
 * Upstream project: u8g2 -- Universal 8bit Graphics Library
 *   https://github.com/olikraus/u8g2/
 * Copyright (c) 2016, olikraus@gmail.com
 * All Rights Reserved.
 *
 * License (BSD-2-Clause-style, verbatim from the vendored u8g2.h header;
 * no separate LICENSE file or per-file license block exists in
 * u8g2_fonts.c itself -- it relies on this project-wide notice, so this
 * generated file carries the same notice explicitly):
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Precise upstream version/commit: NOT DISCOVERABLE from this repository's
 * vendored tree -- no U8G2_VERSION macro, CHANGELOG, or other version
 * marker is present anywhere under Drivers/u8g2_csrc/. Only the copyright
 * year (2016) and the project URL above are known in-tree."""


def extract_one(lines, name):
    """Returns (start0, end0) 0-based inclusive line indices for one font's
    full definition, INCLUDING its immediately-preceding /* ... */ doc
    comment block if present directly above (no blank/code line between).
    Raises RuntimeError if the definition is missing OR appears more than
    once (a duplicate definition must never be silently resolved by
    picking "the first one")."""
    decl_needle = f"const uint8_t {name}["
    decl_idxs = [i for i, line in enumerate(lines) if line.startswith(decl_needle)]
    if not decl_idxs:
        raise RuntimeError(f"font definition not found: {name!r}")
    if len(decl_idxs) > 1:
        raise RuntimeError(
            f"font definition appears {len(decl_idxs)} times, refusing to "
            f"guess which is authoritative: {name!r} at lines {[i + 1 for i in decl_idxs]}"
        )
    decl_idx = decl_idxs[0]

    # Walk forward to the statement terminator: a line ending in '";' or
    # just '";' after possible trailing whitespace/CR.
    end_idx = None
    for j in range(decl_idx, len(lines)):
        stripped = lines[j].rstrip("\r\n")
        if stripped.endswith('";'):
            end_idx = j
            break
    if end_idx is None:
        raise RuntimeError(f"statement terminator not found for: {name!r}")

    # Walk backward for an immediately-adjacent /* ... */ doc comment
    # (Fontname/Copyright/Glyphs/BBX Build Mode), same convention as every
    # entry in u8g2_fonts.c.
    start_idx = decl_idx
    k = decl_idx - 1
    if k >= 0 and lines[k].rstrip("\r\n") == "*/":
        while k >= 0 and lines[k].rstrip("\r\n") != "/*":
            k -= 1
        if k >= 0:
            start_idx = k

    return start_idx, end_idx


def generate(src_path, selected_fonts=SELECTED_FONTS):
    """Pure function: reads src_path, returns (output_text, src_sha256).
    No filesystem writes here -- keeps the "prove it's deterministic" check
    (call twice, diff) trivial and side-effect-free."""
    with open(src_path, "rb") as f:
        raw = f.read()
    src_sha256 = hashlib.sha256(raw).hexdigest()

    # Split preserving line content; u8g2_fonts.c uses CRLF -- keep it
    # byte-exact rather than normalizing, so extracted lines are identical
    # to the source.
    lines = raw.decode("latin-1").splitlines(keepends=True)

    missing = []
    out_parts = [
        "/*\n"
        " * u8g2_fonts_selected.c -- GENERATED by\n"
        " * Drivers/u8g2_csrc/tools/extract_selected_fonts.py. DO NOT EDIT BY\n"
        " * HAND -- re-run the generator instead (see that script's own header\n"
        " * comment for why this exists and exactly how each font below was\n"
        " * selected).\n"
        " *\n"
        " * Every font definition below is a byte-exact copy of its definition\n"
        f" * in the source catalog used to generate this file (SHA-256 of that\n"
        f" * catalog at generation time: {src_sha256}).\n"
        " * u8g2_fonts.c itself is never modified or removed by this script; it\n"
        " * remains in the tree for provenance and as the source for adding a\n"
        " * new font later (re-run the generator after editing SELECTED_FONTS).\n"
        " *\n"
        f"{U8G2_LICENSE_BANNER}\n"
        " */\n"
        '#include "u8g2.h"\n\n'
    ]

    for name in selected_fonts:
        try:
            s, e = extract_one(lines, name)
        except RuntimeError as exc:
            missing.append(str(exc))
            continue
        out_parts.append("".join(lines[s:e + 1]))
        out_parts.append("\n")

    if missing:
        raise RuntimeError(
            "extraction failed for one or more requested fonts:\n  "
            + "\n  ".join(missing)
        )

    return "".join(out_parts), src_sha256


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--src", default=DEFAULT_SRC,
                     help=f"upstream u8g2_fonts.c catalog to extract from (default: {DEFAULT_SRC})")
    ap.add_argument("--out", default=DEFAULT_OUT,
                     help=f"output path for the generated file (default: {DEFAULT_OUT})")
    args = ap.parse_args()

    if not os.path.isfile(args.src):
        print(f"error: --src not found: {args.src}", file=sys.stderr)
        return 1

    try:
        text, src_sha256 = generate(args.src)
    except RuntimeError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    with open(args.out, "wb") as f:
        f.write(text.encode("latin-1"))

    total_bytes = len(text.encode("latin-1"))
    print(f"wrote {args.out} ({total_bytes} bytes, {len(SELECTED_FONTS)} fonts)")
    print(f"source: {args.src}")
    print(f"source SHA-256: {src_sha256}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
