/*
 * gen_corpus.c - Seed corpus generator for libFuzzer harnesses
 *
 * Generates two directories of seed files:
 *   corpus_feed/    - Full DUML frames for fuzz_feed
 *   corpus_payload/ - Raw 17-byte payloads for fuzz_payload
 *
 * Usage: ./gen_corpus <feed_dir> <payload_dir>
 */

#include "rc_monitor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

static int g_feed_count;
static int g_payload_count;

static void write_file(const char *dir, int *counter, const uint8_t *data, size_t len) {
    char path[512];
    snprintf(path, sizeof(path), "%s/seed_%03d", dir, (*counter)++);
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "Error: cannot write %s: %s\n", path, strerror(errno));
        exit(1);
    }
    fwrite(data, 1, len, f);
    fclose(f);
}

/* Build an RC push frame from a 17-byte payload */
static int build_rc_push(uint8_t *out, size_t out_size, const uint8_t *payload) {
    return rcm_build_packet(out, out_size,
                            DUML_DEV_RC, 0,
                            DUML_DEV_APP, 0,
                            0x0001,
                            DUML_PACK_REQUEST,
                            DUML_ACK_NO_ACK, 0,
                            DUML_CMD_SET_RC,
                            DUML_CMD_RC_PUSH,
                            payload, RC_PUSH_PAYLOAD_LEN);
}

/* Build a non-RC frame with arbitrary cmd_set/cmd_id/payload */
static int build_generic(uint8_t *out, size_t out_size,
                         uint8_t cmd_set, uint8_t cmd_id,
                         const uint8_t *payload, size_t payload_len) {
    return rcm_build_packet(out, out_size,
                            DUML_DEV_PC, 0,
                            DUML_DEV_FC, 0,
                            0x0042,
                            DUML_PACK_REQUEST,
                            DUML_ACK_NO_ACK, 0,
                            cmd_set, cmd_id,
                            payload, payload_len);
}

/* Helper: set all stick bytes to a uint16 LE value */
static void set_all_sticks(uint8_t payload[RC_PUSH_PAYLOAD_LEN], uint16_t val) {
    for (int i = 5; i < 17; i += 2) {
        payload[i]     = (uint8_t)(val & 0xFF);
        payload[i + 1] = (uint8_t)(val >> 8);
    }
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <feed_dir> <payload_dir>\n", argv[0]);
        return 1;
    }

    const char *feed_dir = argv[1];
    const char *payload_dir = argv[2];

    /* Create output directories */
    mkdir(feed_dir, 0755);
    mkdir(payload_dir, 0755);

    uint8_t payload[RC_PUSH_PAYLOAD_LEN];
    uint8_t frame[1500];
    int flen;

    /* --- Payload seeds (also used for feed seeds) --- */

    /* 1. All-zero payload */
    memset(payload, 0, RC_PUSH_PAYLOAD_LEN);
    write_file(payload_dir, &g_payload_count, payload, RC_PUSH_PAYLOAD_LEN);
    flen = build_rc_push(frame, sizeof(frame), payload);
    if (flen > 0) write_file(feed_dir, &g_feed_count, frame, (size_t)flen);

    /* 2. All-0xFF payload */
    memset(payload, 0xFF, RC_PUSH_PAYLOAD_LEN);
    write_file(payload_dir, &g_payload_count, payload, RC_PUSH_PAYLOAD_LEN);
    flen = build_rc_push(frame, sizeof(frame), payload);
    if (flen > 0) write_file(feed_dir, &g_feed_count, frame, (size_t)flen);

    /* 3. Sticks centered (0x0400) */
    memset(payload, 0, RC_PUSH_PAYLOAD_LEN);
    set_all_sticks(payload, 0x0400);
    write_file(payload_dir, &g_payload_count, payload, RC_PUSH_PAYLOAD_LEN);
    flen = build_rc_push(frame, sizeof(frame), payload);
    if (flen > 0) write_file(feed_dir, &g_feed_count, frame, (size_t)flen);

    /* 4. Sticks at 0x0000 (min) */
    memset(payload, 0, RC_PUSH_PAYLOAD_LEN);
    set_all_sticks(payload, 0x0000);
    write_file(payload_dir, &g_payload_count, payload, RC_PUSH_PAYLOAD_LEN);
    flen = build_rc_push(frame, sizeof(frame), payload);
    if (flen > 0) write_file(feed_dir, &g_feed_count, frame, (size_t)flen);

    /* 5. Sticks at 0x07FF (max) */
    memset(payload, 0, RC_PUSH_PAYLOAD_LEN);
    set_all_sticks(payload, 0x07FF);
    write_file(payload_dir, &g_payload_count, payload, RC_PUSH_PAYLOAD_LEN);
    flen = build_rc_push(frame, sizeof(frame), payload);
    if (flen > 0) write_file(feed_dir, &g_feed_count, frame, (size_t)flen);

    /* 6. Sticks at 0xFFFF (overflow) */
    memset(payload, 0, RC_PUSH_PAYLOAD_LEN);
    set_all_sticks(payload, 0xFFFF);
    write_file(payload_dir, &g_payload_count, payload, RC_PUSH_PAYLOAD_LEN);
    flen = build_rc_push(frame, sizeof(frame), payload);
    if (flen > 0) write_file(feed_dir, &g_feed_count, frame, (size_t)flen);

    /* 7. All buttons pressed: bytes 0-2 = {0x70, 0xF9, 0x1D} */
    memset(payload, 0, RC_PUSH_PAYLOAD_LEN);
    set_all_sticks(payload, 0x0400);
    payload[0] = 0x70;
    payload[1] = 0xF9;
    payload[2] = 0x1D;
    write_file(payload_dir, &g_payload_count, payload, RC_PUSH_PAYLOAD_LEN);
    flen = build_rc_push(frame, sizeof(frame), payload);
    if (flen > 0) write_file(feed_dir, &g_feed_count, frame, (size_t)flen);

    /* 8. Individual button (pause only) */
    memset(payload, 0, RC_PUSH_PAYLOAD_LEN);
    set_all_sticks(payload, 0x0400);
    payload[0] = 0x10;
    write_file(payload_dir, &g_payload_count, payload, RC_PUSH_PAYLOAD_LEN);
    flen = build_rc_push(frame, sizeof(frame), payload);
    if (flen > 0) write_file(feed_dir, &g_feed_count, frame, (size_t)flen);

    /* 9. All 5D directions: byte 1 = 0xF8 */
    memset(payload, 0, RC_PUSH_PAYLOAD_LEN);
    set_all_sticks(payload, 0x0400);
    payload[1] = 0xF8;
    write_file(payload_dir, &g_payload_count, payload, RC_PUSH_PAYLOAD_LEN);
    flen = build_rc_push(frame, sizeof(frame), payload);
    if (flen > 0) write_file(feed_dir, &g_feed_count, frame, (size_t)flen);

    /* 10-13. Flight modes 0-3 */
    for (int mode = 0; mode <= 3; mode++) {
        memset(payload, 0, RC_PUSH_PAYLOAD_LEN);
        set_all_sticks(payload, 0x0400);
        payload[2] = (uint8_t)(mode & 0x03);
        flen = build_rc_push(frame, sizeof(frame), payload);
        if (flen > 0) write_file(feed_dir, &g_feed_count, frame, (size_t)flen);
    }

    /* 14. Wheel delta max positive: magnitude=31, sign=1 => (31<<1)|(1<<6) = 0x7E */
    memset(payload, 0, RC_PUSH_PAYLOAD_LEN);
    set_all_sticks(payload, 0x0400);
    payload[4] = 0x7E;
    flen = build_rc_push(frame, sizeof(frame), payload);
    if (flen > 0) write_file(feed_dir, &g_feed_count, frame, (size_t)flen);

    /* 15. Wheel delta max negative: magnitude=31, sign=0 => (31<<1) = 0x3E */
    memset(payload, 0, RC_PUSH_PAYLOAD_LEN);
    set_all_sticks(payload, 0x0400);
    payload[4] = 0x3E;
    flen = build_rc_push(frame, sizeof(frame), payload);
    if (flen > 0) write_file(feed_dir, &g_feed_count, frame, (size_t)flen);

    /* 16. Wheel delta zero with sign bit set: (1<<6) = 0x40 */
    memset(payload, 0, RC_PUSH_PAYLOAD_LEN);
    set_all_sticks(payload, 0x0400);
    payload[4] = 0x40;
    flen = build_rc_push(frame, sizeof(frame), payload);
    if (flen > 0) write_file(feed_dir, &g_feed_count, frame, (size_t)flen);

    /* 17. Reserved bits in byte 4: bits 0 and 7 set = 0x81 */
    memset(payload, 0, RC_PUSH_PAYLOAD_LEN);
    set_all_sticks(payload, 0x0400);
    payload[4] = 0x81;
    flen = build_rc_push(frame, sizeof(frame), payload);
    if (flen > 0) write_file(feed_dir, &g_feed_count, frame, (size_t)flen);

    /* 18. Byte 3 nonzero (unused byte = 0xFF) */
    memset(payload, 0, RC_PUSH_PAYLOAD_LEN);
    set_all_sticks(payload, 0x0400);
    payload[3] = 0xFF;
    flen = build_rc_push(frame, sizeof(frame), payload);
    if (flen > 0) write_file(feed_dir, &g_feed_count, frame, (size_t)flen);

    /* --- Feed-only seeds (non-payload frames) --- */

    /* 19. Enable command frame */
    uint8_t enable[64];
    int elen = rcm_build_enable_cmd(enable, sizeof(enable), 0x0010);
    if (elen > 0) write_file(feed_dir, &g_feed_count, enable, (size_t)elen);

    /* 20. Channel request frame */
    uint8_t chan[64];
    int clen = rcm_build_channel_request(chan, sizeof(chan), 0x0020);
    if (clen > 0) write_file(feed_dir, &g_feed_count, chan, (size_t)clen);

    /* 21. Minimum frame (13 bytes) - non-RC, no payload */
    uint8_t minframe[64];
    int mlen = build_generic(minframe, sizeof(minframe), 0x00, 0x00, NULL, 0);
    if (mlen > 0) write_file(feed_dir, &g_feed_count, minframe, (size_t)mlen);

    /* 22. Non-RC command set (cmd_set=0x01, cmd_id=0x01, 4-byte payload) */
    {
        uint8_t nonrc_payload[4] = {0x01, 0x02, 0x03, 0x04};
        uint8_t nonrc[64];
        int nlen = build_generic(nonrc, sizeof(nonrc), 0x01, 0x01, nonrc_payload, 4);
        if (nlen > 0) write_file(feed_dir, &g_feed_count, nonrc, (size_t)nlen);
    }

    /* 23. Two concatenated RC push frames */
    {
        memset(payload, 0, RC_PUSH_PAYLOAD_LEN);
        set_all_sticks(payload, 0x0400);
        int f1 = build_rc_push(frame, sizeof(frame), payload);
        if (f1 > 0) {
            payload[0] = 0x10; /* pause pressed in second frame */
            int f2 = build_rc_push(frame + f1, sizeof(frame) - (size_t)f1, payload);
            if (f2 > 0) write_file(feed_dir, &g_feed_count, frame, (size_t)(f1 + f2));
        }
    }

    /* 24. Three concatenated frames (enable + push + channel) */
    {
        uint8_t multi[256];
        size_t off = 0;
        int l1 = rcm_build_enable_cmd(multi, sizeof(multi), 1);
        if (l1 > 0) off += (size_t)l1;
        memset(payload, 0, RC_PUSH_PAYLOAD_LEN);
        set_all_sticks(payload, 0x0400);
        int l2 = build_rc_push(multi + off, sizeof(multi) - off, payload);
        if (l2 > 0) off += (size_t)l2;
        int l3 = rcm_build_channel_request(multi + off, sizeof(multi) - off, 2);
        if (l3 > 0) off += (size_t)l3;
        if (off > 0) write_file(feed_dir, &g_feed_count, multi, off);
    }

    /* 25. Garbage prefix (5 junk bytes) + valid frame */
    {
        uint8_t garb[256];
        garb[0] = 0xDE; garb[1] = 0xAD; garb[2] = 0xBE;
        garb[3] = 0xEF; garb[4] = 0x42;
        memset(payload, 0, RC_PUSH_PAYLOAD_LEN);
        set_all_sticks(payload, 0x0400);
        int fl = build_rc_push(garb + 5, sizeof(garb) - 5, payload);
        if (fl > 0) write_file(feed_dir, &g_feed_count, garb, (size_t)(5 + fl));
    }

    /* 26. Garbage between frames (frame + 10x 0xAA + frame) */
    {
        uint8_t between[256];
        size_t off = 0;
        memset(payload, 0, RC_PUSH_PAYLOAD_LEN);
        set_all_sticks(payload, 0x0400);
        int f1 = build_rc_push(between, sizeof(between), payload);
        if (f1 > 0) {
            off = (size_t)f1;
            memset(between + off, 0xAA, 10);
            off += 10;
            payload[0] = 0x20; /* gohome in second frame */
            int f2 = build_rc_push(between + off, sizeof(between) - off, payload);
            if (f2 > 0) write_file(feed_dir, &g_feed_count, between, off + (size_t)f2);
        }
    }

    /* 27. Mixed frame types (enable + RC push + channel request) */
    {
        uint8_t mixed[256];
        size_t off = 0;
        int l1 = rcm_build_enable_cmd(mixed, sizeof(mixed), 0x100);
        if (l1 > 0) off += (size_t)l1;
        memset(payload, 0, RC_PUSH_PAYLOAD_LEN);
        set_all_sticks(payload, 0x0400);
        payload[1] = 0xF8; /* all 5D */
        int l2 = build_rc_push(mixed + off, sizeof(mixed) - off, payload);
        if (l2 > 0) off += (size_t)l2;
        int l3 = rcm_build_channel_request(mixed + off, sizeof(mixed) - off, 0x101);
        if (l3 > 0) off += (size_t)l3;
        if (off > 0) write_file(feed_dir, &g_feed_count, mixed, off);
    }

    /* 28. Consecutive SOF bytes (5x 0x55) + valid frame */
    {
        uint8_t sofs[256];
        memset(sofs, DUML_SOF, 5);
        memset(payload, 0, RC_PUSH_PAYLOAD_LEN);
        set_all_sticks(payload, 0x0400);
        int fl = build_rc_push(sofs + 5, sizeof(sofs) - 5, payload);
        if (fl > 0) write_file(feed_dir, &g_feed_count, sofs, (size_t)(5 + fl));
    }

    /* 29. Short RC payload (10 bytes — should be rejected) */
    {
        uint8_t short_payload[10] = {0};
        uint8_t short_frame[64];
        int sl = rcm_build_packet(short_frame, sizeof(short_frame),
                                  DUML_DEV_RC, 0, DUML_DEV_APP, 0,
                                  0x0005, DUML_PACK_REQUEST, DUML_ACK_NO_ACK, 0,
                                  DUML_CMD_SET_RC, DUML_CMD_RC_PUSH,
                                  short_payload, 10);
        if (sl > 0) write_file(feed_dir, &g_feed_count, short_frame, (size_t)sl);
    }

    /* 30. Large payload (100-byte non-RC frame) */
    {
        uint8_t large_payload[100];
        memset(large_payload, 0x42, sizeof(large_payload));
        uint8_t large_frame[256];
        int ll = build_generic(large_frame, sizeof(large_frame), 0x02, 0x03,
                               large_payload, sizeof(large_payload));
        if (ll > 0) write_file(feed_dir, &g_feed_count, large_frame, (size_t)ll);
    }

    printf("Generated %d feed seeds in %s\n", g_feed_count, feed_dir);
    printf("Generated %d payload seeds in %s\n", g_payload_count, payload_dir);
    return 0;
}
