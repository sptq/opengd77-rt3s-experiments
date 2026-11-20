// WebUSB DFU writer for the TYT / Retevis STM32 bootloader ("Digital Radio in USB mode", 0483:df11).
// Same command sequence as MDUV380_firmware/tools/opengd77_stm32_firmware_loader.py.

import { ADDRESSES, writePlan } from './flasher-core.js';

export const USB_FILTER = { vendorId: 0x0483, productId: 0xdf11 };

const DNLOAD = 1, GETSTATUS = 3, CLRSTATUS = 4, ABORT = 6;
const STATE = { IDLE: 2, DNLOAD_BUSY: 4, DNLOAD_IDLE: 5, MANIFEST: 7, UPLOAD_IDLE: 9, ERROR: 10 };
const STATE_NAMES = Object.fromEntries(Object.entries(STATE).map(([k, v]) => [v, k]));

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

export class DfuError extends Error {}

export class Dfu {
  constructor(device) {
    this.device = device;
    this.iface = 0;
  }

  static supported() {
    return ('usb' in navigator) && window.isSecureContext;
  }

  static async request() {
    const device = await navigator.usb.requestDevice({ filters: [USB_FILTER] });
    return new Dfu(device);
  }

  static async known() {
    const devices = await navigator.usb.getDevices();
    const d = devices.find((x) => x.vendorId === USB_FILTER.vendorId && x.productId === USB_FILTER.productId);
    return d ? new Dfu(d) : null;
  }

  get name() {
    return this.device.productName || 'DFU device';
  }

  async open() {
    const d = this.device;
    if (!d.opened) await d.open();
    if (d.configuration === null) await d.selectConfiguration(1);
    try {
      await d.claimInterface(this.iface);
    } catch (e) {
      throw new DfuError('claim');
    }
    // Get the bootloader into the idle state
    for (let i = 0; i < 4; i++) {
      const s = await this.status();
      if (s === STATE.IDLE) break;
      if (s === STATE.DNLOAD_IDLE || s === STATE.UPLOAD_IDLE) await this.abort();
      else await this.clearStatus();
    }
  }

  async close() {
    try { await this.device.close(); } catch (e) { /* the radio may have reset already */ }
  }

  out(request, value, data) {
    return this.device.controlTransferOut(
      { requestType: 'class', recipient: 'interface', request, value, index: this.iface },
      data ?? new Uint8Array(0));
  }

  async status() {
    try {
      const r = await this.device.controlTransferIn(
        { requestType: 'class', recipient: 'interface', request: GETSTATUS, value: 0, index: this.iface }, 6);
      if (r.status !== 'ok' || r.data.byteLength < 6) return null;
      return r.data.getUint8(4);
    } catch (e) {
      return null;
    }
  }

  abort() { return this.out(ABORT, 0); }
  clearStatus() { return this.out(CLRSTATUS, 0); }

  async expect(stage, wanted) {
    let s = await this.status();
    // The Python loader asks once; give a slow sector erase a few more chances
    for (let i = 0; (s === STATE.DNLOAD_BUSY) && (wanted === STATE.DNLOAD_IDLE) && (i < 50); i++) {
      await sleep(20);
      s = await this.status();
    }
    if (s !== wanted) {
      throw new DfuError(`${stage}: ${STATE_NAMES[s] ?? s}`);
    }
  }

  async waitForIdle() {
    let s = await this.status();
    for (let i = 0; s !== STATE.IDLE; i++) {
      if (i > 40) throw new DfuError(`idle: ${STATE_NAMES[s] ?? s}`);
      await this.clearStatus();
      s = await this.status();
    }
  }

  async command(bytes, stage) {
    await this.out(DNLOAD, 0, bytes);
    await this.expect(stage, STATE.DNLOAD_BUSY);
    await this.expect(stage, STATE.DNLOAD_IDLE);
  }

  static addressCommand(code, address) {
    const b = new Uint8Array(5);
    b[0] = code;
    new DataView(b.buffer).setUint32(1, address, true);
    return b;
  }

  erase(address) { return this.command(Dfu.addressCommand(0x41, address), 'erase'); }
  setAddress(address) { return this.command(Dfu.addressCommand(0x21, address), 'address'); }

  async writeBlock(blockNumber, data) {
    await this.out(DNLOAD, blockNumber, data);
    await this.expect('write', STATE.DNLOAD_BUSY);
    await this.expect('write', STATE.DNLOAD_IDLE);
  }

  async custom(a, b) {
    await this.out(DNLOAD, 0, new Uint8Array([a & 0xFF, b & 0xFF]));
    await this.status();
    await sleep(100);
    await this.status();
    await this.waitForIdle();
  }

  // Writes an encrypted image (flasher-core buildImage). onProgress({ phase, done, total })
  async flash(image, onProgress = () => {}) {
    const plan = writePlan(image);
    const total = plan.reduce((n, s) => n + s.blocks.length, 0);
    let done = 0;

    await this.waitForIdle();
    await this.custom(0x91, 0x01);
    await this.custom(0x91, 0x31);

    for (let i = 0; i < ADDRESSES.length; i++) {
      onProgress({ phase: 'erase', done: i, total: ADDRESSES.length });
      await this.erase(ADDRESSES[i]);
    }
    onProgress({ phase: 'erase', done: ADDRESSES.length, total: ADDRESSES.length });

    for (const sector of plan) {
      await this.setAddress(sector.address);
      for (const block of sector.blocks) {
        await this.writeBlock(block.blockNumber, block.data);
        done++;
        if ((done % 4 === 0) || (done === total)) onProgress({ phase: 'write', done, total });
      }
    }

    // Leave DFU: jump address, then an empty download
    await this.setAddress(0x08000000);
    try {
      await this.out(DNLOAD, 0);
      await this.status();
    } catch (e) { /* the radio restarts */ }
    await this.close();
    onProgress({ phase: 'done', done: total, total });
  }
}
