#include "door.h"
#include <stdlib.h>
#include <string.h>

static int in_bounds(const DoorGrid* g, int tx, int ty) {
    return tx >= 0 && ty >= 0 && tx < g->w && ty < g->h;
}

static int valid_state(int state) {
    return state == DOOR_CLOSED || state == DOOR_OPEN;
}

const char* door_strerror(int code) {
    switch (code) {
        case DOOR_OK:            return "ok";
        case DOOR_E_ARG:         return "bad argument";
        case DOOR_E_BOUNDS:      return "tile out of bounds";
        case DOOR_E_STATE:       return "illegal door state";
        case DOOR_E_NOT_DOOR:    return "cell is not a door";
        case DOOR_E_DUP_BATCH:   return "duplicate cell inside one batch";
        case DOOR_E_DUP_FRAME:   return "same cell switched twice in one frame";
        case DOOR_E_FRAME:       return "negative frame id";
        case DOOR_E_ALLOC:       return "snapshot allocation failed";
        case DOOR_E_NO_SNAPSHOT: return "no checkpoint to roll back";
        case DOOR_E_SHAPE:       return "snapshot shape mismatch";
        default:                 return "unknown door error";
    }
}

int doorgrid_init(DoorGrid* g, unsigned char* map, int w, int h) {
    if (!g || !map || w <= 0 || h <= 0) return DOOR_E_ARG;
    memset(g, 0, sizeof(*g));
    size_t n = (size_t)w * (size_t)h;
    g->is_door = (unsigned char*)calloc(n, 1);
    g->state = (unsigned char*)calloc(n, 1);
    g->frame_toggled = (int*)malloc(n * sizeof(int));
    g->snap_map = (unsigned char*)malloc(n);
    g->snap_state = (unsigned char*)malloc(n);
    g->snap_frame = (int*)malloc(n * sizeof(int));
    if (!g->is_door || !g->state || !g->frame_toggled ||
        !g->snap_map || !g->snap_state || !g->snap_frame) {
        doorgrid_free(g);
        return DOOR_E_ALLOC;
    }
    for (size_t i = 0; i < n; ++i) g->frame_toggled[i] = -1;
    g->map = map;
    g->w = w;
    g->h = h;
    g->last_frame = -1;
    g->snap_valid = 0;
    return DOOR_OK;
}

void doorgrid_free(DoorGrid* g) {
    if (!g) return;
    free(g->is_door);
    free(g->state);
    free(g->frame_toggled);
    free(g->snap_map);
    free(g->snap_state);
    free(g->snap_frame);
    memset(g, 0, sizeof(*g));
}

int doorgrid_set_door(DoorGrid* g, int tx, int ty) {
    if (!g) return DOOR_E_ARG;
    if (!in_bounds(g, tx, ty)) return DOOR_E_BOUNDS;
    int idx = ty * g->w + tx;
    g->is_door[idx] = 1;
    g->state[idx] = DOOR_CLOSED;      /* doors start closed */
    g->frame_toggled[idx] = -1;
    g->map[idx] = 1;                  /* closed door == solid marker */
    return DOOR_OK;
}

int doorgrid_is_door_at(const DoorGrid* g, int tx, int ty) {
    if (!g || !in_bounds(g, tx, ty)) return 0;
    return g->is_door[ty * g->w + tx] != 0;
}

int doorgrid_state_at(const DoorGrid* g, int tx, int ty) {
    if (!g || !in_bounds(g, tx, ty)) return DOOR_E_BOUNDS;
    return g->state[ty * g->w + tx];
}

/* Single switch: validate hard, then update state table + solid marker. */
int doorgrid_apply(DoorGrid* g, int tx, int ty, int state) {
    if (!g) return DOOR_E_ARG;
    if (!in_bounds(g, tx, ty)) return DOOR_E_BOUNDS;
    if (!valid_state(state)) return DOOR_E_STATE;
    int idx = ty * g->w + tx;
    if (!g->is_door[idx]) return DOOR_E_NOT_DOOR;
    g->state[idx] = (unsigned char)state;
    g->map[idx] = (state == DOOR_CLOSED) ? 1 : 0;
    return DOOR_OK;
}

/* Validate every entry before touching anything; no partial mutation. */
static int validate_batch(const DoorGrid* g, const DoorSwitch* sw, int n, int frame) {
    if (!g || !sw || n <= 0) return DOOR_E_ARG;
    if (frame < 0) return DOOR_E_FRAME;
    for (int i = 0; i < n; ++i) {
        if (!in_bounds(g, sw[i].tx, sw[i].ty)) return DOOR_E_BOUNDS;
        if (!valid_state(sw[i].state)) return DOOR_E_STATE;
        int idx = sw[i].ty * g->w + sw[i].tx;
        if (!g->is_door[idx]) return DOOR_E_NOT_DOOR;
        if (g->frame_toggled[idx] == frame) return DOOR_E_DUP_FRAME;
        for (int j = 0; j < i; ++j)
            if (sw[j].tx == sw[i].tx && sw[j].ty == sw[i].ty)
                return DOOR_E_DUP_BATCH;
    }
    return DOOR_OK;
}

int doorgrid_toggle_batch(DoorGrid* g, const DoorSwitch* sw, int n, int frame) {
    int rc = validate_batch(g, sw, n, frame);
    if (rc != DOOR_OK) return rc;

    /* Stage 3: snapshot before the first mutation. */
    size_t cnt = (size_t)g->w * (size_t)g->h;
    memcpy(g->snap_map, g->map, cnt);
    memcpy(g->snap_state, g->state, cnt);
    memcpy(g->snap_frame, g->frame_toggled, cnt * sizeof(int));
    g->snap_valid = 1;

    for (int i = 0; i < n; ++i) {
        int idx = sw[i].ty * g->w + sw[i].tx;
        g->state[idx] = (unsigned char)sw[i].state;
        g->map[idx] = (sw[i].state == DOOR_CLOSED) ? 1 : 0;
        g->frame_toggled[idx] = frame;
    }
    g->last_frame = frame;
    return DOOR_OK;
}

int doorgrid_snapshot(const DoorGrid* g, DoorSnapshot* out) {
    if (!g || !out) return DOOR_E_ARG;
    size_t cnt = (size_t)g->w * (size_t)g->h;
    memset(out, 0, sizeof(*out));
    out->w = g->w;
    out->h = g->h;
    out->map = (unsigned char*)malloc(cnt);
    out->state = (unsigned char*)malloc(cnt);
    out->frame_toggled = (int*)malloc(cnt * sizeof(int));
    if (!out->map || !out->state || !out->frame_toggled) {
        doorgrid_snapshot_free(out);
        return DOOR_E_ALLOC;
    }
    memcpy(out->map, g->map, cnt);
    memcpy(out->state, g->state, cnt);
    memcpy(out->frame_toggled, g->frame_toggled, cnt * sizeof(int));
    out->last_frame = g->last_frame;
    return DOOR_OK;
}

int doorgrid_restore(DoorGrid* g, const DoorSnapshot* s) {
    if (!g || !s || !s->map || !s->state || !s->frame_toggled) return DOOR_E_ARG;
    if (s->w != g->w || s->h != g->h) return DOOR_E_SHAPE;
    size_t cnt = (size_t)g->w * (size_t)g->h;
    memcpy(g->map, s->map, cnt);
    memcpy(g->state, s->state, cnt);
    memcpy(g->frame_toggled, s->frame_toggled, cnt * sizeof(int));
    g->last_frame = s->last_frame;
    return DOOR_OK;
}

void doorgrid_snapshot_free(DoorSnapshot* s) {
    if (!s) return;
    free(s->map);
    free(s->state);
    free(s->frame_toggled);
    memset(s, 0, sizeof(*s));
}

int doorgrid_rollback_last(DoorGrid* g) {
    if (!g) return DOOR_E_ARG;
    if (!g->snap_valid) return DOOR_E_NO_SNAPSHOT;
    size_t cnt = (size_t)g->w * (size_t)g->h;
    memcpy(g->map, g->snap_map, cnt);
    memcpy(g->state, g->snap_state, cnt);
    memcpy(g->frame_toggled, g->snap_frame, cnt * sizeof(int));
    /* checkpoint stays armed: rolling back twice must also work */
    return DOOR_OK;
}
