/* SPDX-License-Identifier: GPL-3.0-only */
/* Mocky on the host: the whole app (UI, gallery, SysEx protocol) against a simulated FM-1 driven by a
 * script; the screen to PNGs. The device-only parts (HAL, USB, OTA, flash driver) are replaced by
 * plat.h implemented here; the gallery flash behaves as NOR (erase to FF, programming only clears
 * bits; programming bits that are not erased is an error). (From FoMni's omni_host.)
 *
 *   mock_host SCRIPT [OUTDIR]
 *
 * Script, one command per line (# comments):
 *   wait MS                      run the device for MS milliseconds
 *   tap BTN                      press and release (FX SEL ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY REC OCT- OCT+)
 *   tapkey K                     K: 0..26, or w0..w15 white keys
 *   turn ENC N                   (SELECT ALGO PRESET K1 K2 K3 K4) N detents at once
 *   send FILE [STATUS]           SysEx messages, one per line in hex (F0 .. F7), each sent after the
 *                                reply to the one before, as the web page does; the last reply's
 *                                status must be STATUS (default 0), every other one 0
 *   raw HEX..                    one message, no reply expected
 *   shot FILE.png                save the screen
 *   expect WHAT VALUE            count cur shown live note screen(=FILE.raw) store_writes: exit 1 on mismatch
 *   reboot                       re-run boot from the simulated flash (persistence test)
 *   listen                       a screen reader starts listening (HELLO, again every 2 s): each SAY is printed
 *                                ("said: ..."; "said!:" cuts off what was being said)
 *   where                        the reader asks for everything on the screen
 *   said TEXT                    some SAY since the last "said" (or "listen") has TEXT in it: else a failure
 *   speakdump                    every screen and message as the reader gets them (the audit)
 * The reader's SysEx (F0 7D 53 52) sent with "raw" goes through the queue as on the device: announce.c takes it
 * before Mocky's own handler.
 * MOCK_SAID_LOG=FILE: a reader listens from boot, quietly, and every SAY is appended to FILE (the audit).
 */
#define OM_HOST 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#define __attribute__(x)

/* ------------------------------------------------------------ screen --- */
static uint16_t fb[240 * 240];          /* as the panel takes it: the bytes in memory are RGB565 high first */
static void lcd_sync(void) {}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    uint16_t sc = (uint16_t)(((c >> 8) & 0xFFu) | ((c & 0xFFu) << 8));
    for (j = y; j < y + h && j < 240u; j++)
        for (i = x; i < x + w && i < 240u; i++)
            fb[j * 240u + i] = sc;
}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    uint32_t i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            if (x + i < 240u && y + j < 240u)
                fb[(y + j) * 240u + x + i] = px[j * w + i];
}
#include "../firmware/src/gfx.c"
static inline int32_t clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

/* ---------------------------------------------------------- platform --- */
#include "../firmware/src/app/plat.h"

static uint32_t now_ms, held_btn, held_keys;
static int32_t enc_acc[NE];
static uint32_t lit_btn, lit_keys;

uint32_t plat_ms(void) { return now_ms; }
uint32_t plat_buttons(void) { return held_btn; }
uint32_t plat_keys(void) { return held_keys; }
int32_t plat_enc(int role)
{
    int32_t v = enc_acc[role];
    enc_acc[role] = 0;
    return v;
}
void plat_leds(uint32_t b, uint32_t k)
{
    lit_btn = b;
    lit_keys = k;
}
void plat_glow(int on) { (void)on; }
void plat_idle(void) {}

/* SysEx: a queue of received messages (the bytes between F0 and F7), the last reply kept */
#define SXQ 8
static uint8_t sx_in[SXQ][700];
static uint32_t sx_in_n[SXQ], sx_w, sx_r;
static uint8_t reply[64];
static uint32_t reply_n, replies;
int plat_sysex_get(const uint8_t **p, uint32_t *n)
{
    if (sx_r == sx_w)
        return 0;
    *p = sx_in[sx_r % SXQ];
    *n = sx_in_n[sx_r % SXQ];
    return 1;
}
void plat_sysex_done(void) { sx_r++; }
int plat_sysex_send(const uint8_t *p, uint32_t n)
{
    if (n > sizeof reply)
        n = sizeof reply;
    memcpy(reply, p, n);
    reply_n = n;
    replies++;
    return 0;
}

/* storage objects, kept across a simulated reboot */
static uint8_t store[OBJ_NOBJ][PLAT_STORE_MAX];
static int store_len[OBJ_NOBJ] = {-1, -1};
static uint32_t store_writes;
int plat_store_load(uint32_t obj, void *dst, uint32_t max)
{
    int n;
    if (obj >= OBJ_NOBJ || (n = store_len[obj]) < 0)
        return -1;
    if ((uint32_t)n > max)
        n = (int)max;
    memcpy(dst, store[obj], (size_t)n);
    return n;
}
int plat_store_save(uint32_t obj, const void *src, uint32_t len)
{
    if (obj >= OBJ_NOBJ || len > PLAT_STORE_MAX)
        return -1;
    memcpy(store[obj], src, len);
    store_len[obj] = (int)len;
    store_writes++;
    return 0;
}

/* the gallery flash: NOR */
static uint8_t gal[GAL_SIZE];
static uint32_t nor_errors, erases;
int plat_gal_read(uint32_t off, void *dst, uint32_t n)
{
    if (off + n > GAL_SIZE)
        return -1;
    memcpy(dst, gal + off, n);
    return 0;
}
int plat_gal_erase(uint32_t off)
{
    if ((off & 0xFFFu) || off + 0x1000u > GAL_SIZE)
        return -1;
    memset(gal + off, 0xFF, 0x1000u);
    erases++;
    return 0;
}
int plat_gal_prog(uint32_t off, const void *src, uint32_t n)
{
    uint32_t i;
    if (off + n > GAL_SIZE)
        return -1;
    for (i = 0; i < n; i++) {
        uint8_t b = ((const uint8_t *)src)[i];
        if ((gal[off + i] & b) != b)
            nor_errors++;
        gal[off + i] &= b;
    }
    return 0;
}

/* -------------------------------------------------------------- app --- */
#include "../firmware/src/app/app.h"
#define ANN_NAME "Mocky " OM_VERSION
#include "ann_words.h"                                  /* the words file's link (tools/gen_words.py) */
#include "../firmware/src/announce.c"
/* what the reader is told: every SAY to stdout with "said:" (listen), and kept for the "said" check */
static char said_last[ANN_MAX + 1], said_log[16384];   /* said_log: every SAY since the last "said" check */
static uint32_t said_len, hellos;
static int said_print = 1, sim_listen;
static FILE *said_log_f;                                /* MOCK_SAID_LOG=FILE: every SAY, for fm1-reader's audit */
static int ann_wire_send(const uint8_t *p, uint32_t n)
{
    if (n >= 7u && p[4] == ANN_HELLO)
        hellos++;
    if (n >= 7u && p[4] == ANN_SAY) {
        uint32_t i, k = 0;
        for (i = 6; i + 1u < n && k < ANN_MAX; i++)
            said_last[k++] = (char)p[i];
        said_last[k] = 0;
        if (said_len + k + 2u < sizeof said_log) {
            memcpy(said_log + said_len, said_last, k);
            said_len += k;
            said_log[said_len++] = '\n';
            said_log[said_len] = 0;
        }
        if (said_print)
            printf("said%s: %s\n", (p[5] & ANN_NOW) ? "!" : "", said_last);
        if (said_log_f)
            fprintf(said_log_f, "%s\t%s\n", (p[5] & ANN_KEEP) ? "WARN" : "MSG", said_last);
    }
    return 0;
}
#include "../firmware/src/app/project.c"
#include "../firmware/src/app/mock.c"
#include "../firmware/src/app/ui.c"

/* ---------------------------------------------------------- outputs --- */
static void png_write(const char *path)   /* RGB PNG, stored deflate blocks (no zlib) */
{
    static uint8_t raw[240 * (1 + 240 * 3)];
    FILE *f = fopen(path, "wb");
    uint32_t crc_t[256], i, j, k, n = 0, a = 1, b = 0, pos, len = sizeof raw;
    uint8_t hdr[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (!f) {
        fprintf(stderr, "cannot write %s\n", path);
        return;
    }
    for (i = 0; i < 256; i++) {
        uint32_t c = i;
        for (k = 0; k < 8; k++)
            c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_t[i] = c;
    }
    for (j = 0; j < 240; j++) {
        raw[n++] = 0;
        for (i = 0; i < 240; i++) {
            uint16_t p = fb[j * 240 + i];
            p = (uint16_t)((p >> 8) | (p << 8));
            raw[n++] = (uint8_t)(((p >> 11) & 31) * 255 / 31);
            raw[n++] = (uint8_t)(((p >> 5) & 63) * 255 / 63);
            raw[n++] = (uint8_t)((p & 31) * 255 / 31);
        }
    }
    for (i = 0; i < len; i++) {
        a = (a + raw[i]) % 65521;
        b = (b + a) % 65521;
    }
#define PUT32(v) do { uint8_t q_[4] = {(uint8_t)((v) >> 24), (uint8_t)((v) >> 16), (uint8_t)((v) >> 8), (uint8_t)(v)}; fwrite(q_, 1, 4, f); } while (0)
    fwrite(hdr, 1, 8, f);
    {
        uint8_t ih[17] = {'I', 'H', 'D', 'R', 0, 0, 0, 240, 0, 0, 0, 240, 8, 2, 0, 0, 0};
        uint32_t c = 0xFFFFFFFFu;
        PUT32(13u);
        fwrite(ih, 1, 17, f);
        for (i = 0; i < 17; i++)
            c = crc_t[(c ^ ih[i]) & 255] ^ (c >> 8);
        PUT32(~c);
    }
    {
        uint32_t nblk = (len + 65534) / 65535, zlen = 2 + len + nblk * 5 + 4, c = 0xFFFFFFFFu;
        uint8_t *z = malloc(zlen + 4), *q = z;
        memcpy(q, "IDAT", 4);
        q += 4;
        *q++ = 0x78;
        *q++ = 0x01;
        for (pos = 0; pos < len;) {
            uint32_t m = len - pos > 65535 ? 65535 : len - pos;
            *q++ = pos + m >= len ? 1 : 0;
            *q++ = (uint8_t)m;
            *q++ = (uint8_t)(m >> 8);
            *q++ = (uint8_t)~m;
            *q++ = (uint8_t)(~m >> 8);
            memcpy(q, raw + pos, m);
            q += m;
            pos += m;
        }
        *q++ = (uint8_t)(b >> 8);
        *q++ = (uint8_t)b;
        *q++ = (uint8_t)(a >> 8);
        *q++ = (uint8_t)a;
        PUT32(zlen);
        fwrite(z, 1, zlen + 4, f);
        for (i = 0; i < zlen + 4; i++)
            c = crc_t[(c ^ z[i]) & 255] ^ (c >> 8);
        PUT32(~c);
        free(z);
    }
    {
        uint8_t ie[4] = {'I', 'E', 'N', 'D'};
        uint32_t c = 0xFFFFFFFFu;
        PUT32(0u);
        fwrite(ie, 1, 4, f);
        for (i = 0; i < 4; i++)
            c = crc_t[(c ^ ie[i]) & 255] ^ (c >> 8);
        PUT32(~c);
    }
    fclose(f);
}

/* ----------------------------------------------------------- the loop --- */
static void boot(void)
{
    memset(&ui, 0, sizeof ui);
    memset(&sp, 0, sizeof sp);          /* the reader's view starts again too (WHERE once it listens) */
    sx_r = sx_w = 0;
    project_load();
    mk_init();
    ui_init();
}

static void loop_once(void)             /* main_fm1.c's loop body, for one millisecond */
{
    static uint32_t last_frame;
    const uint8_t *p;
    uint32_t n;
    if (plat_sysex_get(&p, &n) && ann_frame(p, n, now_ms))   /* the screen reader's first, as main_fm1.c */
        plat_sysex_done();
    if (!mk_service() && sx_r != sx_w)
        sx_r++;                         /* not ours: the updater would take it (ota_service) */
    if (now_ms - last_frame >= 16u) {
        last_frame = now_ms;
        ui_frame();
    } else {
        ui_input_only();
    }
}

static void run_ms(uint32_t ms)
{
    while (ms--) {
        now_ms++;
        if (sim_listen && now_ms % 2000u == 0) {        /* the reader's HELLO, as it sends it */
            static const uint8_t HELLO[] = {ANN_H0, ANN_H1, ANN_H2, ANN_HELLO, ANN_VER};
            ann_frame(HELLO, sizeof HELLO, now_ms);
        }
        loop_once();
    }
}

static int queue_hex(const char *s)     /* "F0 7D ... F7" (spaces optional) -> the receive queue */
{
    uint8_t b[700];
    uint32_t n = 0;
    while (*s) {
        unsigned v;
        while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')
            s++;
        if (!*s)
            break;
        if (sscanf(s, "%2x", &v) != 1 || n >= sizeof b)
            return -1;
        b[n++] = (uint8_t)v;
        s += 2;
    }
    if (n < 2 || b[0] != 0xF0 || b[n - 1] != 0xF7 || sx_w - sx_r >= SXQ)
        return -1;
    memcpy(sx_in[sx_w % SXQ], b + 1, n - 2);
    sx_in_n[sx_w % SXQ] = n - 2;
    sx_w++;
    return 0;
}

static int send_file(const char *path, int want_last)
{
    static char line[4096];
    FILE *f = fopen(path, "r");
    uint32_t sent = 0, t0 = now_ms;
    int last = -1;
    if (!f) {
        fprintf(stderr, "cannot read %s\n", path);
        return 1;
    }
    while (fgets(line, sizeof line, f)) {
        uint32_t r0 = replies, t;
        if (line[0] == '#' || line[0] == '\n')
            continue;
        if (last > 0) {
            fprintf(stderr, "send %s: message %u answered status %d\n", path, sent, last);
            fclose(f);
            return 1;
        }
        if (queue_hex(line)) {
            fprintf(stderr, "send %s: bad line\n", path);
            fclose(f);
            return 1;
        }
        for (t = 0; replies == r0 && t < 5000u; t++)
            run_ms(1);
        if (replies == r0) {
            fprintf(stderr, "send %s: no reply to message %u\n", path, sent);
            fclose(f);
            return 1;
        }
        last = reply_n > 4 ? reply[4] : -1;
        sent++;
    }
    fclose(f);
    run_ms(20);                         /* the page waits for nothing more: the UI catches up */
    if (last != want_last) {
        fprintf(stderr, "send %s: last status %d, wanted %d\n", path, last, want_last);
        return 1;
    }
    printf("send %s: %u messages, %u ms simulated\n", path, sent, now_ms - t0);
    return 0;
}

static const char *const BNAMES[NB] = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE",
                                       "ARP", "SEQ", "PLAY", "REC", "OCT-", "OCT+"};
static const char *const ENAMES[NE] = {"SELECT", "ALGO", "PRESET", "K1", "K2", "K3", "K4"};
static int find_name(const char *const *t, int n, const char *s)
{
    int i;
    for (i = 0; i < n; i++)
        if (!strcmp(t[i], s))
            return i;
    return -1;
}

static int expect(const char *what, const char *val)
{
    long got;
    if (!strcmp(what, "screen")) {      /* the screen equals a raw RGB565 file (high byte first) */
        static uint8_t want[240 * 240 * 2];
        FILE *f = fopen(val, "rb");
        uint32_t i, diff = 0;
        if (!f || fread(want, 1, sizeof want, f) != sizeof want) {
            fprintf(stderr, "expect screen: cannot read %s\n", val);
            if (f)
                fclose(f);
            return 1;
        }
        fclose(f);
        for (i = 0; i < 240u * 240u; i++)
            diff += fb[i] != (uint16_t)(want[2 * i] | want[2 * i + 1] << 8);
        if (diff) {
            fprintf(stderr, "expect screen %s: %u pixels differ\n", val, diff);
            return 1;
        }
        return 0;
    }
    if (!strcmp(what, "note")) {
        const char *now = ui.note_up ? ui.note : "";
        if (strcmp(now, val) && !(val[0] == '-' && !val[1] && !*now)) {
            fprintf(stderr, "expect note: \"%s\", wanted \"%s\"\n", now, val);
            return 1;
        }
        return 0;
    }
    if (!strcmp(what, "count"))
        got = (long)mk.n;
    else if (!strcmp(what, "cur"))
        got = mk.cur == MK_NONE ? -1 : (long)mk.cur;
    else if (!strcmp(what, "shown"))
        got = (long)mk.shown;
    else if (!strcmp(what, "live"))
        got = mk.live_len != 0;
    else if (!strcmp(what, "welcome"))
        got = (long)ui.welcome;
    else if (!strcmp(what, "slideshow"))
        got = (long)ui.show_on;
    else if (!strcmp(what, "nor_errors"))
        got = (long)nor_errors;
    else if (!strcmp(what, "hellos"))
        got = (long)hellos;
    else if (!strcmp(what, "free_kb"))
        got = (long)(gal_free() / 1024u);
    else {
        fprintf(stderr, "expect: unknown %s\n", what);
        return 1;
    }
    if ((val[0] == '<' && got < atol(val + 1)) || (val[0] == '>' && got > atol(val + 1)) ||
        (val[0] != '<' && val[0] != '>' && got == atol(val)))
        return 0;
    fprintf(stderr, "expect %s: %ld, wanted %s\n", what, got, val);
    return 1;
}

int main(int argc, char **argv)
{
    static char line[4096], a[512], b[512], path[1024];
    const char *out = argc > 2 ? argv[2] : ".";
    FILE *f;
    int ln = 0, fails = 0;
    if (argc < 2 || !(f = fopen(argv[1], "r"))) {
        fprintf(stderr, "usage: mock_host SCRIPT [OUTDIR]\n");
        return 2;
    }
    memset(gal, 0xFF, sizeof gal);
    if (getenv("MOCK_SAID_LOG")) {                       /* a reader listens from the start, quietly: the log only */
        said_log_f = fopen(getenv("MOCK_SAID_LOG"), "a");
        sim_listen = 1;
        said_print = 0;
    }
    boot();
    if (sim_listen) {
        static const uint8_t HELLO[] = {ANN_H0, ANN_H1, ANN_H2, ANN_HELLO, ANN_VER};
        ann_frame(HELLO, sizeof HELLO, now_ms);
    }
    while (fgets(line, sizeof line, f)) {
        int n, i;
        ln++;
        a[0] = b[0] = 0;
        n = sscanf(line, "%511s %511s", a, b);
        if (n < 1 || a[0] == '#')
            continue;
        if (!strcmp(a, "wait")) {
            run_ms((uint32_t)atoi(b));
        } else if (!strcmp(a, "tap")) {
            if ((i = find_name(BNAMES, NB, b)) < 0)
                goto bad;
            held_btn |= 1u << i;
            run_ms(40);
            held_btn &= ~(1u << i);
            run_ms(40);
        } else if (!strcmp(a, "tapkey")) {
            i = b[0] == 'w' ? WHITE_K[atoi(b + 1) & 15] : atoi(b);
            held_keys |= 1u << i;
            run_ms(40);
            held_keys &= ~(1u << i);
            run_ms(40);
        } else if (!strcmp(a, "turn")) {
            char c[32];
            if (sscanf(line, "%*s %*s %31s", c) != 1 || (i = find_name(ENAMES, NE, b)) < 0)
                goto bad;
            enc_acc[i] += atoi(c);
            run_ms(40);
        } else if (!strcmp(a, "send")) {
            char c[32];
            int want = sscanf(line, "%*s %*s %31s", c) == 1 ? atoi(c) : 0;
            fails += send_file(b, want);
        } else if (!strcmp(a, "raw")) {
            if (queue_hex(line + 3))
                goto bad;
            run_ms(20);
        } else if (!strcmp(a, "shot")) {
            snprintf(path, sizeof path, "%s/%s", out, b);
            png_write(path);
        } else if (!strcmp(a, "expect")) {
            char c[512];
            if (sscanf(line, "%*s %*s %511[^\n]", c) != 1)
                goto bad;
            if (expect(b, c)) {
                fprintf(stderr, "  line %d\n", ln);
                fails++;
            }
        } else if (!strcmp(a, "reboot")) {
            boot();
        } else if (!strcmp(a, "listen")) {
            static const uint8_t HELLO[] = {ANN_H0, ANN_H1, ANN_H2, ANN_HELLO, ANN_VER};
            sim_listen = 1;
            said_len = 0;
            said_log[0] = 0;
            ann_frame(HELLO, sizeof HELLO, now_ms);
            run_ms(40);
        } else if (!strcmp(a, "where")) {
            static const uint8_t WHERE[] = {ANN_H0, ANN_H1, ANN_H2, ANN_WHERE};
            ann_frame(WHERE, sizeof WHERE, now_ms);
            run_ms(40);
        } else if (!strcmp(a, "speakdump")) {    /* every screen's text for a screen reader, to stdout */
            speak_dump();
        } else if (!strcmp(a, "said")) {
            char *t = line + 4, *e;
            while (*t == ' ')
                t++;
            e = t + strlen(t);
            while (e > t && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' '))
                *--e = 0;
            if (!strstr(said_log, t)) {
                fprintf(stderr, "line %d: said \"%s\": not said (since the last check: %s)\n", ln, t,
                        said_len ? said_log : "nothing\n");
                fails++;
            } else {
                printf("  ok said \"%s\"\n", t);
            }
            said_len = 0;
            said_log[0] = 0;
        } else {
            goto bad;
        }
        continue;
    bad:
        fprintf(stderr, "line %d: %s", ln, line);
        return 2;
    }
    fclose(f);
    if (nor_errors) {
        fprintf(stderr, "flash: %u bytes programmed over unerased bits\n", nor_errors);
        fails++;
    }
    printf("%s: %s (%u erases, %u project saves)\n", argv[1], fails ? "FAILED" : "ok", erases, store_writes);
    return fails ? 1 : 0;
}
