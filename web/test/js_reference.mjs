// Same output as python_reference.py, from web/js/flasher-core.js:
//   node js_reference.mjs MODEL firmware.bin [lang.gla|-] [donor.bin]
import { readFileSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { buildImage, writePlan, ADDRESSES } from '../js/flasher-core.js';

globalThis.atob ??= (s) => Buffer.from(s, 'base64').toString('binary');
const [model, fw, lang, donor] = process.argv.slice(2);
const read = (p) => new Uint8Array(readFileSync(p));
const { image } = buildImage({
  firmware: read(fw), platform: model,
  language: (lang && lang !== '-') ? read(lang) : null,
  donor: donor ? read(donor) : null,
});
const ops = ['cmd 91 01', 'cmd 91 31', ...ADDRESSES.map((a) => `erase ${a.toString(16).padStart(8, '0')}`)];
for (const s of writePlan(image)) {
  ops.push(`addr ${s.address.toString(16).padStart(8, '0')}`);
  for (const b of s.blocks) ops.push(`write ${b.blockNumber} ${createHash('sha256').update(b.data).digest('hex').slice(0, 16)}`);
}
ops.push('exit');
console.log(ops.join('\n'));
