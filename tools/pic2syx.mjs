// SPDX-License-Identifier: GPL-3.0-only
// A 240 x 240 RGBA file (raw bytes) -> the SysEx messages the web page sends for it, one per line in
// hex (the simulator's "send"). For tools/gen_screens.sh.
//   node tools/pic2syx.mjs IN.rgba OUT.syx [keep 0|1]
import { readFileSync, writeFileSync } from 'node:fs';
import { to565, encode, pictureMsgs } from '../web/mocky.js';

const [inp, out, keep = '1'] = process.argv.slice(2);
const msgs = pictureMsgs(encode(to565(readFileSync(inp))), keep === '1');
writeFileSync(out, msgs.map((m) => Array.from(m, (b) => b.toString(16).padStart(2, '0')).join(' ')).join('\n') + '\n');
