#!/usr/bin/env python3
# Records what the Python loader would send over DFU (no radio needed), for comparison with
# web/js/flasher-core.js: python3 python_reference.py MODEL firmware.bin [lang.gla] [donor.bin]
import hashlib, os, sys, types

usb = types.ModuleType('usb'); usb.core = types.ModuleType('usb.core'); usb.util = types.ModuleType('usb.util')
usb.util.get_string = lambda dev, index: None
sys.modules.update({'usb': usb, 'usb.core': usb.core, 'usb.util': usb.util})
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'MDUV380_firmware', 'tools'))
import opengd77_stm32_firmware_loader as L

ops = []
L.wait_for_idle = lambda: None
L.send_custom_command = lambda a, b: ops.append(f'cmd {a:02x} {b:02x}')
L.page_erase = lambda a: ops.append(f'erase {a:08x}')
L.set_address = lambda a: ops.append(f'addr {a:08x}')
L.write_firmware_data = lambda n, c: ops.append(f'write {n} {hashlib.sha256(bytes(c)).hexdigest()[:16]}')
L.exit_dfu = lambda *a: ops.append('exit')
L.print = lambda *a, **k: None

model, fw = sys.argv[1], sys.argv[2]
lang = sys.argv[3] if len(sys.argv) > 3 and sys.argv[3] != '-' else None
donor = sys.argv[4] if len(sys.argv) > 4 else ''
L.config = {'GLOBAL': {'SourceSTM32Firmware': donor}}
L.patch_and_download_firmware(fw, lang, L.FWPlatformOutput(L.FWOutputPlatforms.index(model)))
print('\n'.join(ops))
