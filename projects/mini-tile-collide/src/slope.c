#include "slope.h"
#include <math.h>

#define SLOPE_TILE 8.0f
#define SLOPE_SKIN 0.01f
#define SLOPE_PI 3.14159265358979323846f

/* Surface of one tile in normal form: base point B, unit normal N pointing
 * away from the solid body. Signed distance of point P is s(P) = (P-B).N,
 * with s > 0 on the free side and s < 0 inside the solid.
 * The surface is valid for world x inside the tile's x span; steep slopes
 * whose line leaves the tile vertically should be chained across tiles. */
static int tile_surface(const unsigned char* solid,
                        const float* ang, const signed char* dir,
                        int w, int h, int tx, int ty,
                        float* bx, float* by, float* nx, float* ny,
                        int* is_slope)
{
    int i;
    if (tx < 0 || ty < 0 || tx >= w || ty >= h) return 0;
    i = ty * w + tx;
    if (solid && solid[i]) {
        /* solid tile: top edge acts as a flat walkable surface */
        *bx = tx * SLOPE_TILE; *by = ty * SLOPE_TILE;
        *nx = 0.0f; *ny = -1.0f; *is_slope = 0;
        return 1;
    }
    if (ang && ang[i] >= 0.0f) {
        /* slope tile: line through the bottom corner, normal from the angle.
         * ang = 0   -> flat top, n = (0,-1)
         * ang = 90  -> vertical wall, n = (-d,0)
         * ang > 90  -> overhang, n gains a +y (downward) component */
        float a = ang[i] * (SLOPE_PI / 180.0f);
        int d = (dir && dir[i] < 0) ? -1 : 1;
        *nx = -d * sinf(a);
        *ny = -cosf(a);
        *bx = (d > 0) ? tx * SLOPE_TILE : (tx + 1) * SLOPE_TILE;
        *by = (ty + 1) * SLOPE_TILE;
        *is_slope = 1;
        return 1;
    }
    return 0;
}

float slope_surface_y(const unsigned char* solid,
                      const float* slope_angles, const signed char* slope_dirs,
                      int w, int h, int tx, int ty, float x)
{
    float bx, by, nx, ny;
    int sl;
    if (!tile_surface(solid, slope_angles, slope_dirs, w, h, tx, ty,
                      &bx, &by, &nx, &ny, &sl))
        return -1.0e30f;
    if (x < tx * SLOPE_TILE - 1e-4f || x > (tx + 1) * SLOPE_TILE + 1e-4f)
        return -1.0e30f;
    if (fabsf(ny) < 1e-6f) return -1.0e30f; /* vertical: no height value */
    return by - (x - bx) * nx / ny;
}

int slope_move(const unsigned char* solid,
               const float* slope_angles, const signed char* slope_dirs,
               int w, int h,
               float px, float py,
               float* x, float* y,
               float bw, float bh,
               float* vx, float* vy,
               slope_contact_t* out)
{
    float fx0, fy0, fx, fy, dx, dy;
    int txa, txb, iter, contacts = 0;
    slope_contact_t last;

    if (!x || !y || !vx || !vy || bw <= 0.0f || bh <= 0.0f || w <= 0 || h <= 0)
        return -1;

    fx0 = px + bw * 0.5f; fy0 = py + bh;   /* foot: bottom-center, previous */
    fx  = fx0;          fy  = fy0;
    dx  = (*x + bw * 0.5f) - fx0;          /* remaining motion of the foot  */
    dy  = (*y + bh)       - fy0;

    /* Candidate columns: the swept span plus a margin (the tangential
     * remainder after a projection can glide past the segment's x range,
     * e.g. sliding off a slope edge). Exact for per-step motion of a
     * couple of tiles; the crossing test itself is exact for any step. */
    txa = (int)floorf(fminf(fx0, fx0 + dx) / SLOPE_TILE) - 2;
    txb = (int)floorf(fmaxf(fx0, fx0 + dx) / SLOPE_TILE) + 2;
    if (txa < 0) txa = 0;
    if (txb > w - 1) txb = w - 1;

    last.nx = last.ny = 0.0f; last.depth = 0.0f;
    last.slide_vx = last.slide_vy = 0.0f;
    last.tx = last.ty = -1; last.is_slope = -1;

    /* Up to 4 collide-and-slide iterations per step: each contact clamps
     * the foot to the crossed surface and re-aims the remaining motion
     * along it, so a step can glide around a slope/floor corner. */
    for (iter = 0; iter < 4; ++iter) {
        float best_t = 2.0f, bcx = 0, bcy = 0, bnx = 0, bny = 0, bs1 = 0;
        int btx = 0, bty = 0, bsl = 0;
        int tx, ty;
        for (tx = txa; tx <= txb; ++tx)
            for (ty = 0; ty < h; ++ty) {
                float bx, by, nx, ny, s0, s1, t, cx;
                int sl;
                if (!tile_surface(solid, slope_angles, slope_dirs, w, h, tx, ty,
                                  &bx, &by, &nx, &ny, &sl))
                    continue;
                s0 = (fx - bx) * nx + (fy - by) * ny;
                s1 = (fx + dx - bx) * nx + (fy + dy - by) * ny;
                /* contact: the segment ends on the solid side while
                 * starting on the free side or on the surface. The -1e-4
                 * threshold keeps float noise on an exact resting contact
                 * (s == 0) from re-triggering a zero-depth contact. */
                if (!(s1 < -1e-4f && s0 > s1 && s0 >= -SLOPE_SKIN))
                    continue;
                t = s0 / (s0 - s1);        /* analytic time of impact */
                if (t < 0.0f) t = 0.0f;
                else if (t > 1.0f) t = 1.0f;
                cx = fx + t * dx;
                if (cx < tx * SLOPE_TILE - 1e-4f || cx > (tx + 1) * SLOPE_TILE + 1e-4f)
                    continue;              /* crossing outside this tile's span */
                if (t < best_t) {
                    best_t = t;
                    bcx = cx; bcy = fy + t * dy;
                    bnx = nx; bny = ny; bs1 = s1;
                    btx = tx; bty = ty; bsl = sl;
                }
            }
        if (best_t > 1.0f) break;          /* free motion: done */

        /* Clamp the foot exactly onto the crossed surface (s == 0). This is
         * why no tunneling is possible: whatever the step size, the foot
         * never ends a step past the first surface it crossed. */
        fx = bcx; fy = bcy;

        /* Remove the into-surface velocity component: the push is fully
         * determined by the surface normal; what remains is the slide. */
        {
            float vn = (*vx) * bnx + (*vy) * bny;
            if (vn < 0.0f) {
                *vx -= vn * bnx;
                *vy -= vn * bny;
            }
        }

        /* Slide the remaining motion along the surface. */
        {
            float rem = 1.0f - best_t;
            float rx = rem * dx, ry = rem * dy;
            float rn = rx * bnx + ry * bny;
            dx = rx - rn * bnx;
            dy = ry - rn * bny;
        }

        last.nx = bnx; last.ny = bny; last.depth = -bs1;
        last.tx = btx; last.ty = bty; last.is_slope = bsl;
        ++contacts;
        if (dx == 0.0f && dy == 0.0f) break;
    }

    fx += dx; fy += dy;                    /* apply the (slid) remainder */
    *x = fx - bw * 0.5f;
    *y = fy - bh;

    if (!contacts) return 0;
    if (out) {
        last.slide_vx = *vx; last.slide_vy = *vy;
        *out = last;
    }
    return 1;
}
