/*
 * verify_recording.c - Feed a recorded .bin file back through rcm_feed()
 * and report how many valid RC push frames are decoded.
 *
 * Usage: ./verify_recording <recording.bin>
 */
#include <stdio.h>
#include <stdlib.h>
#include "rc_monitor.h"

static int g_count;
static rc_state_t g_last;

static void cb(const rc_state_t *s, void *ud) {
    (void)ud;
    g_count++;
    g_last = *s;
    printf("  Frame %3d: L.stick=(%+4d,%+4d) R.stick=(%+4d,%+4d) "
           "mode=%s pause=%d gohome=%d shutter=%d record=%d "
           "L.whl=%+4d R.whl=%+4d delta=%+2d\n",
           g_count,
           s->stick_left.horizontal, s->stick_left.vertical,
           s->stick_right.horizontal, s->stick_right.vertical,
           rcm_flight_mode_str(s->flight_mode),
           s->pause, s->gohome, s->shutter, s->record,
           s->left_wheel, s->right_wheel, s->right_wheel_delta);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <recording.bin>\n", argv[0]);
        return 1;
    }

    FILE *fp = fopen(argv[1], "rb");
    if (!fp) { perror("fopen"); return 1; }

    rcm_parser_t *p = rcm_create(cb, NULL);
    if (!p) { fprintf(stderr, "rcm_create failed\n"); fclose(fp); return 1; }

    uint8_t buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
        rcm_feed(p, buf, n);

    printf("\nDecoded %d RC push frames from %s\n", g_count, argv[1]);

    rcm_destroy(p);
    fclose(fp);
    return 0;
}
