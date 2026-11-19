#ifndef SHADOW_H
#define SHADOW_H

/* ---- hard shadow path: frozen, outputs must stay byte-identical ---- */
int shadow_project(float x1,float y1,float x2,float y2,float lx,float ly,float reach,float* ox1,float* oy1,float* ox2,float* oy2);
int shadow_point_lit(float px,float py,float lx,float ly,const float* segs,int nseg);

/* ---- soft area-light penumbra ----
 * light_size : area-light extent in world units; controls the geometric
 *              penumbra term w_um = light_size * f * (1 - f), where f is
 *              the occluder depth fraction along the light ray.
 * trans_width: artist-controlled transition band half-width in world units,
 *              added on top of the geometric term. Keeps contacts soft even
 *              where the geometric term pinches to zero (f -> 0 or f -> 1).
 */
typedef struct {
    float light_size;
    float trans_width;
} shadow_soft_t;

/* Return codes (mutually distinguishable):
 *   0  success, *vis holds visibility in [0,1] (0 = full umbra, 1 = lit)
 *  -1  null pointer or non-finite input
 *  -2  light_size <= 0 (zero-size light: hard path must be used instead)
 *  -3  trans_width < 0
 *  -4  degenerate occluder segment (A == B)
 */
int shadow_soft_factor(float px,float py,float lx,float ly,
                       float ax,float ay,float bx,float by,
                       const shadow_soft_t* cfg,float* vis);

/* Multi-segment scene: combined visibility is the min over segments, so a
 * point inside any segment's umbra stays exactly 0. Same error codes. */
int shadow_soft_scene(float px,float py,float lx,float ly,
                      const float* segs,int nseg,
                      const shadow_soft_t* cfg,float* vis);
#endif
