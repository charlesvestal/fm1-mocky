/* SPDX-License-Identifier: GPL-3.0-only */
/* Mocky's pictures: the framebuffer, the picture format, the gallery in flash and the SysEx
 * protocol the web page (web/mocky.js) talks. Part of the unity build (after app.h); the host
 * simulator includes it too.
 *
 * PICTURE: 240 x 240 RGB565 pixels, 2 bytes each, high byte first (as the panel takes them),
 * run-length coded, row after row:
 *   c < 0x80    c + 1 pixels follow, as they are
 *   c >= 0x80   one pixel follows, repeated (c & 0x7F) + 1 times
 * Flat mockups come out a few KiB; a photo, ~114 KiB.
 *
 * GALLERY: the pictures one after the other from GAL_BASE, each on whole 4 KiB sectors: a 32-byte
 * header (gal_hdr_t), the data at +256. The header is written last (a torn write leaves no picture),
 * and entry k must say k, so the chain ends at the first sector that is not the next picture.
 * Clearing erases the headers last to first: an interrupted clear leaves a shorter gallery.
 *
 * SYSEX (all 7-bit; numbers in 7-bit groups, low first; host -> device, the device answers each
 * with cmd | 0x70 and a status, 0 = ok):
 *   F0 7D 4D 01 len:5 flags F7     begin a picture of len coded bytes (flags bit 0: keep it)
 *   F0 7D 4D 02 off:3 data.. F7    coded bytes at off, 8-to-7 packed (a high-bit byte, then 7)
 *                                  -> 72 st next:3 (the bytes held so far: resend from there)
 *   F0 7D 4D 03 crc:5 F7           done: CRC-32 of the coded bytes; shown, then kept
 *                                  -> 73 st idx:2 count:2
 *   F0 7D 4D 04 F7                 clear the gallery
 *   F0 7D 4D 05 F7                 info -> 75 st proto count:2 free:3 cur:2 max:2
 *   F0 7D 4D 06 idx:2 F7           show a kept picture -> 76 st */
#define MK_W 240u
#define MK_H 240u
#define MK_PIX (MK_W * MK_H)
#define MK_MAX (MK_PIX * 2u + MK_PIX / 128u + 8u)   /* the longest picture: all literal runs */
#define MK_PROTO 1u
#define MK_NONE 0x3FFFu                             /* cur: nothing kept on the screen */

enum { MK_OK, MK_E_LEN, MK_E_CRC, MK_E_DATA, MK_E_FULL, MK_E_FLASH, MK_E_STATE, MK_E_CMD };

static uint8_t mk_fb[MK_PIX * 2u] OM_POOL __attribute__((aligned(4)));   /* the screen, as sent */
static uint8_t mk_buf[MK_MAX] OM_POOL __attribute__((aligned(4)));       /* one coded picture */

static struct {
    uint32_t n;                     /* pictures kept */
    uint32_t cur;                   /* the one on the screen, MK_NONE: a picture just sent, not kept */
    uint32_t shown;                 /* something is on the screen (else the welcome page) */
    uint32_t live_len;              /* mk_buf holds a picture not kept yet (SAVE keeps it) */
    uint32_t up_len, up_got, up_flags, up_on;
    uint32_t changed;               /* the web page changed the screen or the gallery (for the UI) */
    uint32_t got, got_rc;           /* pictures received; the last one's keeping: MK_OK, an error, MK_NOKEEP */
    uint32_t clears;                /* the gallery cleared from the page (for the screen reader) */
} mk;
#define MK_NOKEEP 0xFFu             /* got_rc: sent without keeping */

/* ---------------------------------------------------------------- CRC --- */
static uint32_t mk_crc_step(uint32_t c, const uint8_t *b, uint32_t n)   /* zlib CRC-32, c = ~crc */
{
    static const uint32_t T[16] = {
        0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
        0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu, 0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu};
    while (n--) {
        c ^= *b++;
        c = (c >> 4) ^ T[c & 15u];
        c = (c >> 4) ^ T[c & 15u];
    }
    return c;
}
static uint32_t mk_crc32(const void *p, uint32_t n) { return ~mk_crc_step(0xFFFFFFFFu, p, n); }

/* ------------------------------------------------------------ picture --- */
/* decode s[0..n) into dst (NULL: only check it); 0 = exactly one whole picture */
static int mk_decode(const uint8_t *s, uint32_t n, uint8_t *dst)
{
    uint32_t i = 0, o = 0, j;
    while (i < n) {
        uint32_t c = s[i++], k = 2u * ((c & 0x7Fu) + 1u);
        if (o + k > MK_PIX * 2u)
            return -1;
        if (c < 0x80u) {
            if (i + k > n)
                return -1;
            if (dst)
                for (j = 0; j < k; j++)
                    dst[o + j] = s[i + j];
            i += k;
        } else {
            if (i + 2u > n)
                return -1;
            if (dst)
                for (j = 0; j < k; j += 2u) {
                    dst[o + j] = s[i];
                    dst[o + j + 1u] = s[i + 1u];
                }
            i += 2u;
        }
        o += k;
    }
    return o == MK_PIX * 2u ? 0 : -1;
}

static void mk_present(void)                        /* the framebuffer to the panel, in four strips */
{
    uint32_t y;
    for (y = 0; y < MK_H; y += 60u)
        lcd_blit(0, y, MK_W, 60u, (const uint16_t *)(const void *)(mk_fb + y * MK_W * 2u));
}

/* the framebuffer's rows y0 .. y1-1 again (after a note drawn over them) */
static void mk_present_rows(uint32_t y0, uint32_t y1)
{
    if (mk.shown && y1 > y0)
        lcd_blit(0, y0, MK_W, y1 - y0, (const uint16_t *)(const void *)(mk_fb + y0 * MK_W * 2u));
}

/* ------------------------------------------------------------ gallery --- */
#define GAL_MAGIC 0x31504B4Du                       /* "MKP1" */
#define GAL_DATA 256u
#define GAL_MAXN 64u
typedef struct {
    uint32_t magic;
    uint16_t idx, rsv0;
    uint32_t len, crc;
    uint16_t w, h;
    uint32_t rsv[2];
    uint32_t hcrc;
} gal_hdr_t;
static uint32_t gal_off[GAL_MAXN + 1u];             /* where each picture starts; [n]: the free space */
static uint8_t gal_tmp[256] __attribute__((aligned(4)));

static uint32_t gal_span(uint32_t len) { return (GAL_DATA + len + 0xFFFu) & ~0xFFFu; }

static int gal_head(uint32_t off, uint32_t idx, gal_hdr_t *h)
{
    if (off + GAL_DATA > GAL_SIZE || plat_gal_read(off, h, sizeof *h))
        return -1;
    if (h->magic != GAL_MAGIC || h->idx != idx || !h->len || h->len > MK_MAX || h->w != MK_W || h->h != MK_H ||
        off + gal_span(h->len) > GAL_SIZE || h->hcrc != mk_crc32(h, sizeof *h - 4u))
        return -1;
    return 0;
}

static void gal_scan(void)
{
    gal_hdr_t h;
    uint32_t off = 0;
    mk.n = 0;
    while (mk.n < GAL_MAXN && gal_head(off, mk.n, &h) == 0) {
        gal_off[mk.n++] = off;
        off += gal_span(h.len);
    }
    gal_off[mk.n] = off;
}

static uint32_t gal_free(void)
{
    uint32_t end = gal_off[mk.n];
    return mk.n >= GAL_MAXN || end + GAL_DATA >= GAL_SIZE ? 0u : GAL_SIZE - end - GAL_DATA;
}

/* keep coded picture b[0..len) as the next one; MK_OK or an error */
static int gal_add(const uint8_t *b, uint32_t len)
{
    gal_hdr_t h;
    uint32_t off = gal_off[mk.n], span = gal_span(len), i, c;
    if (mk.n >= GAL_MAXN || off + span > GAL_SIZE)
        return MK_E_FULL;
    for (i = 0; i < span; i += 0x1000u) {
        if (plat_gal_erase(off + i))
            return MK_E_FLASH;
        plat_idle();
    }
    for (i = 0; i < len; i += 256u) {               /* the data first, page by page */
        uint32_t k = len - i > 256u ? 256u : len - i;
        if (plat_gal_prog(off + GAL_DATA + i, b + i, k))
            return MK_E_FLASH;
        if ((i & 0xFFFu) == 0)
            plat_idle();
    }
    for (i = 0, c = 0xFFFFFFFFu; i < len; i += 256u) {   /* read back before the header commits it */
        uint32_t k = len - i > 256u ? 256u : len - i;
        if (plat_gal_read(off + GAL_DATA + i, gal_tmp, k))
            return MK_E_FLASH;
        c = mk_crc_step(c, gal_tmp, k);
    }
    memset(&h, 0xFF, sizeof h);
    h.magic = GAL_MAGIC;
    h.idx = (uint16_t)mk.n;
    h.len = len;
    h.crc = mk_crc32(b, len);
    h.w = MK_W;
    h.h = MK_H;
    h.hcrc = mk_crc32(&h, sizeof h - 4u);
    if (~c != h.crc)
        return MK_E_FLASH;
    for (i = 0; i < sizeof h; i++)
        gal_tmp[i] = ((const uint8_t *)&h)[i];      /* the driver wants RAM sources */
    if (plat_gal_prog(off, gal_tmp, sizeof h) || gal_head(off, mk.n, &h))
        return MK_E_FLASH;
    mk.n++;
    gal_off[mk.n] = off + span;
    return MK_OK;
}

static int gal_clear(void)
{
    int rc = MK_OK;
    while (mk.n) {                                  /* last to first: a torn clear keeps a prefix */
        if (plat_gal_erase(gal_off[mk.n - 1u]))
            rc = MK_E_FLASH;
        plat_idle();
        mk.n--;
    }
    gal_off[0] = 0;
    gal_scan();                                     /* what is really left */
    return rc;
}

/* picture i to the screen; MK_OK or an error (the screen is left as it was) */
static int mk_show(uint32_t i)
{
    gal_hdr_t h;
    if (i >= mk.n)
        return MK_E_STATE;
    if (gal_head(gal_off[i], i, &h) || plat_gal_read(gal_off[i] + GAL_DATA, mk_buf, h.len))
        return MK_E_FLASH;
    mk.live_len = 0;                                /* mk_buf now holds a kept picture */
    if (mk_crc32(mk_buf, h.len) != h.crc || mk_decode(mk_buf, h.len, 0))
        return MK_E_DATA;
    mk_decode(mk_buf, h.len, mk_fb);
    mk.cur = i;
    mk.shown = 1;
    mk_present();
    return MK_OK;
}

/* SAVE on the panel: keep the picture just sent */
static int mk_keep_live(void)
{
    int rc;
    if (!mk.live_len)
        return MK_E_STATE;
    rc = gal_add(mk_buf, mk.live_len);
    if (rc == MK_OK) {
        mk.live_len = 0;
        mk.cur = mk.n - 1u;
    }
    return rc;
}

/* ----------------------------------------------------------- protocol --- */
static uint32_t mk_get7(const uint8_t *p, uint32_t k)
{
    uint32_t v = 0, i;
    for (i = 0; i < k; i++)
        v |= (uint32_t)(p[i] & 0x7Fu) << (7u * i);
    return v;
}
static uint32_t mk_put7(uint8_t *p, uint32_t v, uint32_t k)
{
    uint32_t i;
    for (i = 0; i < k; i++)
        p[i] = (uint8_t)((v >> (7u * i)) & 0x7Fu);
    return k;
}

static void mk_reply(uint32_t cmd, uint32_t st, const uint8_t *more, uint32_t nmore)
{
    uint8_t b[24];
    uint32_t n = 0, i;
    b[n++] = 0xF0;
    b[n++] = 0x7D;
    b[n++] = 0x4D;
    b[n++] = (uint8_t)(0x70u | cmd);
    b[n++] = (uint8_t)st;
    for (i = 0; i < nmore && n < sizeof b - 1u; i++)
        b[n++] = more[i];
    b[n++] = 0xF7;
    plat_sysex_send(b, n);
}

static void mk_data(const uint8_t *p, uint32_t n)   /* p: off:3 then the packed bytes */
{
    uint8_t r[3];
    uint32_t off, len, i, o;
    if (n < 3u || !mk.up_on) {
        mk_reply(2, MK_E_STATE, 0, 0);
        return;
    }
    off = mk_get7(p, 3);
    p += 3;
    n -= 3u;
    len = n - (n + 7u) / 8u;                        /* 8 wire bytes carry 7 */
    if (off > mk.up_got || off + len > mk.up_len) {
        mk_reply(2, off > mk.up_got ? MK_OK : MK_E_LEN, r, mk_put7(r, mk.up_got, 3));
        return;
    }
    for (o = off; n;) {
        uint32_t m = *p++, k = n - 1u < 7u ? n - 1u : 7u;
        for (i = 0; i < k; i++)
            mk_buf[o++] = (uint8_t)(p[i] | (((m >> i) & 1u) << 7));
        p += k;
        n -= k + 1u;
    }
    if (off + len > mk.up_got)
        mk.up_got = off + len;
    mk_reply(2, MK_OK, r, mk_put7(r, mk.up_got, 3));
}

static void mk_end(const uint8_t *p, uint32_t n)
{
    uint8_t r[4];
    uint32_t st = MK_OK, len = mk.up_len;
    if (n < 5u || !mk.up_on || mk.up_got != len)
        st = MK_E_STATE;
    else if (mk_crc32(mk_buf, len) != mk_get7(p, 5))
        st = MK_E_CRC;
    else if (mk_decode(mk_buf, len, 0))
        st = MK_E_DATA;
    mk.up_on = 0;
    if (st == MK_OK) {
        mk_decode(mk_buf, len, mk_fb);
        mk.shown = 1;
        mk.cur = MK_NONE;
        mk.live_len = len;
        mk.changed = 1;
        mk_present();                               /* on the screen first: keeping it takes a moment */
        if (mk.up_flags & 1u)
            st = (uint32_t)mk_keep_live();
        mk.got++;
        mk.got_rc = mk.up_flags & 1u ? st : MK_NOKEEP;
    }
    mk_put7(r, mk.cur, 2);
    mk_put7(r + 2, mk.n, 2);
    mk_reply(3, st, r, 4);
}

/* one SysEx of ours, if any (other SysEx is left for the updater); 1 = handled one */
static int mk_service(void)
{
    const uint8_t *p;
    uint32_t n, cmd;
    uint8_t r[12];
    if (!plat_sysex_get(&p, &n))
        return 0;
    if (n < 3u || p[0] != 0x7Du || p[1] != 0x4Du)
        return 0;
    cmd = p[2];
    p += 3;
    n -= 3u;
    switch (cmd) {
    case 1:
        if (n < 6u || !mk_get7(p, 5) || mk_get7(p, 5) > MK_MAX) {
            mk_reply(1, MK_E_LEN, 0, 0);
            break;
        }
        mk.up_len = mk_get7(p, 5);
        mk.up_flags = p[5];
        mk.up_got = 0;
        mk.up_on = 1;
        mk.live_len = 0;                            /* mk_buf is being overwritten */
        mk_reply(1, MK_OK, 0, 0);
        break;
    case 2:
        mk_data(p, n);
        break;
    case 3:
        mk_end(p, n);
        break;
    case 4:
        mk_reply(4, (uint32_t)gal_clear(), 0, 0);
        if (mk.cur != MK_NONE)
            mk.shown = 0;                           /* what was on the screen is gone */
        mk.cur = MK_NONE;
        mk.changed = 1;
        mk.clears++;
        break;
    case 5: {
        uint32_t k = 0;
        r[k++] = MK_PROTO;
        k += mk_put7(r + k, mk.n, 2);
        k += mk_put7(r + k, gal_free(), 3);
        k += mk_put7(r + k, mk.cur, 2);
        k += mk_put7(r + k, GAL_MAXN, 2);
        mk_reply(5, MK_OK, r, k);
        break;
    }
    case 6: {
        uint32_t st = n >= 2u ? (uint32_t)mk_show(mk_get7(p, 2)) : MK_E_LEN;
        mk.changed |= st == MK_OK;
        mk_reply(6, st, 0, 0);
        break;
    }
    default:
        mk_reply(cmd & 0x0Fu, MK_E_CMD, 0, 0);
        break;
    }
    plat_sysex_done();
    return 1;
}

static void mk_init(void)
{
    memset(&mk, 0, sizeof mk);
    mk.cur = MK_NONE;
    gal_scan();
}
