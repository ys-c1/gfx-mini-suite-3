#ifndef DOOR_H
#define DOOR_H

/*
 * Door grid layered over the legacy solid marker map.
 *
 * A closed door writes 1 into the underlying map cell, an open door
 * writes 0, so tile_solid()/aabb_vs_tiles() keep working unchanged and
 * a closed door collides exactly like a legacy solid cell.
 */

#define DOOR_CLOSED 0
#define DOOR_OPEN   1

typedef struct {
    int tx, ty;
    int state; /* DOOR_CLOSED / DOOR_OPEN */
} DoorSwitch;

typedef struct {
    unsigned char* map;           /* shared solid markers, w*h bytes */
    int w, h;
    unsigned char* is_door;       /* 1 on registered door cells */
    unsigned char* state;         /* DOOR_CLOSED / DOOR_OPEN */
    int* frame_toggled;           /* last frame each cell switched, -1 = never */
    int last_frame;
    /* internal pre-batch checkpoint used by rollback_last() */
    unsigned char* snap_map;
    unsigned char* snap_state;
    int* snap_frame;
    int snap_valid;
} DoorGrid;

typedef struct {
    int w, h;
    unsigned char* map;
    unsigned char* state;
    int* frame_toggled;
    int last_frame;
} DoorSnapshot;

enum {
    DOOR_OK = 0,
    DOOR_E_ARG = -1,         /* null pointer / bad size / empty batch */
    DOOR_E_BOUNDS = -2,      /* cell outside the grid */
    DOOR_E_STATE = -3,       /* state is not DOOR_CLOSED/DOOR_OPEN */
    DOOR_E_NOT_DOOR = -4,    /* target cell was never registered as a door */
    DOOR_E_DUP_BATCH = -5,   /* same cell listed twice in one batch */
    DOOR_E_DUP_FRAME = -6,   /* cell already switched during this frame */
    DOOR_E_FRAME = -7,       /* frame id < 0 */
    DOOR_E_ALLOC = -8,       /* out of memory while snapshotting */
    DOOR_E_NO_SNAPSHOT = -9,/* rollback with no checkpoint available */
    DOOR_E_SHAPE = -10       /* snapshot dimensions do not match grid */
};

int doorgrid_init(DoorGrid* g, unsigned char* map, int w, int h);
void doorgrid_free(DoorGrid* g);

int doorgrid_set_door(DoorGrid* g, int tx, int ty);
int doorgrid_is_door_at(const DoorGrid* g, int tx, int ty);
int doorgrid_state_at(const DoorGrid* g, int tx, int ty);

/* Stage 1: single switch, updates the solid marker immediately. */
int doorgrid_apply(DoorGrid* g, int tx, int ty, int state);

/* Stages 2+3: validated order-independent batch, snapshotted first. */
int doorgrid_toggle_batch(DoorGrid* g, const DoorSwitch* sw, int n, int frame);

/* External checkpoint API. */
int doorgrid_snapshot(const DoorGrid* g, DoorSnapshot* out);
int doorgrid_restore(DoorGrid* g, const DoorSnapshot* s);
void doorgrid_snapshot_free(DoorSnapshot* s);

/* Roll back to the checkpoint taken by the last successful batch. */
int doorgrid_rollback_last(DoorGrid* g);

const char* door_strerror(int code);

#endif
