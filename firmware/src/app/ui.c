/* SPDX-License-Identifier: GPL-3.0-only */
/* Mocky UI: a picture viewer. Part of the unity build (after gfx.c, project.c and mock.c); the
 * host simulator includes it too.
 *
 *   the web page     sends a picture (shown at once; kept in the gallery unless asked not to)
 *   white keys       picture 1 .. 16          SELECT ALGORITHM PRESETS KNOB 1-3, OCT- / OCT+   step
 *   PLAY             slideshow on / off       KNOB 4   its pace (1 .. 30 s a picture)
 *   SAVE             keep the picture just sent     HOME   which picture this is, and the room left
 * Notes sit on the picture's bottom edge for a moment. */

/* FoMni's colours: cream paper, warm brown ink, coral and teal */
#define K_BG RGB(250, 244, 232)
#define K_PANEL RGB(236, 228, 212)
#define K_DIM RGB(150, 136, 118)
#define K_TEXT RGB(72, 58, 48)
#define K_CORAL RGB(242, 120, 72)
#define K_TEAL RGB(38, 166, 154)
#define K_PILL RGB(40, 34, 30)
#define K_PILL_T RGB(250, 244, 232)

#define NOTE_Y 204                       /* the note: rows NOTE_Y .. NOTE_Y + NOTE_H - 1 */
#define NOTE_H 30
#define NOTE_MS 1300u
#define PROJ_QUIET 3000u                 /* the picture shown is remembered this long after the last change */

static const uint8_t WHITE_K[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};

static struct {
    uint32_t btn, keys;
    char note[32];
    uint32_t note_until, note_up;
    uint32_t show_on, show_t;            /* slideshow */
    uint32_t dirty, act_t;               /* proj changed (the picture shown) */
    uint32_t frame;
    uint32_t welcome;                    /* the welcome page is up */
} ui;

static void cv_round(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t c)   /* FoMni's */
{
    static const uint8_t IN[7][6] = {{0}, {1}, {1, 0}, {2, 1, 0}, {2, 1, 0, 0}, {3, 2, 1, 0, 0}, {4, 2, 1, 1, 0, 0}};
    int32_t j;
    if (r > 6)
        r = 6;
    if (r > h / 2)
        r = h / 2;
    cv_rect(x, y + r, w, h - 2 * r, c);
    for (j = 0; j < r; j++) {
        int32_t d = IN[r][j];
        cv_rect(x + d, y + j, w - 2 * d, 1, c);
        cv_rect(x + d, y + h - 1 - j, w - 2 * d, 1, c);
    }
}

static void text_c(int32_t cx, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    cv_text(cx - text_w(f, s) / 2, y, f, s, c);
}

static char *put_s(char *b, const char *s)
{
    while (*s)
        *b++ = *s++;
    *b = 0;
    return b;
}
static char *put_u(char *b, uint32_t v)
{
    char t[12];
    int n = 0;
    do {
        t[n++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v);
    while (n)
        *b++ = t[--n];
    *b = 0;
    return b;
}

/* ------------------------------------------------------------ screen --- */
static void draw_welcome(void)
{
    lcd_fill(0, 0, 240, 240, K_BG);
    cv_begin(240, 150, K_BG);
    text_c(120, 0, &FONT_L, "Mocky", K_CORAL);
    text_c(120, 44, &FONT_S, "Pictures for the FM-1 screen", K_DIM);
    cv_round(16, 76, 208, 66, 6, K_PANEL);
    text_c(120, 84, &FONT_S, "Plug in USB and open", K_TEXT);
    text_c(120, 102, &FONT_S, "charlesvestal.github.io", K_TEAL);
    text_c(120, 120, &FONT_S, "/fm1-mocky in Chrome", K_TEAL);
    cv_blit(0, 46);
    lcd_sync();
    ui.welcome = 1;
}

static void say(const char *a, const char *b)
{
    char *p = put_s(ui.note, a);
    if (b && *b) {
        p = put_s(p, "  ");
        put_s(p, b);
    }
    ui.note_until = plat_ms() + NOTE_MS;
    ui.note_up = 1;
    {
        int32_t w = text_w(&FONT_S, ui.note) + 24;
        if (w > 232)
            w = 232;
        cv_begin(240, NOTE_H, K_BG);
        if (mk.shown && !ui.welcome) {          /* over the picture: its own rows under the pill */
            uint32_t i;
            for (i = 0; i < 240u * NOTE_H; i++)
                cv_px[i] = (uint16_t)(mk_fb[(NOTE_Y * 240u + i) * 2u] | mk_fb[(NOTE_Y * 240u + i) * 2u + 1u] << 8);
        }
        cv_round(120 - w / 2, 2, w, NOTE_H - 4, 6, K_PILL);
        text_c(120, (NOTE_H - FONT_S.h) / 2, &FONT_S, ui.note, K_PILL_T);
        cv_blit(0, NOTE_Y);
    }
}
void ui_say(const char *a, const char *b) { say(a, b); }

static void note_clear(void)
{
    ui.note_up = 0;
    if (mk.shown && !ui.welcome)
        mk_present_rows(NOTE_Y, NOTE_Y + NOTE_H);
    else
        lcd_fill(0, NOTE_Y, 240, NOTE_H, K_BG);
}

static void say_where(void)              /* "3 / 7" */
{
    char a[16], *p = a;
    if (mk.cur == MK_NONE) {
        say(mk.shown ? "NOT KEPT" : "NO PICTURES", mk.shown ? "SAVE KEEPS IT" : "");
        return;
    }
    p = put_u(p, mk.cur + 1u);
    p = put_s(p, " / ");
    put_u(p, mk.n);
    say(a, "");
}

static const char *mk_err(int rc)
{
    return rc == MK_E_FULL ? "GALLERY FULL" : rc == MK_E_FLASH ? "FLASH ERROR" : rc == MK_E_DATA ? "BAD PICTURE" : "";
}

/* ------------------------------------------------------------- input --- */
static void go(int32_t i)
{
    int rc;
    if (!mk.n) {
        say_where();
        return;
    }
    i = ((i % (int32_t)mk.n) + (int32_t)mk.n) % (int32_t)mk.n;
    if ((rc = mk_show((uint32_t)i)) != MK_OK) {
        say(mk_err(rc), "");
        return;
    }
    ui.welcome = 0;
    ui.note_up = 0;
    proj.cur = (uint16_t)i;
    ui.dirty = 1;
    ui.act_t = plat_ms();
}

static void step(int32_t d)
{
    int32_t at = mk.cur == MK_NONE ? (d > 0 ? -1 : 0) : (int32_t)mk.cur;
    go(at + d);
    if (mk.n)
        say_where();
}

static void button(int b)
{
    char t[16];
    int rc;
    switch (b) {
    case B_OCTDN:
        step(-1);
        break;
    case B_OCTUP:
        step(1);
        break;
    case B_PLAY:
        ui.show_on = !ui.show_on && mk.n > 1u;
        ui.show_t = plat_ms();
        put_s(put_u(put_s(t, "EVERY "), proj.show_s), " S");
        say(ui.show_on ? "SLIDESHOW" : mk.n > 1u ? "SLIDESHOW OFF" : "KEEP TWO PICTURES", ui.show_on ? t : "");
        break;
    case B_SAVE:
        if (!mk.live_len) {
            say(mk.shown ? "ALREADY KEPT" : "NOTHING TO KEEP", "");
            break;
        }
        rc = mk_keep_live();
        if (rc == MK_OK) {
            proj.cur = (uint16_t)mk.cur;
            ui.dirty = 1;
            ui.act_t = plat_ms();
            put_u(put_s(t, "KEPT AS "), mk.cur + 1u);
            say(t, "");
        } else {
            say(mk_err(rc), "");
        }
        break;
    case B_HOME: {                          /* "3 / 7  120 KB FREE" */
        char a[40], *p = a;
        if (mk.cur != MK_NONE)
            p = put_s(put_u(put_s(put_u(p, mk.cur + 1u), " / "), mk.n), "  ");
        put_s(put_u(p, gal_free() / 1024u), " KB FREE");
        say(a, "");
        break;
    }
    default:
        break;
    }
}

static void input(void)
{
    uint32_t btn = plat_buttons(), keys = plat_keys(), ch, i;
    int32_t e, sum = 0;
    ch = btn & ~ui.btn;                     /* on the press */
    ui.btn = btn;
    for (i = 0; i < NB; i++)
        if (ch & (1u << i))
            button((int)i);
    ch = keys & ~ui.keys;
    ui.keys = keys;
    for (i = 0; i < 16u; i++)
        if (ch >> WHITE_K[i] & 1u) {
            if (i < mk.n) {
                go((int32_t)i);
                say_where();
            } else {
                char t[24];
                put_u(put_s(t, "NO PICTURE "), i + 1u);
                say(t, "");
            }
        }
    sum += plat_enc(EN_SELECT) + plat_enc(EN_ALGO) + plat_enc(EN_PRESET);
    for (i = 0; i < 3u; i++)
        sum += plat_enc(EN_K1 + (int)i);
    if (sum)
        step(sum > 0 ? 1 : -1);             /* one picture per turn burst */
    if ((e = plat_enc(EN_K4)) != 0) {
        char t[16];
        proj.show_s = (uint8_t)clamp((int32_t)proj.show_s + (e > 0 ? 1 : -1), 1, 30);
        ui.dirty = 1;
        ui.act_t = plat_ms();
        ui.show_t = plat_ms();
        put_s(put_u(put_s(t, "EVERY "), proj.show_s), " S");
        say("SLIDESHOW", t);
    }
}

/* ------------------------------------------------------------ lights --- */
static void leds(void)
{
    uint32_t b = (1u << B_OCTDN) | (1u << B_OCTUP), k = 0, i;
    if (ui.show_on)
        b |= 1u << B_PLAY;
    if (mk.live_len && (ui.frame / 16u) & 1u)
        b |= 1u << B_SAVE;                  /* blinks: SAVE would keep this one */
    for (i = 0; i < 16u && i < mk.n; i++)
        if (i != mk.cur || (ui.frame / 8u) & 1u)
            k |= 1u << WHITE_K[i];          /* the kept pictures; the one shown blinks */
    plat_glow(1);
    plat_leds(b, k);
}

/* ------------------------------------------------------------- frame --- */
void ui_init(void)
{
    memset(&ui, 0, sizeof ui);
    if (mk.n && mk_show(proj.cur < mk.n ? proj.cur : 0u) == MK_OK)
        ui.welcome = 0;
    else
        draw_welcome();
}

void ui_frame(void)
{
    uint32_t now = plat_ms();
    if (mk.changed) {                       /* a picture came (and is on the screen), or the gallery went */
        if (!mk.shown) {
            draw_welcome();
            ui.note_up = 0;
        } else {
            ui.welcome = 0;
            if (ui.note_up)
                note_clear();
        }
        if (mk.cur != MK_NONE && mk.cur != proj.cur) {
            proj.cur = (uint16_t)mk.cur;
            ui.dirty = 1;
            ui.act_t = now;
        }
        ui.show_on = 0;
    }
    mk.changed = 0;
    input();
    if (ui.show_on && mk.n > 1u && now - ui.show_t >= proj.show_s * 1000u) {
        ui.show_t = now;
        go(mk.cur == MK_NONE ? 0 : (int32_t)mk.cur + 1);
    }
    if (ui.note_up && (int32_t)(now - ui.note_until) >= 0)
        note_clear();
    if (ui.dirty && now - ui.act_t >= PROJ_QUIET && !ui.btn && !ui.keys) {
        if (project_save() == 0)
            ui.dirty = 0;
        else
            ui.act_t = now;                 /* try again later */
    }
    leds();
    ui.frame++;
}

void ui_input_only(void) { input(); }
