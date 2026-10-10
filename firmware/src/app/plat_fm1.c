/* SPDX-License-Identifier: GPL-3.0-only */
/* Mocky platform on the FM-1: plat.h over Felucca's HAL, panel map, USB SysEx and flash.
 * Part of the unity build (main.c), after the HAL, usb.c, storage.c and panel.c. */

uint32_t plat_ms(void) { return fm1_ms; }

uint32_t plat_buttons(void)
{
    uint32_t raw = fm1_in.buttons, out = 0, i;
    for (i = 0; i < NB; i++)
        if (raw & (1u << panel.btn[i]))
            out |= 1u << i;
    return out;
}

uint32_t plat_keys(void) { return fm1_in.notes & ((1u << NKEYS) - 1u); }

int32_t plat_enc(int role)
{
    if (role < 0 || role >= NE)
        return 0;
    return fm1_enc_take(panel.enc[role]) * panel.dir[role];
}

/* LEDs: the picture is built off-line and copied one byte per column (Felucca: clearing
 * and relighting would let the 10 kHz scan catch the dark gap and flicker) */
static uint8_t led_pos[FM1_NKEY];
static void led_pos_init(void)
{
    uint32_t id, p, r;
    for (id = 0; id < FM1_NKEY; id++) {
        led_pos[id] = 0xFF;
        for (p = 0; p < FM1_NCOL; p++)
            for (r = 1; r < 5u; r++)
                if (FM1_KEYMAP[r][p] == (int8_t)id)
                    led_pos[id] = (uint8_t)((p << 3) | r);
    }
}
/* Glow: every button not lit is lit dim, on one scan frame in GLOW_DIV (the HAL's fm1_led_dim) */
#define GLOW_DIV 4u
static uint8_t glow_on = 1;
void plat_glow(int on) { glow_on = (uint8_t)(on != 0); }
/* PLAY has two LEDs: its red at the button's own position, a green at the keyless matrix position
 * column 8, row PA5 (found on the device). Mocky lights the green one. */
void plat_leds(uint32_t buttons, uint32_t keys)
{
    uint8_t nl[FM1_NCOL] = {0}, dl[FM1_NCOL] = {0};
    uint32_t i, c;
    for (i = 0; i < NB + NKEYS; i++) {
        uint32_t id = i < NB ? panel.btn[i] : 14u + (i - NB);
        int on = i < NB ? (buttons >> i) & 1u : (keys >> (i - NB)) & 1u;
        uint8_t q = led_pos[id];
        if (i == B_PLAY)            /* PLAY's green LED instead (keyless position col 8, row PA5) */
            q = (uint8_t)((8u << 3) | 1u);
        if (q == 0xFF)
            continue;
        if (on)
            nl[q >> 3] |= (uint8_t)(1u << (q & 7u));
        else if (i < NB && glow_on)
            dl[q >> 3] |= (uint8_t)(1u << (q & 7u));
    }
    for (c = 0; c < FM1_NCOL; c++) {
        fm1_led[c] = nl[c];
        fm1_led_dim[c] = dl[c];
    }
    fm1_led_dim_div = glow_on ? GLOW_DIV : 0u;
}

void plat_idle(void) { fm1_wdt_feed(); }

/* SysEx: usb.c's M-UPGRADE frame (FELUCCA_OTA), shared with the updater (main_fm1.c: ours first) */
int plat_sysex_get(const uint8_t **p, uint32_t *n) { return ota_frame_get(p, n); }
void plat_sysex_done(void) { ota_frame_done(); }
int plat_sysex_send(const uint8_t *p, uint32_t n) { return usb.config ? ota_wire_send(p, n) : -1; }

int plat_store_load(uint32_t obj, void *dst, uint32_t max) { return flash_ok ? st_load(obj, dst, max) : -1; }
int plat_store_save(uint32_t obj, const void *src, uint32_t len) { return flash_ok ? st_save(obj, src, len) : -9; }

/* the gallery: Felucca's data area (storage.c's guards: FL_STORE_OK) */
int plat_gal_read(uint32_t off, void *dst, uint32_t n)
{
    if (!flash_ok || off + n > GAL_SIZE)
        return -1;
    return st_read(GAL_BASE + off, dst, n);
}
int plat_gal_erase(uint32_t off)
{
    if (!flash_ok || off + 0x1000u > GAL_SIZE)
        return -1;
    return st_erase(GAL_BASE + off);
}
int plat_gal_prog(uint32_t off, const void *src, uint32_t n)
{
    if (!flash_ok || off + n > GAL_SIZE)
        return -1;
    return st_prog(GAL_BASE + off, src, n);
}
