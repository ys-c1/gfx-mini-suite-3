#include "tile.h"
#include "slope.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

/* Four slope scenarios, each reporting slide direction, displacement and
 * elapsed time. Coordinates: y grows downward, tiles are 8x8 px. */

enum { W = 16, H = 16 };
#define DT   (1.0f / 240.0f)
#define GRAV 300.0f
#define BW   4.0f
#define BH   4.0f

static unsigned char solid[W * H];
static float ang[W * H];
static signed char sdir[W * H];

static void reset_maps(void) {
    int i;
    for (i = 0; i < W * H; ++i) { solid[i] = 0; ang[i] = -1.0f; sdir[i] = 1; }
}
static void set_slope(int tx, int ty, float a, int d) {
    ang[ty * W + tx] = a; sdir[ty * W + tx] = (signed char)d;
}
static void set_solid(int tx, int ty) { solid[ty * W + tx] = 1; }

static void print_dir(const char* tag, float vx, float vy) {
    char b[64]; b[0] = 0;
    if (vy < -1e-3f) strcat(b, "up");
    else if (vy > 1e-3f) strcat(b, "down");
    if (vx < -1e-3f) { if (b[0]) strcat(b, "-"); strcat(b, "left"); }
    else if (vx > 1e-3f) { if (b[0]) strcat(b, "-"); strcat(b, "right"); }
    if (!b[0]) strcat(b, "stationary");
    printf("%s v=(%8.3f,%8.3f) -> %s\n", tag, vx, vy, b);
}

struct run {
    float x0, y0, x1, y1;     /* position at first / last contact */
    float t_first, t_last;    /* contact times (s) */
    float nx0, ny0;           /* normal at first contact */
    float nx1, ny1;           /* normal at last contact */
    float svx, svy;           /* slide velocity right after first contact */
    float xend, yend;         /* position at end of run */
    int contacts;
    int kind_first, kind_last; /* 1 = slope, 0 = solid top */
    int penetr_violations;
};

static struct run simulate(float x, float y, float vx, float vy,
                           float seconds, const char* name) {
    struct run r;
    float px, py, t = 0.0f;
    int i, n = (int)(seconds / DT + 0.5f);
    r.t_first = r.t_last = -1.0f;
    r.x0 = r.y0 = r.x1 = r.y1 = 0.0f;
    r.nx0 = r.ny0 = r.nx1 = r.ny1 = 0.0f;
    r.svx = r.svy = 0.0f;
    r.contacts = 0; r.kind_first = r.kind_last = -1;
    r.penetr_violations = 0;

    printf("== %s ==\n", name);
    printf("start x=%.3f y=%.3f vx=%.3f vy=%.3f (dt=%.5fs g=%.1fpx/s^2)\n",
           x, y, vx, vy, DT, GRAV);
    for (i = 0; i < n; ++i) {
        slope_contact_t ct;
        int hit;
        px = x; py = y;
        vy += GRAV * DT;
        x += vx * DT; y += vy * DT;
        hit = slope_move(solid, ang, sdir, W, H, px, py,
                         &x, &y, BW, BH, &vx, &vy, &ct);
        t += DT;
        if (hit) {
            /* invariants: the foot must sit exactly ON the contacted
             * surface, and must never end up inside a solid tile */
            float fx = x + BW * 0.5f, fy = y + BH;
            float sy = slope_surface_y(solid, ang, sdir, W, H, ct.tx, ct.ty, fx);
            if (sy > -1.0e29f && fabsf(fy - sy) > 1e-2f) ++r.penetr_violations;
            if (tile_solid(solid, W, H, (int)(fx / 8.0f), (int)((fy - 1e-3f) / 8.0f)))
                ++r.penetr_violations;
            if (r.contacts == 0) {
                r.t_first = t; r.x0 = x; r.y0 = y;
                r.nx0 = ct.nx; r.ny0 = ct.ny;
                r.svx = ct.slide_vx; r.svy = ct.slide_vy;
                r.kind_first = ct.is_slope;
            }
            r.t_last = t; r.x1 = x; r.y1 = y;
            r.nx1 = ct.nx; r.ny1 = ct.ny;
            r.kind_last = ct.is_slope;
            ++r.contacts;
        }
        if ((i % 30) == 0 || i == n - 1)
            printf("  t=%.3f x=%8.3f y=%8.3f vx=%9.3f vy=%9.3f %s\n",
                   t, x, y, vx, vy, hit ? "[contact]" : "");
    }
    r.xend = x; r.yend = y;
    printf("contacts=%d first@t=%.3f last@t=%.3f kind: %s -> %s\n",
           r.contacts, r.t_first, r.t_last,
           r.kind_first == 1 ? "slope" : r.kind_first == 0 ? "solid-top" : "none",
           r.kind_last == 1 ? "slope" : r.kind_last == 0 ? "solid-top" : "none");
    printf("normal first=(%.3f,%.3f) last=(%.3f,%.3f)\n", r.nx0, r.ny0, r.nx1, r.ny1);
    print_dir("slide dir (first contact):", r.svx, r.svy);
    printf("slide phase: d=(%.3f,%.3f) |d|=%.3f px in %.3f s\n",
           r.x1 - r.x0, r.y1 - r.y0,
           sqrtf((r.x1 - r.x0) * (r.x1 - r.x0) + (r.y1 - r.y0) * (r.y1 - r.y0)),
           (r.t_last >= r.t_first && r.contacts > 1) ? r.t_last - r.t_first : 0.0f);
    printf("whole run:  end=(%.3f,%.3f) in %.3f s\n", r.xend, r.yend, t);
    printf("penetration violations=%d\n\n", r.penetr_violations);
    return r;
}

int main(void) {
    /* ---- S1: 30 deg slope rising right, released from rest ---- */
    reset_maps(); set_slope(8, 10, 30.0f, +1);
    {
        float fx = 8 * 8 + 4;
        float fy = slope_surface_y(solid, ang, sdir, W, H, 8, 10, fx);
        simulate(fx - BW * 0.5f, fy - BH, 0.0f, 0.0f, 0.6f,
                 "S1: 30deg slope (rises right), released from rest");
    }

    /* ---- S2: zero-degree (flat) tiles, initial horizontal speed ----
     * a 0 deg slope surface is the tile's bottom edge: y = 88 at row 10 */
    reset_maps();
    {
        int tx;
        for (tx = 5; tx <= 11; ++tx) set_slope(tx, 10, 0.0f, +1);
        simulate(50.0f, 88.0f - BH, 40.0f, 0.0f, 0.5f,
                 "S2: 0deg flat tiles, initial vx=40");
    }

    /* ---- S3: 120 deg overhang, moving up-right into the ceiling ---- */
    reset_maps(); set_slope(8, 6, 120.0f, +1);
    simulate(58.0f, 66.0f, 30.0f, -80.0f, 0.5f,
             "S3: 120deg overhang, moving up-right into it");

    /* ---- S4: mixed map: 25 deg slope flowing onto a solid-tile floor ---- */
    reset_maps();
    {
        int tx;
        for (tx = 0; tx < W; ++tx) set_solid(tx, 11); /* floor, top y=88 */
        set_slope(6, 10, 25.0f, +1);                  /* bottom corner at (48,88) */
        {
            float fx = 6 * 8 + 6;
            float fy = slope_surface_y(solid, ang, sdir, W, H, 6, 10, fx);
            simulate(fx - BW * 0.5f, fy - BH, 0.0f, 0.0f, 0.9f,
                     "S4: 25deg slope tile + solid floor tiles (mixed)");
        }
    }

    /* ---- original whole-tile API: unchanged, slope data invisible to it ---- */
    printf("== original whole-tile API (unchanged code, unchanged results) ==\n");
    reset_maps();
    set_solid(1, 1);
    set_slope(3, 3, 45.0f, 1);
    {
        int tx = -1, ty = -1;
        int r1 = aabb_vs_tiles(solid, W, H, 4, 4, 10, 10, &tx, &ty);
        printf("aabb_vs_tiles(4,4,10,10) = %d tx=%d ty=%d (expect 1,1,1)\n", r1, tx, ty);
        tx = ty = -1;
        r1 = aabb_vs_tiles(solid, W, H, 24, 24, 6, 6, &tx, &ty);
        printf("aabb_vs_tiles(24,24,6,6) = %d (expect 0: tile (3,3) is a slope, not solid)\n", r1);
        printf("tile_solid(1,1)=%d (expect 1), tile_solid(3,3)=%d (expect 0)\n",
               tile_solid(solid, W, H, 1, 1), tile_solid(solid, W, H, 3, 3));
    }
    return 0;
}
