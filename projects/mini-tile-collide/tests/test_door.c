#include "../src/door.h"
#include "../src/tile.h"
#include <stdio.h>
#include <string.h>

static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

/* 4x4 table from docs/door-state-table.md: A=(1,1), B=(2,2) */
#define W 4
#define H 4
#define A_IDX (1 * W + 1)
#define B_IDX (2 * W + 2)

static int mark(const unsigned char* m, int idx) { return m[idx] != 0; }

int main(void) {
    unsigned char map[W * H];
    DoorGrid g;

    /* ---- row 0: init + register, doors start closed and solid ---- */
    memset(map, 0, sizeof(map));
    CHECK(doorgrid_init(&g, map, W, H) == DOOR_OK);
    CHECK(doorgrid_set_door(&g, 1, 1) == DOOR_OK);
    CHECK(doorgrid_set_door(&g, 2, 2) == DOOR_OK);
    CHECK(mark(map, A_IDX) && mark(map, B_IDX));
    CHECK(g.state[A_IDX] == DOOR_CLOSED && g.state[B_IDX] == DOOR_CLOSED);

    /* stage-1 single apply flips the solid marker */
    CHECK(doorgrid_apply(&g, 1, 1, DOOR_OPEN) == DOOR_OK);
    CHECK(!mark(map, A_IDX) && g.state[A_IDX] == DOOR_OPEN);
    CHECK(doorgrid_apply(&g, 1, 1, DOOR_CLOSED) == DOOR_OK);
    CHECK(mark(map, A_IDX) && g.state[A_IDX] == DOOR_CLOSED);

    /* single-apply error paths: nothing mutates on failure */
    CHECK(doorgrid_apply(&g, 9, 1, DOOR_OPEN) == DOOR_E_BOUNDS);
    CHECK(doorgrid_apply(&g, 0, 0, DOOR_OPEN) == DOOR_E_NOT_DOOR);
    CHECK(doorgrid_apply(&g, 1, 1, 7) == DOOR_E_STATE);
    CHECK(mark(map, A_IDX) && g.state[A_IDX] == DOOR_CLOSED);

    /* rollback with no checkpoint must error, not silently pass */
    CHECK(doorgrid_rollback_last(&g) == DOOR_E_NO_SNAPSHOT);

    /* ---- row 1: batch f0 opens A ---- */
    DoorSwitch r1[] = {{1, 1, DOOR_OPEN}};
    CHECK(doorgrid_toggle_batch(&g, r1, 1, 0) == DOOR_OK);
    CHECK(g.state[A_IDX] == DOOR_OPEN && !mark(map, A_IDX));
    CHECK(g.state[B_IDX] == DOOR_CLOSED && mark(map, B_IDX));
    CHECK(g.frame_toggled[A_IDX] == 0);

    /* ---- row 2: same cell again in frame 0 -> -6, state untouched ---- */
    CHECK(doorgrid_toggle_batch(&g, r1, 1, 0) == DOOR_E_DUP_FRAME);
    CHECK(g.state[A_IDX] == DOOR_OPEN && !mark(map, A_IDX));

    /* ---- row 3: same cell twice inside one batch -> -5 ---- */
    DoorSwitch r3[] = {{1, 1, DOOR_OPEN}, {1, 1, DOOR_CLOSED}};
    CHECK(doorgrid_toggle_batch(&g, r3, 2, 1) == DOOR_E_DUP_BATCH);
    CHECK(g.state[A_IDX] == DOOR_OPEN);

    /* ---- row 4/5: out of bounds (negative and past edge) -> -2 ---- */
    DoorSwitch r4[] = {{-1, 0, DOOR_OPEN}};
    DoorSwitch r5[] = {{W, 0, DOOR_OPEN}};
    CHECK(doorgrid_toggle_batch(&g, r4, 1, 1) == DOOR_E_BOUNDS);
    CHECK(doorgrid_toggle_batch(&g, r5, 1, 1) == DOOR_E_BOUNDS);

    /* ---- row 6: illegal state -> -3 ---- */
    DoorSwitch r6[] = {{1, 1, 9}};
    CHECK(doorgrid_toggle_batch(&g, r6, 1, 1) == DOOR_E_STATE);

    /* ---- row 7: non-door cell -> -4 ---- */
    DoorSwitch r7[] = {{0, 0, DOOR_OPEN}};
    CHECK(doorgrid_toggle_batch(&g, r7, 1, 1) == DOOR_E_NOT_DOOR);
    CHECK(!mark(map, 0));

    /* bad frame / empty batch / null args */
    CHECK(doorgrid_toggle_batch(&g, r1, 1, -1) == DOOR_E_FRAME);
    CHECK(doorgrid_toggle_batch(&g, r1, 0, 1) == DOOR_E_ARG);
    CHECK(doorgrid_toggle_batch(&g, NULL, 1, 1) == DOOR_E_ARG);

    /* state still exactly the row-1 result: A open, B closed */
    CHECK(g.state[A_IDX] == DOOR_OPEN && !mark(map, A_IDX));
    CHECK(g.state[B_IDX] == DOOR_CLOSED && mark(map, B_IDX));

    /* ---- row 8: valid multi-switch at f1 (A close, B open) ---- */
    DoorSwitch r8[] = {{1, 1, DOOR_CLOSED}, {2, 2, DOOR_OPEN}};
    CHECK(doorgrid_toggle_batch(&g, r8, 2, 1) == DOOR_OK);
    CHECK(g.state[A_IDX] == DOOR_CLOSED && mark(map, A_IDX));
    CHECK(g.state[B_IDX] == DOOR_OPEN && !mark(map, B_IDX));

    /* ---- rows 9-10: rollback returns to row-1 checkpoint, twice works ---- */
    CHECK(doorgrid_rollback_last(&g) == DOOR_OK);
    CHECK(g.state[A_IDX] == DOOR_OPEN && !mark(map, A_IDX));
    CHECK(g.state[B_IDX] == DOOR_CLOSED && mark(map, B_IDX));
    CHECK(g.frame_toggled[A_IDX] == 0);
    CHECK(doorgrid_rollback_last(&g) == DOOR_OK);
    CHECK(g.state[A_IDX] == DOOR_OPEN && g.state[B_IDX] == DOOR_CLOSED);

    /* ---- rows 11-12: external snapshot / restore ---- */
    DoorSnapshot s;
    CHECK(doorgrid_snapshot(&g, &s) == DOOR_OK);
    CHECK(doorgrid_toggle_batch(&g, r8, 2, 2) == DOOR_OK);
    CHECK(g.state[A_IDX] == DOOR_CLOSED && g.state[B_IDX] == DOOR_OPEN);
    CHECK(doorgrid_restore(&g, &s) == DOOR_OK);
    CHECK(g.state[A_IDX] == DOOR_OPEN && !mark(map, A_IDX));
    CHECK(g.state[B_IDX] == DOOR_CLOSED && mark(map, B_IDX));
    doorgrid_snapshot_free(&s);

    /* snapshot shape mismatch against a different-sized grid */
    unsigned char map2[9] = {0};
    DoorGrid g2;
    CHECK(doorgrid_init(&g2, map2, 3, 3) == DOOR_OK);
    DoorSnapshot s2;
    CHECK(doorgrid_snapshot(&g, &s2) == DOOR_OK);
    CHECK(doorgrid_restore(&g2, &s2) == DOOR_E_SHAPE);
    doorgrid_snapshot_free(&s2);
    doorgrid_free(&g2);

    /* ---- row 13: order independence — shuffled batches converge ---- */
    unsigned char ref_map[W * H], shuf_map[W * H];
    DoorGrid ref, shuf;
    memset(ref_map, 0, sizeof(ref_map));
    memset(shuf_map, 0, sizeof(shuf_map));
    CHECK(doorgrid_init(&ref, ref_map, W, H) == DOOR_OK);
    CHECK(doorgrid_init(&shuf, shuf_map, W, H) == DOOR_OK);
    doorgrid_set_door(&ref, 1, 1); doorgrid_set_door(&ref, 2, 2);
    doorgrid_set_door(&shuf, 1, 1); doorgrid_set_door(&shuf, 2, 2);
    DoorSwitch ord[] = {{1, 1, DOOR_CLOSED}, {2, 2, DOOR_OPEN}};
    DoorSwitch rev[] = {{2, 2, DOOR_OPEN}, {1, 1, DOOR_CLOSED}};
    CHECK(doorgrid_toggle_batch(&ref, ord, 2, 3) == DOOR_OK);
    CHECK(doorgrid_toggle_batch(&shuf, rev, 2, 3) == DOOR_OK);
    CHECK(memcmp(ref_map, shuf_map, sizeof(ref_map)) == 0);
    CHECK(memcmp(ref.state, shuf.state, sizeof(ref_map)) == 0);
    /* frame-3 bookkeeping now blocks A/B again on frame 3, allows frame 4 */
    CHECK(doorgrid_toggle_batch(&ref, ord, 2, 3) == DOOR_E_DUP_FRAME);
    CHECK(doorgrid_toggle_batch(&ref, ord, 2, 4) == DOOR_OK);
    doorgrid_free(&ref);
    doorgrid_free(&shuf);

    /* ---- row 14: closed-door collision identical to legacy solid map ---- */
    DoorGrid coll;
    unsigned char door_map[W * H], legacy_map[W * H];
    memset(door_map, 0, sizeof(door_map));
    memset(legacy_map, 0, sizeof(legacy_map));
    CHECK(doorgrid_init(&coll, door_map, W, H) == DOOR_OK);
    CHECK(doorgrid_set_door(&coll, 1, 1) == DOOR_OK); /* closed */
    legacy_map[A_IDX] = 1;                            /* same marker, no doors */
    int dtx = -1, dty = -1, ltx = -2, lty = -2;
    CHECK(aabb_vs_tiles(door_map, W, H, 4, 4, 10, 10, &dtx, &dty) ==
          aabb_vs_tiles(legacy_map, W, H, 4, 4, 10, 10, &ltx, &lty));
    CHECK(dtx == ltx && dty == lty && dtx == 1 && dty == 1);
    /* open the door: the same AABB now misses; legacy map still hits */
    DoorSwitch open_a[] = {{1, 1, DOOR_OPEN}};
    CHECK(doorgrid_toggle_batch(&coll, open_a, 1, 0) == DOOR_OK);
    CHECK(aabb_vs_tiles(door_map, W, H, 4, 4, 10, 10, &dtx, &dty) == 0);
    CHECK(aabb_vs_tiles(legacy_map, W, H, 4, 4, 10, 10, &ltx, &lty) == 1);
    /* close it again: parity restored */
    DoorSwitch close_a[] = {{1, 1, DOOR_CLOSED}};
    CHECK(doorgrid_toggle_batch(&coll, close_a, 1, 1) == DOOR_OK);
    CHECK(aabb_vs_tiles(door_map, W, H, 4, 4, 10, 10, &dtx, &dty) == 1);
    doorgrid_free(&coll);
    doorgrid_free(&g);

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
