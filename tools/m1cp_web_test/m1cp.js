/* See COPYING.txt for license details. */
/*
 * m1cp.js - M1 Manager Control Protocol (M1CP) v1, browser/Node implementation.
 *
 * Protocol authority: documentation/M1CP_v1.md. Frame:
 *   magic 'M1CP'(4) | version(1)=0x01 | msg_type(1) | flags(1) | seq(2 LE) |
 *   payload_len(2 LE, <=512) | payload(N) | crc16(2 LE)
 * CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF) over version..payload.
 *
 * Pure protocol logic only (no DOM, no Web Serial) so it can run under Node for
 * deterministic tests and inside the browser page unchanged.
 */
(function (root, factory) {
  if (typeof module === 'object' && module.exports) { module.exports = factory(); }
  else { root.M1CP = factory(); }
}(typeof self !== 'undefined' ? self : this, function () {
  'use strict';

  var MAGIC = [0x4D, 0x31, 0x43, 0x50]; // "M1CP"
  var VERSION = 0x01;
  var HEADER_LEN = 11;
  var CRC_LEN = 2;
  var MAX_PAYLOAD = 512;
  var MAX_FRAME = HEADER_LEN + MAX_PAYLOAD + CRC_LEN;

  var FLAG = { RESPONSE: 0x01, EVENT: 0x02, ACK: 0x04, NACK: 0x08 };

  var CMD = {
    HELLO: 0x01, PING: 0x02, GET_CAPABILITIES: 0x03, END_SESSION: 0x04,
    GET_DEVICE_INFO: 0x10, GET_STATUS: 0x11,
    FW_VALIDATE: 0x20,
    FS_BEGIN: 0x40, FS_DATA: 0x41, FS_COMMIT: 0x42, FS_ABORT: 0x43,
    FS_STAT: 0x44, FS_DELETE: 0x46
  };
  var CMD_NAME = {};
  Object.keys(CMD).forEach(function (k) { CMD_NAME[CMD[k]] = k; });

  var ERR = {
    0x00: 'NONE', 0x01: 'BAD_CRC', 0x02: 'BAD_VERSION', 0x03: 'UNKNOWN_CMD',
    0x04: 'BAD_LENGTH', 0x05: 'NO_SESSION', 0x06: 'BUSY',
    0x07: 'NOT_IMPLEMENTED', 0x08: 'BAD_PAYLOAD',
    0x09: 'BAD_STATE', 0x0A: 'BAD_ARG', 0x0B: 'TIMEOUT', 0x0C: 'INTEGRITY',
    0x0D: 'SD_UNAVAILABLE', 0x0E: 'NO_SPACE', 0x0F: 'FLASH', 0x10: 'ESP',
    0x11: 'NOT_FOUND', 0x12: 'IO', 0x13: 'ABORTED', 0x14: 'INCOMPATIBLE'
  };

  var CAP = {
    CORE: 0x01, DEVICE_INFO: 0x02, STM32_UPDATE: 0x04, ESP32_UPDATE: 0x08,
    FILESYSTEM: 0x10, EVENTS: 0x20
  };
  function capList(mask) {
    var out = []; Object.keys(CAP).forEach(function (k) { if (mask & CAP[k]) out.push(k); });
    return out;
  }

  // CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, xorout 0.
  function crc16(bytes) {
    var crc = 0xFFFF;
    for (var i = 0; i < bytes.length; i++) {
      crc ^= (bytes[i] << 8);
      for (var b = 0; b < 8; b++) {
        crc = (crc & 0x8000) ? ((crc << 1) ^ 0x1021) & 0xFFFF : (crc << 1) & 0xFFFF;
      }
    }
    return crc & 0xFFFF;
  }

  // Encode a full frame. payload is a Uint8Array/array (may be empty).
  function encodeFrame(type, flags, seq, payload) {
    payload = payload || new Uint8Array(0);
    if (payload.length > MAX_PAYLOAD) { throw new Error('payload too large'); }
    var plen = payload.length;
    var frame = new Uint8Array(HEADER_LEN + plen + CRC_LEN);
    frame[0] = MAGIC[0]; frame[1] = MAGIC[1]; frame[2] = MAGIC[2]; frame[3] = MAGIC[3];
    frame[4] = VERSION;
    frame[5] = type & 0xFF;
    frame[6] = flags & 0xFF;
    frame[7] = seq & 0xFF; frame[8] = (seq >> 8) & 0xFF;
    frame[9] = plen & 0xFF; frame[10] = (plen >> 8) & 0xFF;
    frame.set(payload, HEADER_LEN);
    var crc = crc16(frame.subarray(4, HEADER_LEN + plen)); // version..payload
    frame[HEADER_LEN + plen] = crc & 0xFF;
    frame[HEADER_LEN + plen + 1] = (crc >> 8) & 0xFF;
    return frame;
  }

  // CRC-32/ISO-HDLC (zlib): poly 0xEDB88320 reflected, init/xorout 0xFFFFFFFF.
  // Matches firmware m1cp_fs_crc32 (Add Files whole-file + .m1pkg component CRCs).
  function crc32(bytes, crc) {
    crc = ((crc === undefined ? 0 : crc) ^ 0xFFFFFFFF) >>> 0;
    for (var i = 0; i < bytes.length; i++) {
      crc = (crc ^ bytes[i]) >>> 0;
      for (var b = 0; b < 8; b++) { crc = ((crc >>> 1) ^ (0xEDB88320 & -(crc & 1))) >>> 0; }
    }
    return (crc ^ 0xFFFFFFFF) >>> 0;
  }

  function strBytes(s) { var a = new Uint8Array(s.length); for (var i = 0; i < s.length; i++) { a[i] = s.charCodeAt(i) & 0xFF; } return a; }
  function wr32(a, o, v) { a[o] = v & 0xFF; a[o + 1] = (v >>> 8) & 0xFF; a[o + 2] = (v >>> 16) & 0xFF; a[o + 3] = (v >>> 24) & 0xFF; }

  // name_len(1)|name  (FS_STAT / FS_DELETE / FW_VALIDATE)
  function encodeName(name) {
    var nb = strBytes(name);
    var out = new Uint8Array(1 + nb.length);
    out[0] = nb.length; out.set(nb, 1);
    return out;
  }
  // FS_BEGIN: name_len(1)|name|total(4)|crc32(4)|flags(1)
  function encodeFsBegin(name, totalSize, crc32val, overwrite) {
    var nb = strBytes(name), o = 0;
    var out = new Uint8Array(1 + nb.length + 9);
    out[o++] = nb.length; out.set(nb, o); o += nb.length;
    wr32(out, o, totalSize >>> 0); o += 4;
    wr32(out, o, crc32val >>> 0); o += 4;
    out[o++] = overwrite ? 1 : 0;
    return out;
  }
  // FS_DATA: session(2)|offset(4)|data(N)
  function encodeFsData(session, offset, data) {
    var out = new Uint8Array(6 + data.length);
    out[0] = session & 0xFF; out[1] = (session >> 8) & 0xFF;
    wr32(out, 2, offset >>> 0);
    out.set(data, 6);
    return out;
  }
  // FS_COMMIT / (FS_ABORT accepts empty): session(2)
  function encodeSession(session) { return new Uint8Array([session & 0xFF, (session >> 8) & 0xFF]); }

  /*
   * Streaming decoder. Never assumes one read == one frame. push() returns an
   * array of events: {frame:{type,flags,seq,payload}} for each valid frame, or
   * {error:'crc'|'version'|'oversize', ...} for a rejected/resynced frame.
   * Handles fragmentation, batching, junk-before-magic, bad CRC, oversized
   * length and truncation with bounded buffering.
   */
  function Decoder() {
    this.buf = new Uint8Array(0);
  }
  Decoder.prototype._concat = function (chunk) {
    var out = new Uint8Array(this.buf.length + chunk.length);
    out.set(this.buf, 0); out.set(chunk, this.buf.length);
    this.buf = out;
  };
  Decoder.prototype._findMagic = function (from) {
    var b = this.buf;
    for (var i = from; i + 4 <= b.length; i++) {
      if (b[i] === MAGIC[0] && b[i + 1] === MAGIC[1] &&
          b[i + 2] === MAGIC[2] && b[i + 3] === MAGIC[3]) { return i; }
    }
    return -1;
  };
  Decoder.prototype.push = function (chunk) {
    if (chunk && chunk.length) { this._concat(chunk instanceof Uint8Array ? chunk : new Uint8Array(chunk)); }
    var events = [];
    for (;;) {
      var m = this._findMagic(0);
      if (m < 0) {
        // no magic; retain only a possible partial-magic tail (<=3 bytes)
        var keep = Math.min(3, this.buf.length);
        this.buf = this.buf.subarray(this.buf.length - keep);
        // copy to detach from old backing store
        this.buf = new Uint8Array(this.buf);
        break;
      }
      if (m > 0) { this.buf = new Uint8Array(this.buf.subarray(m)); } // drop junk before magic
      if (this.buf.length < HEADER_LEN) { break; } // need full header
      var plen = this.buf[9] | (this.buf[10] << 8);
      if (plen > MAX_PAYLOAD) {
        events.push({ error: 'oversize', payload_len: plen });
        this.buf = new Uint8Array(this.buf.subarray(1)); // resync past this magic
        continue;
      }
      var frameLen = HEADER_LEN + plen + CRC_LEN;
      if (this.buf.length < frameLen) { break; } // need full frame
      var version = this.buf[4];
      var type = this.buf[5], flags = this.buf[6];
      var seq = this.buf[7] | (this.buf[8] << 8);
      var crcGot = this.buf[frameLen - 2] | (this.buf[frameLen - 1] << 8);
      var crcCalc = crc16(this.buf.subarray(4, HEADER_LEN + plen));
      if (version !== VERSION) {
        events.push({ error: 'version', version: version });
        this.buf = new Uint8Array(this.buf.subarray(1));
        continue;
      }
      if (crcGot !== crcCalc) {
        events.push({ error: 'crc', got: crcGot, calc: crcCalc });
        this.buf = new Uint8Array(this.buf.subarray(1));
        continue;
      }
      var payload = new Uint8Array(this.buf.subarray(HEADER_LEN, HEADER_LEN + plen));
      events.push({ frame: { type: type, flags: flags, seq: seq, payload: payload } });
      this.buf = new Uint8Array(this.buf.subarray(frameLen));
    }
    return events;
  };

  // ---- payload decoders (truthful; keep firmware sentinels) ----
  function u16(p, o) { return p[o] | (p[o + 1] << 8); }
  function u32(p, o) { return (p[o] | (p[o + 1] << 8) | (p[o + 2] << 16) | (p[o + 3] << 24)) >>> 0; }

  function decodeCapabilities(p) { // GET_CAPABILITIES resp: caps(4)|max(2)|ver(1)
    if (p.length < 7) { return { error: 'short' }; }
    return { capabilities: u32(p, 0), max_payload: u16(p, 4), version: p[6] };
  }
  function decodeHelloResponse(p) { // ver(1)|max(2)|caps(4)
    if (p.length < 7) { return { error: 'short' }; }
    return { version: p[0], max_payload: u16(p, 1), capabilities: u32(p, 3) };
  }
  function hex(b) { var s = ''; for (var i = 0; i < b.length; i++) { s += (b[i] < 16 ? '0' : '') + b[i].toString(16); } return s; }

  function decodeDeviceInfo(p) { // fixed 56-byte structure per M1CP_v1.md
    if (p.length < 56) { return { error: 'short', len: p.length }; }
    var UNAVAIL8 = true; for (var i = 29; i < 45; i++) { if (p[i] !== 0xFF) UNAVAIL8 = false; }
    var esp = u32(p, 46);
    return {
      protocol_version: p[0],
      device_family: p[1] === 0x01 ? 'MonstaTek M1' : ('0x' + p[1].toString(16)),
      fw_version: p[2] + '.' + p[3] + '.' + p[4] + '.' + p[5],
      hw_revision: p[6] === 0xFF ? 'unknown' : p[6],
      stm32_devid: '0x' + u16(p, 7).toString(16),
      stm32_uid: hex(p.subarray(9, 21)),
      active_bank: p[21] === 0xFF ? 'unknown' : p[21],
      flash_bank_capacity: u32(p, 22),
      sd_present: p[26] === 1,
      sd_mount_state: ['NotReady', 'Unmounted', 'Mounted', 'NoFS', 'NotOK'][p[27]] ||
                      (p[27] === 0xFF ? 'unknown' : ('0x' + p[27].toString(16))),
      sd_owner: p[28] === 1 ? 'usb-host(MSC)' : (p[28] === 0 ? 'device/internal' : 'unknown'),
      sd_total_bytes: UNAVAIL8 ? 'unavailable' : 'present',
      sd_free_bytes: UNAVAIL8 ? 'unavailable' : 'present',
      esp32_present: p[45] === 1,
      esp32_fw_version: (esp === 0xFFFFFFFF) ? 'unavailable' : ('0x' + esp.toString(16)),
      capability_flags: u32(p, 50),
      max_payload: u16(p, 54)
    };
  }

  var OP_STATE = ['idle', 'receiving', 'verifying', 'staging', '(4)', 'programming',
    'verifying_prog', 'installing_res', 'reboot_required', 'esp_owning', 'esp_flashing',
    'esp_verifying', 'esp_resetting', 'esp_confirming', 'complete', 'failed'];
  var OP_DOMAIN = ['none', 'stm32', 'esp32', 'filesystem'];

  function u64(p, o) { // all-0xFF => 'unavailable', else Number (SD sizes < 2^53)
    var all = true; for (var i = 0; i < 8; i++) { if (p[o + i] !== 0xFF) { all = false; break; } }
    if (all) { return 'unavailable'; }
    return u32(p, o) + u32(p, o + 4) * 4294967296;
  }
  function ver4(p, o) { return p[o] + '.' + p[o + 1] + '.' + p[o + 2] + '.' + p[o + 3]; }

  function decodeStatus(p) { // GET_STATUS resp (struct_version 0x01, 43 bytes)
    if (p.length < 43) { return { error: 'short', len: p.length }; }
    return {
      struct_version: p[0],
      device_state: ['ok', 'busy', 'fault'][p[1]] || ('0x' + p[1].toString(16)),
      op_domain: OP_DOMAIN[p[2]] || ('0x' + p[2].toString(16)),
      op_state: OP_STATE[p[3]] || ('0x' + p[3].toString(16)),
      op_progress: p[4] === 0xFF ? 'indeterminate' : p[4],
      last_error: errName(p[5]),
      flags: { reboot_required: !!(p[6] & 1), fw_pkg_staged: !!(p[6] & 2), transfer_active: !!(p[6] & 4) },
      stm32_fw_version: ver4(p, 7),
      resource_present: p[11] === 1,
      resource_version: p[11] === 1 ? ver4(p, 12) : 'not-installed',
      esp32_present: p[16] === 1,
      esp32_ver_state: ['unavailable', 'valid', 'stale'][p[17]] || ('0x' + p[17].toString(16)),
      esp32_fw_version: (u32(p, 18) === 0xFFFFFFFF) ? 'unavailable' : ('0x' + u32(p, 18).toString(16)),
      sd_present: p[22] === 1,
      sd_state: ['NotReady', 'Unmounted', 'Mounted', 'NoFS', 'NotOK'][p[23]] ||
                (p[23] === 0xFF ? 'unknown' : ('0x' + p[23].toString(16))),
      sd_owner: p[24] === 1 ? 'usb-host(MSC)' : (p[24] === 0 ? 'device/internal' : 'unknown'),
      sd_total_bytes: u64(p, 25),
      sd_free_bytes: u64(p, 33),
      active_bank: p[41] === 0xFF ? 'unknown' : p[41],
      max_concurrent_xfer: p[42]
    };
  }
  function decodeFsBeginResp(p) { // max_chunk(2)|session(2)
    if (p.length < 4) { return { error: 'short' }; }
    return { max_chunk: u16(p, 0), session: u16(p, 2) };
  }
  function decodeFsDataResp(p) { return p.length >= 4 ? { next_offset: u32(p, 0) } : { error: 'short' }; }
  function decodeFsStatResp(p) { return p.length >= 5 ? { exists: p[0] === 1, size: u32(p, 1) } : { error: 'short' }; }
  function decodeFwValidateResp(p) { // fw_version(4)|resource_version(4)
    if (p.length < 8) { return { error: 'short' }; }
    return { fw_version: ver4(p, 0), resource_version: ver4(p, 4) };
  }

  function errName(code) { return ERR[code] || ('0x' + (code || 0).toString(16)); }
  function toHex(bytes) { var s = ''; for (var i = 0; i < bytes.length; i++) { s += (bytes[i] < 16 ? '0' : '') + bytes[i].toString(16) + ' '; } return s.trim(); }

  return {
    MAGIC: MAGIC, VERSION: VERSION, HEADER_LEN: HEADER_LEN, CRC_LEN: CRC_LEN,
    MAX_PAYLOAD: MAX_PAYLOAD, MAX_FRAME: MAX_FRAME, FLAG: FLAG, CMD: CMD,
    CMD_NAME: CMD_NAME, ERR: ERR, CAP: CAP, capList: capList,
    crc16: crc16, crc32: crc32, encodeFrame: encodeFrame, Decoder: Decoder,
    encodeName: encodeName, encodeFsBegin: encodeFsBegin, encodeFsData: encodeFsData,
    encodeSession: encodeSession,
    decodeCapabilities: decodeCapabilities, decodeHelloResponse: decodeHelloResponse,
    decodeDeviceInfo: decodeDeviceInfo, decodeStatus: decodeStatus,
    decodeFsBeginResp: decodeFsBeginResp, decodeFsDataResp: decodeFsDataResp,
    decodeFsStatResp: decodeFsStatResp, decodeFwValidateResp: decodeFwValidateResp,
    errName: errName, toHex: toHex
  };
}));
