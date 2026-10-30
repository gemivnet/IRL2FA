/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "app.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "base32.h"
#include "fwlink.h"
#include "ct.h"
#include "gfx.h"
#include "hal.h"
#include "pairing.h"
#include "store.h"
#include "third_party/monocypher/monocypher.h"
#include "timeutil.h"
#include "totp.h"
#include "words.h"

/* ---- tunables ---- */
#define IDLE_SLEEP_S_DEFAULT 30
#define REPEAT_DELAY_MS 450
#define REPEAT_RATE_MS 90
/* A key held longer than this is taken as jammed (pressed in a pocket):
 * it stops auto-repeating and stops counting as activity, so the device
 * goes to sleep instead of redrawing the panel ten times a second until the
 * cell is flat. Long enough for any real hold (scrolling a full list, a
 * clock field through 0..59). port/stm32u0 BTN_STUCK_MS must stay below. */
#define HOLD_LIMIT_MS 20000u
#define FULL_REFRESH_EVERY 5   /* GDEY0213B74 datasheet: full refresh after 5 fast/partial ones */
#define BAR_STEPS 3            /* countdown bars move in thirds of the period (partial refreshes) */
#define PAIR_TX_MS 220
/* Once the other device's COMMIT is in, its REVEAL follows within a second
 * or two even on a lossy link. A device stuck waiting longer is the sign of
 * a man in the middle who saw the two SAS values would not match and held
 * the REVEAL back: show that as a failed pairing, not an endless wait. */
#define PAIR_REVEAL_TIMEOUT_MS 15000
/* Listen before talk: the link is half duplex (each phototransistor also
 * sees its own LED), so a frame sent while the other device's is in the air
 * is lost on both sides. A frame is only started once no byte from the
 * other device has arrived for PAIR_QUIET_MS (about 3 byte times at 4800
 * baud); otherwise it waits a random 4-35 ms and looks again. */
#define PAIR_QUIET_MS 6
#define PAIR_BAUD 4800
/* Give up when no exchange completes in a minute (nobody there, or out of
 * range), so a device left on the pairing screen goes back to the menu and
 * can sleep: pairing keeps the device awake only until then. */
#define PAIR_TIMEOUT_MS 60000
/* Pairing keeps the device awake (the two people compare symbols and name
 * each other at their own pace) for at most this long from the start. */
#define PAIR_AWAKE_MS 180000
#define TIME_VALID_AFTER 1767225600 /* 2026-01-01T00:00:00Z */
#define TITLE_H 15
#define ROW_H 18
/* Simple mode: holding the centre this long opens the family PIN; after
 * FAMILY_HOLD_HINT_MS the screen says to keep holding. */
#define FAMILY_HOLD_MS 10000
#define FAMILY_HOLD_HINT_MS 3000
#define LOW_BATTERY_MV 2550

typedef enum {
    SCR_MENU,     /* generic menu, including home and settings */
    SCR_CODES,
    SCR_CODE,
    SCR_CONTACTS,
    SCR_CONTACT,
    SCR_PAIR,
    SCR_SAS,
    SCR_TEXT,
    SCR_CODE_OPTS,
    SCR_CONFIRM,
    SCR_TIME,
    SCR_TZ,
    SCR_PIN,
    SCR_MESSAGE,
    SCR_ABOUT,
    SCR_SIMPLE,       /* simple mode: one contact's name and HEAR word */
    SCR_SIMPLE_INTRO, /* what simple mode is, before the family PIN is set */
    SCR_UPDATE,   /* firmware update: sending this device's image (or entering the receiver) */
} screen_t;

typedef enum {
    M_HOME,
    M_CODE_ACTIONS,
    M_CONTACT_ACTIONS,
    M_SETTINGS,
    M_UPDATE,
} menu_id_t;

typedef enum {
    T_CODE_LABEL,
    T_CODE_SECRET,
    T_CONTACT_NAME,
    T_RENAME_CODE,
    T_RENAME_CONTACT,
} text_id_t;

typedef enum {
    C_DELETE,
    C_WIPE,
    C_REMOVE_PIN,
    C_SIMPLE_OFF,
} confirm_id_t;

typedef enum {
    P_UNLOCK,
    P_NEW,
    P_CONFIRM,
    P_FAMILY_NEW,     /* turning simple mode on: choose, confirm, confirm again */
    P_FAMILY_CONFIRM,
    P_FAMILY_AGAIN,
    P_FAMILY_UNLOCK,  /* leaving simple mode */
    P_UPDATE,     /* the PIN again before the update receiver */
} pin_id_t;

static const char CS_LABEL[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 -_.@+&'#";
static const char CS_BASE32[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

#define TEXT_MAX 104 /* 64-byte secret = 103 base32 characters */
#define MENU_MAX 8

static struct {
    screen_t scr;
    bool dirty;
    bool force_full;
    int fast_count;
    uint32_t view_key;     /* changes when time-dependent content changes */
    uint32_t tick_key;     /* changes when only a countdown bar moves */
    uint8_t code_period;   /* period of the code on screen (s) */
    uint32_t last_input_ms;

    uint8_t held;
    uint32_t held_since;
    uint32_t next_repeat;

    /* lists */
    int sel;
    int slot;              /* slot being viewed or edited */

    struct {
        menu_id_t id;
        int sel;
        int n;
        char items[MENU_MAX][28];
    } menu;

    struct {
        text_id_t id;
        const char *charset;
        char buf[TEXT_MAX + 1];
        int len;
        int max;
        int cand;
        bool touched;
        char title[24];
    } text;

    struct {
        confirm_id_t id;
        char msg[40];
    } confirm;

    struct {
        pin_id_t id;
        uint8_t digits[STORE_PIN_MAX];
        int len;
        uint8_t first[STORE_PIN_MAX];
        int first_len;
        uint32_t wait_until;
        char msg[32];
    } pin;

    struct {
        civil_t c;
        int field;
    } tm;

    struct {
        char lines[3][32];
        screen_t back;
        bool back_menu;
        menu_id_t back_menu_id;
    } msg;

    int opt_row;
    int8_t tz_edit;
    totp_rec_t draft;

    pairing_t pair;
    unsigned pair_seq;
    uint32_t pair_next_tx;
    uint32_t pair_found_ms; /* when the peer's COMMIT arrived */
    uint32_t pair_started_ms;
    uint32_t pair_rx_ms;    /* last byte heard from the other device */
    uint32_t pair_tx_end;   /* our own frame is in the air until then */

    int simple_rank;        /* simple mode: which contact (0 = first) */
    bool hold_shown;        /* simple mode: "keep holding" is on screen */
    struct {
        bool receiving;     /* SCR_UPDATE shows "restarting into the receiver" */
        fwtx_t tx;          /* else: sending this device's image */
        unsigned shown;     /* progress on screen, permille */
    } upd;
} ui;

static const char *const SCREEN_NAMES[] = {
    "menu", "codes", "code", "contacts", "contact", "pair", "sas", "text",
    "code_opts", "confirm", "time", "tz", "pin", "message", "about",
    "simple", "simple_intro", "update",
};

/* ------------------------------------------------------------------ */
/* helpers                                                            */

static void copy_str(char *dst, size_t cap, const char *src)
{
    size_t n = strlen(src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static int64_t now_utc(void)
{
    return hal_rtc_get();
}

static int64_t now_local(void)
{
    return now_utc() + (int64_t)store_settings()->tz_quarters * 900;
}

static bool time_valid(void)
{
    return now_utc() >= TIME_VALID_AFTER;
}

static void go(screen_t s)
{
    ui.scr = s;
    ui.dirty = true;
}

static bool simple_on(void)
{
    return store_settings()->simple != 0;
}

/* Simple mode shows words (not a plain message) only with someone paired
 * and the clock set. */
static bool simple_words_shown(void)
{
    return store_count(REC_CONTACT) > 0 && time_valid();
}

static void enter_simple(void)
{
    ui.simple_rank = 0;
    ui.hold_shown = false;
    go(SCR_SIMPLE);
}

static void title_bar(const char *title)
{
    gfx_fill(0, 0, DISPLAY_W, TITLE_H, true);
    gfx_invert(0, 0, DISPLAY_W, TITLE_H);
    gfx_text_fit(&font_small, 3, 1, 150, title);

    char buf[8];
    if (time_valid()) {
        civil_t c;
        time_to_civil(now_local(), &c);
        snprintf(buf, sizeof(buf), "%02d:%02d", c.hour, c.min);
    } else {
        snprintf(buf, sizeof(buf), "--:--");
    }
    gfx_text_right(&font_small, DISPLAY_W - 26, 1, buf);

    /* battery: 2.4 V empty, 3.0 V full */
    int mv = hal_battery_mv();
    int level = (mv - 2400) * 14 / 600;
    if (level < 0) level = 0;
    if (level > 14) level = 14;
    gfx_rect(DISPLAY_W - 21, 3, 17, 9);
    gfx_fill(DISPLAY_W - 4, 5, 2, 5, true);
    gfx_fill(DISPLAY_W - 20 + 1, 5, level, 5, true);
    gfx_invert(0, 0, DISPLAY_W, TITLE_H);
}

static void hint(const char *s)
{
    gfx_hline(0, DISPLAY_H - 14, DISPLAY_W);
    gfx_text_centered(&font_small, DISPLAY_W / 2, DISPLAY_H - 13, s);
}

static void progress(int y, int remaining, int period)
{
    int w = DISPLAY_W - 40;
    gfx_rect(4, y, w, 7);
    gfx_fill(5, y + 1, (w - 2) * remaining / period, 5, true);
    char buf[8];
    snprintf(buf, sizeof(buf), "%ds", remaining);
    gfx_text_right(&font_small, DISPLAY_W - 4, y - 3, buf);
}

static void message(const char *l1, const char *l2, const char *l3, screen_t back)
{
    copy_str(ui.msg.lines[0], sizeof(ui.msg.lines[0]), l1 ? l1 : "");
    copy_str(ui.msg.lines[1], sizeof(ui.msg.lines[1]), l2 ? l2 : "");
    copy_str(ui.msg.lines[2], sizeof(ui.msg.lines[2]), l3 ? l3 : "");
    ui.msg.back = back;
    ui.msg.back_menu = false;
    go(SCR_MESSAGE);
}

static void open_menu(menu_id_t id);

static void message_to_menu(const char *l1, const char *l2, menu_id_t id)
{
    message(l1, l2, NULL, SCR_MENU);
    ui.msg.back_menu = true;
    ui.msg.back_menu_id = id;
}

/* ------------------------------------------------------------------ */
/* menus                                                              */

static void menu_add(const char *s)
{
    if (ui.menu.n < MENU_MAX) {
        copy_str(ui.menu.items[ui.menu.n], sizeof(ui.menu.items[0]), s);
        ui.menu.n++;
    }
}

static void open_menu(menu_id_t id)
{
    char buf[28];
    ui.menu.id = id;
    ui.menu.n = 0;
    ui.menu.sel = 0;
    switch (id) {
    case M_HOME:
        snprintf(buf, sizeof(buf), "Codes (%d)", store_count(REC_TOTP));
        menu_add(buf);
        snprintf(buf, sizeof(buf), "Contacts (%d)", store_count(REC_CONTACT));
        menu_add(buf);
        menu_add("Pair with someone");
        menu_add("Add a code");
        menu_add("Settings");
        if (simple_on()) menu_add("Back to simple mode");
        break;
    case M_CODE_ACTIONS:
    case M_CONTACT_ACTIONS:
        menu_add("Rename");
        menu_add("Delete");
        break;
    case M_SETTINGS: {
        const settings_t *s = store_settings();
        int q = s->tz_quarters < 0 ? -s->tz_quarters : s->tz_quarters;
        menu_add("Date & time");
        snprintf(buf, sizeof(buf), "Time zone  UTC%c%02d:%02d", s->tz_quarters < 0 ? '-' : '+',
                 q / 4, (q % 4) * 15);
        menu_add(buf);
        menu_add(s->pin_set ? "Change PIN" : "Set a PIN");
        if (s->pin_set) menu_add("Remove PIN");
        menu_add("About");
        menu_add(s->simple ? "Simple mode: on" : "Simple mode");
        menu_add("Firmware update");
        menu_add("Erase everything");
        break;
    }
    case M_UPDATE: {
        uint8_t hdr[FW_HDR_SIZE];
        menu_add("Receive an update");
        if (hal_update_image(hdr)) menu_add("Send it to another");
        break;
    }
    }
    go(SCR_MENU);
}

static const char *menu_title(void)
{
    switch (ui.menu.id) {
    case M_HOME: return "IRL2FA";
    case M_CODE_ACTIONS: return "Code";
    case M_CONTACT_ACTIONS: return "Contact";
    case M_UPDATE: return "Firmware update";
    default: return "Settings";
    }
}

static void draw_list_rows(int n, int sel, int *top, void (*row)(int idx, int y))
{
    int rows = (DISPLAY_H - TITLE_H - 2) / ROW_H;
    if (sel < *top) *top = sel;
    if (sel >= *top + rows) *top = sel - rows + 1;
    for (int r = 0; r < rows && *top + r < n; r++) {
        int y = TITLE_H + 2 + r * ROW_H;
        row(*top + r, y);
        if (*top + r == sel) gfx_invert(0, y, DISPLAY_W - 6, ROW_H);
    }
    if (n > rows) { /* scrollbar */
        int h = DISPLAY_H - TITLE_H - 2;
        int bh = h * rows / n;
        if (bh < 6) bh = 6;
        int by = TITLE_H + 2 + (h - bh) * *top / (n - rows);
        gfx_fill(DISPLAY_W - 3, by, 2, bh, true);
    }
}

static void menu_row(int i, int y)
{
    gfx_text(&font_medium, 6, y + 1, ui.menu.items[i]);
}

static int menu_top;

static void draw_menu(void)
{
    title_bar(menu_title());
    draw_list_rows(ui.menu.n, ui.menu.sel, &menu_top, menu_row);
}

/* ------------------------------------------------------------------ */
/* text entry                                                         */

static void open_text(text_id_t id, const char *title, const char *charset, int max,
                      const char *initial)
{
    ui.text.id = id;
    ui.text.charset = charset;
    ui.text.max = max;
    copy_str(ui.text.title, sizeof(ui.text.title), title);
    copy_str(ui.text.buf, sizeof(ui.text.buf), initial ? initial : "");
    ui.text.len = (int)strlen(ui.text.buf);
    ui.text.cand = 0;
    ui.text.touched = false;
    go(SCR_TEXT);
}

static void draw_text(void)
{
    title_bar(ui.text.title);
    const char *cs = ui.text.charset;
    int ncs = (int)strlen(cs);
    char cand = cs[ui.text.cand];

    /* Show the tail of the text that fits, then the candidate box. */
    int cw = gfx_text_width(&font_mono, "M");
    int maxchars = (DISPLAY_W - 16) / cw - 1;
    int start = ui.text.len > maxchars ? ui.text.len - maxchars : 0;
    int y = 52;
    int x = 6;
    if (start > 0) {
        gfx_text(&font_small, 0, y + 3, "<");
    }
    char one[2] = {0, 0};
    for (int i = start; i < ui.text.len; i++) {
        one[0] = ui.text.buf[i];
        gfx_text(&font_mono, x, y, one);
        x += cw;
    }
    /* Candidate wheel: previous and next characters above and below. */
    one[0] = cand == ' ' ? '_' : cand;
    gfx_text(&font_mono, x + 1, y, one);
    gfx_invert(x, y - 1, cw + 2, font_mono.height + 1);
    one[0] = cs[(ui.text.cand + ncs - 1) % ncs];
    gfx_text(&font_small, x + 2, y - 15, one);
    one[0] = cs[(ui.text.cand + 1) % ncs];
    gfx_text(&font_small, x + 2, y + 18, one);
    gfx_arrow(ARROW_UP, x + 12, y - 13, 7);
    gfx_arrow(ARROW_DOWN, x + 12, y + 20, 7);

    char buf[24];
    snprintf(buf, sizeof(buf), "%d/%d", ui.text.len, ui.text.max);
    gfx_text_right(&font_small, DISPLAY_W - 4, TITLE_H + 2, buf);
    hint(GFX_UP GFX_DOWN " letter  " GFX_RIGHT " add  " GFX_LEFT " delete  " GFX_DOT " done");
}

static void text_finish(void);

static void text_key(uint8_t b)
{
    int ncs = (int)strlen(ui.text.charset);
    switch (b) {
    case BTN_UP:
        ui.text.cand = (ui.text.cand + ncs - 1) % ncs;
        ui.text.touched = true;
        break;
    case BTN_DOWN:
        ui.text.cand = (ui.text.cand + 1) % ncs;
        ui.text.touched = true;
        break;
    case BTN_RIGHT:
        if (ui.text.len < ui.text.max) {
            ui.text.buf[ui.text.len++] = ui.text.charset[ui.text.cand];
            ui.text.buf[ui.text.len] = '\0';
        }
        ui.text.touched = false;
        break;
    case BTN_LEFT:
        if (ui.text.len == 0) { /* cancel */
            if (ui.text.id == T_CODE_SECRET) {
                open_text(T_CODE_LABEL, "Name for this code", CS_LABEL, STORE_LABEL_LEN - 1,
                          ui.draft.label);
            } else if (ui.text.id == T_CONTACT_NAME) {
                return; /* a pairing must be named; nothing to go back to */
            } else {
                open_menu(M_HOME);
            }
            return;
        }
        /* Put the deleted character back on the wheel for quick fixes. */
        {
            const char *p = strchr(ui.text.charset, ui.text.buf[--ui.text.len]);
            if (p) ui.text.cand = (int)(p - ui.text.charset);
            ui.text.buf[ui.text.len] = '\0';
        }
        ui.text.touched = false;
        break;
    case BTN_CENTER:
        if (ui.text.touched && ui.text.len < ui.text.max) {
            ui.text.buf[ui.text.len++] = ui.text.charset[ui.text.cand];
            ui.text.buf[ui.text.len] = '\0';
        }
        /* trim trailing spaces */
        while (ui.text.len > 0 && ui.text.buf[ui.text.len - 1] == ' ')
            ui.text.buf[--ui.text.len] = '\0';
        if (ui.text.len == 0) return;
        text_finish();
        return;
    default:
        break;
    }
    ui.dirty = true;
}

/* ------------------------------------------------------------------ */
/* codes                                                              */

static void codes_row(int idx, int y)
{
    int slot = store_nth(REC_TOTP, idx);
    totp_rec_t r;
    if (slot < 0 || !store_read_totp((uint16_t)slot, &r)) return;
    gfx_text_fit(&font_medium, 6, y + 1, 150, r.label);
    char code[10];
    totp_format(totp((hash_alg_t)r.alg, r.secret, r.secret_len, (uint64_t)now_utc(), r.period,
                     r.digits),
                r.digits, code);
    gfx_text_right(&font_mono, DISPLAY_W - 10, y + 1, code);
    crypto_wipe(&r, sizeof(r));
}

static int list_top;

static void draw_codes(void)
{
    title_bar("Codes");
    int n = store_count(REC_TOTP);
    if (n == 0) {
        gfx_text_centered(&font_medium, DISPLAY_W / 2, 45, "No codes yet");
        gfx_text_centered(&font_small, DISPLAY_W / 2, 66, "Press " GFX_DOT " to add one");
        return;
    }
    draw_list_rows(n, ui.sel, &list_top, codes_row);
}

static void draw_code(void)
{
    totp_rec_t r;
    if (!store_read_totp((uint16_t)ui.slot, &r)) {
        title_bar("Code");
        return;
    }
    title_bar(r.label);
    ui.code_period = r.period;
    char code[10], spaced[12];
    totp_format(totp((hash_alg_t)r.alg, r.secret, r.secret_len, (uint64_t)now_utc(), r.period,
                     r.digits),
                r.digits, code);
    /* "123 456" / "1234 5678" */
    int half = r.digits / 2;
    memcpy(spaced, code, (size_t)half);
    spaced[half] = ' ';
    copy_str(spaced + half + 1, sizeof(spaced) - (size_t)half - 1, code + half);
    gfx_text_centered(&font_digits, DISPLAY_W / 2, TITLE_H + 6, spaced);

    int remaining = r.period - (int)(now_utc() % r.period);
    progress(DISPLAY_H - 26, remaining, r.period);

    char pos[16];
    snprintf(pos, sizeof(pos), "%d/%d", store_rank((uint16_t)ui.slot) + 1, store_count(REC_TOTP));
    gfx_text(&font_small, 4, DISPLAY_H - 13, pos);
    gfx_text_right(&font_small, DISPLAY_W - 4, DISPLAY_H - 13, GFX_UP GFX_DOWN " next   " GFX_DOT " options");
    crypto_wipe(&r, sizeof(r));
}

static void draw_code_opts(void)
{
    title_bar("New code: options");
    static const char *const ALG[] = {"SHA1", "SHA256", "SHA512"};
    char rows[3][24];
    snprintf(rows[0], sizeof(rows[0]), "Digits      %d", ui.draft.digits);
    snprintf(rows[1], sizeof(rows[1]), "Period      %ds", ui.draft.period);
    snprintf(rows[2], sizeof(rows[2]), "Algorithm   %s", ALG[ui.draft.alg % 3]);
    for (int i = 0; i < 3; i++) {
        int y = TITLE_H + 4 + i * ROW_H;
        gfx_text(&font_medium, 8, y + 1, rows[i]);
        if (i == ui.opt_row) gfx_invert(0, y, DISPLAY_W, ROW_H);
    }
    gfx_text(&font_small, 8, TITLE_H + 4 + 3 * ROW_H + 2,
             "Most sites: 6 digits, 30s, SHA1.");
    hint(GFX_UP GFX_DOWN " row  " GFX_LEFT GFX_RIGHT " change  " GFX_DOT " save");
}

static void save_draft(void)
{
    int slot = store_write_totp(-1, &ui.draft);
    crypto_wipe(&ui.draft, sizeof(ui.draft));
    if (slot < 0) {
        message_to_menu("Storage is full", "Delete a code first", M_HOME);
        return;
    }
    ui.slot = slot;
    go(SCR_CODE);
}

/* ------------------------------------------------------------------ */
/* contacts                                                           */

static void contacts_row(int idx, int y)
{
    int slot = store_nth(REC_CONTACT, idx);
    contact_rec_t r;
    if (slot < 0 || !store_read_contact((uint16_t)slot, &r)) return;
    gfx_text_fit(&font_medium, 6, y + 1, DISPLAY_W - 20, r.name);
    crypto_wipe(&r, sizeof(r));
}

static void draw_contacts(void)
{
    title_bar("Contacts");
    int n = store_count(REC_CONTACT);
    if (n == 0) {
        gfx_text_centered(&font_medium, DISPLAY_W / 2, 45, "No contacts yet");
        gfx_text_centered(&font_small, DISPLAY_W / 2, 66, "Press " GFX_DOT " to pair with someone");
        return;
    }
    draw_list_rows(n, ui.sel, &list_top, contacts_row);
}

static void draw_contact(void)
{
    contact_rec_t r;
    if (!store_read_contact((uint16_t)ui.slot, &r)) {
        title_bar("Contact");
        return;
    }
    title_bar(r.name);
    uint64_t t = (uint64_t)now_utc();
    uint64_t w = t / WORDS_PERIOD_S;
    uint16_t give, recv, pgive, precv;
    words_pair(r.key, r.is_low, w, &give, &recv);
    words_pair(r.key, r.is_low, w - 1, &pgive, &precv);
    (void)pgive;

    gfx_text(&font_small, 6, TITLE_H + 6, "SAY");
    gfx_text(&font_large, 50, TITLE_H + 1, words_word(give));
    gfx_text(&font_small, 6, TITLE_H + 36, "HEAR");
    gfx_text(&font_large, 50, TITLE_H + 31, words_word(recv));
    char also[32];
    snprintf(also, sizeof(also), "or %s", words_word(precv));
    gfx_text(&font_small, 50, TITLE_H + 58, also);

    int remaining = WORDS_PERIOD_S - (int)(t % WORDS_PERIOD_S);
    progress(DISPLAY_H - 26, remaining, WORDS_PERIOD_S);
    gfx_text(&font_small, 4, DISPLAY_H - 13, GFX_UP GFX_DOWN " next contact");
    gfx_text_right(&font_small, DISPLAY_W - 4, DISPLAY_H - 13, GFX_DOT " options");
    crypto_wipe(&r, sizeof(r));
}

/* ------------------------------------------------------------------ */
/* simple mode                                                        */

/* The bottom line: where we are in the list, and the battery in words. Or,
 * while the centre is held, that the hold is being counted. */
static void simple_footer(int rank, int n)
{
    int y = DISPLAY_H - 13;
    if (ui.hold_shown) {
        gfx_text_centered(&font_small, DISPLAY_W / 2, y, "Family settings: keep holding " GFX_DOT);
        gfx_invert(0, y - 1, DISPLAY_W, DISPLAY_H - y + 1);
        return;
    }
    if (n > 1) {
        char pos[32];
        snprintf(pos, sizeof(pos), GFX_UP GFX_DOWN " %d of %d", rank + 1, n);
        gfx_text(&font_small, 3, y, pos);
    }
    if (hal_battery_mv() < LOW_BATTERY_MV) {
        int w = gfx_text_width(&font_small, "Battery low: tell your family") + 6;
        gfx_text(&font_small, DISPLAY_W - w + 3, y, "Battery low: tell your family");
        gfx_invert(DISPLAY_W - w, y - 1, w, DISPLAY_H - y + 1);
    }
}

static void simple_note(const char *l1, const char *l2)
{
    gfx_text_centered(&font_medium, DISPLAY_W / 2, 30, l1);
    gfx_text_centered(&font_medium, DISPLAY_W / 2, 52, l2);
}

/* One contact, read only: the name, the word they will say (this device's
 * HEAR word) and what to do if they don't. The SAY word is never drawn:
 * in simple mode this device only checks the caller. */
static void draw_simple(void)
{
    int n = store_count(REC_CONTACT);
    if (n == 0) {
        simple_note("Nobody is set up yet.", "Ask your family to set this up.");
        simple_footer(0, 0);
        return;
    }
    if (!time_valid()) { /* the words would be wrong */
        simple_note("The clock needs setting.", "Ask your family to set it.");
        simple_footer(0, 0);
        return;
    }
    if (ui.simple_rank >= n) ui.simple_rank = 0;
    int slot = store_nth(REC_CONTACT, ui.simple_rank);
    contact_rec_t r;
    if (slot < 0 || !store_read_contact((uint16_t)slot, &r)) return;

    /* "Susan will say:" with the name large; a long name is shortened. */
    static const char WILL_SAY[] = " will say:";
    int ws = gfx_text_width(&font_medium, WILL_SAY);
    int max_name = DISPLAY_W - 6 - ws;
    /* The fonts' top rows are blank (room for accents): start above 0. */
    const int name_y = -3;
    int x = 3;
    if (gfx_text_width(&font_large, r.name) <= max_name) {
        x = gfx_text(&font_large, x, name_y, r.name);
    } else {
        gfx_text_fit(&font_large, x, name_y, max_name, r.name);
        x += max_name;
    }
    gfx_text(&font_medium, x, name_y + font_large.ascent - font_medium.ascent, WILL_SAY);

    uint64_t t = (uint64_t)now_utc();
    uint64_t w = t / WORDS_PERIOD_S;
    uint16_t give, recv, pgive, precv;
    words_pair(r.key, r.is_low, w, &give, &recv);
    words_pair(r.key, r.is_low, w - 1, &pgive, &precv);
    (void)give;
    (void)pgive;
    crypto_wipe(&r, sizeof(r));
    gfx_text_centered(&font_huge, DISPLAY_W / 2, 17, words_word(recv));

    /* The word just before also counts (it may change while they talk). */
    char also[32];
    snprintf(also, sizeof(also), "Also OK: %s", words_word(precv));
    gfx_text(&font_small, 3, 58, also);
    /* Time left as three dots that empty, in step with the partial-refresh
     * countdown ticks (BAR_STEPS); no numbers to read. */
    int remaining = WORDS_PERIOD_S - (int)(t % WORDS_PERIOD_S);
    int filled = (remaining * BAR_STEPS + WORDS_PERIOD_S - 1) / WORDS_PERIOD_S;
    for (int i = 0; i < BAR_STEPS; i++) {
        int cx = DISPLAY_W - 8 - (BAR_STEPS - 1 - i) * 11;
        if (i < filled) gfx_arrow(ARROW_DOT, cx - 5, 60, 11);
        else gfx_rect(cx - 2, 63, 5, 5);
    }

    gfx_hline(0, 74, DISPLAY_W);
    gfx_text_centered(&font_medium, DISPLAY_W / 2, 75, "If they don't say this word,");
    gfx_text_centered(&font_medium, DISPLAY_W / 2, 89, "hang up and call them back.");
    simple_footer(ui.simple_rank, n);
}

static void draw_simple_intro(void)
{
    title_bar("Simple mode");
    static const char *const L[] = {
        "For someone who only needs to check",
        "who is calling. Shows each person's",
        "name and the word they will say.",
        GFX_UP GFX_DOWN " next person. Nothing else to press.",
        "To leave: hold " GFX_DOT " for 10 s, then the",
        "family PIN. Write the PIN down!",
    };
    for (int i = 0; i < 6; i++) gfx_text(&font_small, 5, TITLE_H + 2 + i * 13, L[i]);
    hint(GFX_DOT " set a family PIN     " GFX_LEFT " back");
}

/* ------------------------------------------------------------------ */
/* pairing                                                            */

static void start_pairing(void)
{
    uint8_t rnd[48];
    hal_random(rnd, sizeof(rnd));
    pairing_start(&ui.pair, rnd);
    crypto_wipe(rnd, sizeof(rnd));
    ui.pair_seq = 0;
    ui.pair_next_tx = hal_millis();
    ui.pair_started_ms = ui.pair_next_tx;
    ui.pair_rx_ms = ui.pair_next_tx - 1000u;
    ui.pair_tx_end = ui.pair_next_tx;
    hal_ir_enable(true);
    go(SCR_PAIR);
}

static void stop_pairing(void)
{
    hal_ir_enable(false);
    pairing_wipe(&ui.pair);
}

static void draw_pair(void)
{
    title_bar("Pair with someone");
    gfx_text(&font_small, 6, TITLE_H + 6, "Hold the two devices top edge to");
    gfx_text(&font_small, 6, TITLE_H + 19, "top edge. Both choose Pair.");
    const char *status = "Looking for the other device...";
    if (ui.pair.state == PAIR_REVEALING) status = "Found it. Exchanging keys...";
    if (ui.pair.state == PAIR_FAILED) status = "Pairing failed. " GFX_DOT " to retry.";
    gfx_text_centered(&font_medium, DISPLAY_W / 2, 62, status);
    hint(GFX_LEFT " cancel");
}

static arrow_t sas_arrow(uint8_t s)
{
    static const arrow_t A[5] = {ARROW_UP, ARROW_RIGHT, ARROW_DOWN, ARROW_LEFT, ARROW_DOT};
    return A[s % 5];
}

static void draw_sas(void)
{
    title_bar("Compare screens");
    gfx_text_centered(&font_small, DISPLAY_W / 2, TITLE_H + 4,
                      "Both screens must show the same symbols:");
    /* If the other device never got here, a man in the middle may have held
     * its REVEAL back: both must start again (docs/formal-verification.md). */
    gfx_text_centered(&font_small, DISPLAY_W / 2, 78, "No symbols on the other? " GFX_LEFT " on both.");
    int size = 26, gap = 10;
    int total = PAIR_SAS_LEN * size + (PAIR_SAS_LEN - 1) * gap;
    int x = (DISPLAY_W - total) / 2;
    for (int i = 0; i < PAIR_SAS_LEN; i++) {
        gfx_rect(x - 3, 40, size + 6, size + 6);
        gfx_arrow(sas_arrow(ui.pair.sas[i]), x, 43, size);
        x += size + gap;
    }
    hint(GFX_DOT " they match     " GFX_LEFT " they differ");
}

/* ------------------------------------------------------------------ */
/* confirm, PIN, time, time zone, about, message                     */

static void ask(confirm_id_t id, const char *msg)
{
    ui.confirm.id = id;
    copy_str(ui.confirm.msg, sizeof(ui.confirm.msg), msg);
    go(SCR_CONFIRM);
}

static void draw_confirm(void)
{
    title_bar("Are you sure?");
    gfx_text_centered(&font_medium, DISPLAY_W / 2, 45, ui.confirm.msg);
    hint(GFX_DOT " yes     " GFX_LEFT " no");
}

static void open_pin(pin_id_t id, const char *msg)
{
    ui.pin.id = id;
    ui.pin.len = 0;
    copy_str(ui.pin.msg, sizeof(ui.pin.msg), msg);
    go(SCR_PIN);
}

static const char *pin_title(void)
{
    switch (ui.pin.id) {
    case P_UNLOCK: return "Locked";
    case P_NEW:
    case P_CONFIRM: return "PIN";
    default: return "Family PIN";
    }
}

static void draw_pin(void)
{
    title_bar(pin_title());
    gfx_text_centered(&font_medium, DISPLAY_W / 2, TITLE_H + 8, ui.pin.msg);
    int x = (DISPLAY_W - STORE_PIN_MAX * 16) / 2;
    for (int i = 0; i < STORE_PIN_MAX; i++) {
        if (i < ui.pin.len) gfx_arrow(ARROW_DOT, x + i * 16, 52, 14);
        else gfx_hline(x + i * 16 + 2, 64, 10);
    }
    int32_t wait = (int32_t)(ui.pin.wait_until - hal_millis());
    if (wait > 0) {
        char buf[24];
        snprintf(buf, sizeof(buf), "Wait %ld s", (long)((wait + 999) / 1000));
        gfx_text_centered(&font_small, DISPLAY_W / 2, 76, buf);
    } else if (ui.pin.id == P_FAMILY_UNLOCK && store_family_wait_s(now_utc()) > 0) {
        /* RTC-based, so it holds across sleep and resets. */
        uint32_t s = store_family_wait_s(now_utc());
        char buf[32];
        unsigned shown = s >= 60 ? (unsigned)((s + 59) / 60) : (unsigned)((s + 9) / 10) * 10u;
        snprintf(buf, sizeof(buf), s >= 60 ? "Try again in %u min" : "Try again in %u s", shown);
        gfx_text_centered(&font_small, DISPLAY_W / 2, 76, buf);
    } else if (ui.pin.id == P_UNLOCK && store_pin_fails() > 0) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d tries left before erase",
                 STORE_PIN_TRIES - store_pin_fails());
        gfx_text_centered(&font_small, DISPLAY_W / 2, 76, buf);
    }
    if (ui.pin.id == P_FAMILY_UNLOCK && ui.pin.len == 0)
        hint(GFX_UP GFX_DOWN GFX_LEFT GFX_RIGHT " enter   " GFX_DOT " done   " GFX_LEFT " first: back");
    else
        hint("enter with " GFX_UP GFX_DOWN GFX_LEFT GFX_RIGHT "    " GFX_DOT " done");
}

static void enter_time_editor(void)
{
    int64_t t = time_valid() ? now_local() : TIME_VALID_AFTER;
    time_to_civil(t, &ui.tm.c);
    ui.tm.field = 0;
    go(SCR_TIME);
}

static void draw_time(void)
{
    title_bar("Date & time (local)");
    char date[16], tim[16];
    const civil_t *c = &ui.tm.c;
    snprintf(date, sizeof(date), "%04d-%02d-%02d", c->year, c->month, c->day);
    snprintf(tim, sizeof(tim), "%02d:%02d:%02d", c->hour, c->min, c->sec);
    int cw = gfx_text_width(&font_mono, "0");
    int xd = (DISPLAY_W - 10 * cw) / 2, xt = (DISPLAY_W - 8 * cw) / 2;
    gfx_text(&font_mono, xd, 34, date);
    gfx_text(&font_mono, xt, 60, tim);
    /* underline the selected field */
    static const int DX[6] = {0, 5, 8, 0, 3, 6};
    static const int DW[6] = {4, 2, 2, 2, 2, 2};
    int f = ui.tm.field;
    int x = (f < 3 ? xd : xt) + DX[f] * cw;
    int y = (f < 3 ? 34 : 60) + font_mono.height + 1;
    gfx_fill(x, y, DW[f] * cw, 2, true);
    hint(GFX_LEFT GFX_RIGHT " field  " GFX_UP GFX_DOWN " change  " GFX_DOT " save");
}

static void time_adjust(int d)
{
    civil_t *c = &ui.tm.c;
    switch (ui.tm.field) {
    case 0: c->year += d; if (c->year < 2026) c->year = 2026; if (c->year > 2099) c->year = 2099; break;
    case 1: c->month = (c->month - 1 + d + 12) % 12 + 1; break;
    case 2: c->day += d; break;
    case 3: c->hour = (c->hour + d + 24) % 24; break;
    case 4: c->min = (c->min + d + 60) % 60; break;
    case 5: c->sec = (c->sec + d + 60) % 60; break;
    default: break;
    }
    int dim = time_days_in_month(c->year, c->month);
    if (c->day < 1) c->day = dim;
    if (c->day > dim) c->day = (ui.tm.field == 2) ? 1 : dim;
}

static void draw_tz(void)
{
    title_bar("Time zone");
    int q = ui.tz_edit < 0 ? -ui.tz_edit : ui.tz_edit;
    char buf[20];
    snprintf(buf, sizeof(buf), "UTC%c%02d:%02d", ui.tz_edit < 0 ? '-' : '+', q / 4, (q % 4) * 15);
    gfx_text_centered(&font_large, DISPLAY_W / 2, 40, buf);
    gfx_text_centered(&font_small, DISPLAY_W / 2, 72, "Codes always use UTC; this only");
    gfx_text_centered(&font_small, DISPLAY_W / 2, 84, "changes the clock you see.");
    hint(GFX_UP GFX_DOWN " change  " GFX_DOT " save  " GFX_LEFT " back");
}

static void draw_about(void)
{
    title_bar("About");
    char buf[40];
    uint8_t uid[12];
    hal_device_uid(uid);
    gfx_text(&font_medium, 6, TITLE_H + 3, "IRL2FA v" IRL2FA_VERSION);
    snprintf(buf, sizeof(buf), "Device %02X%02X%02X%02X", uid[0], uid[1], uid[2], uid[3]);
    gfx_text(&font_small, 6, TITLE_H + 22, buf);
    int mv = hal_battery_mv();
    snprintf(buf, sizeof(buf), "Battery %d.%02d V", mv / 1000, (mv % 1000) / 10);
    gfx_text(&font_small, 6, TITLE_H + 35, buf);
    snprintf(buf, sizeof(buf), "%d codes, %d contacts, %d free", store_count(REC_TOTP),
             store_count(REC_CONTACT), store_count(REC_EMPTY));
    gfx_text(&font_small, 6, TITLE_H + 48, buf);
    gfx_text(&font_small, 6, TITLE_H + 66, "irl2fa.com");
    gfx_text(&font_small, 6, TITLE_H + 79, "github.com/gemivnet/IRL2FA");
}

static void draw_message(void)
{
    title_bar("IRL2FA");
    for (int i = 0; i < 3; i++)
        gfx_text_centered(i == 0 ? &font_medium : &font_small, DISPLAY_W / 2,
                          32 + i * 20, ui.msg.lines[i]);
    hint("press any direction");
}

static void draw_sleep(void)
{
    gfx_clear();
    gfx_text_centered(&font_large, DISPLAY_W / 2, 30, "IRL2FA");
    gfx_text_centered(&font_small, DISPLAY_W / 2, 62, "Asleep. Press the joystick to wake.");
    gfx_text_centered(&font_small, DISPLAY_W / 2, 100, "irl2fa.com");
}

/* ------------------------------------------------------------------ */
/* firmware update (docs/firmware-updates.md)                         */

static void render(bool tick_only);

static void fmt_version(char *buf, size_t cap, uint32_t v)
{
    snprintf(buf, cap, "v%u.%u.%u", (unsigned)(v >> 16), (unsigned)((v >> 8) & 0xFF),
             (unsigned)(v & 0xFF));
}

static const char *update_error(int e)
{
    switch (e) {
    case FW_E_ROLLBACK: return "Older than this one";
    case FW_E_HW: return "Made for other hardware";
    case FW_E_BOOT: return "Needs a newer bootloader";
    case FW_E_SIGNATURE: return "Not signed by IRL2FA";
    case FW_E_UNSIGNED: return "Not a release (unsigned)";
    case FW_E_NOKEY: return "This device takes no updates";
    case FW_E_SIZE: return "Too big";
    case FW_E_CANCELLED: return "Cancelled";
    case FW_E_TIMEOUT: return "The other device went quiet";
    case FW_E_BATTERY: return "Battery too low";
    default: return "Transfer failed";
    }
}

/* Into the bootloader's receiver: the panel keeps this screen while it
 * listens (it has no display driver of its own). */
static void update_receive(void)
{
    ui.upd.receiving = true;
    go(SCR_UPDATE);
    ui.force_full = true;
    render(false);
    hal_update_enter();
    /* Still here: no bootloader (the simulator). */
    ui.upd.receiving = false;
    message_to_menu("Not on this device", "Updates need a real IRL2FA", M_UPDATE);
}

static bool update_read(void *ctx, uint32_t off, uint8_t *buf, size_t n)
{
    (void)ctx;
    return hal_update_read(off, buf, n);
}

static void update_send(void)
{
    uint8_t hdr[FW_HDR_SIZE];
    if (!hal_update_image(hdr)) {
        open_menu(M_UPDATE);
        return;
    }
    memset(&ui.upd, 0, sizeof(ui.upd));
    fwtx_start(&ui.upd.tx, hdr, update_read, NULL, hal_millis());
    hal_ir_enable(true);
    go(SCR_UPDATE);
}

static void update_stop(void)
{
    if (!ui.upd.receiving) hal_ir_enable(false);
}

static void update_select(int item)
{
    if (item == 0) {
        /* Gated by the PIN, asked again: an unlocked device left on a table
         * is not enough. */
        if (store_settings()->pin_set) open_pin(P_UPDATE, "PIN to update");
        else update_receive();
    } else {
        update_send();
    }
}

/* The PIN is checked the only way the store can: lock, unlock. A wrong one
 * counts like any other and leaves the device locked. */
static void update_pin_submit(void)
{
    store_lock();
    store_status_t st = store_unlock(ui.pin.digits, ui.pin.len);
    crypto_wipe(ui.pin.digits, sizeof(ui.pin.digits));
    ui.pin.len = 0;
    if (st == STORE_OK) {
        update_receive();
    } else if (st == STORE_WIPED) {
        message_to_menu("Too many wrong PINs", "Everything was erased", M_HOME);
    } else {
        open_pin(P_UNLOCK, "Wrong PIN");
    }
}

static void update_poll(uint32_t now)
{
    fwtx_t *tx = &ui.upd.tx;
    uint8_t frame[FWL_FRAME_MAX];
    size_t n = fwtx_poll(tx, now, frame, sizeof(frame));
    if (n) hal_ir_send(frame, n);
    ui.last_input_ms = now; /* awake while it runs; fwtx's timeouts end it */
    unsigned p = fwtx_permille(tx);
    if (p >= ui.upd.shown + 100 || (p == 1000 && ui.upd.shown != 1000)) {
        ui.upd.shown = p;
        ui.dirty = true;
    }
    if (tx->state == FWTX_DONE) {
        update_stop();
        char v[16], line[48];
        fmt_version(v, sizeof(v), hal_fw_version());
        snprintf(line, sizeof(line), "The other one now runs %s", v);
        message_to_menu("Sent", line, M_UPDATE);
    } else if (tx->state == FWTX_FAILED) {
        update_stop();
        message_to_menu("Not sent", update_error(tx->err), M_UPDATE);
    }
}

static void draw_update(void)
{
    title_bar("Firmware update");
    char v[16];
    fmt_version(v, sizeof(v), hal_fw_version());
    if (ui.upd.receiving) {
        gfx_text_centered(&font_medium, DISPLAY_W / 2, TITLE_H + 6, "Waiting for the update");
        gfx_text_centered(&font_small, DISPLAY_W / 2, TITLE_H + 30, "Hold the sender to the top edge.");
        gfx_text_centered(&font_small, DISPLAY_W / 2, TITLE_H + 43, "This screen stays until it is done");
        gfx_text_centered(&font_small, DISPLAY_W / 2, TITLE_H + 56, "(a few minutes). Codes are kept.");
        char buf[40];
        snprintf(buf, sizeof(buf), "Now %s.  " GFX_LEFT " cancels", v);
        hint(buf);
        return;
    }
    const fwtx_t *tx = &ui.upd.tx;
    char buf[40];
    snprintf(buf, sizeof(buf), "Sending %s", v);
    gfx_text_centered(&font_medium, DISPLAY_W / 2, TITLE_H + 6, buf);
    const char *what = "Looking for the other device...";
    if (tx->state == FWTX_SENDING) what = "Sending...";
    if (tx->state == FWTX_INSTALLING) what = "The other device is installing";
    gfx_text_centered(&font_small, DISPLAY_W / 2, TITLE_H + 28, what);
    int w = DISPLAY_W - 40;
    gfx_rect(20, TITLE_H + 50, w, 9);
    gfx_fill(21, TITLE_H + 51, (w - 2) * (int)ui.upd.shown / 1000, 7, true);
    hint(GFX_LEFT " cancel");
}

static void update_show_result(void)
{
    uint32_t ver;
    int r = hal_update_result(&ver);
    if (r < 0 || !store_unlocked() || !time_valid()) return;
    char v[16];
    fmt_version(v, sizeof(v), ver);
    if (r == FW_OK) {
        char line[32];
        snprintf(line, sizeof(line), "Now running %s", v);
        message_to_menu("Firmware updated", line, M_HOME);
    } else {
        message_to_menu("Update not installed", update_error(r), M_HOME);
    }
}

/* ------------------------------------------------------------------ */
/* actions                                                            */

static void text_finish(void)
{
    switch (ui.text.id) {
    case T_CODE_LABEL:
        copy_str(ui.draft.label, sizeof(ui.draft.label), ui.text.buf);
        open_text(T_CODE_SECRET, "Secret key (base32)", CS_BASE32, TEXT_MAX - 1, NULL);
        break;
    case T_CODE_SECRET: {
        int n = base32_decode(ui.text.buf, ui.draft.secret, sizeof(ui.draft.secret));
        if (n < 10) {
            message("That key is not valid", n < 0 ? "Too long" : "Too short (min 16 chars)",
                    "Check it and try again", SCR_TEXT);
            return;
        }
        ui.draft.secret_len = (uint8_t)n;
        ui.draft.digits = 6;
        ui.draft.period = 30;
        ui.draft.alg = HASH_SHA1;
        ui.opt_row = 0;
        crypto_wipe(ui.text.buf, sizeof(ui.text.buf));
        go(SCR_CODE_OPTS);
        break;
    }
    case T_CONTACT_NAME: {
        contact_rec_t c;
        memset(&c, 0, sizeof(c));
        copy_str(c.name, sizeof(c.name), ui.text.buf);
        memcpy(c.key, ui.pair.key, 32);
        c.is_low = ui.pair.is_low;
        c.paired_at = (uint32_t)now_utc();
        int slot = store_write_contact(-1, &c);
        crypto_wipe(&c, sizeof(c));
        stop_pairing();
        if (slot < 0) {
            message_to_menu("Storage is full", "Delete a code first", M_HOME);
            return;
        }
        ui.slot = slot;
        go(SCR_CONTACT);
        break;
    }
    case T_RENAME_CODE: {
        totp_rec_t r;
        if (store_read_totp((uint16_t)ui.slot, &r)) {
            copy_str(r.label, sizeof(r.label), ui.text.buf);
            store_write_totp(ui.slot, &r);
            crypto_wipe(&r, sizeof(r));
        }
        go(SCR_CODE);
        break;
    }
    case T_RENAME_CONTACT: {
        contact_rec_t r;
        if (store_read_contact((uint16_t)ui.slot, &r)) {
            copy_str(r.name, sizeof(r.name), ui.text.buf);
            store_write_contact(ui.slot, &r);
            crypto_wipe(&r, sizeof(r));
        }
        go(SCR_CONTACT);
        break;
    }
    }
}

static void menu_select(void)
{
    int s = ui.menu.sel;
    switch (ui.menu.id) {
    case M_HOME:
        ui.sel = 0;
        list_top = 0;
        if (s == 0) go(SCR_CODES);
        else if (s == 1) go(SCR_CONTACTS);
        else if (s == 2) start_pairing();
        else if (s == 3) {
            memset(&ui.draft, 0, sizeof(ui.draft));
            open_text(T_CODE_LABEL, "Name for this code", CS_LABEL, STORE_LABEL_LEN - 1, NULL);
        } else if (s == 4) open_menu(M_SETTINGS);
        else enter_simple(); /* "Back to simple mode" */
        break;
    case M_CODE_ACTIONS:
    case M_CONTACT_ACTIONS: {
        bool code = ui.menu.id == M_CODE_ACTIONS;
        char name[STORE_LABEL_LEN] = "";
        if (code) {
            totp_rec_t r;
            if (store_read_totp((uint16_t)ui.slot, &r)) memcpy(name, r.label, sizeof(name));
            crypto_wipe(&r, sizeof(r));
        } else {
            contact_rec_t r;
            if (store_read_contact((uint16_t)ui.slot, &r)) memcpy(name, r.name, sizeof(name));
            crypto_wipe(&r, sizeof(r));
        }
        if (s == 0) {
            open_text(code ? T_RENAME_CODE : T_RENAME_CONTACT, "Rename", CS_LABEL,
                      STORE_LABEL_LEN - 1, name);
        } else {
            char msg[40];
            snprintf(msg, sizeof(msg), "Delete %s?", name);
            ask(C_DELETE, msg);
        }
        break;
    }
    case M_SETTINGS: {
        const settings_t *st = store_settings();
        /* Item list differs when a PIN is set ("Remove PIN" is extra). */
        int item = s;
        if (!st->pin_set && item >= 3) item++;
        if (item == 0) enter_time_editor();
        else if (item == 1) {
            ui.tz_edit = st->tz_quarters;
            go(SCR_TZ);
        } else if (item == 2) open_pin(P_NEW, "Choose a PIN (4-12)");
        else if (item == 3) ask(C_REMOVE_PIN, "Remove the PIN?");
        else if (item == 4) go(SCR_ABOUT);
        else if (item == 5) {
            if (st->simple) ask(C_SIMPLE_OFF, "Turn simple mode off?");
            else go(SCR_SIMPLE_INTRO);
        } else if (item == 6) open_menu(M_UPDATE);
        else ask(C_WIPE, "Erase ALL codes & contacts?");
        break;
    }
    case M_UPDATE:
        update_select(s);
        break;
    }
}

static void confirm_yes(void)
{
    switch (ui.confirm.id) {
    case C_DELETE: {
        bool code = store_slot_type((uint16_t)ui.slot) == REC_TOTP;
        store_delete((uint16_t)ui.slot);
        ui.sel = 0;
        list_top = 0;
        go(code ? SCR_CODES : SCR_CONTACTS);
        break;
    }
    case C_WIPE:
        store_wipe();
        message_to_menu("Everything erased", NULL, M_HOME);
        break;
    case C_REMOVE_PIN:
        store_set_pin(NULL, 0);
        message_to_menu("PIN removed", NULL, M_SETTINGS);
        break;
    case C_SIMPLE_OFF:
        store_set_simple(NULL, 0);
        message_to_menu("Simple mode is off", NULL, M_SETTINGS);
        break;
    }
}

static void confirm_no(void)
{
    switch (ui.confirm.id) {
    case C_DELETE:
        go(store_slot_type((uint16_t)ui.slot) == REC_TOTP ? SCR_CODE : SCR_CONTACT);
        break;
    default:
        open_menu(M_SETTINGS);
        break;
    }
}

static void pin_submit(void)
{
    switch (ui.pin.id) {
    case P_UNLOCK: {
        store_status_t st = store_unlock(ui.pin.digits, ui.pin.len);
        crypto_wipe(ui.pin.digits, sizeof(ui.pin.digits));
        ui.pin.len = 0;
        if (st == STORE_OK) {
            if (simple_on()) enter_simple();
            else if (!time_valid()) enter_time_editor();
            else open_menu(M_HOME);
        } else if (st == STORE_WIPED) {
            message_to_menu("Too many wrong PINs", "Everything was erased", M_HOME);
        } else {
            uint8_t f = store_pin_fails();
            uint32_t delay = f >= 3 ? (1000u << (f - 3 > 6 ? 6 : f - 3)) : 0;
            ui.pin.wait_until = hal_millis() + delay;
            snprintf(ui.pin.msg, sizeof(ui.pin.msg), "Wrong PIN");
            ui.dirty = true;
        }
        break;
    }
    case P_NEW:
        if (ui.pin.len < STORE_PIN_MIN) {
            snprintf(ui.pin.msg, sizeof(ui.pin.msg), "At least %d moves", STORE_PIN_MIN);
            ui.pin.len = 0;
            ui.dirty = true;
            return;
        }
        memcpy(ui.pin.first, ui.pin.digits, sizeof(ui.pin.first));
        ui.pin.first_len = ui.pin.len;
        open_pin(P_CONFIRM, "Enter it again");
        break;
    case P_UPDATE:
        update_pin_submit();
        break;
    case P_CONFIRM:
        /* The length is public (the screen shows a dot per symbol). */
        if (ui.pin.len != ui.pin.first_len ||
            !ct_equal(ui.pin.digits, ui.pin.first, (size_t)ui.pin.len)) {
            crypto_wipe(ui.pin.first, sizeof(ui.pin.first));
            open_pin(P_NEW, "Didn't match. Choose again");
            return;
        }
        store_set_pin(ui.pin.digits, ui.pin.len);
        crypto_wipe(ui.pin.first, sizeof(ui.pin.first));
        crypto_wipe(ui.pin.digits, sizeof(ui.pin.digits));
        message_to_menu("PIN set", "Needed every time it wakes", M_SETTINGS);
        break;
    case P_FAMILY_NEW:
        if (ui.pin.len < STORE_FAMILY_PIN_MIN) {
            snprintf(ui.pin.msg, sizeof(ui.pin.msg), "At least %d moves", STORE_FAMILY_PIN_MIN);
            ui.pin.len = 0;
            ui.dirty = true;
            return;
        }
        memcpy(ui.pin.first, ui.pin.digits, sizeof(ui.pin.first));
        ui.pin.first_len = ui.pin.len;
        crypto_wipe(ui.pin.digits, sizeof(ui.pin.digits));
        open_pin(P_FAMILY_CONFIRM, "Enter it again");
        break;
    case P_FAMILY_CONFIRM:
    case P_FAMILY_AGAIN:
        /* Entered three times in all: there is no reset if it is forgotten. */
        if (ui.pin.len != ui.pin.first_len ||
            !ct_equal(ui.pin.digits, ui.pin.first, (size_t)ui.pin.len)) {
            crypto_wipe(ui.pin.first, sizeof(ui.pin.first));
            crypto_wipe(ui.pin.digits, sizeof(ui.pin.digits));
            open_pin(P_FAMILY_NEW, "Didn't match. Choose again");
            return;
        }
        if (ui.pin.id == P_FAMILY_CONFIRM) {
            crypto_wipe(ui.pin.digits, sizeof(ui.pin.digits));
            open_pin(P_FAMILY_AGAIN, "Once more, to be sure");
            return;
        }
        store_set_simple(ui.pin.digits, ui.pin.len);
        crypto_wipe(ui.pin.first, sizeof(ui.pin.first));
        crypto_wipe(ui.pin.digits, sizeof(ui.pin.digits));
        ui.pin.len = 0;
        ui.simple_rank = 0;
        ui.hold_shown = false;
        message("Simple mode is on", "To leave: hold " GFX_DOT " for 10 s,", "then the family PIN",
                SCR_SIMPLE);
        break;
    case P_FAMILY_UNLOCK: {
        if (ui.pin.len < STORE_FAMILY_PIN_MIN) { /* too short to be it: not a try */
            snprintf(ui.pin.msg, sizeof(ui.pin.msg), "At least %d moves", STORE_FAMILY_PIN_MIN);
            ui.pin.len = 0;
            ui.dirty = true;
            return;
        }
        store_status_t st = store_check_family_pin(ui.pin.digits, ui.pin.len, now_utc());
        crypto_wipe(ui.pin.digits, sizeof(ui.pin.digits));
        ui.pin.len = 0;
        if (st == STORE_OK) {
            open_menu(M_HOME); /* simple mode stays on: waking goes back to it */
        } else {
            snprintf(ui.pin.msg, sizeof(ui.pin.msg), "Wrong family PIN");
            ui.dirty = true;
        }
        break;
    }
    }
}

/* ------------------------------------------------------------------ */
/* input                                                              */

static int list_step(int sel, int n, int d)
{
    if (n <= 0) return 0;
    return (sel + d + n) % n;
}

static void on_key(uint8_t b)
{
    bool was_dirty = ui.dirty;
    ui.dirty = true;
    switch (ui.scr) {
    case SCR_MENU:
        if (b == BTN_UP) ui.menu.sel = list_step(ui.menu.sel, ui.menu.n, -1);
        else if (b == BTN_DOWN) ui.menu.sel = list_step(ui.menu.sel, ui.menu.n, 1);
        else if (b == BTN_CENTER || b == BTN_RIGHT) menu_select();
        else if (b == BTN_LEFT) {
            if (ui.menu.id == M_HOME) ui.last_input_ms -= 3600u * 1000u; /* sleep now */
            else if (ui.menu.id == M_CODE_ACTIONS) go(SCR_CODE);
            else if (ui.menu.id == M_CONTACT_ACTIONS) go(SCR_CONTACT);
            else if (ui.menu.id == M_UPDATE) open_menu(M_SETTINGS);
            else open_menu(M_HOME);
        }
        break;

    case SCR_CODES:
    case SCR_CONTACTS: {
        rec_type_t t = ui.scr == SCR_CODES ? REC_TOTP : REC_CONTACT;
        int n = store_count(t);
        if (b == BTN_UP) ui.sel = list_step(ui.sel, n, -1);
        else if (b == BTN_DOWN) ui.sel = list_step(ui.sel, n, 1);
        else if (b == BTN_LEFT) open_menu(M_HOME);
        else if (n == 0 && b == BTN_CENTER) {
            if (t == REC_TOTP) {
                memset(&ui.draft, 0, sizeof(ui.draft));
                open_text(T_CODE_LABEL, "Name for this code", CS_LABEL, STORE_LABEL_LEN - 1,
                          NULL);
            } else {
                start_pairing();
            }
        } else if (n > 0 && (b == BTN_CENTER || b == BTN_RIGHT)) {
            ui.slot = store_nth(t, ui.sel);
            go(t == REC_TOTP ? SCR_CODE : SCR_CONTACT);
        }
        break;
    }

    case SCR_CODE:
    case SCR_CONTACT: {
        rec_type_t t = ui.scr == SCR_CODE ? REC_TOTP : REC_CONTACT;
        int n = store_count(t);
        int r = store_rank((uint16_t)ui.slot);
        if (b == BTN_UP || b == BTN_DOWN) {
            r = list_step(r, n, b == BTN_UP ? -1 : 1);
            ui.slot = store_nth(t, r);
            ui.sel = r;
        } else if (b == BTN_LEFT) {
            ui.sel = r;
            go(t == REC_TOTP ? SCR_CODES : SCR_CONTACTS);
        } else if (b == BTN_CENTER || b == BTN_RIGHT) {
            open_menu(t == REC_TOTP ? M_CODE_ACTIONS : M_CONTACT_ACTIONS);
        }
        break;
    }

    case SCR_TEXT:
        text_key(b);
        break;

    case SCR_CODE_OPTS:
        if (b == BTN_UP) ui.opt_row = (ui.opt_row + 2) % 3;
        else if (b == BTN_DOWN) ui.opt_row = (ui.opt_row + 1) % 3;
        else if (b == BTN_LEFT || b == BTN_RIGHT) {
            int d = b == BTN_RIGHT ? 1 : -1;
            if (ui.opt_row == 0) ui.draft.digits = (uint8_t)(ui.draft.digits == 6 ? 8 : 6);
            else if (ui.opt_row == 1) ui.draft.period = (uint8_t)(ui.draft.period == 30 ? 60 : 30);
            else ui.draft.alg = (uint8_t)((ui.draft.alg + 3 + d) % 3);
        } else if (b == BTN_CENTER) {
            save_draft();
        }
        break;

    case SCR_PAIR:
        if (b == BTN_LEFT) {
            stop_pairing();
            open_menu(M_HOME);
        } else if (b == BTN_CENTER && ui.pair.state == PAIR_FAILED) {
            start_pairing();
        }
        break;

    case SCR_SAS:
        if (b == BTN_CENTER) {
            open_text(T_CONTACT_NAME, "Name this contact", CS_LABEL, STORE_LABEL_LEN - 1, NULL);
        } else if (b == BTN_LEFT) {
            stop_pairing();
            message_to_menu("Pairing cancelled", "Symbols must match exactly", M_HOME);
        }
        break;

    case SCR_CONFIRM:
        if (b == BTN_CENTER) confirm_yes();
        else if (b == BTN_LEFT) confirm_no();
        break;

    case SCR_PIN: {
        bool family = ui.pin.id >= P_FAMILY_NEW;
        if (b == BTN_LEFT && ui.pin.len == 0 && ui.pin.id == P_FAMILY_UNLOCK) {
            enter_simple(); /* back, even during a wait */
            break;
        }
        if ((int32_t)(ui.pin.wait_until - hal_millis()) > 0) break;
        if (ui.pin.id == P_FAMILY_UNLOCK && store_family_wait_s(now_utc()) > 0) break;
        if (b == BTN_CENTER) {
            pin_submit();
        } else if (b == BTN_LEFT && ui.pin.len == 0 && ui.pin.id != P_UNLOCK) {
            if (family) {
                crypto_wipe(ui.pin.first, sizeof(ui.pin.first));
                crypto_wipe(ui.pin.digits, sizeof(ui.pin.digits));
            }
            open_menu(ui.pin.id == P_UPDATE ? M_UPDATE : M_SETTINGS);
        } else if (ui.pin.len < STORE_PIN_MAX) {
            uint8_t sym = b == BTN_UP ? 1 : b == BTN_RIGHT ? 2 : b == BTN_DOWN ? 3 : 4;
            ui.pin.digits[ui.pin.len++] = sym;
        }
        break;
    }

    case SCR_TIME:
        if (b == BTN_LEFT) {
            if (ui.tm.field == 0) {
                if (time_valid()) open_menu(M_SETTINGS);
            } else {
                ui.tm.field--;
            }
        } else if (b == BTN_RIGHT) {
            if (ui.tm.field < 5) ui.tm.field++;
        } else if (b == BTN_UP) {
            time_adjust(1);
        } else if (b == BTN_DOWN) {
            time_adjust(-1);
        } else if (b == BTN_CENTER) {
            int64_t local = time_from_civil(&ui.tm.c);
            hal_rtc_set(local - (int64_t)store_settings()->tz_quarters * 900);
            message_to_menu("Time saved", NULL, M_HOME);
        }
        break;

    case SCR_TZ:
        if (b == BTN_UP && ui.tz_edit < 56) ui.tz_edit++;
        else if (b == BTN_DOWN && ui.tz_edit > -48) ui.tz_edit--;
        else if (b == BTN_LEFT) open_menu(M_SETTINGS);
        else if (b == BTN_CENTER) {
            settings_t s = *store_settings();
            s.tz_quarters = ui.tz_edit;
            store_save_settings(&s);
            open_menu(M_SETTINGS);
        }
        break;

    case SCR_ABOUT:
        open_menu(M_SETTINGS);
        break;

    case SCR_UPDATE:
        if (b == BTN_LEFT) {
            update_stop();
            open_menu(M_UPDATE);
        }
        break;

    case SCR_MESSAGE:
        if (ui.msg.back_menu) open_menu(ui.msg.back_menu_id);
        else go(ui.msg.back);
        break;

    case SCR_SIMPLE: {
        /* Only up and down. Everything else does nothing, not even a
         * redraw; a long centre hold is handled in scan_buttons(). */
        int n = store_count(REC_CONTACT);
        if ((b == BTN_UP || b == BTN_DOWN) && n > 1)
            ui.simple_rank = list_step(ui.simple_rank, n, b == BTN_UP ? -1 : 1);
        else ui.dirty = was_dirty;
        break;
    }

    case SCR_SIMPLE_INTRO:
        if (b == BTN_CENTER || b == BTN_RIGHT)
            open_pin(P_FAMILY_NEW, "Choose a family PIN (6-12)");
        else if (b == BTN_LEFT) open_menu(M_SETTINGS);
        else ui.dirty = was_dirty;
        break;
    }
}

static bool repeats(uint8_t b)
{
    if (b == BTN_CENTER) return false;
    if (ui.scr == SCR_PIN || ui.scr == SCR_SIMPLE) return false;
    if (b == BTN_UP || b == BTN_DOWN) return true;
    return ui.scr == SCR_TEXT || ui.scr == SCR_TIME;
}

static void scan_buttons(uint32_t now)
{
    uint8_t held = hal_buttons();
    uint8_t pressed = (uint8_t)(held & ~ui.held);
    if (pressed) {
        /* A nav switch reports one direction at a time; take the lowest bit. */
        uint8_t b = (uint8_t)(pressed & (uint8_t)-(int8_t)pressed);
        ui.held_since = now;
        ui.next_repeat = now + REPEAT_DELAY_MS;
        ui.last_input_ms = now;
        on_key(b);
    } else if (held && held == ui.held && repeats(held) && now - ui.held_since < HOLD_LIMIT_MS &&
               (int32_t)(now - ui.next_repeat) >= 0) {
        ui.next_repeat = now + REPEAT_RATE_MS;
        ui.last_input_ms = now;
        on_key(held);
    }
    ui.held = held;

    /* Simple mode's only way out: hold the centre for 10 s. */
    if (ui.scr == SCR_SIMPLE) {
        uint32_t t = held == BTN_CENTER ? now - ui.held_since : 0;
        bool hint_now = t >= FAMILY_HOLD_HINT_MS;
        if (t >= FAMILY_HOLD_MS) {
            ui.hold_shown = false;
            open_pin(P_FAMILY_UNLOCK, "Enter the family PIN");
        } else if (hint_now != ui.hold_shown) {
            ui.hold_shown = hint_now;
            ui.dirty = true;
        }
    }
}

/* ------------------------------------------------------------------ */
/* rendering and main loop                                            */

/* Changes when what a screen shows changes with time. A redraw for it may
 * be a full refresh (after FULL_REFRESH_EVERY partial ones). */
static uint32_t current_view_key(void)
{
    int64_t t = now_utc();
    switch (ui.scr) {
    case SCR_CODE:                       /* the code (and the clock) */
        return (uint32_t)(t / (ui.code_period ? ui.code_period : 30));
    case SCR_CODES:                      /* 30 and 60 s codes both change on 30 s steps */
        return (uint32_t)(t / 30);
    case SCR_CONTACT:                    /* the words */
        return (uint32_t)(t / WORDS_PERIOD_S);
    case SCR_PIN:
        if (ui.pin.id == P_FAMILY_UNLOCK) { /* as drawn: minutes, then 10 s steps */
            uint32_t w = store_family_wait_s(t);
            return w >= 60 ? 1000u + (w + 59) / 60 : (w + 9) / 10;
        }
        return (uint32_t)(hal_millis() / 1000);
    case SCR_SIMPLE: /* the words; no clock on this screen */
        return simple_words_shown() ? (uint32_t)(t / WORDS_PERIOD_S) : 0;
    case SCR_SIMPLE_INTRO:
        return 0;
    default:
        return (uint32_t)(now_local() / 60); /* the clock in the title bar */
    }
}

/* Changes when only a countdown bar moves: always a partial refresh, and
 * not counted towards the next full one (the bar is a few hundred pixels;
 * its ghosting clears at the next content change). Before this, every 5 s
 * bar step counted, so a code or contact left on screen got a 3 s full
 * refresh every 30 s: most of a use's charge (docs/power-calculations.md). */
static uint32_t current_tick_key(void)
{
    int64_t t = now_utc();
    if (ui.scr == SCR_CONTACT || (ui.scr == SCR_SIMPLE && simple_words_shown()))
        return (uint32_t)(t / (WORDS_PERIOD_S / BAR_STEPS));
    if (ui.scr == SCR_CODE) return (uint32_t)(t / ((ui.code_period ? ui.code_period : 30) / BAR_STEPS));
    return 0;
}

static void render(bool tick_only)
{
    gfx_clear();
    switch (ui.scr) {
    case SCR_MENU: draw_menu(); break;
    case SCR_CODES: draw_codes(); break;
    case SCR_CODE: draw_code(); break;
    case SCR_CONTACTS: draw_contacts(); break;
    case SCR_CONTACT: draw_contact(); break;
    case SCR_PAIR: draw_pair(); break;
    case SCR_SAS: draw_sas(); break;
    case SCR_TEXT: draw_text(); break;
    case SCR_CODE_OPTS: draw_code_opts(); break;
    case SCR_CONFIRM: draw_confirm(); break;
    case SCR_TIME: draw_time(); break;
    case SCR_TZ: draw_tz(); break;
    case SCR_PIN: draw_pin(); break;
    case SCR_MESSAGE: draw_message(); break;
    case SCR_ABOUT: draw_about(); break;
    case SCR_SIMPLE: draw_simple(); break;
    case SCR_SIMPLE_INTRO: draw_simple_intro(); break;
    case SCR_UPDATE: draw_update(); break;
    }
    /* Keys as of what was just drawn (drawing the code screen sets
     * code_period), so a redraw is not followed by a spurious one. */
    ui.view_key = current_view_key();
    ui.tick_key = current_tick_key();
    display_mode_t mode = DISPLAY_FAST;
    if (ui.force_full || (!tick_only && ui.fast_count >= FULL_REFRESH_EVERY)) {
        mode = DISPLAY_FULL;
        ui.fast_count = 0;
        ui.force_full = false;
    } else if (!tick_only) {
        ui.fast_count++;
    }
    hal_display_show(gfx_fb, mode);
}

static void enter_first_screen(void)
{
    if (!store_unlocked()) open_pin(P_UNLOCK, "Enter your PIN");
    else if (simple_on()) enter_simple();
    else if (!time_valid()) enter_time_editor();
    else open_menu(M_HOME);
}

void app_init(void)
{
    memset(&ui, 0, sizeof(ui));
    store_mount();
    ui.held = hal_buttons(); /* the press that woke us is not a key */
    ui.last_input_ms = hal_millis();
    ui.held_since = ui.last_input_ms;
    ui.force_full = true;
    enter_first_screen();
    update_show_result();
    ui.view_key = current_view_key();
}

void app_wake(void)
{
    ui.held = hal_buttons();
    ui.last_input_ms = hal_millis();
    ui.held_since = ui.last_input_ms;
    /* The sleep screen went up with a full refresh, so the panel is clean:
     * the first screen can be a partial one (0.42 s, not 3 s). The STM32
     * driver still does a full one if it lost track of the panel. */
    ui.force_full = false;
    ui.sel = 0;
    list_top = 0;
    menu_top = 0;
    enter_first_screen();
    ui.view_key = current_view_key();
}

int64_t app_wake_at(void)
{
    return 0; /* only the joystick wakes us */
}

void app_ir_rx(const uint8_t *data, size_t len)
{
    /* Carrier sense for the pairing transmitter: any byte while our own
     * frame is not in the air (that would be our echo) means the other
     * device is talking. */
    uint32_t now = hal_millis();
    if (len && (int32_t)(now - ui.pair_tx_end) > 0) ui.pair_rx_ms = now;
    if (ui.scr == SCR_UPDATE && !ui.upd.receiving) {
        fwtx_rx(&ui.upd.tx, data, len, now);
        return;
    }
    if (ui.scr != SCR_PAIR && ui.scr != SCR_SAS) return;
    if (pairing_rx(&ui.pair, data, len)) {
        if (ui.pair.state == PAIR_REVEALING) ui.pair_found_ms = hal_millis();
        if (ui.pair.state == PAIR_DONE && ui.scr == SCR_PAIR) go(SCR_SAS);
        ui.dirty = true;
    }
}

uint32_t app_poll(void)
{
    uint32_t now = hal_millis();
    scan_buttons(now);

    if (ui.scr == SCR_PAIR && ui.pair.state == PAIR_REVEALING &&
        now - ui.pair_found_ms >= PAIR_REVEAL_TIMEOUT_MS) {
        pairing_abort(&ui.pair);
        ui.dirty = true;
    }

    /* Pairing keeps retransmitting until the person confirms the SAS. */
    if ((ui.scr == SCR_PAIR || ui.scr == SCR_SAS || ui.scr == SCR_TEXT) &&
        ui.pair.state >= PAIR_SEARCHING && ui.pair.state <= PAIR_DONE &&
        (int32_t)(now - ui.pair_next_tx) >= 0) {
        uint8_t jitter;
        hal_random(&jitter, 1);
        if ((int32_t)(now - ui.pair_rx_ms) < PAIR_QUIET_MS) {
            /* The other device is mid-frame: back off and listen again. */
            ui.pair_next_tx = now + 4u + (jitter & 0x1f);
        } else {
            uint8_t frame[PAIR_FRAME_MAX];
            size_t n = pairing_tx(&ui.pair, frame, sizeof(frame), ui.pair_seq++);
            if (n) hal_ir_send(frame, n);
            ui.pair_tx_end = now + (uint32_t)(n * 10u * 1000u / PAIR_BAUD) + 2u;
            ui.pair_next_tx = now + PAIR_TX_MS + (jitter & 0x7f);
        }
        /* Don't fall asleep mid-pairing, but not for ever either. */
        if ((int32_t)(now - ui.pair_started_ms) < PAIR_AWAKE_MS) ui.last_input_ms = now;
    }
    if (ui.scr == SCR_PAIR && (ui.pair.state == PAIR_SEARCHING || ui.pair.state == PAIR_REVEALING) &&
        (int32_t)(now - ui.pair_started_ms) >= PAIR_TIMEOUT_MS) {
        /* Signed: start_pairing() ran inside this poll, after `now` was
         * read, and key generation takes a while on the device. */
        stop_pairing();
        message_to_menu("No other device found", "Hold them top edge to top edge", M_HOME);
    }

    if (ui.scr == SCR_UPDATE && !ui.upd.receiving) update_poll(now);

    if (current_view_key() != ui.view_key) ui.dirty = true;
    if (ui.dirty) {
        ui.dirty = false;
        render(false);
    } else if (current_tick_key() != ui.tick_key) {
        render(true);
    }

    uint32_t idle_limit = (store_settings()->sleep_s ? store_settings()->sleep_s
                                                       : IDLE_SLEEP_S_DEFAULT) * 1000u;
    bool stuck = ui.held != 0 && now - ui.held_since >= HOLD_LIMIT_MS;
    if ((ui.held == 0 || stuck) && now - ui.last_input_ms >= idle_limit) {
        if (ui.scr == SCR_PAIR || ui.scr == SCR_SAS || ui.scr == SCR_TEXT) stop_pairing();
        if (ui.scr == SCR_UPDATE) update_stop();
        crypto_wipe(&ui.draft, sizeof(ui.draft));
        crypto_wipe(ui.text.buf, sizeof(ui.text.buf));
        crypto_wipe(&ui.pin, sizeof(ui.pin)); /* a PIN (or family PIN) half entered */
        store_lock();
        draw_sleep();
        /* Always full: it wipes every trace of words and codes off the
         * glass for as long as the device sleeps. */
        hal_display_show(gfx_fb, DISPLAY_FULL);
        ui.fast_count = 0;
        return APP_SLEEP;
    }
    if (ui.held && !stuck) return 20;
    if (ui.pair.state >= PAIR_SEARCHING && ui.pair.state <= PAIR_DONE) return 20;
    if (ui.scr == SCR_UPDATE) return 10;
    return 100;
}

const char *app_screen_name(void)
{
    return SCREEN_NAMES[ui.scr];
}
