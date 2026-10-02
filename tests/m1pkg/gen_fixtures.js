/* See COPYING.txt for license details. */
/*
 * gen_fixtures.js - deterministic .m1pkg fixtures for physical-M1 validation of
 * Gate C (FW_VALIDATE). Emits one valid package and a set of malformed variants,
 * each exercising a documented validation error. Deterministic (fixed byte
 * patterns, no randomness) so the SHA-256 hashes are reproducible.
 *
 *   node tests/m1pkg/gen_fixtures.js [outdir]   (default outdir: <script>/fixtures)
 *
 * Layout (see documentation/M1CP_web_manager.md section 5): 48-byte LE header,
 * then the firmware image, then the resource blob, tiled exactly.
 *
 * The expected FW_VALIDATE result (and, for the valid package, the expected
 * firmware/resource versions) is recorded per fixture in MANIFEST.json. These
 * are HOST expectations of the validator logic; they do not by themselves
 * validate FatFs, SD ownership, USB-MSC interaction or physical-device
 * behaviour - those require the on-device procedure.
 */
'use strict';
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

const HEADER_LEN = 48;
const MAGIC = Buffer.from([0x4D, 0x31, 0x50, 0x4B]); // 'M1PK'
const FORMAT_VERSION = 1;
const DEVID_H573 = 0x0484;

/* CRC-32/ISO-HDLC (zlib) - matches firmware m1cp_fs_crc32. */
function crc32(buf, crc = 0) {
  crc = (crc ^ 0xFFFFFFFF) >>> 0;
  for (const b of buf) {
    crc = (crc ^ b) >>> 0;
    for (let i = 0; i < 8; i++) crc = ((crc >>> 1) ^ (0xEDB88320 & -(crc & 1))) >>> 0;
  }
  return (crc ^ 0xFFFFFFFF) >>> 0;
}

function patt(len, base) { const b = Buffer.alloc(len); for (let i = 0; i < len; i++) b[i] = (base + i) & 0xFF; return b; }

/* Build a package from parts. opts let variants override fields post-layout. */
function buildPkg(opts = {}) {
  const devid   = opts.devid   ?? DEVID_H573;
  const fwVer   = opts.fwVer   ?? [0, 8, 0, 4];
  const resVer  = opts.resVer  ?? [0, 8, 1, 0];
  const fw      = opts.fw      ?? patt(256, 0x00);
  const res     = opts.res     ?? patt(128, 0xA0);
  const fwOff   = HEADER_LEN;
  const resOff  = fwOff + fw.length;
  const total   = resOff + res.length;

  const buf = Buffer.alloc(total);
  MAGIC.copy(buf, 0);
  buf[4] = (opts.formatVersion ?? FORMAT_VERSION);
  buf.writeUInt16LE(devid & 0xFFFF, 8);
  Buffer.from(fwVer).copy(buf, 12);
  Buffer.from(resVer).copy(buf, 16);
  buf.writeUInt32LE(opts.fwOff  ?? fwOff, 20);
  buf.writeUInt32LE(opts.fwSize ?? fw.length, 24);
  buf.writeUInt32LE(opts.fwCrc  ?? crc32(fw), 28);
  buf.writeUInt32LE(opts.resOff ?? resOff, 32);
  buf.writeUInt32LE(opts.resSize?? res.length, 36);
  buf.writeUInt32LE(opts.resCrc ?? crc32(res), 40);
  fw.copy(buf, fwOff);
  res.copy(buf, resOff);
  // header CRC over [0..43], unless the variant wants it left stale
  const hcrc = (opts.headerCrc !== undefined) ? opts.headerCrc : crc32(buf.subarray(0, 44));
  buf.writeUInt32LE(hcrc >>> 0, 44);
  return buf;
}

/* Each fixture: {name, expect, buf}. `expect` is the documented FW_VALIDATE
 * result (M1CP error name) the firmware validator must return. */
const FW_VER = [0, 8, 0, 4];
const RES_VER = [0, 8, 1, 0]; // same major.minor as fw -> compatible
const verStr = (a) => a.join('.');

function fixtures() {
  const out = [];
  const add = (name, expect, buf, versions) => out.push({ name, expect, buf, versions: versions || null });

  // valid baseline (expected fw/resource versions recorded)
  add('valid', 'NONE', buildPkg(), { fw: verStr(FW_VER), resource: verStr(RES_VER) });

  // container magic
  { const b = buildPkg(); b[0] = 0x58; b.writeUInt32LE(crc32(b.subarray(0, 44)), 44); add('bad_magic', 'BAD_PAYLOAD', b); }

  // unsupported format version
  add('bad_format', 'INCOMPATIBLE', buildPkg({ formatVersion: 2 }));

  // header CRC mismatch (tamper devid, leave header CRC stale)
  { const b = buildPkg(); b[8] ^= 0xFF; add('bad_header_crc', 'INTEGRITY', b); }

  // truncated file (drop 4 bytes of the resource)
  { const b = buildPkg(); add('truncated', 'BAD_PAYLOAD', b.subarray(0, b.length - 4)); }

  // trailing data after the last component
  { const b = Buffer.concat([buildPkg(), Buffer.from([1, 2, 3, 4, 5, 6, 7, 8])]); add('trailing_data', 'BAD_PAYLOAD', b); }

  // unaligned fw_size (declare 254; file still tiles but alignment fails)
  add('unaligned_fw', 'BAD_PAYLOAD', buildPkg({ fw: patt(254, 0x00), fwSize: 254 }));

  // overlap: res_offset points into fw region
  add('overlap', 'BAD_PAYLOAD', buildPkg({ resOff: HEADER_LEN }));

  // leading gap: fw starts past header
  add('leading_gap', 'BAD_PAYLOAD', buildPkg({ fwOff: HEADER_LEN + 8 }));

  // missing firmware component (fw_size 0)
  add('missing_fw', 'BAD_PAYLOAD', buildPkg({ fwSize: 0 }));

  // wrong target device id
  add('wrong_target', 'INCOMPATIBLE', buildPkg({ devid: 0x0450 }));

  // incompatible resource version (different minor)
  add('incompatible_res', 'INCOMPATIBLE', buildPkg({ resVer: [0, 9, 0, 0] }));

  // bad firmware component CRC
  add('bad_fw_crc', 'INTEGRITY', buildPkg({ fwCrc: 0xDEADBEEF }));

  // bad resource component CRC
  add('bad_res_crc', 'INTEGRITY', buildPkg({ resCrc: 0x12345678 }));

  return out;
}

/* H573 target the fixtures are validated against (see m1cp_pkg_validate). */
const TARGET = { devid: '0x0484', flash_bank_size: 0x00100000 };

function main() {
  const outdir = process.argv[2] || path.join(__dirname, 'fixtures');
  fs.mkdirSync(outdir, { recursive: true });
  const fx = fixtures();
  const fixturesOut = [];
  for (const f of fx) {
    const file = path.join(outdir, f.name + '.m1pkg');
    fs.writeFileSync(file, f.buf);
    const sha = crypto.createHash('sha256').update(f.buf).digest('hex');
    fixturesOut.push({
      name: f.name + '.m1pkg', bytes: f.buf.length,
      expect: f.expect, versions: f.versions, sha256: sha
    });
  }
  const manifest = {
    description: 'Gate C .m1pkg validation fixtures. expect = FW_VALIDATE result; ' +
      'versions = expected fw/resource on NONE. HOST expectations only - do not ' +
      'imply FatFs/SD/MSC/physical validation.',
    format: 'm1pkg v1 (48-byte header, see documentation/M1CP_web_manager.md section 5)',
    target: TARGET,
    generator: 'tests/m1pkg/gen_fixtures.js',
    fixtures: fixturesOut
  };
  fs.writeFileSync(path.join(outdir, 'MANIFEST.json'), JSON.stringify(manifest, null, 2) + '\n');
  console.log('name'.padEnd(22), 'bytes'.padStart(6), ' expect'.padEnd(14), 'sha256');
  for (const m of fixturesOut) {
    console.log(m.name.padEnd(22), String(m.bytes).padStart(6), (' ' + m.expect).padEnd(14), m.sha256);
  }
  console.log('\nwrote ' + fx.length + ' fixtures + MANIFEST.json to ' + outdir);
}
main();
