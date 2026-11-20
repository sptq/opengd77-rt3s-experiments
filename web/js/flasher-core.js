// Firmware image preparation for the OpenGD77 STM32 radios (RT3S / MD-UV380, RT-84 / DM-1701),
// ported from MDUV380_firmware/tools/opengd77_stm32_firmware_loader.py (F1RMB, VK3KYY).
// No DOM, no USB: works in the browser and in Node (tests compare it with the Python loader).

import { MD9600_CIPHER, MDUV380_CIPHER, MD380_CIPHER, DM1701_CIPHER, DONOR_SHA256 } from './ciphers.js';

export { DONOR_SHA256 };

export const PLATFORMS = {
  'MD-UV380': MDUV380_CIPHER,
  'MD-2017': MDUV380_CIPHER,
  'MD-380': MD380_CIPHER,
  'DM-1701': DM1701_CIPHER,
  'MD-9600': MD9600_CIPHER,
};

// Flash sectors written by the loader, their sizes and the last DFU block number of each
export const ADDRESSES = [0x0800C000, 0x08010000, 0x08020000, 0x08040000, 0x08060000,
  0x08080000, 0x080A0000, 0x080C0000, 0x080E0000, 0x08100000];
export const SIZES = [0x4000, 0x10000, 0x20000, 0x20000, 0x20000, 0x20000, 0x20000, 0x20000, 0x20000, 0x20000];
const BLOCK_ENDS = [0x11, 0x41, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81];
export const BLOCK_SIZE = 1024;

const OFFICIAL_HEADER = 'OutSecurityBin\0\0';
const CODEC_DONOR_OFFSET = 0xC2C7C;
const CODEC_LENGTH = 0x48BB0;
const CODEC_TARGET = 0x6937C;
const MAX_FIRMWARE = SIZES.reduce((a, b) => a + b, 0);

const enc = new TextEncoder();

function find(buf, needle, start = 0, end = buf.length) {
  const n = (typeof needle === 'string') ? enc.encode(needle) : needle;
  const last = Math.min(end, buf.length) - n.length;
  outer: for (let i = Math.max(0, start); i <= last; i++) {
    for (let j = 0; j < n.length; j++) {
      if (buf[i + j] !== n[j]) continue outer;
    }
    return i;
  }
  return -1;
}

function be32(buf, off) {
  return ((buf[off] << 24) | (buf[off + 1] << 16) | (buf[off + 2] << 8) | buf[off + 3]) >>> 0;
}

export function isOfficialFirmware(fw) {
  return find(fw, OFFICIAL_HEADER, 0, OFFICIAL_HEADER.length) === 0;
}

// Language pack version stored in a .gla file (null: not a language pack)
export function languageVersion(gla) {
  return (find(gla, 'GD77LANG', 0, 8) === 0) ? be32(gla, 8) : null;
}

// Version of the language table the firmware expects (the English table, the first one)
export function firmwareLanguageVersion(fw) {
  let found = find(fw, 'GD77LANG');
  while (found > 0) {
    if (find(fw, 'English', found + 12, found + 19) === found + 12) {
      return be32(fw, found + 8);
    }
    found = find(fw, 'GD77LANG', found + 8);
  }
  return null;
}

// Copies the .gla over the second language slot of the firmware.
// Returns 'merged', 'ignored' (firmware without languages), 'mismatch', 'badfile' or 'noslot'.
export function mergeLanguage(fw, gla) {
  if (find(fw, 'IGNRLANG') >= 0) return 'ignored';
  const langVersion = languageVersion(gla);
  if (langVersion === null) return 'badfile';

  let found = find(fw, 'GD77LANG');
  while (found > 0) {
    if (find(fw, 'English', found + 12, found + 19) === found + 12) {
      if (be32(fw, found + 8) !== langVersion) return 'mismatch';
      const slot = find(fw, 'GD77LANG', found + 12);
      if (slot <= 0 || slot + gla.length > fw.length) return 'noslot';
      fw.set(gla, slot);
      return 'merged';
    }
    found = find(fw, 'GD77LANG', found + 8);
  }
  return 'noslot';
}

// Copies the AMBE codec from the MD-9600 V26.45 donor firmware (decrypted) into the open firmware.
export function patchCodec(fw, donor) {
  if (donor.length < CODEC_DONOR_OFFSET + CODEC_LENGTH) return false;
  if (fw.length < CODEC_LENGTH + CODEC_TARGET) return false;
  const shift = CODEC_TARGET % 1024;
  for (let j = 0; j < CODEC_LENGTH; j++) {
    fw[CODEC_TARGET + j] = donor[CODEC_DONOR_OFFSET + j] ^ MD9600_CIPHER[(j + shift) % 1024];
  }
  return true;
}

export function encrypt(fw, platform) {
  const cipher = PLATFORMS[platform];
  if (!cipher) throw new Error(`unknown model ${platform}`);
  for (let j = 0; j < fw.length; j++) fw[j] ^= cipher[j % 1024];
}

// Builds the encrypted image. firmware / language / donor are Uint8Arrays (language and donor optional).
// Returns { image, dmr, language } where language is the mergeLanguage() result or null.
export function buildImage({ firmware, language = null, donor = null, platform }) {
  if (firmware.length > MAX_FIRMWARE) throw new Error('firmware file is too large');
  if (isOfficialFirmware(firmware)) throw new Error('this is the original (donor) firmware, not OpenGD77');
  const fw = new Uint8Array(firmware);
  const lang = language ? mergeLanguage(fw, language) : null;
  const dmr = donor ? patchCodec(fw, donor) : false;
  encrypt(fw, platform);
  return { image: fw, dmr, language: lang };
}

// The DFU write plan, exactly as the Python loader sends it: per sector, block numbers from 2,
// 1024-byte blocks padded with 0xFF.
export function writePlan(image) {
  const sectors = [];
  let offset = 0;
  for (let s = 0; s < ADDRESSES.length; s++) {
    const blocks = [];
    let written = 0;
    let blockNumber = 2;
    while (offset < image.length && written < SIZES[s]) {
      if (blockNumber > BLOCK_ENDS[s]) throw new Error('block overflow');
      const chunk = new Uint8Array(BLOCK_SIZE).fill(0xFF);
      chunk.set(image.subarray(offset, offset + BLOCK_SIZE));
      blocks.push({ blockNumber, data: chunk });
      offset += BLOCK_SIZE;
      written += BLOCK_SIZE;
      blockNumber++;
    }
    sectors.push({ address: ADDRESSES[s], size: SIZES[s], blocks });
  }
  return sectors;
}

export async function sha256Hex(data) {
  const digest = await crypto.subtle.digest('SHA-256', data);
  return Array.from(new Uint8Array(digest), (b) => b.toString(16).padStart(2, '0')).join('');
}
