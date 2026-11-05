# Mocky licensing

Mocky is free software under the GNU General Public License, version 3 only
(`GPL-3.0-only`, full text in `LICENSE`). It is built on Felucca's platform layer (through X0X and
FoMni-1) and keeps Felucca's licence. If you distribute Mocky, or firmware derived from it, you must
give your recipients its complete corresponding source under the same licence.

## Where the code comes from

| What | Origin | Licence |
| --- | --- | --- |
| Platform: `firmware/hal/`, `firmware/loader/`, `firmware/src/{libc,lcd,gfx,usb,storage,ota,main}.c`, `tools/` (build, package, install, rescue), `web/fm1*.js` | [Felucca](https://github.com/hugelton/Felucca), Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments, as changed for [X0X](https://github.com/charlesvestal/fm1-x0x), [FoMni-1](https://github.com/charlesvestal/fm1-fomni) and Mocky | GPL-3.0-only |
| `firmware/src/app/{panel,plat_fm1,main_fm1}.c`, `host/mock_host.c` | adapted from FoMni-1 (itself from X0X and Felucca's `panel.c`, `main.c`, `audio.c`) | GPL-3.0-only |
| The screen reader's firmware side: `firmware/src/announce.c`, `tools/gen_words.py` | [fm1-base](https://github.com/charlesvestal/fm1-base) (`device/announce.c`, `tools/gen_words.py`), Copyright (C) 2026 Charles Vestal, copied by [fm1-reader](https://github.com/charlesvestal/fm1-reader)'s `tools/sync_announce.sh` | MPL-2.0 (compatible with the GPL: MPL-2.0 section 3.3) |
| The flash parts the storage may use: `firmware/hal/fm1_flash_ok.h`, `tests/host/flash_ok_test.c` | [fm1-base](https://github.com/charlesvestal/fm1-base) (`device/fm1_flash_ok.h`, `tests/flash_ok_test.c`, fm1-base#35), Copyright (C) 2026 Laurens (@profikid) | MPL-2.0 (compatible with the GPL: MPL-2.0 section 3.3) |
| The update handshake's tests: `tests/ota_hs_test.c`, `tests/host/installer_hs_test.mjs` | [felucca-bsp](https://github.com/hugelton/felucca-bsp)'s `tests/ota_test.c` and [fm1-base](https://github.com/charlesvestal/fm1-base)'s `tests/installer_hs_test.mjs`, Copyright (C) 2026 Charles Vestal | MPL-2.0 (compatible with the GPL: MPL-2.0 section 3.3) |
| Everything else: `firmware/src/app/{mock,ui,project}.c` (the pictures, the gallery, the SysEx protocol), `web/mocky.js` and `web/index.html` (the page that sends pictures), `tests/` | Mocky | GPL-3.0-only |

## Third-party material

| What | Licence | Where |
| --- | --- | --- |
| Barlow Semi Condensed (The Barlow Project Authors), the UI face | SIL OFL 1.1 | `assets/fonts/BarlowSemiCondensed-*.ttf`, `assets/fonts/Barlow-OFL.txt` |
| Terminus (Dimitar Toshkov Zhekov), an alternative font set | SIL OFL 1.1 | `assets/fonts/ter-u*.bdf`, `assets/fonts/Terminus-LICENSE.txt` |
| JieLi AC79 SDK: `uboot.boot`, `cfg_tool.bin`, `eq_cfg_hw.bin` are read from your SDK checkout at build time and placed in the package; no SDK files are in this tree | Apache-2.0 | <https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK> |

## Trademarks

"Felucca" and "Hügelton Instruments" are names of Hügelton Instruments. "M-VAVE" and "FM-1" are
trademarks of their respective owners. Mocky is independent firmware; it is not affiliated with,
endorsed by or supported by any of them.

## Radio

Mocky never enables the Bluetooth / Wi-Fi radio of the hardware.
