// SPDX-License-Identifier: GPL-3.0-only
// Mocky: pictures for the FM-1's screen. The picture format and the SysEx protocol of
// firmware/src/app/mock.c, for the web page (index.html) and the tests (tests/host/mock_test.mjs).

export const W = 240, H = 240, PIX = W * H;
export const MAX_CODED = PIX * 2 + PIX / 128 + 8;
const MAN = [0x7d, 0x4d];                          // non-commercial id, 'M'
export const STATUS = ['ok', 'bad length', 'bad CRC', 'bad picture', 'the gallery is full', 'flash error',
                       'out of step', 'unknown command'];

// ---------------------------------------------------------------- pixels
// RGBA (canvas ImageData: data, width = height = 240) -> RGB565, 2 bytes a pixel, high byte first.
// dither: a 4x4 ordered dither, for photos and gradients (flat mockups code smaller without)
const BAYER = [0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5];
export function to565(rgba, dither = false) {
    const out = new Uint8Array(PIX * 2);
    for (let i = 0, y = 0; y < H; y++)
        for (let x = 0; x < W; x++, i++) {
            let r = rgba[i * 4], g = rgba[i * 4 + 1], b = rgba[i * 4 + 2];
            if (dither) {
                const t = BAYER[(y & 3) * 4 + (x & 3)] / 16 - 0.5;
                r = Math.min(255, Math.max(0, r + t * 8));
                g = Math.min(255, Math.max(0, g + t * 4));
                b = Math.min(255, Math.max(0, b + t * 8));
            }
            const v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
            out[i * 2] = v >> 8;
            out[i * 2 + 1] = v & 0xff;
        }
    return out;
}

// RGB565 (high byte first) -> RGBA, for the preview of what the screen shows
export function from565(px, rgba = new Uint8ClampedArray(PIX * 4)) {
    for (let i = 0; i < PIX; i++) {
        const v = (px[i * 2] << 8) | px[i * 2 + 1], r = v >> 11, g = (v >> 5) & 63, b = v & 31;
        rgba[i * 4] = (r << 3) | (r >> 2);
        rgba[i * 4 + 1] = (g << 2) | (g >> 4);
        rgba[i * 4 + 2] = (b << 3) | (b >> 2);
        rgba[i * 4 + 3] = 255;
    }
    return rgba;
}

// ------------------------------------------------------------ run length
// c < 0x80: c + 1 pixels follow; c >= 0x80: one pixel follows, (c & 0x7f) + 1 times
export function encode(px) {
    const n = px.length / 2, out = new Uint8Array(MAX_CODED);
    const same = (a, b) => px[a * 2] === px[b * 2] && px[a * 2 + 1] === px[b * 2 + 1];
    let o = 0, i = 0, lit = -1;                    // lit: where the open literal's count byte is
    while (i < n) {
        let r = 1;
        while (i + r < n && r < 128 && same(i, i + r)) r++;
        if (r >= 3 || (r === 2 && lit < 0)) {      // a run (a pair only between runs: it costs 1 more in a literal)
            out[o++] = 0x80 | (r - 1);
            out[o++] = px[i * 2];
            out[o++] = px[i * 2 + 1];
            lit = -1;
            i += r;
        } else {
            if (lit < 0 || out[lit] === 0x7f) {
                lit = o;
                out[o++] = 0xff;                   // counted up from -1 below
            }
            out[lit] = (out[lit] + 1) & 0xff;
            out[o++] = px[i * 2];
            out[o++] = px[i * 2 + 1];
            i++;
        }
    }
    return out.slice(0, o);
}

export function decode(s) {                        // as mock.c's mk_decode; null if not one whole picture
    const out = new Uint8Array(PIX * 2);
    let i = 0, o = 0;
    while (i < s.length) {
        const c = s[i++], k = 2 * ((c & 0x7f) + 1);
        if (o + k > out.length) return null;
        if (c < 0x80) {
            if (i + k > s.length) return null;
            out.set(s.subarray(i, i + k), o);
            i += k;
        } else {
            if (i + 2 > s.length) return null;
            for (let j = 0; j < k; j += 2) { out[o + j] = s[i]; out[o + j + 1] = s[i + 1]; }
            i += 2;
        }
        o += k;
    }
    return o === out.length ? out : null;
}

const CRC_T = (() => {
    const t = new Uint32Array(256);
    for (let n = 0; n < 256; n++) {
        let c = n;
        for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
        t[n] = c >>> 0;
    }
    return t;
})();
export function crc32(b) {
    let c = 0xffffffff;
    for (let i = 0; i < b.length; i++) c = CRC_T[(c ^ b[i]) & 0xff] ^ (c >>> 8);
    return (c ^ 0xffffffff) >>> 0;
}

// --------------------------------------------------------------- SysEx
const put7 = (v, k) => Array.from({ length: k }, (_, i) => Math.floor(v / 2 ** (7 * i)) & 0x7f);
const get7 = (b, k) => b.slice(0, k).reduce((v, x, i) => v + x * 2 ** (7 * i), 0);
export function pack7(b) {                         // 7 bytes -> a high-bit byte, then the 7 low parts
    const out = [];
    for (let i = 0; i < b.length; i += 7) {
        const g = b.subarray(i, i + 7);
        out.push(g.reduce((m, x, j) => m | ((x >> 7) << j), 0), ...Array.from(g, (x) => x & 0x7f));
    }
    return out;
}
const msg = (cmd, ...body) => new Uint8Array([0xf0, ...MAN, cmd, ...body, 0xf7]);
export const CHUNK = 504;                          // coded bytes a message: 576 packed + 7 fit usb.c's 640
export const beginMsg = (len, keep) => msg(1, ...put7(len, 5), keep ? 1 : 0);
export const dataMsg = (off, bytes) => msg(2, ...put7(off, 3), ...pack7(bytes));
export const endMsg = (crc) => msg(3, ...put7(crc, 5));
export const clearMsg = () => msg(4);
export const infoMsg = () => msg(5);
export const showMsg = (i) => msg(6, ...put7(i, 2));

// every message of one picture, in order (for the tests; the page sends them one reply at a time)
export function pictureMsgs(coded, keep) {
    const m = [beginMsg(coded.length, keep)];
    for (let off = 0; off < coded.length; off += CHUNK) m.push(dataMsg(off, coded.subarray(off, off + CHUNK)));
    m.push(endMsg(crc32(coded)));
    return m;
}

// a reply F0 7D 4D 7x st ... F7 -> {cmd, status, body}, or null
export function parseReply(d) {
    if (d.length < 6 || d[0] !== 0xf0 || d[1] !== MAN[0] || d[2] !== MAN[1] || (d[3] & 0xf0) !== 0x70) return null;
    return { cmd: d[3] & 0x0f, status: d[4], body: Array.from(d.subarray(5, d.length - 1)) };
}

// ------------------------------------------------------------- the FM-1
// talks to the FM-1 over Web MIDI: one message, one reply
export class Device {
    constructor(input, output) {
        this.input = input;
        this.output = output;
        this.wait = null;
        input.onmidimessage = (e) => {
            const r = parseReply(e.data);
            if (r && this.wait && this.wait.cmd === r.cmd) {
                const w = this.wait;
                this.wait = null;
                clearTimeout(w.timer);
                w.resolve(r);
            }
        };
    }

    close() { this.input.onmidimessage = null; }

    request(m, ms = 1000, tries = 3) {
        const cmd = m[3];
        return new Promise((resolve, reject) => {
            const go = (left) => {
                this.wait = {
                    cmd, resolve,
                    timer: setTimeout(() => {
                        this.wait = null;
                        if (left > 1) go(left - 1);
                        else reject(new Error('the FM-1 did not answer'));
                    }, ms),
                };
                this.output.send(m);
            };
            go(tries);
        });
    }

    static check(r) {
        if (r.status) throw new Error(STATUS[r.status] || `status ${r.status}`);
        return r;
    }

    async info() {
        const r = Device.check(await this.request(infoMsg()));
        const b = r.body;
        return { proto: b[0], count: get7(b.slice(1), 2), free: get7(b.slice(3), 3), cur: get7(b.slice(6), 2),
                 max: get7(b.slice(8), 2) };
    }

    // send one coded picture; resolves {cur, count}. progress(fraction)
    async send(coded, keep, progress = () => {}) {
        Device.check(await this.request(beginMsg(coded.length, keep)));
        let off = 0, stuck = 0;
        while (off < coded.length) {
            const r = Device.check(await this.request(dataMsg(off, coded.subarray(off, off + CHUNK))));
            const got = get7(r.body, 3);           // the device says what it holds: go on from there
            stuck = got > off ? 0 : stuck + 1;
            if (stuck > 3) throw new Error('the FM-1 lost the picture');
            off = got;
            progress(off / coded.length);
        }
        const r = Device.check(await this.request(endMsg(crc32(coded)), keep ? 8000 : 2000, 1));
        return { cur: get7(r.body, 2), count: get7(r.body.slice(2), 2) };
    }

    async clear() { Device.check(await this.request(clearMsg(), 8000, 1)); }
    async show(i) { Device.check(await this.request(showMsg(i), 2000)); }
}

// the FM-1's MIDI ports: Mocky's own name first, then anything called FM-1
export function findPorts(access) {
    const pick = (ports) => {
        const all = [...ports.values()];
        return all.find((p) => /mocky/i.test(p.name)) || all.find((p) => /fm-?1/i.test(p.name)) || null;
    };
    const input = pick(access.inputs), output = pick(access.outputs);
    return input && output ? { input, output } : null;
}
