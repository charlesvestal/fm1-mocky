/* SPDX-License-Identifier: GPL-3.0-only */
/* Mocky platform interface (from FoMni's): everything the app needs from the hardware.
 *
 * The device implementation (plat_fm1.c) wraps Felucca's HAL, panel map, USB SysEx and flash; the
 * host implementation (host/mock_host.c) is a simulator fed by a script, which is what lets the
 * whole app run and be tested on a Mac. Drawing goes through Felucca's lcd_fill / lcd_blit (lcd.c on
 * the device, a framebuffer on the host) and gfx.c on top. */
#pragma once
#include <stdint.h>

/* big zero-initialised buffers go in the .pool RAM region on the device */
#ifdef OM_HOST
#define OM_POOL
#else
#define OM_POOL __attribute__((section(".pool")))
#endif

/* panel controls, by printed label (Felucca's panel.c map turns these into matrix ids; Felucca
 * calls the second button SCL, the panel prints SEL) */
enum { B_FX, B_SEL, B_ENV, B_LFO, B_EDIT, B_GLO, B_HOME, B_SAVE, B_ARP, B_SEQ, B_PLAY, B_REC,
       B_OCTDN, B_OCTUP, NB };
enum { EN_SELECT, EN_ALGO, EN_PRESET, EN_K1, EN_K2, EN_K3, EN_K4, NE };

#define NKEYS 27                       /* F3 .. G5 */

uint32_t plat_ms(void);
uint32_t plat_buttons(void);           /* held: bit B_* */
uint32_t plat_keys(void);              /* held: bit k = key k (0 = F3) */
int32_t plat_enc(int role);            /* detents since the last call, + = clockwise; EN_* */
void plat_leds(uint32_t buttons, uint32_t keys);   /* lit: bit B_*, bit key */
void plat_glow(int on);                /* the unlit buttons glow dimly, readable in the dark (call before plat_leds) */
void plat_idle(void);                  /* called inside long jobs (flash): watchdog */

/* USB SysEx: one received message at a time (the 7-bit bytes between F0 and F7) */
int plat_sysex_get(const uint8_t **p, uint32_t *n);   /* 1 = got one; release it with plat_sysex_done */
void plat_sysex_done(void);
int plat_sysex_send(const uint8_t *p, uint32_t n);    /* one whole F0 .. F7 message; 0 = queued */

/* persistent storage (Felucca storage.c objects: A/B sectors, CRC, torn-write safe) */
int plat_store_load(uint32_t obj, void *dst, uint32_t max);   /* bytes loaded, < 0 = none / bad */
int plat_store_save(uint32_t obj, const void *src, uint32_t len);   /* 0 = ok */
#define PLAT_STORE_MAX 3840u           /* bytes per object */
enum { OBJ_PROJ, OBJ_PANEL, OBJ_NOBJ };
/* flash: OBJ_PROJ 0xFC000, OBJ_PANEL 0xDC000 (2 sectors each, inside Felucca's data area) */

/* the picture gallery: a raw flash area, offsets from its start, 4 KiB sectors (0 = ok) */
#define GAL_BASE 0x97000u              /* Felucca's main store, up to the panel calibration */
#define GAL_SIZE (0xDC000u - GAL_BASE) /* 276 KiB */
int plat_gal_read(uint32_t off, void *dst, uint32_t n);
int plat_gal_erase(uint32_t off);      /* the 4 KiB sector at off */
int plat_gal_prog(uint32_t off, const void *src, uint32_t n);   /* src in RAM */
