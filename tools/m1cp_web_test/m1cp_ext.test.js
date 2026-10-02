/* See COPYING.txt for license details. */
/*
 * m1cp_ext.test.js - tests for the Gate A/B/C client protocol additions
 * (GET_STATUS decode, CRC-32, Add Files + FW_VALIDATE encoders). Plain asserts.
 *   node tools/m1cp_web_test/m1cp_ext.test.js
 */
'use strict';
var M1CP = require('./m1cp.js');
var fails = 0, count = 0;
function ok(c, m) { count++; if (c) { console.log('  ok  : ' + m); } else { console.log('  FAIL: ' + m); fails++; } }
function u8() { return new Uint8Array(Array.prototype.slice.call(arguments)); }

// [1] CRC-32/ISO-HDLC known vector "123456789" == 0xCBF43926
(function () {
  var s = u8(0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39);
  ok(M1CP.crc32(s) === 0xCBF43926, 'CRC-32/ISO-HDLC("123456789") == 0xCBF43926');
  // streaming composition equals one-shot
  ok(M1CP.crc32(s.subarray(4), M1CP.crc32(s.subarray(0,4))) === 0xCBF43926, 'crc32 composes across chunks');
})();

// [2] JS crc32 matches the crc in the committed valid .m1pkg fixture (ties the
//     client crc to the firmware validator, which accepts valid.m1pkg).
(function () {
  var fs = require('fs'), path = require('path');
  var buf = fs.readFileSync(path.join(__dirname, '..', '..', 'tests', 'm1pkg', 'fixtures', 'valid.m1pkg'));
  var fwOff = buf.readUInt32LE(20), fwSize = buf.readUInt32LE(24), fwCrc = buf.readUInt32LE(28);
  var got = M1CP.crc32(new Uint8Array(buf.subarray(fwOff, fwOff + fwSize)));
  ok(got === fwCrc, 'client crc32 matches committed fixture fw_crc32 (== firmware crc)');
})();

// [3] decodeStatus of a synthetic 43-byte status
(function () {
  var p = new Uint8Array(43);
  p[0]=1; p[1]=1; p[2]=3; p[3]=1; p[4]=42; p[5]=0x0E; p[6]=0x04; // busy, fs, receiving, 42%, NO_SPACE, transfer_active
  p[7]=0;p[8]=8;p[9]=0;p[10]=4;            // stm32 0.8.0.4
  p[11]=1;p[12]=0;p[13]=8;p[14]=1;p[15]=0; // resource present 0.8.1.0
  p[16]=1;p[17]=0;p[18]=p[19]=p[20]=p[21]=0xFF; // esp present, version unavailable
  p[22]=1;p[23]=2;p[24]=0;                 // sd present, Mounted, device-owned
  for (var i=25;i<41;i++) p[i]=0xFF;       // sd sizes unavailable
  p[41]=1; p[42]=1;
  var s = M1CP.decodeStatus(p);
  ok(s.device_state==='busy' && s.op_domain==='filesystem' && s.op_state==='receiving', 'status state decoded');
  ok(s.op_progress===42 && s.last_error==='NO_SPACE' && s.flags.transfer_active===true, 'status progress/error/flags');
  ok(s.stm32_fw_version==='0.8.0.4' && s.resource_version==='0.8.1.0', 'status versions');
  ok(s.esp32_fw_version==='unavailable' && s.sd_total_bytes==='unavailable', 'status sentinels preserved');
  ok(s.sd_state==='Mounted' && s.sd_owner==='device/internal' && s.active_bank===1, 'status sd/bank');
})();

// [4] Add Files encoders
(function () {
  var b = M1CP.encodeFsBegin('fw.m1pkg', 432, 0x11223344, true);
  ok(b[0]===8 && String.fromCharCode.apply(null, b.subarray(1,9))==='fw.m1pkg', 'FS_BEGIN name');
  ok((b[9]|(b[10]<<8)|(b[11]<<16)|(b[12]*16777216))===432, 'FS_BEGIN total size');
  // name(8): out[1..8]; total: out[9..12]; crc32: out[13..16]; flags: out[17]
  ok(b[13]===0x44 && b[14]===0x33 && b[15]===0x22 && b[16]===0x11, 'FS_BEGIN crc32 LE');
  ok(b[17]===1, 'FS_BEGIN overwrite flag');

  var d = M1CP.encodeFsData(0x1234, 0x00010000, u8(1,2,3,4));
  ok(d[0]===0x34 && d[1]===0x12, 'FS_DATA session LE');
  ok(d[2]===0 && d[3]===0 && d[4]===1 && d[5]===0, 'FS_DATA offset LE (0x10000)');
  ok(d[6]===1 && d[9]===4, 'FS_DATA payload appended');

  var c = M1CP.encodeSession(0xABCD);
  ok(c[0]===0xCD && c[1]===0xAB && c.length===2, 'FS_COMMIT session');
})();

// [5] name + FW_VALIDATE encoders and response decoders
(function () {
  var n = M1CP.encodeName('valid.m1pkg');
  ok(n[0]===11 && n.length===12, 'encodeName length prefix');
  var r = M1CP.decodeFwValidateResp(u8(0,8,0,4, 0,8,1,0));
  ok(r.fw_version==='0.8.0.4' && r.resource_version==='0.8.1.0', 'FW_VALIDATE resp decode');
  var b = M1CP.decodeFsBeginResp(u8(0xE0,0x01, 0x02,0x00));
  ok(b.max_chunk===480 && b.session===2, 'FS_BEGIN resp decode');
  ok(M1CP.decodeFsDataResp(u8(0,0,1,0)).next_offset===0x10000, 'FS_DATA resp decode');
  ok(M1CP.errName(0x14)==='INCOMPATIBLE' && M1CP.errName(0x0C)==='INTEGRITY', 'extended error names');
  ok(M1CP.capList(0x13).join(',')==='CORE,DEVICE_INFO,FILESYSTEM', 'capList decode');
})();

// [6] full-frame round-trip through the decoder (GET_STATUS request)
(function () {
  var f = M1CP.encodeFrame(M1CP.CMD.GET_STATUS, 0, 0x77, new Uint8Array(0));
  var d = new M1CP.Decoder(); var ev = d.push(f);
  ok(ev.length===1 && ev[0].frame.type===0x11 && ev[0].frame.seq===0x77, 'GET_STATUS frame round-trips');
})();

console.log('\n' + (fails === 0 ? 'ALL PASS' : 'FAILURES') + ' (' + (count - fails) + '/' + count + ')');
if (typeof process !== 'undefined' && process.exit) { process.exit(fails ? 1 : 0); }
