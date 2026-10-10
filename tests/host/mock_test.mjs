// SPDX-License-Identifier: GPL-3.0-only
// The web page's picture coder and messages (web/mocky.js), checked by themselves, then written out
// for the simulator (tests/scenarios/*.mock): OUT/NAME.syx (the messages, one per line, in hex) and
// OUT/NAME.raw (the screen they should give, RGB565 high byte first).
//   node tests/host/mock_test.mjs OUT
import { writeFileSync, mkdirSync } from 'node:fs';
import { PIX, W, H, MAX_CODED, to565, from565, encode, decode, crc32, pictureMsgs, pack7, CHUNK, clearMsg,
         infoMsg, showMsg, beginMsg, dataMsg, endMsg, Device } from '../../web/mocky.js';

const out = process.argv[2] || 'build/host/pics';
mkdirSync(out, { recursive: true });
let fails = 0;
const ok = (c, what) => { if (!c) { console.error('FAIL', what); fails++; } };

// test pictures, RGBA as a canvas gives them
function picture(f) {
    const d = new Uint8ClampedArray(PIX * 4);
    for (let y = 0; y < H; y++)
        for (let x = 0; x < W; x++) {
            const [r, g, b] = f(x, y), i = (y * W + x) * 4;
            d[i] = r; d[i + 1] = g; d[i + 2] = b; d[i + 3] = 255;
        }
    return d;
}
let seed = 1;
const rnd = () => (seed = (seed * 1103515245 + 12345) >>> 0) >>> 8;
const PICS = {
    // a flat app screen: cream page, a coral header, teal buttons, a few text-like stripes
    ui: picture((x, y) => {
        if (y < 36) return (x + y) % 9 === 0 && x > 20 && x < 120 && y > 12 && y < 24 ? [255, 255, 255] : [242, 120, 72];
        if (y > 180 && y < 220 && ((x > 16 && x < 112) || (x > 128 && x < 224))) return [38, 166, 154];
        if (y % 20 < 4 && y > 50 && y < 170 && x > 16 && x < 16 + ((y * 7) % 200)) return [72, 58, 48];
        return [250, 244, 232];
    }),
    gradient: picture((x, y) => [x, y, 255 - x]),
    noise: picture(() => [rnd() & 255, rnd() & 255, rnd() & 255]),
    second: picture((x, y) => ((x >> 4) + (y >> 4)) & 1 ? [20, 20, 30] : [230, 230, 240]),
};

const hex = (m) => Array.from(m, (b) => b.toString(16).padStart(2, '0')).join(' ');
for (const [name, rgba] of Object.entries(PICS)) {
    for (const dither of [false, true]) {
        if (dither && name !== 'gradient') continue;
        const px = to565(rgba, dither), coded = encode(px), back = decode(coded);
        const tag = name + (dither ? '-dither' : '');
        ok(back && back.every((v, i) => v === px[i]), `${tag}: decode(encode(x)) == x`);
        ok(coded.length <= MAX_CODED, `${tag}: within MAX_CODED`);
        const msgs = pictureMsgs(coded, true);
        ok(msgs.every((m) => m.length - 2 <= 640 && m.slice(1, -1).every((b) => b < 0x80)), `${tag}: 7-bit, fits 640`);
        console.log(`${tag.padEnd(16)} ${String(coded.length).padStart(6)} B coded, ${msgs.length} messages`);
        writeFileSync(`${out}/${tag}.syx`, msgs.map(hex).join('\n') + '\n');
        writeFileSync(`${out}/${tag}-live.syx`, pictureMsgs(coded, false).map(hex).join('\n') + '\n');
        writeFileSync(`${out}/${tag}.raw`, px);
        // the preview path: 565 back to RGBA keeps 565 exactly
        ok(to565(from565(px)).every((v, i) => v === px[i]), `${tag}: from565 round trip`);
    }
}

// a picture whose CRC is wrong, and one cut short (END before all its bytes)
{
    const coded = encode(to565(PICS.second));
    const msgs = pictureMsgs(coded, true);
    msgs[msgs.length - 1] = endMsg((crc32(coded) ^ 1) >>> 0);
    writeFileSync(`${out}/badcrc.syx`, msgs.map(hex).join('\n') + '\n');
    const short = [beginMsg(coded.length, true), dataMsg(0, coded.subarray(0, CHUNK)), endMsg(crc32(coded))];
    writeFileSync(`${out}/short.syx`, short.map(hex).join('\n') + '\n');
    // a picture of the right length that does not decode to 240 x 240
    const bad = coded.slice(0, coded.length - 3);
    writeFileSync(`${out}/baddata.syx`, pictureMsgs(bad, true).map(hex).join('\n') + '\n');
}
writeFileSync(`${out}/clear.syx`, hex(clearMsg()) + '\n');
writeFileSync(`${out}/info.syx`, hex(infoMsg()) + '\n');
writeFileSync(`${out}/show0.syx`, hex(showMsg(0)) + '\n');
writeFileSync(`${out}/show9.syx`, hex(showMsg(9)) + '\n');

// pack7 and the coder's edges
ok(pack7(new Uint8Array([0x80, 1, 2, 3, 4, 5, 6, 0xff])).join() === [0x01, 0, 1, 2, 3, 4, 5, 6, 0x01, 0x7f].join(), 'pack7');
{
    const flat = new Uint8Array(PIX * 2).fill(0x12);
    ok(encode(flat).length === Math.ceil(PIX / 128) * 3, 'flat: runs of 128');
    ok(decode(new Uint8Array([0x80, 1, 2])) === null, 'decode: short picture refused');
}
ok(crc32(new TextEncoder().encode('123456789')) === 0xcbf43926, 'crc32 check value');

// the page's Device against a fake FM-1 (mock.c's rules) that loses some replies: it must resend and
// carry on from what the device says it holds
{
    const unpack = (b) => { const o = []; for (let i = 0; i < b.length;) { const m = b[i++]; for (let j = 0; j < 7 && i < b.length; j++) o.push(b[i++] | (((m >> j) & 1) << 7)); } return o; };
    const g7 = (b, k) => b.slice(0, k).reduce((v, x, i) => v + x * 2 ** (7 * i), 0);
    const p7 = (v, k) => Array.from({ length: k }, (_, i) => Math.floor(v / 2 ** (7 * i)) & 0x7f);
    const dev = { buf: null, got: 0, len: 0, shown: null };
    let n = 0;
    const input = { onmidimessage: null };
    const output = { send(m) {
        const cmd = m[3], b = Array.from(m.subarray(4, m.length - 1));
        let rep = [0];
        if (cmd === 1) { dev.len = g7(b, 5); dev.buf = new Uint8Array(dev.len); dev.got = 0; }
        else if (cmd === 2) {
            const off = g7(b, 3), d = unpack(b.slice(3));
            if (off <= dev.got) { dev.buf.set(d, off); dev.got = Math.max(dev.got, off + d.length); }
            rep = [0, ...p7(dev.got, 3)];
        } else if (cmd === 3) {
            const okc = dev.got === dev.len && crc32(dev.buf) === g7(b, 5);
            if (okc) dev.shown = decode(dev.buf);
            rep = [okc ? 0 : 2, 0, 0, 1, 0];
        }
        n++;
        if (n === 5 || n === 9) return;            // lost replies
        setTimeout(() => input.onmidimessage({ data: new Uint8Array([0xf0, 0x7d, 0x4d, 0x70 | cmd, ...rep, 0xf7]) }), 1);
    } };
    const d = new Device(input, output);
    const px = to565(PICS.gradient), coded = encode(px);
    let last = 0;
    const r = await d.send(coded, true, (f) => { last = f; });
    ok(r.count === 1 && last === 1, 'Device.send: finished, progress to 1');
    ok(dev.shown && dev.shown.every((v, i) => v === px[i]), 'Device.send: the fake FM-1 got the picture, despite lost replies');
}
console.log(fails ? `${fails} FAILED` : 'mocky.js ok');
process.exit(fails ? 1 : 0);
