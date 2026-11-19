#ifndef MINI_SLOPE_H
#define MINI_SLOPE_H

/* Slope-aware collision layer (add-on for tile.c).
 *
 * The original whole-tile functions (tile_solid, aabb_vs_tiles) are NOT
 * modified and keep their exact behavior; this layer lives in parallel:
 *
 *   solid[]        : original solid map (nonzero = whole tile solid)
 *   slope_angles[i]: incline angle in degrees for tile i, < 0 = no slope.
 *                    0 deg = flat surface, 90 deg = vertical wall,
 *                    > 90 deg = overhang (surface leaning past vertical).
 *   slope_dirs[i]  : +1 = surface rises to the right, -1 = rises left.
 *
 * A solid tile also exposes a walkable surface here: its top edge (flat),
 * so slope tiles and solid tiles can be mixed in one map.
 * A slope surface is the line through the tile's bottom corner (bottom-left
 * for dir=+1, bottom-right for dir=-1) at the given incline; a 0 deg slope
 * is therefore the tile's bottom edge. Chain slope tiles across rows for
 * surfaces taller than one tile.
 * Coordinates match tile.c: y grows downward, tiles are 8x8 px.
 */

typedef struct {
    float nx, ny;             /* push direction = unit surface normal, away from solid */
    float depth;              /* penetration along the normal removed this step (px) */
    float slide_vx, slide_vy; /* velocity after projection onto the surface */
    int tx, ty;               /* tile that produced the contact */
    int is_slope;             /* 1 = slope tile, 0 = solid tile top */
} slope_contact_t;

/* Move an AABB (character) from previous origin (px,py) to desired origin
 * (*x,*y), resolving the foot point (bottom-center of the box) against
 * every solid top edge and slope surface swept along the way.
 *
 * Continuous, not discrete: the segment foot(prev)->foot(desired) is
 * intersected analytically with each candidate surface line, so no step
 * size can tunnel through a surface -- the foot always ends exactly ON
 * the first surface it crossed (signed distance == 0), never below it.
 *
 * On contact the into-surface velocity component is removed:
 *     v -= (v . n) n        (push direction IS the surface normal)
 * and the remaining tangential component is the slide.
 *
 * Returns 1 on contact (out filled; x, y, vx, vy resolved in place),
 *         0 if no surface was crossed,
 *        -1 on bad arguments. */
int slope_move(const unsigned char* solid,
               const float* slope_angles, const signed char* slope_dirs,
               int w, int h,
               float px, float py,
               float* x, float* y,
               float bw, float bh,
               float* vx, float* vy,
               slope_contact_t* out);

/* World y of the walkable surface of tile (tx,ty) at world x.
 * Returns -1.0e30f when the tile has no usable surface at x. */
float slope_surface_y(const unsigned char* solid,
                      const float* slope_angles, const signed char* slope_dirs,
                      int w, int h, int tx, int ty, float x);

#endif
