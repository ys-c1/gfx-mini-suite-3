#ifndef SHADOW_H
#define SHADOW_H
#include <math.h>

/* ===== legacy single-occluder API (frozen, do not change semantics) ===== */
int shadow_project(float x1,float y1,float x2,float y2,float lx,float ly,
                   float reach,float* ox1,float* oy1,float* ox2,float* oy2);
int shadow_point_lit(float px,float py,float lx,float ly,const float* segs,int nseg);

/* ===== depth-aware overlap clipping =====
 *
 * An occluder is a segment plus a depth value. Smaller depth means closer to
 * the light and therefore wins the overlap. SHADOW_DEPTH_MISSING (NaN) sorts
 * as +infinity: an occluder with unknown depth can never claim precedence
 * over one with known depth.
 */
#define SHADOW_DEPTH_MISSING ((float)NAN)
#define SHADOW_MAX_VERTS 16   /* max vertices of one output polygon  */
#define SHADOW_MAX_PIECES 16  /* max polygons in one clip result set */

typedef struct {
    float ax, ay;   /* segment endpoint A */
    float bx, by;   /* segment endpoint B */
    float depth;    /* smaller = closer to light; NaN = missing */
    int   id;       /* unique non-negative owner id */
} shadow_occluder_t;

typedef struct {
    float x[SHADOW_MAX_VERTS];
    float y[SHADOW_MAX_VERTS];
    int   n;            /* 0 == empty polygon */
    int   owner;        /* occluder id that owns this piece at render time */
    int   is_overlap;   /* 1 for the intersection polygon */
} shadow_polygon_t;

typedef struct {
    shadow_polygon_t piece[SHADOW_MAX_PIECES];
    int   count;        /* number of non-empty polygons */
    int   winner;       /* owner id of the overlap polygon; -1 if disjoint */
    int   tie;          /* equal effective depth, lower-id tie-break used */
    int   depth_missing;/* at least one input depth was NaN */
} shadow_clip_result_t;

/* Project one occluder to its CCW shadow quad (wraps shadow_project). */
int shadow_occluder_quad(const shadow_occluder_t* oc, float lx, float ly,
                         float reach, shadow_polygon_t* q);

/* Signed-area shoelace formula (absolute area, double precision accumulator). */
int shadow_polygon_area(const shadow_polygon_t* p, double* area);

/* Remove duplicate/collapsed vertices; empties degenerate polygons. */
void shadow_polygon_clean(shadow_polygon_t* p);

/* Depth-aware merge of two occluder shadows.
 *
 * On success returns 0 and *out holds a partition of the shadow union:
 *   - one overlap polygon (is_overlap=1) owned by the nearer occluder,
 *   - zero or more difference pieces per occluder.
 * Piece interiors are pairwise disjoint, so rendering the list paints the
 * overlap exactly once instead of stacking two darkened quads.
 * Return: 0 ok, -1 bad argument, -2 result capacity exceeded.
 *
 * Precedence rules (fully deterministic):
 *   1. known smaller depth wins;
 *   2. equal depth (relative tolerance): tie flag set, lower id wins;
 *   3. missing depth sorts as +infinity; both missing -> tie, lower id wins;
 *   4. geometrically coincident occluders -> single overlap polygon, no
 *      difference pieces.
 */
int shadow_clip_two(const shadow_occluder_t* a, const shadow_occluder_t* b,
                    float lx, float ly, float reach,
                    shadow_clip_result_t* out);

/* Ray-cast containment test; polygon boundary counts as inside. */
int shadow_point_in_poly(float px, float py, const shadow_polygon_t* p);

/* Reference per-cell sampling: count cell centres inside the union of polys
 * on a grid x grid lattice over the given bounding box. */
int shadow_raster_count(const shadow_polygon_t* polys, int npoly,
                        float xmin, float ymin, float xmax, float ymax,
                        int grid, long* inside_cells);

/* Rigorous upper bound on |exact area - raster area| for the same lattice.
 *
 * An edge of projected length (dx,dy) can affect at most
 * |dx|/hx + |dy|/hy + 1 cells; every disputed cell contributes at most
 * hx*hy area. Summing over every output edge gives the bound. */
int shadow_area_error_bound(const shadow_polygon_t* polys, int npoly,
                            float xmin, float ymin, float xmax, float ymax,
                            int grid, double* bound);

#endif
