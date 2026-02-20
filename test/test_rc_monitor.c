/*
 * test_rc_monitor.c - Unit tests for the RC monitor payload parser
 *
 * Build and run:
 *   cd rc-monitor && mkdir build && cd build
 *   cmake .. && make
 *   ./test_rc_monitor
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "rc_monitor.h"

#define TEST(name) static void name(void)
#define RUN(name) do { printf("  %-40s", #name); name(); printf("OK\n"); } while(0)

/* ---- Payload parser tests ---- */

TEST(test_all_zeros) {
    uint8_t payload[17] = {0};
    rc_state_t s;
    assert(rcm_parse_payload(payload, 17, &s) == 0);

    /* All buttons off */
    assert(!s.pause && !s.gohome && !s.shutter && !s.record);
    assert(!s.custom1 && !s.custom2 && !s.custom3);
    assert(!s.five_d.up && !s.five_d.down && !s.five_d.left &&
           !s.five_d.right && !s.five_d.center);

    /* Flight mode 0 = Sport */
    assert(s.flight_mode == RC_MODE_SPORT);

    /* Sticks at 0x0000 => -0x400 = -1024 */
    assert(s.stick_right.horizontal == -1024);
    assert(s.stick_right.vertical == -1024);
    assert(s.stick_left.vertical == -1024);
    assert(s.stick_left.horizontal == -1024);
    assert(s.left_wheel == -1024);
    assert(s.right_wheel == -1024);
    assert(s.right_wheel_delta == 0);
}

TEST(test_sticks_centered) {
    /* Sticks at center value: 0x0400 = 1024 */
    uint8_t payload[17] = {0};
    /* Right H at bytes 5-6 */
    payload[5] = 0x00; payload[6] = 0x04;
    /* Right V at bytes 7-8 */
    payload[7] = 0x00; payload[8] = 0x04;
    /* Left V at bytes 9-10 */
    payload[9] = 0x00; payload[10] = 0x04;
    /* Left H at bytes 11-12 */
    payload[11] = 0x00; payload[12] = 0x04;
    /* Left wheel at bytes 13-14 */
    payload[13] = 0x00; payload[14] = 0x04;
    /* Right wheel at bytes 15-16 */
    payload[15] = 0x00; payload[16] = 0x04;

    rc_state_t s;
    assert(rcm_parse_payload(payload, 17, &s) == 0);
    assert(s.stick_right.horizontal == 0);
    assert(s.stick_right.vertical == 0);
    assert(s.stick_left.vertical == 0);
    assert(s.stick_left.horizontal == 0);
    assert(s.left_wheel == 0);
    assert(s.right_wheel == 0);
}

TEST(test_sticks_full_deflection) {
    uint8_t payload[17] = {0};
    /* Max value: 0x0694 = 1684 => centered: 1684 - 1024 = 660 */
    payload[5] = 0x94; payload[6] = 0x06;   /* right H = +660 */
    /* Min value: 0x016C = 364 => centered: 364 - 1024 = -660 */
    payload[7] = 0x6C; payload[8] = 0x01;   /* right V = -660 */
    /* Center values for the rest */
    payload[9] = 0x00; payload[10] = 0x04;
    payload[11] = 0x00; payload[12] = 0x04;
    payload[13] = 0x00; payload[14] = 0x04;
    payload[15] = 0x00; payload[16] = 0x04;

    rc_state_t s;
    assert(rcm_parse_payload(payload, 17, &s) == 0);
    assert(s.stick_right.horizontal == 660);
    assert(s.stick_right.vertical == -660);
}

TEST(test_buttons_individual) {
    rc_state_t s;
    uint8_t payload[17] = {0};
    /* Center sticks to avoid noise */
    for (int i = 5; i < 17; i += 2) { payload[i] = 0x00; payload[i+1] = 0x04; }

    /* Pause: byte0 bit4 */
    payload[0] = 0x10;
    assert(rcm_parse_payload(payload, 17, &s) == 0);
    assert(s.pause && !s.gohome && !s.shutter);
    payload[0] = 0;

    /* GoHome: byte0 bit5 */
    payload[0] = 0x20;
    assert(rcm_parse_payload(payload, 17, &s) == 0);
    assert(!s.pause && s.gohome && !s.shutter);
    payload[0] = 0;

    /* Shutter: byte0 bit6 */
    payload[0] = 0x40;
    assert(rcm_parse_payload(payload, 17, &s) == 0);
    assert(s.shutter && !s.record);
    payload[0] = 0;

    /* Record: byte1 bit0 */
    payload[1] = 0x01;
    assert(rcm_parse_payload(payload, 17, &s) == 0);
    assert(s.record && !s.shutter);
    payload[1] = 0;

    /* Custom1: byte2 bit2 */
    payload[2] = 0x04;
    assert(rcm_parse_payload(payload, 17, &s) == 0);
    assert(s.custom1 && !s.custom2 && !s.custom3);
    payload[2] = 0;

    /* Custom2: byte2 bit3 */
    payload[2] = 0x08;
    assert(rcm_parse_payload(payload, 17, &s) == 0);
    assert(!s.custom1 && s.custom2 && !s.custom3);
    payload[2] = 0;

    /* Custom3: byte2 bit4 */
    payload[2] = 0x10;
    assert(rcm_parse_payload(payload, 17, &s) == 0);
    assert(!s.custom1 && !s.custom2 && s.custom3);
    payload[2] = 0;
}

TEST(test_five_d_joystick) {
    rc_state_t s;
    uint8_t payload[17] = {0};
    for (int i = 5; i < 17; i += 2) { payload[i] = 0x00; payload[i+1] = 0x04; }

    /* 5D Right: byte1 bit3 */
    payload[1] = 0x08;
    assert(rcm_parse_payload(payload, 17, &s) == 0);
    assert(s.five_d.right && !s.five_d.up && !s.five_d.down &&
           !s.five_d.left && !s.five_d.center);

    /* 5D Up: byte1 bit4 */
    payload[1] = 0x10;
    assert(rcm_parse_payload(payload, 17, &s) == 0);
    assert(s.five_d.up && !s.five_d.right);

    /* 5D Down: byte1 bit5 */
    payload[1] = 0x20;
    assert(rcm_parse_payload(payload, 17, &s) == 0);
    assert(s.five_d.down);

    /* 5D Left: byte1 bit6 */
    payload[1] = 0x40;
    assert(rcm_parse_payload(payload, 17, &s) == 0);
    assert(s.five_d.left);

    /* 5D Center: byte1 bit7 */
    payload[1] = 0x80;
    assert(rcm_parse_payload(payload, 17, &s) == 0);
    assert(s.five_d.center);

    /* All 5D pressed */
    payload[1] = 0xF8;
    assert(rcm_parse_payload(payload, 17, &s) == 0);
    assert(s.five_d.up && s.five_d.down && s.five_d.left &&
           s.five_d.right && s.five_d.center);
}

TEST(test_flight_mode_switch) {
    rc_state_t s;
    uint8_t payload[17] = {0};
    for (int i = 5; i < 17; i += 2) { payload[i] = 0x00; payload[i+1] = 0x04; }

    /* Mode 0 = Sport */
    payload[2] = 0x00;
    assert(rcm_parse_payload(payload, 17, &s) == 0);
    assert(s.flight_mode == RC_MODE_SPORT);

    /* Mode 1 = Normal */
    payload[2] = 0x01;
    assert(rcm_parse_payload(payload, 17, &s) == 0);
    assert(s.flight_mode == RC_MODE_NORMAL);

    /* Mode 2 = Tripod */
    payload[2] = 0x02;
    assert(rcm_parse_payload(payload, 17, &s) == 0);
    assert(s.flight_mode == RC_MODE_TRIPOD);

    /* Mode with custom button bits set too: byte2 = 0x07 => mode=3, custom1=1 */
    payload[2] = 0x07;
    assert(rcm_parse_payload(payload, 17, &s) == 0);
    assert(s.flight_mode == RC_MODE_UNKNOWN);
    assert(s.custom1);
}

TEST(test_right_new_wheel) {
    rc_state_t s;
    uint8_t payload[17] = {0};
    for (int i = 5; i < 17; i += 2) { payload[i] = 0x00; payload[i+1] = 0x04; }

    /* Magnitude 10, direction positive (bit6 of byte4 = 1) */
    /* byte4: bits[5:1] = 10 = 0b01010, bit6 = 1 => byte4 = 0b0_1_01010_0 = 0x54 */
    payload[4] = (10 << 1) | (1 << 6);  /* 0x14 | 0x40 = 0x54 */
    assert(rcm_parse_payload(payload, 17, &s) == 0);
    assert(s.right_wheel_delta == 10);

    /* Magnitude 10, direction negative (bit6 = 0) */
    payload[4] = (10 << 1);  /* 0x14 */
    assert(rcm_parse_payload(payload, 17, &s) == 0);
    assert(s.right_wheel_delta == -10);

    /* Magnitude 31 (max), positive */
    payload[4] = (31 << 1) | (1 << 6);  /* 0x3E | 0x40 = 0x7E */
    assert(rcm_parse_payload(payload, 17, &s) == 0);
    assert(s.right_wheel_delta == 31);

    /* Zero magnitude */
    payload[4] = 0;
    assert(rcm_parse_payload(payload, 17, &s) == 0);
    assert(s.right_wheel_delta == 0);
}

TEST(test_all_buttons_pressed) {
    uint8_t payload[17] = {0};
    for (int i = 5; i < 17; i += 2) { payload[i] = 0x00; payload[i+1] = 0x04; }

    /* Set all button bits */
    payload[0] = 0x70;  /* pause + gohome + shutter */
    payload[1] = 0xF9;  /* record + all 5D */
    payload[2] = 0x1D;  /* mode=1(Normal) + custom1 + custom2 + custom3 */

    rc_state_t s;
    assert(rcm_parse_payload(payload, 17, &s) == 0);
    assert(s.pause && s.gohome && s.shutter && s.record);
    assert(s.custom1 && s.custom2 && s.custom3);
    assert(s.five_d.up && s.five_d.down && s.five_d.left &&
           s.five_d.right && s.five_d.center);
    assert(s.flight_mode == RC_MODE_NORMAL);
}

TEST(test_payload_too_short) {
    uint8_t payload[16] = {0};
    rc_state_t s;
    assert(rcm_parse_payload(payload, 16, &s) == -1);
    assert(rcm_parse_payload(payload, 0, &s) == -1);
    assert(rcm_parse_payload(NULL, 17, &s) == -1);
}

TEST(test_payload_longer_ok) {
    /* Payload longer than 17 bytes should still work */
    uint8_t payload[32] = {0};
    payload[0] = 0x40; /* shutter */
    for (int i = 5; i < 17; i += 2) { payload[i] = 0x00; payload[i+1] = 0x04; }
    rc_state_t s;
    assert(rcm_parse_payload(payload, 32, &s) == 0);
    assert(s.shutter);
}

/* ---- Helpers ---- */

#define ASSERT(expr) assert(expr)
#define ASSERT_EQ(a, b) assert((a) == (b))

/*
 * Build a DUML v1 RC push frame with the given 17-byte payload using
 * rcm_build_packet(). Returns total frame length written to `out`.
 */
static int build_rc_push_frame(uint8_t *out, size_t out_size,
                                const uint8_t *rc_payload) {
    return rcm_build_packet(out, out_size,
                            DUML_DEV_RC, 0,    /* sender */
                            DUML_DEV_APP, 0,   /* receiver */
                            0x0001,            /* seq */
                            DUML_PACK_REQUEST,
                            DUML_ACK_NO_ACK,
                            0,                 /* encrypt */
                            DUML_CMD_SET_RC,
                            DUML_CMD_RC_PUSH,
                            rc_payload, RC_PUSH_PAYLOAD_LEN);
}

/* ---- DUML parser tests ---- */

static int g_callback_count;
static rc_state_t g_last_state;

static void test_callback(const rc_state_t *state, void *userdata) {
    g_callback_count++;
    g_last_state = *state;
    (void)userdata;
}

TEST(test_parser_create_destroy) {
    rcm_parser_t *p = rcm_create(test_callback, NULL);
    assert(p != NULL);
    rcm_destroy(p);

    /* NULL callback should fail */
    assert(rcm_create(NULL, NULL) == NULL);
}

TEST(test_parser_valid_frame) {
    /* Build an RC push frame using rcm_build_packet() with shutter pressed
     * and sticks centered */
    uint8_t rc_payload[17] = {0};
    rc_payload[0] = 0x40;  /* shutter bit6 */
    /* Center sticks at 0x0400 */
    for (int i = 5; i < 17; i += 2) { rc_payload[i] = 0x00; rc_payload[i+1] = 0x04; }

    uint8_t frame[64];
    int frame_len = build_rc_push_frame(frame, sizeof(frame), rc_payload);
    ASSERT(frame_len > 0);

    g_callback_count = 0;
    memset(&g_last_state, 0, sizeof(g_last_state));

    rcm_parser_t *p = rcm_create(test_callback, NULL);
    int n = rcm_feed(p, frame, (size_t)frame_len);
    ASSERT_EQ(n, 1);
    ASSERT_EQ(g_callback_count, 1);
    ASSERT(g_last_state.shutter == true);
    ASSERT_EQ(g_last_state.stick_right.horizontal, 0);
    ASSERT_EQ(g_last_state.stick_right.vertical, 0);
    rcm_destroy(p);
}

TEST(test_parser_garbage_prefix) {
    /* Parser should skip non-0x55 bytes */
    rcm_parser_t *p = rcm_create(test_callback, NULL);
    g_callback_count = 0;

    uint8_t garbage[] = {0x00, 0xFF, 0x12, 0x34, 0xAB};
    rcm_feed(p, garbage, sizeof(garbage));
    assert(g_callback_count == 0);

    rcm_destroy(p);
}

TEST(test_parser_split_frame) {
    /* Feed a valid RC push frame in two separate rcm_feed() calls */
    uint8_t rc_payload[17] = {0};
    rc_payload[0] = 0x20; /* gohome */
    for (int i = 5; i < 17; i += 2) { rc_payload[i] = 0x00; rc_payload[i+1] = 0x04; }

    uint8_t frame[64];
    int frame_len = build_rc_push_frame(frame, sizeof(frame), rc_payload);
    ASSERT(frame_len > 0);

    g_callback_count = 0;
    rcm_parser_t *p = rcm_create(test_callback, NULL);

    /* Feed first half */
    size_t half = (size_t)frame_len / 2;
    int n1 = rcm_feed(p, frame, half);
    ASSERT_EQ(n1, 0);
    ASSERT_EQ(g_callback_count, 0);

    /* Feed second half */
    int n2 = rcm_feed(p, frame + half, (size_t)frame_len - half);
    ASSERT_EQ(n2, 1);
    ASSERT_EQ(g_callback_count, 1);
    ASSERT(g_last_state.gohome == true);

    rcm_destroy(p);
}

TEST(test_parser_multiple_frames) {
    /* Feed 3 valid RC push frames in a single rcm_feed() call */
    uint8_t rc_payload[17] = {0};
    for (int i = 5; i < 17; i += 2) { rc_payload[i] = 0x00; rc_payload[i+1] = 0x04; }

    uint8_t frame[64];
    int frame_len = build_rc_push_frame(frame, sizeof(frame), rc_payload);
    ASSERT(frame_len > 0);

    /* Concatenate 3 copies */
    uint8_t buf[192];
    memcpy(buf, frame, (size_t)frame_len);
    memcpy(buf + frame_len, frame, (size_t)frame_len);
    memcpy(buf + 2 * frame_len, frame, (size_t)frame_len);

    g_callback_count = 0;
    rcm_parser_t *p = rcm_create(test_callback, NULL);
    int n = rcm_feed(p, buf, (size_t)(3 * frame_len));
    ASSERT_EQ(n, 3);
    ASSERT_EQ(g_callback_count, 3);
    rcm_destroy(p);
}

TEST(test_parser_bad_crc16) {
    /* Valid header CRC8 but corrupted CRC16 — frame should be silently skipped */
    uint8_t rc_payload[17] = {0};
    for (int i = 5; i < 17; i += 2) { rc_payload[i] = 0x00; rc_payload[i+1] = 0x04; }

    uint8_t frame[64];
    int frame_len = build_rc_push_frame(frame, sizeof(frame), rc_payload);
    ASSERT(frame_len > 0);

    /* Corrupt CRC16 (last 2 bytes) */
    frame[frame_len - 1] ^= 0xFF;
    frame[frame_len - 2] ^= 0xFF;

    g_callback_count = 0;
    rcm_parser_t *p = rcm_create(test_callback, NULL);
    int n = rcm_feed(p, frame, (size_t)frame_len);
    ASSERT_EQ(n, 0);
    ASSERT_EQ(g_callback_count, 0);
    rcm_destroy(p);
}

TEST(test_parser_bad_crc8) {
    /* 0x55 byte followed by bad header CRC8 — should skip and re-scan */
    uint8_t rc_payload[17] = {0};
    rc_payload[0] = 0x10; /* pause */
    for (int i = 5; i < 17; i += 2) { rc_payload[i] = 0x00; rc_payload[i+1] = 0x04; }

    uint8_t frame[64];
    int frame_len = build_rc_push_frame(frame, sizeof(frame), rc_payload);
    ASSERT(frame_len > 0);

    /* Prepend a fake SOF with bad CRC8, followed by the real frame */
    uint8_t buf[128];
    buf[0] = 0x55;  /* fake SOF */
    buf[1] = 0x1E;  /* some length byte */
    buf[2] = 0x04;
    buf[3] = 0xAA;  /* bad CRC8 */
    memcpy(buf + 4, frame, (size_t)frame_len);

    g_callback_count = 0;
    rcm_parser_t *p = rcm_create(test_callback, NULL);
    int n = rcm_feed(p, buf, (size_t)(4 + frame_len));
    ASSERT_EQ(n, 1);
    ASSERT_EQ(g_callback_count, 1);
    ASSERT(g_last_state.pause == true);
    rcm_destroy(p);
}

TEST(test_parser_frame_too_short) {
    /* Header claims length < DUML_MIN_FRAME_LEN (13) — should skip */
    uint8_t rc_payload[17] = {0};
    for (int i = 5; i < 17; i += 2) { rc_payload[i] = 0x00; rc_payload[i+1] = 0x04; }

    uint8_t frame[64];
    int frame_len = build_rc_push_frame(frame, sizeof(frame), rc_payload);
    ASSERT(frame_len > 0);

    /* Tamper with the length field to claim length=5, then fix CRC8 */
    /* Build a fake 4-byte header with short length */
    uint8_t fake_hdr[128];
    fake_hdr[0] = 0x55;
    uint16_t bad_len_ver = (5 & 0x03FF) | (1 << 10);
    fake_hdr[1] = bad_len_ver & 0xFF;
    fake_hdr[2] = (bad_len_ver >> 8) & 0xFF;
    /* We need the CRC8 to pass so the parser reaches the length check.
     * Build a valid packet with this header, then use rcm_build_packet to get
     * a properly CRC'd short-length header. Instead, let's just check that
     * feeding the real frame after garbage works. */
    /* Actually: the CRC8 won't match for the tampered header, so the parser
     * will just skip the 0x55. Let's test this differently: just verify that
     * a frame with a too-short length field (but no matching CRC8) is skipped
     * and the parser can still decode a subsequent valid frame. */
    fake_hdr[3] = 0x00; /* wrong CRC8 */
    memcpy(fake_hdr + 4, frame, (size_t)frame_len);

    g_callback_count = 0;
    rcm_parser_t *p = rcm_create(test_callback, NULL);
    int n = rcm_feed(p, fake_hdr, (size_t)(4 + frame_len));
    /* The fake header is skipped (bad CRC8), real frame is decoded */
    ASSERT_EQ(n, 1);
    ASSERT_EQ(g_callback_count, 1);
    rcm_destroy(p);
}

TEST(test_parser_frame_too_long) {
    /* Feed a header that claims frame_len > DUML_MAX_FRAME_LEN.
     * Since CRC8 won't match for arbitrary bytes, this just confirms
     * the parser skips the byte and recovers to decode a valid frame. */
    uint8_t rc_payload[17] = {0};
    rc_payload[0] = 0x40; /* shutter */
    for (int i = 5; i < 17; i += 2) { rc_payload[i] = 0x00; rc_payload[i+1] = 0x04; }

    uint8_t frame[64];
    int frame_len = build_rc_push_frame(frame, sizeof(frame), rc_payload);
    ASSERT(frame_len > 0);

    /* Prepend some garbage with 0x55 bytes scattered in */
    uint8_t buf[128];
    buf[0] = 0x55;
    buf[1] = 0xFF; /* garbage */
    buf[2] = 0xFF;
    buf[3] = 0x00; /* wrong CRC8 */
    memcpy(buf + 4, frame, (size_t)frame_len);

    g_callback_count = 0;
    rcm_parser_t *p = rcm_create(test_callback, NULL);
    int n = rcm_feed(p, buf, (size_t)(4 + frame_len));
    ASSERT_EQ(n, 1);
    ASSERT_EQ(g_callback_count, 1);
    ASSERT(g_last_state.shutter == true);
    rcm_destroy(p);
}

TEST(test_parser_ring_buffer_overflow) {
    /* Feed >4096 bytes of non-frame data, then a valid frame.
     * The parser should recover and decode the frame. */
    uint8_t rc_payload[17] = {0};
    rc_payload[1] = 0x01; /* record */
    for (int i = 5; i < 17; i += 2) { rc_payload[i] = 0x00; rc_payload[i+1] = 0x04; }

    uint8_t frame[64];
    int frame_len = build_rc_push_frame(frame, sizeof(frame), rc_payload);
    ASSERT(frame_len > 0);

    /* 5000 bytes of non-SOF garbage to overflow the 4096-byte ring */
    uint8_t garbage[5000];
    memset(garbage, 0xAA, sizeof(garbage));

    g_callback_count = 0;
    rcm_parser_t *p = rcm_create(test_callback, NULL);
    rcm_feed(p, garbage, sizeof(garbage));
    ASSERT_EQ(g_callback_count, 0);

    /* Now feed a valid frame — parser should recover */
    int n = rcm_feed(p, frame, (size_t)frame_len);
    ASSERT_EQ(n, 1);
    ASSERT_EQ(g_callback_count, 1);
    ASSERT(g_last_state.record == true);
    rcm_destroy(p);
}

TEST(test_parser_reset) {
    /* Feed half a frame, reset, then feed a complete frame */
    uint8_t rc_payload[17] = {0};
    rc_payload[0] = 0x40; /* shutter */
    for (int i = 5; i < 17; i += 2) { rc_payload[i] = 0x00; rc_payload[i+1] = 0x04; }

    uint8_t frame[64];
    int frame_len = build_rc_push_frame(frame, sizeof(frame), rc_payload);
    ASSERT(frame_len > 0);

    g_callback_count = 0;
    rcm_parser_t *p = rcm_create(test_callback, NULL);

    /* Feed partial frame */
    rcm_feed(p, frame, (size_t)frame_len / 2);
    ASSERT_EQ(g_callback_count, 0);

    /* Reset mid-parse */
    rcm_reset(p);

    /* Feed complete frame — should decode cleanly */
    int n = rcm_feed(p, frame, (size_t)frame_len);
    ASSERT_EQ(n, 1);
    ASSERT_EQ(g_callback_count, 1);
    ASSERT(g_last_state.shutter == true);
    rcm_destroy(p);
}

TEST(test_parser_interleaved_non_rc_frame) {
    /* Valid DUML frame with different cmd_set — callback not invoked */
    uint8_t buf[64];
    /* Build a non-RC frame (cmd_set=0x01, cmd_id=0x01) */
    uint8_t payload[] = { 0xDE, 0xAD };
    int len = rcm_build_packet(buf, sizeof(buf),
                               DUML_DEV_PC, 0, DUML_DEV_FC, 0,
                               0x0001,
                               DUML_PACK_REQUEST, DUML_ACK_NO_ACK, 0,
                               0x01, 0x01,  /* non-RC cmd_set */
                               payload, 2);
    ASSERT(len > 0);

    g_callback_count = 0;
    rcm_parser_t *p = rcm_create(test_callback, NULL);
    int n = rcm_feed(p, buf, (size_t)len);
    ASSERT_EQ(n, 0);
    ASSERT_EQ(g_callback_count, 0);

    /* Now feed an RC push frame after — should still decode */
    uint8_t rc_payload[17] = {0};
    rc_payload[0] = 0x10; /* pause */
    for (int i = 5; i < 17; i += 2) { rc_payload[i] = 0x00; rc_payload[i+1] = 0x04; }

    uint8_t rc_frame[64];
    int rc_len = build_rc_push_frame(rc_frame, sizeof(rc_frame), rc_payload);
    ASSERT(rc_len > 0);

    n = rcm_feed(p, rc_frame, (size_t)rc_len);
    ASSERT_EQ(n, 1);
    ASSERT_EQ(g_callback_count, 1);
    ASSERT(g_last_state.pause == true);
    rcm_destroy(p);
}

/* ---- Payload edge case tests ---- */

TEST(test_payload_null_out) {
    uint8_t payload[17] = {0};
    ASSERT_EQ(rcm_parse_payload(payload, 17, NULL), -1);
}

TEST(test_sticks_extreme_values) {
    /* uint16 values at 0x0000 and 0xFFFF to verify centering at boundaries */
    uint8_t payload[17] = {0};

    /* All sticks at 0x0000 => -0x400 = -1024 */
    rc_state_t s;
    ASSERT_EQ(rcm_parse_payload(payload, 17, &s), 0);
    ASSERT_EQ(s.stick_right.horizontal, -1024);
    ASSERT_EQ(s.stick_right.vertical, -1024);
    ASSERT_EQ(s.stick_left.vertical, -1024);
    ASSERT_EQ(s.stick_left.horizontal, -1024);
    ASSERT_EQ(s.left_wheel, -1024);
    ASSERT_EQ(s.right_wheel, -1024);

    /* All sticks at 0xFFFF => 0xFFFF - 0x400 = 64511, but as int16: -1025 */
    /* (uint16_t)0xFFFF - 0x400 = 0xFBFF = 64511 unsigned, cast to int16 = -1025 */
    for (int i = 5; i < 17; i++) payload[i] = 0xFF;
    ASSERT_EQ(rcm_parse_payload(payload, 17, &s), 0);
    ASSERT_EQ(s.stick_right.horizontal, (int16_t)(0xFFFF - 0x400));
    ASSERT_EQ(s.stick_right.vertical, (int16_t)(0xFFFF - 0x400));
    ASSERT_EQ(s.stick_left.vertical, (int16_t)(0xFFFF - 0x400));
    ASSERT_EQ(s.stick_left.horizontal, (int16_t)(0xFFFF - 0x400));
    ASSERT_EQ(s.left_wheel, (int16_t)(0xFFFF - 0x400));
    ASSERT_EQ(s.right_wheel, (int16_t)(0xFFFF - 0x400));
}

/* ---- Utility ---- */

TEST(test_flight_mode_str) {
    assert(strcmp(rcm_flight_mode_str(RC_MODE_SPORT), "Sport") == 0);
    assert(strcmp(rcm_flight_mode_str(RC_MODE_NORMAL), "Normal") == 0);
    assert(strcmp(rcm_flight_mode_str(RC_MODE_TRIPOD), "Tripod") == 0);
    assert(strcmp(rcm_flight_mode_str(RC_MODE_UNKNOWN), "Unknown") == 0);
}

/* ---- Packet builder tests ---- */

TEST(test_build_packet_basic) {
    uint8_t buf[64];
    /* Build a minimal packet with no payload */
    int len = rcm_build_packet(buf, sizeof(buf),
                               DUML_DEV_PC, 0,   /* sender */
                               DUML_DEV_RC, 0,   /* receiver */
                               0x0001,            /* seq */
                               DUML_PACK_REQUEST,
                               DUML_ACK_NO_ACK,
                               0,                 /* encrypt */
                               0x06, 0x01,        /* cmd_set, cmd_id */
                               NULL, 0);

    assert(len == 13); /* 11 header + 0 payload + 2 CRC16 */
    assert(buf[0] == 0x55); /* SOF */

    /* Verify length/version in bytes 1-2 */
    uint16_t len_ver = (uint16_t)buf[1] | ((uint16_t)buf[2] << 8);
    assert((len_ver & 0x03FF) == 13);          /* length = 13 */
    assert(((len_ver >> 10) & 0x3F) == 1);     /* version = 1 */

    /* Verify CRC8 at byte 3 matches bytes 0-2 */
    /* Recompute CRC8 with seed 0x77 using same table */
    {
        static const uint8_t crc8_table[256] = {
            0x00,0x5e,0xbc,0xe2,0x61,0x3f,0xdd,0x83,0xc2,0x9c,0x7e,0x20,0xa3,0xfd,0x1f,0x41,
            0x9d,0xc3,0x21,0x7f,0xfc,0xa2,0x40,0x1e,0x5f,0x01,0xe3,0xbd,0x3e,0x60,0x82,0xdc,
            0x23,0x7d,0x9f,0xc1,0x42,0x1c,0xfe,0xa0,0xe1,0xbf,0x5d,0x03,0x80,0xde,0x3c,0x62,
            0xbe,0xe0,0x02,0x5c,0xdf,0x81,0x63,0x3d,0x7c,0x22,0xc0,0x9e,0x1d,0x43,0xa1,0xff,
            0x46,0x18,0xfa,0xa4,0x27,0x79,0x9b,0xc5,0x84,0xda,0x38,0x66,0xe5,0xbb,0x59,0x07,
            0xdb,0x85,0x67,0x39,0xba,0xe4,0x06,0x58,0x19,0x47,0xa5,0xfb,0x78,0x26,0xc4,0x9a,
            0x65,0x3b,0xd9,0x87,0x04,0x5a,0xb8,0xe6,0xa7,0xf9,0x1b,0x45,0xc6,0x98,0x7a,0x24,
            0xf8,0xa6,0x44,0x1a,0x99,0xc7,0x25,0x7b,0x3a,0x64,0x86,0xd8,0x5b,0x05,0xe7,0xb9,
            0x8c,0xd2,0x30,0x6e,0xed,0xb3,0x51,0x0f,0x4e,0x10,0xf2,0xac,0x2f,0x71,0x93,0xcd,
            0x11,0x4f,0xad,0xf3,0x70,0x2e,0xcc,0x92,0xd3,0x8d,0x6f,0x31,0xb2,0xec,0x0e,0x50,
            0xaf,0xf1,0x13,0x4d,0xce,0x90,0x72,0x2c,0x6d,0x33,0xd1,0x8f,0x0c,0x52,0xb0,0xee,
            0x32,0x6c,0x8e,0xd0,0x53,0x0d,0xef,0xb1,0xf0,0xae,0x4c,0x12,0x91,0xcf,0x2d,0x73,
            0xca,0x94,0x76,0x28,0xab,0xf5,0x17,0x49,0x08,0x56,0xb4,0xea,0x69,0x37,0xd5,0x8b,
            0x57,0x09,0xeb,0xb5,0x36,0x68,0x8a,0xd4,0x95,0xcb,0x29,0x77,0xf4,0xaa,0x48,0x16,
            0xe9,0xb7,0x55,0x0b,0x88,0xd6,0x34,0x6a,0x2b,0x75,0x97,0xc9,0x4a,0x14,0xf6,0xa8,
            0x74,0x2a,0xc8,0x96,0x15,0x4b,0xa9,0xf7,0xb6,0xe8,0x0a,0x54,0xd7,0x89,0x6b,0x35
        };
        uint8_t crc = 0x77;
        for (int i = 0; i < 3; i++)
            crc = crc8_table[(crc ^ buf[i]) & 0xFF];
        assert(buf[3] == crc);
    }

    /* Verify sender/receiver fields */
    assert((buf[4] & 0x1F) == DUML_DEV_PC);    /* sender_type */
    assert(((buf[4] >> 5) & 0x07) == 0);       /* sender_index */
    assert((buf[5] & 0x1F) == DUML_DEV_RC);    /* receiver_type */
    assert(((buf[5] >> 5) & 0x07) == 0);       /* receiver_index */

    /* Verify seq */
    assert(buf[6] == 0x01 && buf[7] == 0x00);

    /* cmd_set, cmd_id */
    assert(buf[9] == 0x06);
    assert(buf[10] == 0x01);

    /* Verify CRC16 of bytes 0..10 */
    {
        static const uint16_t crc16_table[256] = {
            0x0000,0x1189,0x2312,0x329b,0x4624,0x57ad,0x6536,0x74bf,
            0x8c48,0x9dc1,0xaf5a,0xbed3,0xca6c,0xdbe5,0xe97e,0xf8f7,
            0x1081,0x0108,0x3393,0x221a,0x56a5,0x472c,0x75b7,0x643e,
            0x9cc9,0x8d40,0xbfdb,0xae52,0xdaed,0xcb64,0xf9ff,0xe876,
            0x2102,0x308b,0x0210,0x1399,0x6726,0x76af,0x4434,0x55bd,
            0xad4a,0xbcc3,0x8e58,0x9fd1,0xeb6e,0xfae7,0xc87c,0xd9f5,
            0x3183,0x200a,0x1291,0x0318,0x77a7,0x662e,0x54b5,0x453c,
            0xbdcb,0xac42,0x9ed9,0x8f50,0xfbef,0xea66,0xd8fd,0xc974,
            0x4204,0x538d,0x6116,0x709f,0x0420,0x15a9,0x2732,0x36bb,
            0xce4c,0xdfc5,0xed5e,0xfcd7,0x8868,0x99e1,0xab7a,0xbaf3,
            0x5285,0x430c,0x7197,0x601e,0x14a1,0x0528,0x37b3,0x263a,
            0xdecd,0xcf44,0xfddf,0xec56,0x98e9,0x8960,0xbbfb,0xaa72,
            0x6306,0x728f,0x4014,0x519d,0x2522,0x34ab,0x0630,0x17b9,
            0xef4e,0xfec7,0xcc5c,0xddd5,0xa96a,0xb8e3,0x8a78,0x9bf1,
            0x7387,0x620e,0x5095,0x411c,0x35a3,0x242a,0x16b1,0x0738,
            0xffcf,0xee46,0xdcdd,0xcd54,0xb9eb,0xa862,0x9af9,0x8b70,
            0x8408,0x9581,0xa71a,0xb693,0xc22c,0xd3a5,0xe13e,0xf0b7,
            0x0840,0x19c9,0x2b52,0x3adb,0x4e64,0x5fed,0x6d76,0x7cff,
            0x9489,0x8500,0xb79b,0xa612,0xd2ad,0xc324,0xf1bf,0xe036,
            0x18c1,0x0948,0x3bd3,0x2a5a,0x5ee5,0x4f6c,0x7df7,0x6c7e,
            0xa50a,0xb483,0x8618,0x9791,0xe32e,0xf2a7,0xc03c,0xd1b5,
            0x2942,0x38cb,0x0a50,0x1bd9,0x6f66,0x7eef,0x4c74,0x5dfd,
            0xb58b,0xa402,0x9699,0x8710,0xf3af,0xe226,0xd0bd,0xc134,
            0x39c3,0x284a,0x1ad1,0x0b58,0x7fe7,0x6e6e,0x5cf5,0x4d7c,
            0xc60c,0xd785,0xe51e,0xf497,0x8028,0x91a1,0xa33a,0xb2b3,
            0x4a44,0x5bcd,0x6956,0x78df,0x0c60,0x1de9,0x2f72,0x3efb,
            0xd68d,0xc704,0xf59f,0xe416,0x90a9,0x8120,0xb3bb,0xa232,
            0x5ac5,0x4b4c,0x79d7,0x685e,0x1ce1,0x0d68,0x3ff3,0x2e7a,
            0xe70e,0xf687,0xc41c,0xd595,0xa12a,0xb0a3,0x8238,0x93b1,
            0x6b46,0x7acf,0x4854,0x59dd,0x2d62,0x3ceb,0x0e70,0x1ff9,
            0xf78f,0xe606,0xd49d,0xc514,0xb1ab,0xa022,0x92b9,0x8330,
            0x7bc7,0x6a4e,0x58d5,0x495c,0x3de3,0x2c6a,0x1ef1,0x0f78
        };
        uint16_t crc = 0x3692;
        for (int i = 0; i < 11; i++)
            crc = crc16_table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
        assert(buf[11] == (crc & 0xFF));
        assert(buf[12] == ((crc >> 8) & 0xFF));
    }
}

TEST(test_build_packet_with_payload) {
    uint8_t buf[64];
    uint8_t payload[] = { 0xAA, 0xBB, 0xCC };
    int len = rcm_build_packet(buf, sizeof(buf),
                               DUML_DEV_APP, 1,
                               DUML_DEV_FC, 2,
                               0x1234,
                               DUML_PACK_RESPONSE,
                               DUML_ACK_AFTER_EXEC,
                               0,
                               0x01, 0x02,
                               payload, 3);

    assert(len == 16); /* 11 + 3 + 2 */

    /* Verify payload at correct offset */
    assert(buf[11] == 0xAA);
    assert(buf[12] == 0xBB);
    assert(buf[13] == 0xCC);

    /* Verify sender/receiver */
    assert((buf[4] & 0x1F) == DUML_DEV_APP);
    assert(((buf[4] >> 5) & 0x07) == 1);
    assert((buf[5] & 0x1F) == DUML_DEV_FC);
    assert(((buf[5] >> 5) & 0x07) == 2);

    /* Verify seq */
    assert(buf[6] == 0x34 && buf[7] == 0x12);

    /* Verify cmd_type_data: pack=1(bit7), ack=2(bits5-6), encrypt=0 */
    assert(buf[8] == ((1 << 7) | (2 << 5)));

    /* Verify cmd_set, cmd_id */
    assert(buf[9] == 0x01);
    assert(buf[10] == 0x02);
}

TEST(test_build_enable_cmd) {
    uint8_t buf[64];
    int len = rcm_build_enable_cmd(buf, sizeof(buf), 42);

    assert(len == 14); /* 11 header + 1 payload + 2 CRC16 */
    assert(buf[0] == 0x55);

    /* Verify length field */
    uint16_t len_ver = (uint16_t)buf[1] | ((uint16_t)buf[2] << 8);
    assert((len_ver & 0x03FF) == 14);

    /* cmd_set = 0x06 (RC), cmd_id = 0x24 (enable) */
    assert(buf[9] == 0x06);
    assert(buf[10] == 0x24);

    /* payload = 0x01 */
    assert(buf[11] == 0x01);

    /* Sender = PC(10), Receiver = RC(6) */
    assert((buf[4] & 0x1F) == DUML_DEV_PC);
    assert((buf[5] & 0x1F) == DUML_DEV_RC);

    /* ack_type = ACK_AFTER_EXEC(2), pack_type = REQUEST(0) */
    assert(buf[8] == (DUML_ACK_AFTER_EXEC << 5));

    /* Verify seq */
    assert(buf[6] == 42 && buf[7] == 0);
}

TEST(test_build_channel_request) {
    uint8_t buf[64];
    int len = rcm_build_channel_request(buf, sizeof(buf), 7);

    assert(len == 13); /* 11 header + 0 payload + 2 CRC16 */
    assert(buf[0] == 0x55);

    /* cmd_set = 0x06, cmd_id = 0x01 */
    assert(buf[9] == 0x06);
    assert(buf[10] == 0x01);

    /* Sender = PC(10), Receiver = RC(6) */
    assert((buf[4] & 0x1F) == DUML_DEV_PC);
    assert((buf[5] & 0x1F) == DUML_DEV_RC);

    /* Verify seq */
    assert(buf[6] == 7 && buf[7] == 0);
}

TEST(test_build_roundtrip) {
    /*
     * Build an enable command, then feed it to the parser.
     * The parser should recognize it as a valid DUML frame (CRCs pass),
     * but should NOT invoke the callback since it's not an RC push
     * (cmd_id=0x24, not 0x05).
     */
    uint8_t pkt[64];
    int len = rcm_build_enable_cmd(pkt, sizeof(pkt), 100);
    assert(len == 14);

    g_callback_count = 0;
    rcm_parser_t *p = rcm_create(test_callback, NULL);
    int decoded = rcm_feed(p, pkt, (size_t)len);

    /* No RC push callback should fire */
    assert(decoded == 0);
    assert(g_callback_count == 0);

    rcm_destroy(p);
}

/* ---- Error handling tests for packet builder ---- */

TEST(test_build_packet_buffer_too_small) {
    uint8_t buf[5]; /* Too small for any packet */
    int len = rcm_build_packet(buf, sizeof(buf),
                               DUML_DEV_PC, 0, DUML_DEV_RC, 0,
                               0, DUML_PACK_REQUEST, DUML_ACK_NO_ACK, 0,
                               0x06, 0x01, NULL, 0);
    assert(len == -1);
}

TEST(test_build_packet_null_output) {
    int len = rcm_build_packet(NULL, 64,
                               DUML_DEV_PC, 0, DUML_DEV_RC, 0,
                               0, DUML_PACK_REQUEST, DUML_ACK_NO_ACK, 0,
                               0x06, 0x01, NULL, 0);
    assert(len == -1);
}

TEST(test_build_packet_null_payload_with_length) {
    /* payload_len > 0 but payload is NULL should fail */
    uint8_t buf[64];
    int len = rcm_build_packet(buf, sizeof(buf),
                               DUML_DEV_PC, 0, DUML_DEV_RC, 0,
                               0, DUML_PACK_REQUEST, DUML_ACK_NO_ACK, 0,
                               0x06, 0x01, NULL, 10);
    assert(len == -1);
}

TEST(test_build_packet_payload_overflow) {
    /* Payload so large total would exceed DUML_MAX_FRAME_LEN */
    uint8_t buf[2048];
    uint8_t payload[1500];
    memset(payload, 0xAA, sizeof(payload));
    int len = rcm_build_packet(buf, sizeof(buf),
                               DUML_DEV_PC, 0, DUML_DEV_RC, 0,
                               0, DUML_PACK_REQUEST, DUML_ACK_NO_ACK, 0,
                               0x06, 0x01, payload, sizeof(payload));
    assert(len == -1);
}

/* ---- NULL-safety tests ---- */

TEST(test_feed_null_parser) {
    uint8_t data[] = {0x55, 0x00};
    ASSERT_EQ(rcm_feed(NULL, data, sizeof(data)), 0);
}

TEST(test_feed_null_data) {
    rcm_parser_t *p = rcm_create(test_callback, NULL);
    ASSERT_EQ(rcm_feed(p, NULL, 10), 0);
    rcm_destroy(p);
}

TEST(test_destroy_null) {
    /* Should not crash */
    rcm_destroy(NULL);
}

TEST(test_reset_null) {
    /* Should not crash */
    rcm_reset(NULL);
}

/* ---- Byte-at-a-time feeding ---- */

TEST(test_parser_byte_at_a_time) {
    /* Feed a valid frame one byte at a time */
    uint8_t rc_payload[17] = {0};
    rc_payload[0] = 0x40; /* shutter */
    for (int i = 5; i < 17; i += 2) { rc_payload[i] = 0x00; rc_payload[i+1] = 0x04; }

    uint8_t frame[64];
    int frame_len = build_rc_push_frame(frame, sizeof(frame), rc_payload);
    ASSERT(frame_len > 0);

    g_callback_count = 0;
    rcm_parser_t *p = rcm_create(test_callback, NULL);

    int total_decoded = 0;
    for (int i = 0; i < frame_len; i++) {
        total_decoded += rcm_feed(p, &frame[i], 1);
    }
    ASSERT_EQ(total_decoded, 1);
    ASSERT_EQ(g_callback_count, 1);
    ASSERT(g_last_state.shutter == true);
    rcm_destroy(p);
}

/* ---- Payload byte 3 isolation ---- */

TEST(test_payload_byte3_ignored) {
    /* Byte 3 has no known fields — setting it shouldn't affect parsed state */
    uint8_t payload[17] = {0};
    for (int i = 5; i < 17; i += 2) { payload[i] = 0x00; payload[i+1] = 0x04; }

    rc_state_t s1, s2;
    payload[3] = 0x00;
    ASSERT_EQ(rcm_parse_payload(payload, 17, &s1), 0);

    payload[3] = 0xFF;
    ASSERT_EQ(rcm_parse_payload(payload, 17, &s2), 0);

    /* All fields should be identical regardless of byte 3 */
    ASSERT_EQ(s1.pause, s2.pause);
    ASSERT_EQ(s1.gohome, s2.gohome);
    ASSERT_EQ(s1.shutter, s2.shutter);
    ASSERT_EQ(s1.record, s2.record);
    ASSERT_EQ(s1.custom1, s2.custom1);
    ASSERT_EQ(s1.custom2, s2.custom2);
    ASSERT_EQ(s1.custom3, s2.custom3);
    ASSERT_EQ(s1.five_d.up, s2.five_d.up);
    ASSERT_EQ(s1.five_d.down, s2.five_d.down);
    ASSERT_EQ(s1.five_d.left, s2.five_d.left);
    ASSERT_EQ(s1.five_d.right, s2.five_d.right);
    ASSERT_EQ(s1.five_d.center, s2.five_d.center);
    ASSERT_EQ(s1.flight_mode, s2.flight_mode);
    ASSERT_EQ(s1.stick_right.horizontal, s2.stick_right.horizontal);
    ASSERT_EQ(s1.stick_right.vertical, s2.stick_right.vertical);
    ASSERT_EQ(s1.stick_left.horizontal, s2.stick_left.horizontal);
    ASSERT_EQ(s1.stick_left.vertical, s2.stick_left.vertical);
    ASSERT_EQ(s1.left_wheel, s2.left_wheel);
    ASSERT_EQ(s1.right_wheel, s2.right_wheel);
}

/* ---- Right wheel delta edge cases ---- */

TEST(test_right_wheel_delta_sign_with_zero_mag) {
    /* Sign bit set but magnitude is zero — result should be 0, not -0 */
    uint8_t payload[17] = {0};
    for (int i = 5; i < 17; i += 2) { payload[i] = 0x00; payload[i+1] = 0x04; }

    payload[4] = (1 << 6); /* sign=1, magnitude=0 */
    rc_state_t s;
    ASSERT_EQ(rcm_parse_payload(payload, 17, &s), 0);
    ASSERT_EQ(s.right_wheel_delta, 0);
}

/* ---- Flight mode string edge case ---- */

TEST(test_flight_mode_str_out_of_range) {
    /* Values beyond the enum should return "Unknown" */
    assert(strcmp(rcm_flight_mode_str((rc_flight_mode_t)99), "Unknown") == 0);
    assert(strcmp(rcm_flight_mode_str((rc_flight_mode_t)-1), "Unknown") == 0);
}

/* ---- RC push round-trip through build + parse ---- */

TEST(test_build_rc_push_roundtrip) {
    /*
     * Build an RC push frame with specific state, feed it through the
     * parser, and verify the callback receives the correct decoded state.
     */
    uint8_t rc_payload[17] = {0};

    /* Set some buttons and sticks */
    rc_payload[0] = 0x30; /* pause + gohome */
    rc_payload[1] = 0x09; /* record + 5D right */
    rc_payload[2] = 0x06; /* mode=2(Tripod) + custom1 */

    /* Right H = +330 => 330 + 1024 = 1354 = 0x054A */
    rc_payload[5] = 0x4A; rc_payload[6] = 0x05;
    /* Right V = -330 => -330 + 1024 = 694 = 0x02B6 */
    rc_payload[7] = 0xB6; rc_payload[8] = 0x02;
    /* Rest centered */
    rc_payload[9] = 0x00; rc_payload[10] = 0x04;
    rc_payload[11] = 0x00; rc_payload[12] = 0x04;
    rc_payload[13] = 0x00; rc_payload[14] = 0x04;
    rc_payload[15] = 0x00; rc_payload[16] = 0x04;

    /* Delta: magnitude 15, positive */
    rc_payload[4] = (15 << 1) | (1 << 6);

    uint8_t frame[64];
    int flen = build_rc_push_frame(frame, sizeof(frame), rc_payload);
    ASSERT(flen > 0);

    g_callback_count = 0;
    rcm_parser_t *p = rcm_create(test_callback, NULL);
    int n = rcm_feed(p, frame, (size_t)flen);
    ASSERT_EQ(n, 1);
    ASSERT_EQ(g_callback_count, 1);

    /* Verify decoded state matches what we encoded */
    ASSERT(g_last_state.pause == true);
    ASSERT(g_last_state.gohome == true);
    ASSERT(g_last_state.shutter == false);
    ASSERT(g_last_state.record == true);
    ASSERT(g_last_state.custom1 == true);
    ASSERT(g_last_state.custom2 == false);
    ASSERT(g_last_state.five_d.right == true);
    ASSERT(g_last_state.five_d.up == false);
    ASSERT_EQ(g_last_state.flight_mode, RC_MODE_TRIPOD);
    ASSERT_EQ(g_last_state.stick_right.horizontal, 330);
    ASSERT_EQ(g_last_state.stick_right.vertical, -330);
    ASSERT_EQ(g_last_state.stick_left.horizontal, 0);
    ASSERT_EQ(g_last_state.stick_left.vertical, 0);
    ASSERT_EQ(g_last_state.right_wheel_delta, 15);

    rcm_destroy(p);
}

/* ---- Parser: frame with valid CRC8 but short length field ---- */

TEST(test_parser_short_length_valid_crc8) {
    /*
     * Craft a header where CRC8 is valid but the length field is < 13.
     * The parser should skip this and recover.
     */
    /* Build a 4-byte header with length=5, version=1 */
    uint8_t hdr[4];
    hdr[0] = 0x55;
    uint16_t len_ver = (5 & 0x03FF) | (1 << 10);
    hdr[1] = len_ver & 0xFF;
    hdr[2] = (len_ver >> 8) & 0xFF;
    /* Compute correct CRC8 for these 3 bytes */
    {
        static const uint8_t crc8_tab[256] = {
            0x00,0x5e,0xbc,0xe2,0x61,0x3f,0xdd,0x83,0xc2,0x9c,0x7e,0x20,0xa3,0xfd,0x1f,0x41,
            0x9d,0xc3,0x21,0x7f,0xfc,0xa2,0x40,0x1e,0x5f,0x01,0xe3,0xbd,0x3e,0x60,0x82,0xdc,
            0x23,0x7d,0x9f,0xc1,0x42,0x1c,0xfe,0xa0,0xe1,0xbf,0x5d,0x03,0x80,0xde,0x3c,0x62,
            0xbe,0xe0,0x02,0x5c,0xdf,0x81,0x63,0x3d,0x7c,0x22,0xc0,0x9e,0x1d,0x43,0xa1,0xff,
            0x46,0x18,0xfa,0xa4,0x27,0x79,0x9b,0xc5,0x84,0xda,0x38,0x66,0xe5,0xbb,0x59,0x07,
            0xdb,0x85,0x67,0x39,0xba,0xe4,0x06,0x58,0x19,0x47,0xa5,0xfb,0x78,0x26,0xc4,0x9a,
            0x65,0x3b,0xd9,0x87,0x04,0x5a,0xb8,0xe6,0xa7,0xf9,0x1b,0x45,0xc6,0x98,0x7a,0x24,
            0xf8,0xa6,0x44,0x1a,0x99,0xc7,0x25,0x7b,0x3a,0x64,0x86,0xd8,0x5b,0x05,0xe7,0xb9,
            0x8c,0xd2,0x30,0x6e,0xed,0xb3,0x51,0x0f,0x4e,0x10,0xf2,0xac,0x2f,0x71,0x93,0xcd,
            0x11,0x4f,0xad,0xf3,0x70,0x2e,0xcc,0x92,0xd3,0x8d,0x6f,0x31,0xb2,0xec,0x0e,0x50,
            0xaf,0xf1,0x13,0x4d,0xce,0x90,0x72,0x2c,0x6d,0x33,0xd1,0x8f,0x0c,0x52,0xb0,0xee,
            0x32,0x6c,0x8e,0xd0,0x53,0x0d,0xef,0xb1,0xf0,0xae,0x4c,0x12,0x91,0xcf,0x2d,0x73,
            0xca,0x94,0x76,0x28,0xab,0xf5,0x17,0x49,0x08,0x56,0xb4,0xea,0x69,0x37,0xd5,0x8b,
            0x57,0x09,0xeb,0xb5,0x36,0x68,0x8a,0xd4,0x95,0xcb,0x29,0x77,0xf4,0xaa,0x48,0x16,
            0xe9,0xb7,0x55,0x0b,0x88,0xd6,0x34,0x6a,0x2b,0x75,0x97,0xc9,0x4a,0x14,0xf6,0xa8,
            0x74,0x2a,0xc8,0x96,0x15,0x4b,0xa9,0xf7,0xb6,0xe8,0x0a,0x54,0xd7,0x89,0x6b,0x35
        };
        uint8_t crc = 0x77;
        for (int i = 0; i < 3; i++)
            crc = crc8_tab[(crc ^ hdr[i]) & 0xFF];
        hdr[3] = crc;
    }

    /* Append a valid RC push frame after the bogus header */
    uint8_t rc_payload[17] = {0};
    rc_payload[0] = 0x40; /* shutter */
    for (int i = 5; i < 17; i += 2) { rc_payload[i] = 0x00; rc_payload[i+1] = 0x04; }

    uint8_t frame[64];
    int frame_len = build_rc_push_frame(frame, sizeof(frame), rc_payload);
    ASSERT(frame_len > 0);

    uint8_t buf[128];
    memcpy(buf, hdr, 4);
    memcpy(buf + 4, frame, (size_t)frame_len);

    g_callback_count = 0;
    rcm_parser_t *p = rcm_create(test_callback, NULL);
    int n = rcm_feed(p, buf, (size_t)(4 + frame_len));
    /* Short-length header is skipped, real frame is decoded */
    ASSERT_EQ(n, 1);
    ASSERT_EQ(g_callback_count, 1);
    ASSERT(g_last_state.shutter == true);
    rcm_destroy(p);
}

/* ---- Zero-length and empty feed ---- */

TEST(test_feed_zero_length) {
    /* Feeding zero bytes should be a harmless no-op */
    rcm_parser_t *p = rcm_create(test_callback, NULL);
    g_callback_count = 0;
    uint8_t data[] = {0x55};
    ASSERT_EQ(rcm_feed(p, data, 0), 0);
    ASSERT_EQ(g_callback_count, 0);
    rcm_destroy(p);
}

/* ---- Garbage between valid frames ---- */

TEST(test_parser_garbage_between_frames) {
    /* Two valid frames with garbage bytes between them */
    uint8_t rc_payload[17] = {0};
    rc_payload[0] = 0x40; /* shutter */
    for (int i = 5; i < 17; i += 2) { rc_payload[i] = 0x00; rc_payload[i+1] = 0x04; }

    uint8_t frame[64];
    int frame_len = build_rc_push_frame(frame, sizeof(frame), rc_payload);
    ASSERT(frame_len > 0);

    /* [frame1][garbage][frame2] */
    uint8_t buf[256];
    int pos = 0;
    memcpy(buf + pos, frame, (size_t)frame_len); pos += frame_len;
    /* 10 bytes of garbage (no 0x55) */
    memset(buf + pos, 0xAA, 10); pos += 10;
    memcpy(buf + pos, frame, (size_t)frame_len); pos += frame_len;

    g_callback_count = 0;
    rcm_parser_t *p = rcm_create(test_callback, NULL);
    int n = rcm_feed(p, buf, (size_t)pos);
    ASSERT_EQ(n, 2);
    ASSERT_EQ(g_callback_count, 2);
    rcm_destroy(p);
}

/* ---- RC cmd_set with wrong cmd_id ---- */

TEST(test_parser_rc_cmd_set_wrong_cmd_id) {
    /* A valid DUML frame with cmd_set=0x06 but cmd_id=0x01 (channel request,
     * not push) should not trigger the callback */
    uint8_t buf[64];
    uint8_t payload[] = { 0xDE };
    int len = rcm_build_packet(buf, sizeof(buf),
                               DUML_DEV_RC, 0, DUML_DEV_APP, 0,
                               0x0001,
                               DUML_PACK_REQUEST, DUML_ACK_NO_ACK, 0,
                               DUML_CMD_SET_RC, DUML_CMD_RC_CHANNEL,
                               payload, 1);
    ASSERT(len > 0);

    g_callback_count = 0;
    rcm_parser_t *p = rcm_create(test_callback, NULL);
    int n = rcm_feed(p, buf, (size_t)len);
    ASSERT_EQ(n, 0);
    ASSERT_EQ(g_callback_count, 0);
    rcm_destroy(p);
}

/* ---- byte4 reserved bits isolation ---- */

TEST(test_byte4_reserved_bits) {
    /* Bits 0 and 7 of byte 4 are reserved. Setting them should not
     * affect the right_wheel_delta value. */
    uint8_t payload[17] = {0};
    for (int i = 5; i < 17; i += 2) { payload[i] = 0x00; payload[i+1] = 0x04; }

    /* Magnitude 5, positive, with reserved bits clear */
    payload[4] = (5 << 1) | (1 << 6);  /* 0x4A */
    rc_state_t s1;
    ASSERT_EQ(rcm_parse_payload(payload, 17, &s1), 0);
    ASSERT_EQ(s1.right_wheel_delta, 5);

    /* Same but with reserved bit 0 and bit 7 set */
    payload[4] = (5 << 1) | (1 << 6) | (1 << 0) | (1 << 7);  /* 0xCB */
    rc_state_t s2;
    ASSERT_EQ(rcm_parse_payload(payload, 17, &s2), 0);
    ASSERT_EQ(s2.right_wheel_delta, 5);

    /* All other fields should be identical */
    ASSERT_EQ(s1.pause, s2.pause);
    ASSERT_EQ(s1.flight_mode, s2.flight_mode);
    ASSERT_EQ(s1.stick_right.horizontal, s2.stick_right.horizontal);
}

/* ---- Build packet with exact buffer size ---- */

TEST(test_build_packet_exact_buffer) {
    /* Buffer exactly the right size should succeed */
    uint8_t buf[13]; /* exactly enough for a 0-payload packet */
    int len = rcm_build_packet(buf, sizeof(buf),
                               DUML_DEV_PC, 0, DUML_DEV_RC, 0,
                               0, DUML_PACK_REQUEST, DUML_ACK_NO_ACK, 0,
                               0x06, 0x01, NULL, 0);
    ASSERT_EQ(len, 13);
    ASSERT_EQ(buf[0], 0x55);

    /* One byte too small should fail */
    uint8_t buf2[12];
    int len2 = rcm_build_packet(buf2, sizeof(buf2),
                                DUML_DEV_PC, 0, DUML_DEV_RC, 0,
                                0, DUML_PACK_REQUEST, DUML_ACK_NO_ACK, 0,
                                0x06, 0x01, NULL, 0);
    ASSERT_EQ(len2, -1);
}

/* ---- Consecutive SOF bytes ---- */

TEST(test_parser_consecutive_sof) {
    /* Multiple 0x55 bytes where only the last one starts a valid frame.
     * Earlier 0x55 bytes with bad CRC8 should be skipped. */
    uint8_t rc_payload[17] = {0};
    rc_payload[0] = 0x10; /* pause */
    for (int i = 5; i < 17; i += 2) { rc_payload[i] = 0x00; rc_payload[i+1] = 0x04; }

    uint8_t frame[64];
    int frame_len = build_rc_push_frame(frame, sizeof(frame), rc_payload);
    ASSERT(frame_len > 0);

    /* Prepend 5 SOF bytes (each followed by non-matching data, so CRC8 fails) */
    uint8_t buf[128];
    int pos = 0;
    for (int i = 0; i < 5; i++) {
        buf[pos++] = 0x55;
    }
    memcpy(buf + pos, frame, (size_t)frame_len);
    pos += frame_len;

    g_callback_count = 0;
    rcm_parser_t *p = rcm_create(test_callback, NULL);
    int n = rcm_feed(p, buf, (size_t)pos);
    ASSERT_EQ(n, 1);
    ASSERT_EQ(g_callback_count, 1);
    ASSERT(g_last_state.pause == true);
    rcm_destroy(p);
}

/* ---- Build packet with max valid payload ---- */

TEST(test_build_packet_max_payload) {
    /* Max payload that still fits: DUML_MAX_FRAME_LEN - DUML_HEADER_LEN - DUML_FOOTER_LEN */
    size_t max_payload = DUML_MAX_FRAME_LEN - DUML_HEADER_LEN - DUML_FOOTER_LEN;
    uint8_t *buf = (uint8_t *)malloc(DUML_MAX_FRAME_LEN);
    uint8_t *payload = (uint8_t *)malloc(max_payload);
    ASSERT(buf != NULL && payload != NULL);
    memset(payload, 0x42, max_payload);

    int len = rcm_build_packet(buf, DUML_MAX_FRAME_LEN,
                               DUML_DEV_PC, 0, DUML_DEV_RC, 0,
                               0, DUML_PACK_REQUEST, DUML_ACK_NO_ACK, 0,
                               0x06, 0x01, payload, max_payload);
    ASSERT_EQ(len, DUML_MAX_FRAME_LEN);
    ASSERT_EQ(buf[0], 0x55);

    /* One more byte should fail */
    uint8_t *payload2 = (uint8_t *)malloc(max_payload + 1);
    ASSERT(payload2 != NULL);
    memset(payload2, 0x42, max_payload + 1);
    int len2 = rcm_build_packet(buf, DUML_MAX_FRAME_LEN + 1,
                                DUML_DEV_PC, 0, DUML_DEV_RC, 0,
                                0, DUML_PACK_REQUEST, DUML_ACK_NO_ACK, 0,
                                0x06, 0x01, payload2, max_payload + 1);
    ASSERT_EQ(len2, -1);

    free(buf);
    free(payload);
    free(payload2);
}

/* ---- Userdata passthrough ---- */

static void *g_last_userdata;
static void userdata_callback(const rc_state_t *state, void *userdata) {
    g_last_userdata = userdata;
    (void)state;
}

TEST(test_parser_userdata) {
    /* Verify userdata pointer is passed through to the callback */
    int sentinel = 42;
    g_last_userdata = NULL;

    rcm_parser_t *p = rcm_create(userdata_callback, &sentinel);

    uint8_t rc_payload[17] = {0};
    for (int i = 5; i < 17; i += 2) { rc_payload[i] = 0x00; rc_payload[i+1] = 0x04; }

    uint8_t frame[64];
    int frame_len = build_rc_push_frame(frame, sizeof(frame), rc_payload);
    ASSERT(frame_len > 0);

    rcm_feed(p, frame, (size_t)frame_len);
    ASSERT(g_last_userdata == &sentinel);
    rcm_destroy(p);
}

/* ---- Byte 0 lower nibble isolation ---- */

TEST(test_byte0_lower_nibble_ignored) {
    /* Bits 0-3 of byte 0 are reserved; setting them should not affect
     * any parsed fields */
    uint8_t payload[17] = {0};
    for (int i = 5; i < 17; i += 2) { payload[i] = 0x00; payload[i+1] = 0x04; }

    rc_state_t s1, s2;
    payload[0] = 0x00;
    ASSERT_EQ(rcm_parse_payload(payload, 17, &s1), 0);

    payload[0] = 0x0F; /* set all reserved lower nibble bits */
    ASSERT_EQ(rcm_parse_payload(payload, 17, &s2), 0);

    /* Button fields should be unaffected */
    ASSERT_EQ(s1.pause, s2.pause);
    ASSERT_EQ(s1.gohome, s2.gohome);
    ASSERT_EQ(s1.shutter, s2.shutter);
    ASSERT(!s2.pause && !s2.gohome && !s2.shutter);
}

/* ---- Reserved bit isolation tests ---- */

TEST(test_byte0_bit7_reserved) {
    /* Bit 7 of byte 0 is reserved per spec; setting it should not affect
     * any parsed fields (pause=bit4, gohome=bit5, shutter=bit6). */
    uint8_t payload[17] = {0};
    for (int i = 5; i < 17; i += 2) { payload[i] = 0x00; payload[i+1] = 0x04; }

    rc_state_t s1, s2;
    payload[0] = 0x00;
    ASSERT_EQ(rcm_parse_payload(payload, 17, &s1), 0);

    payload[0] = 0x80; /* set only bit 7 */
    ASSERT_EQ(rcm_parse_payload(payload, 17, &s2), 0);

    ASSERT_EQ(s1.pause, s2.pause);
    ASSERT_EQ(s1.gohome, s2.gohome);
    ASSERT_EQ(s1.shutter, s2.shutter);
    ASSERT(!s2.pause && !s2.gohome && !s2.shutter);
}

TEST(test_byte1_bits1_2_reserved) {
    /* Bits 1-2 of byte 1 are reserved; setting them should not affect
     * record (bit0) or 5D joystick (bits 3-7). */
    uint8_t payload[17] = {0};
    for (int i = 5; i < 17; i += 2) { payload[i] = 0x00; payload[i+1] = 0x04; }

    rc_state_t s1, s2;
    payload[1] = 0x00;
    ASSERT_EQ(rcm_parse_payload(payload, 17, &s1), 0);

    payload[1] = 0x06; /* set bits 1 and 2 only */
    ASSERT_EQ(rcm_parse_payload(payload, 17, &s2), 0);

    ASSERT_EQ(s1.record, s2.record);
    ASSERT_EQ(s1.five_d.right, s2.five_d.right);
    ASSERT_EQ(s1.five_d.up, s2.five_d.up);
    ASSERT_EQ(s1.five_d.down, s2.five_d.down);
    ASSERT_EQ(s1.five_d.left, s2.five_d.left);
    ASSERT_EQ(s1.five_d.center, s2.five_d.center);
    ASSERT(!s2.record);
    ASSERT(!s2.five_d.right && !s2.five_d.up && !s2.five_d.down &&
           !s2.five_d.left && !s2.five_d.center);
}

TEST(test_byte2_bits5_7_reserved) {
    /* Bits 5-7 of byte 2 are reserved; setting them should not affect
     * flight_mode (bits 0-1) or custom buttons (bits 2-4). */
    uint8_t payload[17] = {0};
    for (int i = 5; i < 17; i += 2) { payload[i] = 0x00; payload[i+1] = 0x04; }

    rc_state_t s1, s2;
    payload[2] = 0x00;
    ASSERT_EQ(rcm_parse_payload(payload, 17, &s1), 0);

    payload[2] = 0xE0; /* set bits 5, 6, 7 */
    ASSERT_EQ(rcm_parse_payload(payload, 17, &s2), 0);

    ASSERT_EQ(s1.flight_mode, s2.flight_mode);
    ASSERT_EQ(s1.custom1, s2.custom1);
    ASSERT_EQ(s1.custom2, s2.custom2);
    ASSERT_EQ(s1.custom3, s2.custom3);
}

/* ---- Parser: short RC push payload in valid frame ---- */

TEST(test_parser_rc_push_short_payload) {
    /* A valid DUML frame with cmd_set=0x06 and cmd_id=0x05 but only
     * 10 bytes of payload (< 17) should NOT trigger the callback. */
    uint8_t short_payload[10] = {0};
    uint8_t frame[64];
    int frame_len = rcm_build_packet(frame, sizeof(frame),
                                      DUML_DEV_RC, 0, DUML_DEV_APP, 0,
                                      0x0001,
                                      DUML_PACK_REQUEST, DUML_ACK_NO_ACK, 0,
                                      DUML_CMD_SET_RC, DUML_CMD_RC_PUSH,
                                      short_payload, sizeof(short_payload));
    ASSERT(frame_len > 0);

    g_callback_count = 0;
    rcm_parser_t *p = rcm_create(test_callback, NULL);
    int n = rcm_feed(p, frame, (size_t)frame_len);
    ASSERT_EQ(n, 0);
    ASSERT_EQ(g_callback_count, 0);
    rcm_destroy(p);
}

/* ---- Parser: minimum-length valid DUML frame ---- */

TEST(test_parser_min_frame) {
    /* A valid 13-byte DUML frame (0 payload) should parse without
     * invoking the callback (not an RC push packet). */
    uint8_t frame[64];
    int frame_len = rcm_build_packet(frame, sizeof(frame),
                                      DUML_DEV_PC, 0, DUML_DEV_FC, 0,
                                      0x0001,
                                      DUML_PACK_REQUEST, DUML_ACK_NO_ACK, 0,
                                      0x01, 0x01,
                                      NULL, 0);
    ASSERT_EQ(frame_len, 13);

    g_callback_count = 0;
    rcm_parser_t *p = rcm_create(test_callback, NULL);
    int n = rcm_feed(p, frame, (size_t)frame_len);
    ASSERT_EQ(n, 0);
    ASSERT_EQ(g_callback_count, 0);

    /* Feed a valid RC push frame after — parser should still work */
    uint8_t rc_payload[17] = {0};
    rc_payload[0] = 0x40; /* shutter */
    for (int i = 5; i < 17; i += 2) { rc_payload[i] = 0x00; rc_payload[i+1] = 0x04; }
    uint8_t rc_frame[64];
    int rc_len = build_rc_push_frame(rc_frame, sizeof(rc_frame), rc_payload);
    ASSERT(rc_len > 0);

    n = rcm_feed(p, rc_frame, (size_t)rc_len);
    ASSERT_EQ(n, 1);
    ASSERT_EQ(g_callback_count, 1);
    ASSERT(g_last_state.shutter == true);
    rcm_destroy(p);
}

/* ---- Parser: exact RC_PUSH_PAYLOAD_LEN boundary ---- */

TEST(test_parser_rc_push_exactly_17_payload) {
    /* Exactly 17 bytes payload with cmd_set=0x06 cmd_id=0x05 should work.
     * This is the normal case, but verify the boundary. */
    uint8_t rc_payload[17] = {0};
    rc_payload[0] = 0x20; /* gohome */
    for (int i = 5; i < 17; i += 2) { rc_payload[i] = 0x00; rc_payload[i+1] = 0x04; }

    uint8_t frame[64];
    int frame_len = rcm_build_packet(frame, sizeof(frame),
                                      DUML_DEV_RC, 0, DUML_DEV_APP, 0,
                                      0x0001,
                                      DUML_PACK_REQUEST, DUML_ACK_NO_ACK, 0,
                                      DUML_CMD_SET_RC, DUML_CMD_RC_PUSH,
                                      rc_payload, 17);
    ASSERT(frame_len == 30); /* 11 + 17 + 2 */

    g_callback_count = 0;
    rcm_parser_t *p = rcm_create(test_callback, NULL);
    int n = rcm_feed(p, frame, (size_t)frame_len);
    ASSERT_EQ(n, 1);
    ASSERT_EQ(g_callback_count, 1);
    ASSERT(g_last_state.gohome == true);
    rcm_destroy(p);
}

/* ---- Parser: RC push with extra trailing payload bytes ---- */

TEST(test_parser_rc_push_extra_payload) {
    /* RC push frame with 20 bytes payload (> 17). The parser should still
     * parse the first 17 bytes and invoke the callback. */
    uint8_t rc_payload[20] = {0};
    rc_payload[0] = 0x10; /* pause */
    for (int i = 5; i < 17; i += 2) { rc_payload[i] = 0x00; rc_payload[i+1] = 0x04; }
    rc_payload[17] = 0xFF; /* extra junk */
    rc_payload[18] = 0xFF;
    rc_payload[19] = 0xFF;

    uint8_t frame[64];
    int frame_len = rcm_build_packet(frame, sizeof(frame),
                                      DUML_DEV_RC, 0, DUML_DEV_APP, 0,
                                      0x0001,
                                      DUML_PACK_REQUEST, DUML_ACK_NO_ACK, 0,
                                      DUML_CMD_SET_RC, DUML_CMD_RC_PUSH,
                                      rc_payload, 20);
    ASSERT(frame_len == 33); /* 11 + 20 + 2 */

    g_callback_count = 0;
    rcm_parser_t *p = rcm_create(test_callback, NULL);
    int n = rcm_feed(p, frame, (size_t)frame_len);
    ASSERT_EQ(n, 1);
    ASSERT_EQ(g_callback_count, 1);
    ASSERT(g_last_state.pause == true);
    ASSERT_EQ(g_last_state.stick_right.horizontal, 0);
    rcm_destroy(p);
}

/* ---- Build packet: encrypt_type and pack_type fields ---- */

TEST(test_build_packet_cmd_type_fields) {
    /* Verify the cmd_type_data byte encodes all three sub-fields correctly */
    uint8_t buf[64];
    int len = rcm_build_packet(buf, sizeof(buf),
                               DUML_DEV_PC, 0, DUML_DEV_RC, 0,
                               0,
                               DUML_PACK_RESPONSE,  /* bit 7 = 1 */
                               DUML_ACK_AFTER_EXEC, /* bits 5-6 = 2 */
                               5,                   /* bits 0-2 = 5 */
                               0x06, 0x01, NULL, 0);
    ASSERT(len == 13);
    /* Expected: bit7=1(0x80), bits5-6=2(0x40), bits0-2=5(0x05) => 0xC5 */
    ASSERT_EQ(buf[8], 0xC5);
}

/* ---- Main ---- */

int main(void) {
    printf("RC Monitor Tests:\n");

    /* Payload parser */
    RUN(test_all_zeros);
    RUN(test_sticks_centered);
    RUN(test_sticks_full_deflection);
    RUN(test_buttons_individual);
    RUN(test_five_d_joystick);
    RUN(test_flight_mode_switch);
    RUN(test_right_new_wheel);
    RUN(test_all_buttons_pressed);
    RUN(test_payload_too_short);
    RUN(test_payload_longer_ok);

    /* DUML parser */
    RUN(test_parser_create_destroy);
    RUN(test_parser_valid_frame);
    RUN(test_parser_garbage_prefix);
    RUN(test_parser_split_frame);
    RUN(test_parser_multiple_frames);
    RUN(test_parser_bad_crc16);
    RUN(test_parser_bad_crc8);
    RUN(test_parser_frame_too_short);
    RUN(test_parser_frame_too_long);
    RUN(test_parser_ring_buffer_overflow);
    RUN(test_parser_reset);
    RUN(test_parser_interleaved_non_rc_frame);

    /* Payload edge cases */
    RUN(test_payload_null_out);
    RUN(test_sticks_extreme_values);

    /* Utility */
    RUN(test_flight_mode_str);

    /* Packet builder */
    RUN(test_build_packet_basic);
    RUN(test_build_packet_with_payload);
    RUN(test_build_enable_cmd);
    RUN(test_build_channel_request);
    RUN(test_build_roundtrip);
    RUN(test_build_packet_buffer_too_small);
    RUN(test_build_packet_null_output);
    RUN(test_build_packet_null_payload_with_length);
    RUN(test_build_packet_payload_overflow);

    /* NULL safety */
    RUN(test_feed_null_parser);
    RUN(test_feed_null_data);
    RUN(test_destroy_null);
    RUN(test_reset_null);

    /* Additional parser tests */
    RUN(test_parser_byte_at_a_time);
    RUN(test_parser_short_length_valid_crc8);

    /* Additional payload tests */
    RUN(test_payload_byte3_ignored);
    RUN(test_right_wheel_delta_sign_with_zero_mag);
    RUN(test_flight_mode_str_out_of_range);

    /* Round-trip */
    RUN(test_build_rc_push_roundtrip);

    /* New: edge cases and coverage gaps */
    RUN(test_feed_zero_length);
    RUN(test_parser_garbage_between_frames);
    RUN(test_parser_rc_cmd_set_wrong_cmd_id);
    RUN(test_byte4_reserved_bits);
    RUN(test_build_packet_exact_buffer);
    RUN(test_parser_consecutive_sof);
    RUN(test_build_packet_max_payload);
    RUN(test_parser_userdata);
    RUN(test_byte0_lower_nibble_ignored);

    /* Reserved bit isolation */
    RUN(test_byte0_bit7_reserved);
    RUN(test_byte1_bits1_2_reserved);
    RUN(test_byte2_bits5_7_reserved);

    /* Parser edge cases */
    RUN(test_parser_rc_push_short_payload);
    RUN(test_parser_min_frame);
    RUN(test_parser_rc_push_exactly_17_payload);
    RUN(test_parser_rc_push_extra_payload);

    /* Packet builder field encoding */
    RUN(test_build_packet_cmd_type_fields);

    printf("\nAll tests passed.\n");
    return 0;
}
