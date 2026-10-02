/* See COPYING.txt for license details. */
/*
 * m1cp.test.js - deterministic tests for the browser M1CP implementation.
 * Run headless:  node tools/m1cp_web_test/m1cp.test.js
 * (No test framework; plain assertions.)
 */
'use strict';
/* console shim for bare engines (e.g. jsc exposes print(), not console). */
var console = (typeof console !== 'undefined') ? console
            : { log: (typeof print === 'function' ? print : function () {}) };
/* Resolve M1CP under Node (require) or a bare JS engine (global, e.g. jsc after
 * loading m1cp.js first). */
var M1CP = (typeof require === 'function') ? require('./m1cp.js')
         : (typeof globalThis !== 'undefined' ? globalThis.M1CP : this.M1CP);

var fails = 0, count = 0;
function ok(cond, msg) { count++; if (cond) { console.log('  ok  : ' + msg); } else { console.log('  FAIL: ' + msg); fails++; } }
function bytes() { return new Uint8Array(Array.prototype.slice.call(arguments)); }

// [1] CRC-16/CCITT-FALSE known vector: "123456789" -> 0x29B1
(function () {
  var s = new Uint8Array([0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39]);
  ok(M1CP.crc16(s) === 0x29B1, 'CRC-16/CCITT-FALSE("123456789") == 0x29B1');
})();

// [2] valid HELLO frame generation + self-decode roundtrip
(function () {
  var f = M1CP.encodeFrame(M1CP.CMD.HELLO, 0, 0x1234, new Uint8Array(0));
  ok(f[0]===0x4D && f[1]===0x31 && f[2]===0x43 && f[3]===0x50, 'HELLO magic bytes');
  ok(f[4]===0x01 && f[5]===M1CP.CMD.HELLO && (f[7]|(f[8]<<8))===0x1234, 'HELLO header fields');
  var d = new M1CP.Decoder(); var ev = d.push(f);
  ok(ev.length===1 && ev[0].frame && ev[0].frame.seq===0x1234, 'HELLO decodes back');
})();

// [3] fragmented RX: split one frame across two pushes
(function () {
  var pl = new Uint8Array([1,2,3,4,5]);
  var f = M1CP.encodeFrame(M1CP.CMD.PING, M1CP.FLAG.RESPONSE, 7, pl);
  var d = new M1CP.Decoder();
  var e1 = d.push(f.subarray(0, 6));
  var e2 = d.push(f.subarray(6));
  ok(e1.length===0, 'split: nothing on first half');
  ok(e2.length===1 && e2[0].frame && e2[0].frame.payload.length===5, 'split: frame after second half');
})();

// [4] byte-at-a-time RX
(function () {
  var f = M1CP.encodeFrame(M1CP.CMD.PING, 0, 42, new Uint8Array([9]));
  var d = new M1CP.Decoder(); var got = null;
  for (var i = 0; i < f.length; i++) { var ev = d.push(f.subarray(i, i+1)); if (ev.length) got = ev[0]; }
  ok(got && got.frame && got.frame.seq===42, 'byte-at-a-time yields one frame');
})();

// [5] two frames in one chunk
(function () {
  var a = M1CP.encodeFrame(M1CP.CMD.PING, 0, 1, new Uint8Array(0));
  var b = M1CP.encodeFrame(M1CP.CMD.PING, 0, 2, new Uint8Array(0));
  var buf = new Uint8Array(a.length + b.length); buf.set(a,0); buf.set(b,a.length);
  var ev = new M1CP.Decoder().push(buf);
  ok(ev.length===2 && ev[0].frame.seq===1 && ev[1].frame.seq===2, 'two frames in one chunk');
})();

// [6] junk before magic
(function () {
  var junk = new Uint8Array([0,0x4D,0x31,0xAA,0x55,0xFF]);
  var f = M1CP.encodeFrame(M1CP.CMD.HELLO, 0, 3, new Uint8Array(0));
  var d = new M1CP.Decoder();
  d.push(junk);
  var ev = d.push(f);
  ok(ev.some(function(x){return x.frame && x.frame.seq===3;}), 'resync after junk before magic');
})();

// [7] invalid CRC -> error event, no frame
(function () {
  var f = M1CP.encodeFrame(M1CP.CMD.PING, 0, 5, new Uint8Array([1]));
  f[f.length-1] ^= 0xFF; // corrupt crc
  var ev = new M1CP.Decoder().push(f);
  ok(ev.some(function(x){return x.error==='crc';}) && !ev.some(function(x){return x.frame;}), 'bad CRC -> error, no frame');
})();

// [8] oversized payload length -> error, no overflow, resync
(function () {
  var h = new Uint8Array(15);
  h[0]=0x4D;h[1]=0x31;h[2]=0x43;h[3]=0x50;h[4]=0x01;h[5]=M1CP.CMD.PING;h[6]=0;h[7]=1;h[8]=0;
  h[9]=0xFF;h[10]=0x0F; // len=0x0FFF > 512
  var c=M1CP.crc16(h.subarray(4,11)); h[11]=c&0xFF;h[12]=c>>8;
  var d = new M1CP.Decoder(); var ev = d.push(h);
  ok(ev.some(function(x){return x.error==='oversize';}), 'oversized length -> error (no crash)');
  // and a following good frame still decodes
  var f = M1CP.encodeFrame(M1CP.CMD.PING, 0, 77, new Uint8Array(0));
  var ev2 = d.push(f);
  ok(ev2.some(function(x){return x.frame && x.frame.seq===77;}), 'recovers after oversized');
})();

// [9] truncated frame -> nothing yet; completes on remainder
(function () {
  var f = M1CP.encodeFrame(M1CP.CMD.PING, 0, 88, new Uint8Array([1,2,3]));
  var d = new M1CP.Decoder();
  var e1 = d.push(f.subarray(0, f.length-2)); // missing last 2 (crc)
  ok(e1.length===0, 'truncated: no event yet');
  var e2 = d.push(f.subarray(f.length-2));
  ok(e2.length===1 && e2[0].frame.seq===88, 'completes when remainder arrives');
})();

// [10] response sequence matching (simulated pending-map)
(function () {
  var pending = {};
  function send(type, seq) { pending[seq] = type; return M1CP.encodeFrame(type, 0, seq, new Uint8Array(0)); }
  var reqSeq = 0x2222;
  send(M1CP.CMD.GET_CAPABILITIES, reqSeq);
  // firmware-style response echoes seq + RESPONSE flag
  var resp = M1CP.encodeFrame(M1CP.CMD.GET_CAPABILITIES, M1CP.FLAG.RESPONSE, reqSeq,
                              new Uint8Array([0x03,0,0,0, 0x00,0x02, 0x01]));
  var ev = new M1CP.Decoder().push(resp)[0].frame;
  ok((ev.flags & M1CP.FLAG.RESPONSE) && ev.seq === reqSeq && pending[ev.seq] === M1CP.CMD.GET_CAPABILITIES,
     'response seq matches pending request');
  var caps = M1CP.decodeCapabilities(ev.payload);
  ok(caps.capabilities === 0x03 && caps.max_payload === 512 && caps.version === 1, 'capabilities decode');
})();

// [11] device-info sentinels stay truthful
(function () {
  var p = new Uint8Array(56);
  p[0]=1;p[1]=1;p[2]=0;p[3]=8;p[4]=0;p[5]=4;p[6]=0xFF; // hw unknown
  p[21]=1; for (var i=29;i<45;i++) p[i]=0xFF; // sd bytes unavailable
  p[45]=1; p[46]=p[47]=p[48]=p[49]=0xFF; // esp32 version unavailable
  var di = M1CP.decodeDeviceInfo(p);
  ok(di.hw_revision==='unknown', 'hw revision unknown preserved');
  ok(di.sd_total_bytes==='unavailable' && di.sd_free_bytes==='unavailable', 'sd bytes unavailable preserved');
  ok(di.esp32_fw_version==='unavailable', 'esp32 version unavailable preserved');
  ok(di.fw_version==='0.8.0.4' && di.device_family==='MonstaTek M1', 'device info fields');
})();

console.log('\n' + (fails===0 ? 'ALL PASS' : 'FAILURES') + ' (' + (count-fails) + '/' + count + ')');
if (typeof process !== 'undefined' && process.exit) { process.exit(fails ? 1 : 0); }
