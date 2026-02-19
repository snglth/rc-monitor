/*
 * rc_emulator.c - Interactive DJI RC Controller Emulator
 *
 * Exercises the full rc-monitor parsing pipeline using keyboard/mouse input.
 * Maps virtual controller state -> 17-byte payload -> DUML frame ->
 * rcm_feed() -> callback, then displays the parsed rc_state_t in a
 * terminal UI.
 *
 * Build: mkdir build && cd build && cmake .. && make
 * Run:   ./rc_emulator [-o recording.bin]
 */

#include <ncurses.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include "rc_monitor.h"

/* --- Constants --- */

#define STICK_MAX   660
#define STICK_STEP  66
#define WHEEL_MAX   660
#define WHEEL_STEP  33
#define DELTA_STEP  5
#define TICK_MS     50

/* UI layout rows */
#define ROW_TITLE    0
#define ROW_STICK    3       /* top border of stick boxes */
#define COL_LSTICK   2
#define COL_RSTICK   29
#define STICK_BOX_H  7      /* including top/bottom borders */
#define ROW_SVAL     (ROW_STICK + STICK_BOX_H)      /* 10 */
#define ROW_BUTTONS  (ROW_SVAL + 2)                  /* 12 */
#define ROW_5D       (ROW_BUTTONS + 1)               /* 13 */
#define ROW_MODE     (ROW_5D + 1)                    /* 14 */
#define ROW_WHEELS   (ROW_MODE + 2)                  /* 16 */
#define ROW_HELP1    (ROW_WHEELS + 2)                /* 18 */
#define ROW_HELP2    (ROW_HELP1 + 1)                 /* 19 */
#define ROW_HELP3    (ROW_HELP2 + 1)                 /* 20 */

/* --- Types --- */

enum drag_target { DRAG_NONE, DRAG_LEFT_STICK, DRAG_RIGHT_STICK };

typedef struct {
    /* Analog sticks (-STICK_MAX..+STICK_MAX) */
    int stick_left_h, stick_left_v;
    int stick_right_h, stick_right_v;

    /* Wheels (-WHEEL_MAX..+WHEEL_MAX), delta is momentary */
    int left_wheel, right_wheel;
    int right_wheel_delta;

    /* Buttons (momentary, cleared each tick) */
    bool pause, gohome, shutter, record;
    bool custom1, custom2, custom3;

    /* 5D joystick (momentary) */
    bool five_d_up, five_d_down, five_d_left, five_d_right, five_d_center;

    /* Flight mode (latching) */
    rc_flight_mode_t flight_mode;

    /* Mouse drag state */
    enum drag_target drag;
} emu_state_t;

/* --- Globals --- */

static rc_state_t g_parsed;
static uint32_t   g_seq;
static FILE      *g_rec_fp;

/* --- Helpers --- */

static inline int clamp(int v, int lo, int hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

static inline void put_u16_le(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

/* --- Payload builder (inverse of rcm_parse_payload) --- */

static void build_payload(const emu_state_t *e,
                           uint8_t out[RC_PUSH_PAYLOAD_LEN]) {
    memset(out, 0, RC_PUSH_PAYLOAD_LEN);

    /* Byte 0: pause(4), gohome(5), shutter(6) */
    out[0] = (uint8_t)((e->pause << 4) |
                        (e->gohome << 5) |
                        (e->shutter << 6));

    /* Byte 1: record(0), 5D right(3), up(4), down(5), left(6), center(7) */
    out[1] = (uint8_t)((e->record << 0) |
                        (e->five_d_right << 3) |
                        (e->five_d_up << 4) |
                        (e->five_d_down << 5) |
                        (e->five_d_left << 6) |
                        (e->five_d_center << 7));

    /* Byte 2: flight_mode(1:0), custom1(2), custom2(3), custom3(4) */
    out[2] = (uint8_t)((e->flight_mode & 0x03) |
                        (e->custom1 << 2) |
                        (e->custom2 << 3) |
                        (e->custom3 << 4));

    /* Byte 4: right wheel delta — 5-bit magnitude(5:1) + sign(6) */
    int d = clamp(e->right_wheel_delta, -31, 31);
    if (d > 0)
        out[4] = (uint8_t)((d << 1) | (1 << 6));
    else if (d < 0)
        out[4] = (uint8_t)((-d) << 1);

    /* Bytes 5-16: analog values as uint16 LE, offset by +0x0400 */
    put_u16_le(out + 5,  (uint16_t)(e->stick_right_h + 0x0400));
    put_u16_le(out + 7,  (uint16_t)(e->stick_right_v + 0x0400));
    put_u16_le(out + 9,  (uint16_t)(e->stick_left_v  + 0x0400));
    put_u16_le(out + 11, (uint16_t)(e->stick_left_h  + 0x0400));
    put_u16_le(out + 13, (uint16_t)(e->left_wheel    + 0x0400));
    put_u16_le(out + 15, (uint16_t)(e->right_wheel   + 0x0400));
}

/* --- Parser callback --- */

static void emulator_cb(const rc_state_t *state, void *ud) {
    (void)ud;
    g_parsed = *state;
}

/* --- Input handling --- */

static void handle_key(emu_state_t *e, int ch) {
    switch (ch) {
    /* Left stick (WASD) */
    case 'w': e->stick_left_v  = clamp(e->stick_left_v  + STICK_STEP,
                                        -STICK_MAX, STICK_MAX); break;
    case 's': e->stick_left_v  = clamp(e->stick_left_v  - STICK_STEP,
                                        -STICK_MAX, STICK_MAX); break;
    case 'a': e->stick_left_h  = clamp(e->stick_left_h  - STICK_STEP,
                                        -STICK_MAX, STICK_MAX); break;
    case 'd': e->stick_left_h  = clamp(e->stick_left_h  + STICK_STEP,
                                        -STICK_MAX, STICK_MAX); break;

    /* Right stick (arrows) */
    case KEY_UP:    e->stick_right_v = clamp(e->stick_right_v + STICK_STEP,
                                              -STICK_MAX, STICK_MAX); break;
    case KEY_DOWN:  e->stick_right_v = clamp(e->stick_right_v - STICK_STEP,
                                              -STICK_MAX, STICK_MAX); break;
    case KEY_LEFT:  e->stick_right_h = clamp(e->stick_right_h - STICK_STEP,
                                              -STICK_MAX, STICK_MAX); break;
    case KEY_RIGHT: e->stick_right_h = clamp(e->stick_right_h + STICK_STEP,
                                              -STICK_MAX, STICK_MAX); break;

    /* Buttons (momentary) */
    case 'p': e->pause   = true; break;
    case 'h': e->gohome  = true; break;
    case 'z': e->shutter = true; break;
    case 'x': e->record  = true; break;
    case '1': e->custom1 = true; break;
    case '2': e->custom2 = true; break;
    case '3': e->custom3 = true; break;

    /* 5D joystick (momentary) */
    case 'i': e->five_d_up     = true; break;
    case 'k': e->five_d_down   = true; break;
    case 'j': e->five_d_left   = true; break;
    case 'l': e->five_d_right  = true; break;
    case 'o': e->five_d_center = true; break;

    /* Flight mode (latching) */
    case '[':  e->flight_mode = RC_MODE_SPORT;  break;
    case ']':  e->flight_mode = RC_MODE_NORMAL; break;
    case '\\': e->flight_mode = RC_MODE_TRIPOD; break;

    /* Left wheel (holds position) */
    case '-': e->left_wheel = clamp(e->left_wheel - WHEEL_STEP,
                                     -WHEEL_MAX, WHEEL_MAX); break;
    case '=': e->left_wheel = clamp(e->left_wheel + WHEEL_STEP,
                                     -WHEEL_MAX, WHEEL_MAX); break;

    /* Right wheel (holds position) */
    case '9': e->right_wheel = clamp(e->right_wheel - WHEEL_STEP,
                                      -WHEEL_MAX, WHEEL_MAX); break;
    case '0': e->right_wheel = clamp(e->right_wheel + WHEEL_STEP,
                                      -WHEEL_MAX, WHEEL_MAX); break;

    /* Right wheel delta (momentary per-tick) */
    case ',': e->right_wheel_delta = -DELTA_STEP; break;
    case '.': e->right_wheel_delta =  DELTA_STEP; break;

    /* Reset all */
    case 'r':
        memset(e, 0, sizeof(*e));
        e->flight_mode = RC_MODE_NORMAL;
        break;
    }
}

/* Map mouse position within a stick box to stick deflection */
static void map_mouse_to_stick(int mx, int my, int box_col,
                                int *h, int *v) {
    int ci = clamp(mx - (box_col + 1), 0, 10);
    int ri = clamp(my - (ROW_STICK + 1), 0, 4);
    *h = ((ci - 5) * STICK_MAX) / 5;
    *v = ((2 - ri) * STICK_MAX) / 2;
}

static void handle_mouse(emu_state_t *e, MEVENT *mev) {
    int my = mev->y, mx = mev->x;

    /* Inner bounds for left/right stick boxes */
    int sr0 = ROW_STICK + 1, sr1 = ROW_STICK + 5;
    int lc0 = COL_LSTICK + 1, lc1 = COL_LSTICK + 11;
    int rc0 = COL_RSTICK + 1, rc1 = COL_RSTICK + 11;

    /* Button press — start drag or activate control */
    if (mev->bstate & BUTTON1_PRESSED) {
        if (my >= sr0 && my <= sr1 && mx >= lc0 && mx <= lc1) {
            e->drag = DRAG_LEFT_STICK;
            map_mouse_to_stick(mx, my, COL_LSTICK,
                               &e->stick_left_h, &e->stick_left_v);
            return;
        }
        if (my >= sr0 && my <= sr1 && mx >= rc0 && mx <= rc1) {
            e->drag = DRAG_RIGHT_STICK;
            map_mouse_to_stick(mx, my, COL_RSTICK,
                               &e->stick_right_h, &e->stick_right_v);
            return;
        }
        /* Button label clicks */
        if (my == ROW_BUTTONS) {
            if      (mx >= 12 && mx <= 18) e->pause   = true;
            else if (mx >= 20 && mx <= 25) e->gohome  = true;
            else if (mx >= 27 && mx <= 32) e->shutter = true;
            else if (mx >= 34 && mx <= 38) e->record  = true;
            else if (mx >= 40 && mx <= 43) e->custom1 = true;
            else if (mx >= 45 && mx <= 48) e->custom2 = true;
            else if (mx >= 50 && mx <= 53) e->custom3 = true;
        }
        /* 5D clicks */
        if (my == ROW_5D) {
            if      (mx >= 12 && mx <= 14) e->five_d_up     = true;
            else if (mx >= 16 && mx <= 18) e->five_d_down   = true;
            else if (mx >= 20 && mx <= 22) e->five_d_left   = true;
            else if (mx >= 24 && mx <= 26) e->five_d_right  = true;
            else if (mx >= 28 && mx <= 32) e->five_d_center = true;
        }
        /* Mode clicks */
        if (my == ROW_MODE) {
            if      (mx >= 12 && mx <= 16) e->flight_mode = RC_MODE_SPORT;
            else if (mx >= 20 && mx <= 25) e->flight_mode = RC_MODE_NORMAL;
            else if (mx >= 29 && mx <= 34) e->flight_mode = RC_MODE_TRIPOD;
        }
        return;
    }

    /* Button release — stop drag */
    if (mev->bstate & BUTTON1_RELEASED) {
        e->drag = DRAG_NONE;
        return;
    }

    /* Motion while dragging */
    if ((mev->bstate & REPORT_MOUSE_POSITION) && e->drag != DRAG_NONE) {
        if (e->drag == DRAG_LEFT_STICK)
            map_mouse_to_stick(mx, my, COL_LSTICK,
                               &e->stick_left_h, &e->stick_left_v);
        else
            map_mouse_to_stick(mx, my, COL_RSTICK,
                               &e->stick_right_h, &e->stick_right_v);
        return;
    }

    /* Scroll wheel -> left wheel adjust */
    if (mev->bstate & BUTTON4_PRESSED)
        e->left_wheel = clamp(e->left_wheel + WHEEL_STEP,
                               -WHEEL_MAX, WHEEL_MAX);
#ifdef BUTTON5_PRESSED
    if (mev->bstate & BUTTON5_PRESSED)
        e->left_wheel = clamp(e->left_wheel - WHEEL_STEP,
                               -WHEEL_MAX, WHEEL_MAX);
#endif
}

/* Decay sticks toward center and clear momentary inputs */
static void decay_sticks(emu_state_t *e) {
    if (e->drag != DRAG_LEFT_STICK) {
        e->stick_left_h /= 2;
        e->stick_left_v /= 2;
    }
    if (e->drag != DRAG_RIGHT_STICK) {
        e->stick_right_h /= 2;
        e->stick_right_v /= 2;
    }

    /* Clear momentary buttons */
    e->pause = e->gohome = e->shutter = e->record = false;
    e->custom1 = e->custom2 = e->custom3 = false;
    e->five_d_up = e->five_d_down = e->five_d_left = false;
    e->five_d_right = e->five_d_center = false;
    e->right_wheel_delta = 0;
    /* Wheels hold position; flight mode latches */
}

/* --- Drawing --- */

static void draw_stick(int row, int col, int h, int v, const char *label) {
    /* Label above box */
    mvprintw(row - 1, col, "%s", label);

    /* Top border */
    mvaddch(row, col, '+');
    for (int i = 1; i <= 11; i++) mvaddch(row, col + i, '-');
    mvaddch(row, col + 12, '+');

    /* Inner rows */
    for (int r = 1; r <= 5; r++) {
        mvaddch(row + r, col, '|');
        for (int c = 1; c <= 11; c++) mvaddch(row + r, col + c, ' ');
        mvaddch(row + r, col + 12, '|');
    }

    /* Bottom border */
    mvaddch(row + 6, col, '+');
    for (int i = 1; i <= 11; i++) mvaddch(row + 6, col + i, '-');
    mvaddch(row + 6, col + 12, '+');

    /* Center crosshair */
    mvaddch(row + 3, col + 6, '+');

    /* Position marker */
    int cx = clamp(5 + (h * 5) / STICK_MAX, 0, 10);
    int cy = clamp(2 - (v * 2) / STICK_MAX, 0, 4);
    attron(A_BOLD);
    mvaddch(row + 1 + cy, col + 1 + cx, 'X');
    attroff(A_BOLD);
}

/* Draw a label with reverse video when active */
static void draw_btn(int row, int col, const char *lbl, bool active) {
    if (active) attron(A_REVERSE);
    mvprintw(row, col, "%s", lbl);
    if (active) attroff(A_REVERSE);
}

static void draw_ui(const emu_state_t *e, const rc_state_t *p) {
    erase();

    /* Title bar */
    attron(A_BOLD);
    mvprintw(ROW_TITLE, 1, "DJI RC Emulator");
    attroff(A_BOLD);
    mvprintw(ROW_TITLE, 45, "20 Hz | Seq: %u", g_seq);
    if (g_rec_fp)
        mvprintw(ROW_TITLE, 65, "[REC]");

    /* Stick boxes */
    draw_stick(ROW_STICK, COL_LSTICK,
               e->stick_left_h, e->stick_left_v,
               "LEFT STICK (WASD)");
    draw_stick(ROW_STICK, COL_RSTICK,
               e->stick_right_h, e->stick_right_v,
               "RIGHT STICK (Arrows)");

    /* Parsed stick values */
    mvprintw(ROW_SVAL, COL_LSTICK,
             " H: %+4d  V: %+4d",
             p->stick_left.horizontal, p->stick_left.vertical);
    mvprintw(ROW_SVAL, COL_RSTICK,
             " H: %+4d  V: %+4d",
             p->stick_right.horizontal, p->stick_right.vertical);

    /* Buttons */
    mvprintw(ROW_BUTTONS, 2, "BUTTONS:");
    draw_btn(ROW_BUTTONS, 12, "[PAUSE]", p->pause);
    draw_btn(ROW_BUTTONS, 20, "[HOME]",  p->gohome);
    draw_btn(ROW_BUTTONS, 27, "[SHUT]",  p->shutter);
    draw_btn(ROW_BUTTONS, 34, "[REC]",   p->record);
    draw_btn(ROW_BUTTONS, 40, "[C1]",    p->custom1);
    draw_btn(ROW_BUTTONS, 45, "[C2]",    p->custom2);
    draw_btn(ROW_BUTTONS, 50, "[C3]",    p->custom3);

    /* 5D joystick */
    mvprintw(ROW_5D, 2, "5D:");
    draw_btn(ROW_5D, 12, "[U]",   p->five_d.up);
    draw_btn(ROW_5D, 16, "[D]",   p->five_d.down);
    draw_btn(ROW_5D, 20, "[L]",   p->five_d.left);
    draw_btn(ROW_5D, 24, "[R]",   p->five_d.right);
    draw_btn(ROW_5D, 28, "[CTR]", p->five_d.center);

    /* Flight mode */
    mvprintw(ROW_MODE, 2, "MODE:");
    draw_btn(ROW_MODE, 12, "Sport",  p->flight_mode == RC_MODE_SPORT);
    draw_btn(ROW_MODE, 20, "Normal", p->flight_mode == RC_MODE_NORMAL);
    draw_btn(ROW_MODE, 29, "Tripod", p->flight_mode == RC_MODE_TRIPOD);

    /* Wheels */
    mvprintw(ROW_WHEELS, 2,
             "WHEELS:   Left: %+4d   Right: %+4d   Delta: %+2d",
             p->left_wheel, p->right_wheel, p->right_wheel_delta);

    /* Help text */
    attron(A_DIM);
    mvprintw(ROW_HELP1, 2,
             "WASD=L.Stick  Arrows=R.Stick  p=Pause h=Home z=Shut x=Rec");
    mvprintw(ROW_HELP2, 2,
             "1/2/3=Custom  ijklo=5D  [/]/\\=Mode  -/==L.Whl  9/0=R.Whl");
    mvprintw(ROW_HELP3, 2,
             ",/.=R.Whl.Delta  r=Reset  q=Quit  Mouse: drag sticks, click btns");
    attroff(A_DIM);

    refresh();
}

/* --- Main --- */

int main(int argc, char *argv[]) {
    const char *rec_path = NULL;
    int opt;
    while ((opt = getopt(argc, argv, "o:")) != -1) {
        if (opt == 'o')
            rec_path = optarg;
        else {
            fprintf(stderr, "Usage: %s [-o recording.bin]\n", argv[0]);
            return 1;
        }
    }

    if (rec_path) {
        g_rec_fp = fopen(rec_path, "wb");
        if (!g_rec_fp) {
            perror("fopen");
            return 1;
        }
    }

    rcm_parser_t *parser = rcm_create(emulator_cb, NULL);
    if (!parser) {
        fprintf(stderr, "rcm_create failed\n");
        if (g_rec_fp) fclose(g_rec_fp);
        return 1;
    }

    /* ncurses initialization */
    initscr();
    cbreak();
    noecho();
    nodelay(stdscr, TRUE);
    keypad(stdscr, TRUE);
    curs_set(0);

    /* Mouse: enable all events + motion tracking */
    mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, NULL);
    mouseinterval(0);

    /* Enable SGR 1003 mode for motion reports while button held */
    printf("\033[?1003h");
    fflush(stdout);

    /* Initial emulator state */
    emu_state_t emu;
    memset(&emu, 0, sizeof(emu));
    emu.flight_mode = RC_MODE_NORMAL;

    /* 20 Hz main loop */
    while (1) {
        int ch;
        while ((ch = getch()) != ERR) {
            if (ch == 'q')
                goto done;
            if (ch == KEY_MOUSE) {
                MEVENT mev;
                if (getmouse(&mev) == OK)
                    handle_mouse(&emu, &mev);
            } else {
                handle_key(&emu, ch);
            }
        }

        /* Build payload -> DUML frame -> parser pipeline */
        uint8_t payload[RC_PUSH_PAYLOAD_LEN];
        build_payload(&emu, payload);

        uint8_t frame[64];
        int flen = rcm_build_packet(frame, sizeof(frame),
                                     DUML_DEV_RC, 0,
                                     DUML_DEV_APP, 0,
                                     (uint16_t)(g_seq & 0xFFFF),
                                     DUML_PACK_REQUEST,
                                     DUML_ACK_NO_ACK, 0,
                                     DUML_CMD_SET_RC,
                                     DUML_CMD_RC_PUSH,
                                     payload, RC_PUSH_PAYLOAD_LEN);

        if (flen > 0) {
            rcm_feed(parser, frame, (size_t)flen);
            if (g_rec_fp)
                fwrite(frame, 1, (size_t)flen, g_rec_fp);
        }

        g_seq++;

        draw_ui(&emu, &g_parsed);
        decay_sticks(&emu);
        napms(TICK_MS);
    }

done:
    /* Disable mouse motion tracking and restore terminal */
    printf("\033[?1003l");
    fflush(stdout);
    endwin();

    rcm_destroy(parser);
    if (g_rec_fp)
        fclose(g_rec_fp);

    return 0;
}
