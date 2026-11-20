#define _POSIX_C_SOURCE 199309L
#include "../src/door.h"
#include "../src/tile.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Deterministic LCG so the two grid sizes use comparable workloads. */
static unsigned long rng_state = 0x12345678ul;
static unsigned long xrnd(void) {
    rng_state = rng_state * 1103515245ul + 12345ul;
    return rng_state;
}

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

/* permanent solid: idx % 3 == 0; door: even idx among the rest. */
static int is_perm(int idx) { return idx % 3 == 0; }
static int is_door_cell(int idx) { return idx % 3 != 0 && idx % 2 == 0; }

static void build_legacy(unsigned char* legacy, int n) {
    for (int i = 0; i < n; ++i) legacy[i] = is_perm(i) ? 1 : 0;
}

/* closed-door reference: permanent solids plus every door cell marked solid */
static void build_legacy_closed(unsigned char* ref, int n) {
    for (int i = 0; i < n; ++i) ref[i] = (is_perm(i) || is_door_cell(i)) ? 1 : 0;
}

static int run_size(int side, int iters) {
    int w = side, h = side, n = w * h;
    unsigned char* door_map = (unsigned char*)calloc((size_t)n, 1);
    unsigned char* legacy = (unsigned char*)malloc((size_t)n);
    unsigned char* legacy_closed = (unsigned char*)malloc((size_t)n);
    build_legacy(legacy, n);
    build_legacy_closed(legacy_closed, n);

    DoorGrid g;
    if (doorgrid_init(&g, door_map, w, h) != DOOR_OK) { printf("init failed\n"); return 1; }
    int n_doors = 0;
    for (int i = 0; i < n; ++i) {
        if (is_perm(i)) { door_map[i] = 1; continue; }
        if (is_door_cell(i)) {
            doorgrid_set_door(&g, i % w, i / w); /* starts closed -> map=1 */
            ++n_doors;
        }
    }

    /* ---- metric 1: hit difference ----
     * one 8x8 AABB probe pinned on every cell. Closed doors must match the
     * "all solids" reference exactly; open doors must match the permanent-
     * solid-only reference. flip = probes whose hit answer changes when the
     * doors open, i.e. the gameplay-visible hit difference. */
    int hit_diff_closed = 0, hit_diff_open = 0, hit_flip = 0;
    for (int ty = 0; ty < h; ++ty) {
        for (int tx = 0; tx < w; ++tx) {
            float px = (float)(tx * 8), py = (float)(ty * 8);
            int r_door = aabb_vs_tiles(door_map, w, h, px, py, 8, 8, NULL, NULL);
            int r_closed = aabb_vs_tiles(legacy_closed, w, h, px, py, 8, 8, NULL, NULL);
            if (r_door != r_closed) ++hit_diff_closed;
        }
    }

    /* open every door in frame 0 via one batch, then probe again */
    DoorSwitch* all = (DoorSwitch*)malloc(sizeof(DoorSwitch) * (size_t)n_doors);
    int k = 0;
    for (int i = 0; i < n; ++i)
        if (is_door_cell(i)) { all[k].tx = i % w; all[k].ty = i / w; all[k].state = DOOR_OPEN; ++k; }
    if (doorgrid_toggle_batch(&g, all, n_doors, 0) != DOOR_OK) { printf("open batch failed\n"); return 1; }
    for (int ty = 0; ty < h; ++ty)
        for (int tx = 0; tx < w; ++tx) {
            float px = (float)(tx * 8), py = (float)(ty * 8);
            int r_open = aabb_vs_tiles(door_map, w, h, px, py, 8, 8, NULL, NULL);
            int r_perm = aabb_vs_tiles(legacy, w, h, px, py, 8, 8, NULL, NULL);
            int r_closed = aabb_vs_tiles(legacy_closed, w, h, px, py, 8, 8, NULL, NULL);
            if (r_open != r_perm) ++hit_diff_open;
            if (r_open != r_closed) ++hit_flip;
        }

    /* ---- metric 2: rollback error (must be 0 bytes of drift) ---- */
    DoorSnapshot base;
    if (doorgrid_snapshot(&g, &base) != DOOR_OK) { printf("snapshot failed\n"); return 1; }
    int accepted = 0, rejected = 0;
    DoorSwitch bad1 = {-1, 0, DOOR_OPEN};
    DoorSwitch bad2 = {0, 0, DOOR_CLOSED}; /* (0,0) is permanent solid, not a door */
    for (int it = 0; it < iters; ++it) {
        for (int j = 0; j < n_doors; ++j)
            all[j].state = (it & 1) ? DOOR_CLOSED : DOOR_OPEN;
        int frame = it + 1;
        if (doorgrid_toggle_batch(&g, all, n_doors, frame) == DOOR_OK) ++accepted;
        /* invalid batches must leave state untouched */
        if (doorgrid_toggle_batch(&g, &bad1, 1, frame) == DOOR_E_BOUNDS) ++rejected;
        if (doorgrid_toggle_batch(&g, &bad2, 1, frame) == DOOR_E_NOT_DOOR) ++rejected;
    }
    int rc_restore = doorgrid_restore(&g, &base);
    int rollback_err = (int)(memcmp(g.map, base.map, (size_t)n) +
                             memcmp(g.state, base.state, (size_t)n));
    /* internal-checkpoint rollback: latest accepted batch's checkpoint vs its
     * pre-image; roll back twice and confirm idempotence. */
    DoorSwitch* flip = (DoorSwitch*)malloc(sizeof(DoorSwitch) * (size_t)n_doors);
    memcpy(flip, all, sizeof(DoorSwitch) * (size_t)n_doors);
    for (int j = 0; j < n_doors; ++j) flip[j].state = DOOR_CLOSED;
    DoorSnapshot pre;
    doorgrid_snapshot(&g, &pre);
    doorgrid_toggle_batch(&g, flip, n_doors, 90000);
    doorgrid_rollback_last(&g);
    int rb_last_err = (int)(memcmp(g.map, pre.map, (size_t)n) +
                            memcmp(g.state, pre.state, (size_t)n) +
                            memcmp(g.frame_toggled, pre.frame_toggled, (size_t)n * sizeof(int)));
    doorgrid_rollback_last(&g);
    int rb_twice_err = (int)(memcmp(g.map, pre.map, (size_t)n) +
                             memcmp(g.state, pre.state, (size_t)n));

    /* ---- metric 3: switch timing (all-door batches, warm + measured) ---- */
    for (int j = 0; j < n_doors; ++j) flip[j].state = DOOR_OPEN;
    doorgrid_toggle_batch(&g, flip, n_doors, 90001); /* warm */
    double t0 = now_sec();
    int frames = 2000;
    for (int f = 0; f < frames; ++f) {
        for (int j = 0; j < n_doors; ++j) flip[j].state = (f & 1) ? DOOR_CLOSED : DOOR_OPEN;
        if (doorgrid_toggle_batch(&g, flip, n_doors, 100000 + f) != DOOR_OK) {
            printf("timing batch failed\n"); return 1;
        }
    }
    double elapsed = now_sec() - t0;

    /* ---- order independence under shuffled order ---- */
    DoorSwitch* shuf = (DoorSwitch*)malloc(sizeof(DoorSwitch) * (size_t)n_doors);
    memcpy(shuf, flip, sizeof(DoorSwitch) * (size_t)n_doors);
    for (int j = n_doors - 1; j > 0; --j) {
        int p = (int)(xrnd() % (unsigned)(j + 1));
        DoorSwitch tmp = shuf[j]; shuf[j] = shuf[p]; shuf[p] = tmp;
    }
    unsigned char* ref_map = (unsigned char*)malloc((size_t)n);
    DoorGrid ref;
    doorgrid_init(&ref, ref_map, w, h);
    for (int i = 0; i < n; ++i) {
        if (is_perm(i)) ref_map[i] = 1;
        else if (is_door_cell(i)) doorgrid_set_door(&ref, i % w, i / w);
    }
    doorgrid_toggle_batch(&ref, flip, n_doors, 500);
    doorgrid_toggle_batch(&g, shuf, n_doors, 200000);
    int order_drift = (int)(memcmp(ref.state, g.state, (size_t)n) +
                            memcmp(ref_map, door_map, (size_t)n));
    doorgrid_free(&ref);

    printf("N=%d (%dx%d) doors=%d\n", n, w, h, n_doors);
    printf("  hit_diff_closed_vs_solid_ref=%d  hit_diff_open_vs_perm_ref=%d  hit_flip_on_open=%d\n",
           hit_diff_closed, hit_diff_open, hit_flip);
    printf("  rollback_restore_rc=%d rollback_byte_drift=%d (accepted=%d rejected=%d)\n",
           rc_restore, rollback_err, accepted, rejected);
    printf("  rollback_last_drift=%d rollback_twice_drift=%d\n", rb_last_err, rb_twice_err);
    printf("  batch_switch: %d batches x %d doors in %.6f s -> %.1f ns/batch, %.2f ns/door\n",
           frames, n_doors, elapsed, elapsed * 1e9 / frames,
           elapsed * 1e9 / (frames * n_doors));
    printf("  order_independence_drift=%d\n", order_drift);

    free(flip); free(shuf); free(ref_map); free(all);
    doorgrid_snapshot_free(&base);
    doorgrid_snapshot_free(&pre);
    doorgrid_free(&g);
    free(door_map); free(legacy); free(legacy_closed);
    return 0;
}

int main(void) {
    run_size(8, 50);    /* 64 cells */
    run_size(16, 50);   /* 256 cells */
    return 0;
}
