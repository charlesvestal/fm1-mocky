/* SPDX-License-Identifier: GPL-3.0-only */
/* Mocky UI: a picture viewer. Part of the unity build (after gfx.c, project.c and mock.c); the
 * host simulator includes it too.
 *
 *   the web page     sends a picture (shown at once; kept in the gallery unless asked not to)
 *   white keys       picture 1 .. 16          SELECT ALGORITHM PRESETS KNOB 1-3, OCT- / OCT+   step
 *   PLAY             slideshow on / off       KNOB 4   its pace (1 .. 30 s a picture)
 *   SAVE             keep the picture just sent     HOME   which picture this is, and the room left
 * Notes sit on the picture's bottom edge for a moment. A screen reader on the computer (fm1-reader) hears
 * each note, and what the page did: the "screen reader" section, near the end. */

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

/* ------------------------------------------------- the reader's text --- */
/* What a screen reader is told (announce.c) is built in sp_buf: the note's own text, or a sentence that says
 * it in full where the note is short for the eye ("3 / 7": "Picture 3 of 7"). */
static char sp_buf[ANN_MAX + 1] OM_POOL;
static uint32_t sp_n;
static uint8_t sp_said;                 /* a note went to the screen reader this frame: speak() leaves it be */
#define SP_LATER 0x80u                  /* say_full: a readout, said once the knob settles */
static void sp_cat(const char *s)
{
    while (*s && sp_n < ANN_MAX)
        sp_buf[sp_n++] = *s++;
    sp_buf[sp_n] = 0;
}
static void sp_u(uint32_t v)
{
    char t[12];
    put_u(t, v);
    sp_cat(t);
}
static void sp_pic(uint32_t i)          /* "Picture 3 of 7" ("kept as picture 3 of 7") */
{
    sp_cat(sp_n ? "picture " : "Picture ");
    sp_u(i + 1u);
    sp_cat(" of ");
    sp_u(mk.n);
}
static void sp_free(void)               /* "120 KB free" */
{
    sp_u(gal_free() / 1024u);
    sp_cat(" KB free");
}
static const char *mk_err(int rc);
static const char *sp_err(const char *lead, int rc)   /* a warning: "Not kept: gallery full" */
{
    sp_n = 0;
    sp_cat(lead);
    sp_cat(mk_err(rc));
    return sp_buf;
}
static void msg_text(const char *a, const char *b)   /* the note as the reader gets it: "a b", without "3.." dots */
{
    sp_n = 0;
    sp_cat(a);
    if (b && *b) {
        sp_cat(" ");
        sp_cat(b);
    }
    while (sp_n && sp_buf[sp_n - 1] == '.')
        sp_buf[--sp_n] = 0;
}

/* a note on the screen, a + b; and to a screen reader: spoken, or the note's text (flags: ANN_NOW, ANN_KEEP for a
 * warning, SP_LATER for a readout) */
static void say_full(const char *a, const char *b, const char *spoken, uint32_t flags)
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
    if (ann_on()) {
        if (!spoken) {
            msg_text(a, b);
            spoken = sp_buf;
        }
        if (flags & SP_LATER)
            ann_later(spoken, ANN_NOW, 120u, plat_ms());
        else
            ann_say(spoken, 0, flags);
        sp_said = 1;
    }
}
static void say(const char *a, const char *b) { say_full(a, b, 0, ANN_NOW); }
/* something did not happen: a screen reader says it whole (what follows does not cut it off) */
static void warn(const char *a, const char *spoken) { say_full(a, "", spoken, ANN_NOW | ANN_KEEP); }
void ui_say(const char *a, const char *b) { say(a, b); }

static void note_clear(void)
{
    ui.note_up = 0;
    if (mk.shown && !ui.welcome)
        mk_present_rows(NOTE_Y, NOTE_Y + NOTE_H);
    else
        lcd_fill(0, NOTE_Y, 240, NOTE_H, K_BG);
}

static void say_where(uint32_t flags)    /* "3 / 7" */
{
    char a[16], *p = a;
    sp_n = 0;
    if (mk.cur == MK_NONE) {
        if (mk.shown) {
            sp_cat("Picture not kept. Press SAVE to keep it");
            say_full("NOT KEPT", "SAVE KEEPS IT", sp_buf, flags);
        } else {
            say_full("NO PICTURES", "", "No pictures kept", flags);
        }
        return;
    }
    p = put_u(p, mk.cur + 1u);
    p = put_s(p, " / ");
    put_u(p, mk.n);
    sp_pic(mk.cur);
    say_full(a, "", sp_buf, flags);
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
        say_where(ANN_NOW);
        return;
    }
    i = ((i % (int32_t)mk.n) + (int32_t)mk.n) % (int32_t)mk.n;
    if ((rc = mk_show((uint32_t)i)) != MK_OK) {
        warn(mk_err(rc), sp_err("Picture not shown: ", rc));
        return;
    }
    ui.welcome = 0;
    ui.note_up = 0;
    proj.cur = (uint16_t)i;
    ui.dirty = 1;
    ui.act_t = plat_ms();
}

static void step(int32_t d, uint32_t flags)   /* flags: SP_LATER for a knob */
{
    int32_t at = mk.cur == MK_NONE ? (d > 0 ? -1 : 0) : (int32_t)mk.cur;
    go(at + d);
    if (mk.n)
        say_where(flags);
}

static void button(int b)
{
    char t[16];
    int rc;
    switch (b) {
    case B_OCTDN:
        step(-1, ANN_NOW);
        break;
    case B_OCTUP:
        step(1, ANN_NOW);
        break;
    case B_PLAY:
        ui.show_on = !ui.show_on && mk.n > 1u;
        ui.show_t = plat_ms();
        put_s(put_u(put_s(t, "EVERY "), proj.show_s), " S");
        if (ui.show_on) {
            sp_n = 0;
            sp_cat("Slideshow on, ");
            sp_cat(t);
            say_full("SLIDESHOW", t, sp_buf, ANN_NOW);
        } else if (mk.n > 1u) {
            say("SLIDESHOW OFF", "");
        } else {
            warn("KEEP TWO PICTURES", "No slideshow: keep two pictures first");
        }
        break;
    case B_SAVE:
        if (!mk.live_len) {
            if (mk.shown)
                say("ALREADY KEPT", "");
            else
                warn("NOTHING TO KEEP", "Not kept: no new picture to keep");
            break;
        }
        rc = mk_keep_live();
        if (rc == MK_OK) {
            proj.cur = (uint16_t)mk.cur;
            ui.dirty = 1;
            ui.act_t = plat_ms();
            put_u(put_s(t, "KEPT AS "), mk.cur + 1u);
            sp_n = 0;
            sp_cat("Kept as ");
            sp_pic(mk.cur);
            say_full(t, "", sp_buf, ANN_NOW);
        } else {
            warn(mk_err(rc), sp_err("Not kept: ", rc));
        }
        break;
    case B_HOME: {                          /* "3 / 7  120 KB FREE" */
        char a[40], *p = a;
        if (mk.cur != MK_NONE)
            p = put_s(put_u(put_s(put_u(p, mk.cur + 1u), " / "), mk.n), "  ");
        put_s(put_u(p, gal_free() / 1024u), " KB FREE");
        sp_n = 0;                           /* "Picture 3 of 7, 120 KB free" */
        if (mk.cur != MK_NONE) {
            sp_pic(mk.cur);
        } else if (mk.n) {
            sp_cat(mk.shown ? "Picture not kept, " : "No picture shown, ");
            sp_u(mk.n);
            sp_cat(" in the gallery");
        } else {
            sp_cat("No pictures kept");
        }
        sp_cat(", ");
        sp_free();
        say_full(a, "", sp_buf, ANN_NOW);
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
                say_where(ANN_NOW);
            } else {
                char t[24];
                put_u(put_s(t, "NO PICTURE "), i + 1u);
                warn(t, 0);
            }
        }
    sum += plat_enc(EN_SELECT) + plat_enc(EN_ALGO) + plat_enc(EN_PRESET);
    for (i = 0; i < 3u; i++)
        sum += plat_enc(EN_K1 + (int)i);
    if (sum)
        step(sum > 0 ? 1 : -1, SP_LATER);   /* one picture per turn burst */
    if ((e = plat_enc(EN_K4)) != 0) {
        char t[16];
        proj.show_s = (uint8_t)clamp((int32_t)proj.show_s + (e > 0 ? 1 : -1), 1, 30);
        ui.dirty = 1;
        ui.act_t = plat_ms();
        ui.show_t = plat_ms();
        put_s(put_u(put_s(t, "EVERY "), proj.show_s), " S");
        say_full("SLIDESHOW", t, 0, SP_LATER);
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

/* ------------------------------------------------------ screen reader --- */
/* The screen as text for a screen reader on the computer (announce.c; fm1-reader). A picture can't be put in
 * words, so the reader hears what Mocky itself shows and does: which picture this is ("Picture 3 of 7"), a
 * picture arriving from the page (kept or not), the gallery cleared, the slideshow, and every note (say_full).
 * WHERE (the reader asks, or has just started listening) is everything at once. Nothing is built while no
 * reader listens. */
static struct {
    uint32_t cur, n, got, clears;
    uint8_t started, show_on;
} sp OM_POOL;

static void sp_snap(void)
{
    sp.cur = mk.cur;
    sp.n = mk.n;
    sp.got = mk.got;
    sp.clears = mk.clears;
    sp.show_on = (uint8_t)ui.show_on;
}

static void sp_gallery(void)            /* "7 in the gallery, 120 KB free" */
{
    if (mk.n) {
        sp_u(mk.n);
        sp_cat(" in the gallery, ");
    } else {
        sp_cat("Gallery empty, ");
    }
    sp_free();
}

static void sp_screen(void)             /* WHERE: everything on the screen */
{
    char t[16];
    sp_n = 0;
    if (!mk.shown) {                        /* the welcome page */
        sp_cat("Mocky, pictures for the FM-1 screen. No picture shown. ");
        sp_gallery();
        sp_cat(". Plug in USB and open the Mocky page in Chrome to send one");
        return;
    }
    if (mk.cur == MK_NONE) {
        sp_cat("New picture, not kept. Press SAVE to keep it. ");
        sp_gallery();
    } else {                                /* "Picture 3 of 7. 120 KB free" */
        sp_pic(mk.cur);
        sp_cat(". ");
        sp_free();
    }
    if (ui.show_on) {
        put_s(put_u(put_s(t, "EVERY "), proj.show_s), " S");
        sp_cat(". Slideshow on, ");
        sp_cat(t);
    }
}

/* a picture came from the page: rc, how keeping it went (MK_OK, an error, MK_NOKEEP) */
static void sp_got(uint32_t rc)
{
    sp_n = 0;
    if (rc == MK_OK) {
        sp_cat("New picture, kept as ");
        sp_pic(mk.cur);
    } else if (rc == MK_NOKEEP) {
        sp_cat("New picture, not kept. Press SAVE to keep it");
    } else {
        sp_err("New picture shown, not kept: ", (int)rc);
    }
}

static void speak(void)
{
    uint32_t flags = ANN_NOW;
    int where = ann_where();
    if (!ann_on()) {
        sp.started = 0;
        sp_said = 0;
        return;
    }
    if (!sp.started || where) {
        sp.started = 1;
        sp_snap();
        sp_screen();
        ann_say(sp_buf, 0, ANN_NOW);
        sp_said = 0;
        return;
    }
    if (sp_said) {                          /* a note said what happened */
        sp_said = 0;
        sp_snap();
        return;
    }
    sp_n = 0;
    if (mk.got != sp.got) {                 /* a picture from the page (once a file changes, when watched) */
        sp_got(mk.got_rc);
        if (mk.got_rc != MK_OK && mk.got_rc != MK_NOKEEP)
            flags |= ANN_KEEP;
    } else if (mk.clears != sp.clears) {    /* the page cleared the gallery */
        sp_cat(mk.shown ? "Gallery cleared. The picture on the screen is not kept" : "Gallery cleared");
    } else if (mk.cur != sp.cur && mk.cur != MK_NONE) {   /* the page showed one, or the slideshow moved on */
        sp_pic(mk.cur);
        if (ui.show_on)
            flags = 0;                      /* not the user's doing: after what is being said */
    }
    if (sp.show_on && !ui.show_on)          /* a picture from the page stops the slideshow */
        sp_cat(sp_n ? ". Slideshow off" : "Slideshow off");
    sp_snap();
    if (sp_n)
        ann_say(sp_buf, 0, flags);
}

#ifdef OM_HOST
/* every screen and message as a screen reader gets them (the "speakdump" command): KIND <tab> the text sent
 * (fm1-reader/tools/audit.mjs puts it in words with web/words/ and checks it); the scenarios' said log adds
 * the notes as they happen */
static void sp_print(const char *kind, const char *t) { printf("%s\t%s\n", kind, t); }
static void speak_dump(void)
{
    static const char *const MSG[][2] = {{"UPDATE MODE IN", "3.."}, {"UPDATE MODE", "CANCELLED"},
                                         {"SLIDESHOW OFF", ""}, {"ALREADY KEPT", ""}};
    static const int ERR[] = {MK_E_FULL, MK_E_FLASH, MK_E_DATA};   /* keeping: full, flash; showing: flash, data */
    uint32_t keep_n = mk.n, keep_cur = mk.cur, keep_shown = mk.shown, keep_show = ui.show_on, i, w;
    uint8_t keep_s = proj.show_s;
    char t[16];
    for (w = 0; w < 6u; w++) {              /* WHERE: the welcome page (empty, or not), a kept picture (the
                                               slideshow off, on), a new one not kept */
        mk.n = w == 0u ? 0u : 7u;
        mk.shown = w >= 2u;
        mk.cur = w == 2u || w == 3u ? 2u : w == 5u ? 0u : MK_NONE;
        if (w == 5u)
            mk.n = 1u;
        ui.show_on = w == 3u;
        sp_screen();
        sp_print("WHERE", sp_buf);
    }
    mk.n = 7u;
    mk.shown = 1u;
    for (i = 0; i < 7u; i++) {
        mk.cur = i;
        sp_n = 0;
        sp_pic(i);
        sp_print("SCREEN", sp_buf);
    }
    mk.cur = 6u;
    sp_got(MK_OK);
    sp_print("MSG", sp_buf);
    sp_got(MK_NOKEEP);
    sp_print("MSG", sp_buf);
    for (i = 0; i < 2u; i++) {
        sp_got((uint32_t)ERR[i]);
        sp_print("WARN", sp_buf);
        sp_print("WARN", sp_err("Not kept: ", ERR[i]));
        sp_print("WARN", sp_err("Picture not shown: ", ERR[i + 1u]));
    }
    for (i = 1; i <= 30u; i++) {            /* KNOB 4: the slideshow's pace */
        put_s(put_u(put_s(t, "EVERY "), i), " S");
        msg_text("SLIDESHOW", t);
        sp_print("KNOB", sp_buf);
        sp_n = 0;
        sp_cat("Slideshow on, ");
        sp_cat(t);
        sp_print("MSG", sp_buf);
    }
    for (i = 0; i < sizeof MSG / sizeof MSG[0]; i++) {
        msg_text(MSG[i][0], MSG[i][1]);
        sp_print("MSG", sp_buf);
    }
    mk.n = keep_n;
    mk.cur = keep_cur;
    mk.shown = keep_shown;
    ui.show_on = keep_show;
    proj.show_s = keep_s;
}
#endif

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
    speak();
    ann_tick(now);
    ui.frame++;
}

void ui_input_only(void) { input(); }
