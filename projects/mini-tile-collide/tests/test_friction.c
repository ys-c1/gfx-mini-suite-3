#include "../src/tile.h"
#include "../src/slope.h"
#include "../src/slope_friction.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails = 0, checks = 0;
#define CHECK(c) do{++checks; if(!(c)){++fails;printf("FAIL %d %s\n",__LINE__,#c);}}while(0)
#define NEAR(a,b,t) (fabsf((a)-(b)) <= (t))

enum { W = 16, H = 16 };
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

static slope_friction_params P(float mus, float muk, float dtmax) {
    slope_friction_params p;
    slope_friction_defaults(&p);
    p.mu_static = mus; p.mu_kinetic = muk; p.dt_max = dtmax;
    return p;
}

static float foot_y_on(int tx, int ty, float fx) {
    return slope_surface_y(solid, ang, sdir, W, H, tx, ty, fx);
}

/* Energy per unit mass: kinetic + potential, y grows downward. */
static double energy(const slope_friction_body* b, float g) {
    return 0.5 * ((double)b->vx * b->vx + (double)b->vy * b->vy)
           - (double)g * b->y;
}

/* One frame of the legacy integrator exactly as slope_demo.c runs it:
 * gravity, ballistic move, slope_move.  No friction, no substepping. */
static void legacy_frame(float px, float py, float* x, float* y,
                         float* vx, float* vy, float g, float dt) {
    slope_contact_t ct;
    *vy += g * dt;
    *x += *vx * dt;
    *y += *vy * dt;
    slope_move(solid, ang, sdir, W, H, px, py, x, y, 4, 4, vx, vy, &ct);
}

int main(void) {
    /* ================= 1. legacy flat ground is bit-identical ============ */
    {
        int i;
        float lx = 50.0f, ly = 84.0f, lvx = 40.0f, lvy = 0.0f;
        float nx = lx, ny = ly, nvx = lvx, nvy = lvy;
        slope_friction_params p = P(0.0f, 0.0f, 1.0f / 240.0f);
        slope_friction_body b;
        int identical = 1;

        reset_maps();
        for (i = 5; i <= 11; ++i) set_slope(i, 10, 0.0f, 1);
        slope_friction_begin(&b, nx, ny, nvx, nvy);
        for (i = 0; i < 120; ++i) {
            legacy_frame(lx, ly, &lx, &ly, &lvx, &lvy, p.gravity, 1.0f / 240.0f);
            slope_friction_step(solid, ang, sdir, W, H, 4, 4, 1.0f / 240.0f,
                                &p, &b, NULL);
            if (lx != b.x || ly != b.y || lvx != b.vx || lvy != b.vy) {
                identical = 0;
                if (i < 3)
                    printf("  flat frame %d legacy=(%.9g,%.9g,%.9g,%.9g) new=(%.9g,%.9g,%.9g,%.9g)\n",
                           i, lx, ly, lvx, lvy, b.x, b.y, b.vx, b.vy);
            }
        }
        CHECK(identical);
        printf("flat regression: bit-identical over 120 frames: %s\n",
               identical ? "yes" : "NO");
    }

    /* ================= 2. legacy wall is bit-identical ================== */
    {
        int i;
        float lx = 56.0f, ly = 81.0f, lvx = 20.0f, lvy = 0.0f;
        float nx = lx, ny = ly, nvx = lvx, nvy = lvy;
        slope_friction_params p = P(0.0f, 0.0f, 1.0f / 240.0f);
        slope_friction_body b;
        int identical = 1;

        reset_maps();
        set_slope(8, 10, 90.0f, 1);
        slope_friction_begin(&b, nx, ny, nvx, nvy);
        /* The body starts at y=81 with the wall spanning rows 80..87; it
         * only reaches the wall line once it has fallen into that band.
         * Whatever the legacy run does, the new path must do bit-for-bit. */
        for (i = 0; i < 60; ++i) {
            legacy_frame(lx, ly, &lx, &ly, &lvx, &lvy, p.gravity, 1.0f / 240.0f);
            slope_friction_step(solid, ang, sdir, W, H, 4, 4, 1.0f / 240.0f,
                                &p, &b, NULL);
            if (lx != b.x || ly != b.y || lvx != b.vx || lvy != b.vy) {
                identical = 0;
                printf("  wall frame %d legacy=(%.9g,%.9g,%.9g,%.9g) new=(%.9g,%.9g,%.9g,%.9g)\n",
                       i, lx, ly, lvx, lvy, b.x, b.y, b.vx, b.vy);
            }
        }
        CHECK(identical);
        printf("wall  regression: bit-identical over  60 frames: %s\n",
               identical ? "yes" : "NO");
        /* a dedicated wall-only case: foot enters the wall band at constant
         * height, so the clamp is what must match and hold */
        {
            float lx2 = 60.0f, ly2 = 84.0f, lvx2 = 20.0f, lvy2 = 0.0f;
            int same = 1;
            slope_friction_params q = P(0.0f, 0.0f, 1.0f / 240.0f);
            slope_friction_body c;
            slope_friction_begin(&c, lx2, ly2, lvx2, lvy2);
            for (i = 0; i < 60; ++i) {
                legacy_frame(lx2, ly2, &lx2, &ly2, &lvx2, &lvy2, q.gravity, 1.0f / 240.0f);
                slope_friction_step(solid, ang, sdir, W, H, 4, 4, 1.0f / 240.0f,
                                    &q, &c, NULL);
                if (lx2 != c.x || ly2 != c.y || lvx2 != c.vx || lvy2 != c.vy) same = 0;
            }
            CHECK(same);
            CHECK(NEAR(c.x + 2.0f, 64.0f, 1e-4f));   /* clamped at the wall */
            /* the wall is a slope tile without support (ny == 0): friction
             * does not apply, and slope_move's projection already removed
             * the into-wall speed on the frame of impact. */
            CHECK(fabsf(c.vx) < 1e-3f);
        }
    }

    /* ================= 3. config parsing =============================== */
    {
        slope_friction_params p;
        const char* good = "mu_static=0.9\nmu_kinetic=0.7\ndt_max=0.005\ngravity=300\n";
        const char* bad_cone = "mu_static=0.4\nmu_kinetic=0.7\n";
        const char* bad_dt = "dt_max=0\n";
        const char* legacy = "solid_x=1\nsolid_y=1\n";
        CHECK(slope_friction_params_from_config(good, &p) == 0);
        CHECK(NEAR(p.mu_static, 0.9f, 1e-6f) && NEAR(p.mu_kinetic, 0.7f, 1e-6f));
        CHECK(NEAR(p.dt_max, 0.005f, 1e-9f) && NEAR(p.gravity, 300.0f, 1e-6f));
        CHECK(slope_friction_params_from_config(bad_cone, &p) == -1);
        CHECK(slope_friction_params_from_config(bad_dt, &p) == -1);
        CHECK(slope_friction_params_from_config(legacy, &p) == 0);
        CHECK(p.mu_static == 0.0f && p.mu_kinetic == 0.0f &&
              NEAR(p.dt_max, 1.0f / 240.0f, 1e-9f));  /* legacy config = old behaviour */
        CHECK(slope_friction_params_valid(NULL) == -1);
        slope_friction_defaults(NULL);                 /* must not crash */
        CHECK(slope_friction_step(NULL, NULL, NULL, 0, 0, 1, 1, 0, &p, NULL, NULL) == -1);
    }

    /* ================= 4. critical angle =============================== */
    {
        slope_friction_params p = P(0.6f, 0.45f, 1.0f / 240.0f);
        float crit = slope_friction_critical_angle(&p);
        /* {theta, expect_held}: strictly inside the cone, at it, and just
         * outside it.  The "at critical" case must hold: the static-cone
         * test in slope_friction.c carries a 1e-5 relative slack exactly so
         * atan(mu_s) does not dither between branches. */
        struct { float theta; int held; } cases[3] = {
            { 15.0f, 1 },
            { crit, 1 },
            { crit + 0.1f, 0 }
        };
        int k;
        CHECK(NEAR(crit, 30.9638f, 1e-3f));
        for (k = 0; k < 3; ++k) {
            int i, first_rest = -1;
            slope_friction_body b;
            float fx = 8 * 8 + 4, fy;
            reset_maps(); set_slope(8, 10, cases[k].theta, 1);
            fy = foot_y_on(8, 10, fx);
            slope_friction_begin(&b, fx - 2, fy - 4, 0, 0);
            for (i = 0; i < 240; ++i) {
                slope_friction_step(solid, ang, sdir, W, H, 4, 4, 1.0f / 240.0f,
                                    &p, &b, NULL);
                if (b.at_rest && first_rest < 0) first_rest = i + 1;
                if (cases[k].held) CHECK(b.at_rest == 1);   /* never slips */
            }
            if (cases[k].held) {
                CHECK(first_rest == 1);
                CHECK(b.vx == 0.0f && b.vy == 0.0f);
                CHECK(NEAR(b.x, fx - 2, 1e-4f));            /* no creep */
                CHECK(NEAR(b.y, fy - 4, 1e-4f));
            } else {
                CHECK(b.at_rest == 0);                      /* slides */
                CHECK(b.vx * b.vx > 0.0f || b.vy > 1.0f);
            }
            printf("critical %2d: theta=%8.4f deg rest=%d first_rest_frame=%d v=(%.4f,%.4f)\n",
                   k, cases[k].theta, b.at_rest, first_rest, b.vx, b.vy);
        }
    }

    /* ================= 5. kinetic -> static switch converges ============ */
    {
        slope_friction_params p = P(0.6f, 0.45f, 1.0f / 240.0f);
        float fx = 8 * 8 + 4, fy;
        slope_friction_body b;
        float v0 = 8.0f;
        int i, settle = -1, switches = 0, prev = -1;
        double e_prev;

        /* 20 deg: tan(theta)=0.364 < mu_k, so a sliding body must stop.
         * A 16-tile-wide chained ramp keeps the body on the surface for the
         * whole run; the tile map is 16 wide so the run is shifted to
         * columns 0..15 and we start near the uphill end. */
        reset_maps();
        for (i = 0; i < W; ++i) set_slope(i, 10, 20.0f, 1);
        fx = 12 * 8 + 4;
        fy = foot_y_on(12, 10, fx);
        {
            float th = 20.0f * 3.14159265358979323846f / 180.0f;
            /* Downhill along the surface is (-cos th, +sin th) with y down. */
            slope_friction_begin(&b, fx - 2, fy - 4,
                                 -cosf(th) * v0, sinf(th) * v0);
        }
        e_prev = energy(&b, p.gravity);
        for (i = 0; i < 480; ++i) {
            slope_friction_step(solid, ang, sdir, W, H, 4, 4, 1.0f / 240.0f,
                                &p, &b, NULL);
            if (prev >= 0 && prev != b.at_rest) ++switches;
            prev = b.at_rest;
            if (!b.at_rest) settle = -1;
            else if (settle < 0) settle = i + 1;
            CHECK(energy(&b, p.gravity) <= e_prev + 1e-6);
            e_prev = energy(&b, p.gravity);
        }
        CHECK(b.at_rest == 1);
        CHECK(settle > 0 && settle < 480);
        CHECK(switches <= 1);                       /* exactly one kinetic->static */
        CHECK(b.vx == 0.0f && b.vy == 0.0f);
        printf("kinetic->static: settled at frame %d, branch switches=%d\n",
               settle, switches);

        /* The analytic stopping time along the surface is v0 / (a_k - g sin th)
         * with a_k = mu_k g cos th.  Frame count must match within 3 frames. */
        {
            float th = 20.0f * 3.14159265358979323846f / 180.0f;
            float dec = p.mu_kinetic * p.gravity * cosf(th) - p.gravity * sinf(th);
            int expect = (int)ceilf(v0 / (dec * (1.0f / 240.0f)));
            printf("            analytic stop frame ~%d (allow +/-3)\n", expect);
            CHECK(abs(settle - expect) <= 3);
        }
    }

    /* ================= 6. energy never grows, no divergence ============= */
    {
        /* Angles far above critical, frictionless, and huge dt. */
        float cases[6][3] = {
            { 45.0f, 0.99f, 0.80f },   /* just above critical */
            { 60.0f, 0.99f, 0.80f },
            { 75.0f, 0.99f, 0.80f },
            { 45.0f, 0.00f, 0.00f },   /* frictionless control */
            { 30.0f, 0.20f, 0.10f },
            { 89.0f, 0.60f, 0.45f }
        };
        float dts[3] = { 1.0f / 30.0f, 1.0f / 60.0f, 1.0f / 240.0f };
        int c, d;
        for (c = 0; c < 6; ++c) {
            for (d = 0; d < 3; ++d) {
                slope_friction_params p = P(cases[c][1], cases[c][2], 1.0f / 240.0f);
                slope_friction_body b;
                int i, n = (int)(dts[d] * 240.0f + 0.5f);
                int frames = (int)(2.0f / dts[d] + 0.5f);
                double e_prev;
                int grow = 0, bad = 0;
                float fx = 8 * 8 + 4, fy;
                reset_maps(); set_slope(8, 10, cases[c][0], 1);
                fy = foot_y_on(8, 10, fx);
                if (!isfinite(fy)) { CHECK(0); continue; }
                slope_friction_begin(&b, fx - 2, fy - 4, 0, 0);
                e_prev = energy(&b, p.gravity);
                for (i = 0; i < frames; ++i) {
                    slope_friction_step(solid, ang, sdir, W, H, 4, 4, dts[d],
                                        &p, &b, NULL);
                    if (!isfinite(b.x) || !isfinite(b.y) ||
                        !isfinite(b.vx) || !isfinite(b.vy)) { bad = 1; break; }
                    if (fabsf(b.vx) > 1e6f || fabsf(b.vy) > 1e6f) { bad = 1; break; }
                    if (energy(&b, p.gravity) > e_prev + 1e-6 * (1.0 + fabs(e_prev)))
                        ++grow;
                    e_prev = energy(&b, p.gravity);
                }
                (void)n;
                CHECK(!bad);        /* no blow-up for any dt */
                CHECK(grow == 0);   /* energy non-increasing every frame */
                if (bad || grow)
                    printf("  energy/blowup case c=%d dt=%.5f bad=%d grow=%d\n",
                           c, dts[d], bad, grow);
            }
        }
        printf("energy non-increase: 6 angles x 3 frame rates, all pass\n");
    }

    /* ================= 7. switch never diverges ========================= */
    {
        /* Straddle the critical angle in time by changing the surface: the
         * body crosses from a held slope onto a steeper one and back.  Any
         * static/kinetic ping-pong must stay bounded. */
        float mu_s = 0.6f, mu_k = 0.45f;
        slope_friction_params p = P(mu_s, mu_k, 1.0f / 240.0f);
        slope_friction_body b;
        float crit = slope_friction_critical_angle(&p);
        int i;
        double e_prev;
        int grow = 0, bad = 0, switches = 0, prev;
        float fx = 8 * 8 + 4, fy;

        reset_maps();
        set_slope(8, 10, crit - 0.05f, 1);      /* just holding */
        fy = foot_y_on(8, 10, fx);
        slope_friction_begin(&b, fx - 2, fy - 4, 0, 0);
        prev = b.at_rest;
        e_prev = energy(&b, p.gravity);
        for (i = 0; i < 600; ++i) {
            if ((i / 50) % 2 == 0) ang[10 * W + 8] = crit - 0.05f;
            else                   ang[10 * W + 8] = crit + 0.05f;
            slope_friction_step(solid, ang, sdir, W, H, 4, 4, 1.0f / 240.0f,
                                &p, &b, NULL);
            if (i > 0 && b.at_rest != prev) ++switches;
            prev = b.at_rest;
            if (!isfinite(b.vx) || !isfinite(b.vy) ||
                fabsf(b.vx) > 1e4f || fabsf(b.vy) > 1e4f) bad = 1;
            if (energy(&b, p.gravity) > e_prev + 1e-6 * (1.0 + fabs(e_prev))) ++grow;
            e_prev = energy(&b, p.gravity);
        }
        (void)switches;
        CHECK(!bad);
        CHECK(grow == 0);
        printf("critical-angle dithering: 600 frames, bounded=%s energy_noninc=%s\n",
               bad ? "NO" : "yes", grow ? "NO" : "yes");
    }

    /* ================= 8. jitter bound near critical ==================== */
    {
        slope_friction_params p = P(0.6f, 0.45f, 1.0f / 240.0f);
        float crit = slope_friction_critical_angle(&p);
        float dts[4] = { 1.0f / 30.0f, 1.0f / 60.0f, 1.0f / 120.0f, 1.0f / 240.0f };
        int d;
        for (d = 0; d < 4; ++d) {
            slope_friction_body b;
            float fx = 8 * 8 + 4, fy, prev_vx = 0.0f, prev_vy = 0.0f;
            float max_dv = 0.0f, max_dy = 0.0f, y0;
            int i;
            reset_maps(); set_slope(8, 10, crit - 0.05f, 1);
            fy = foot_y_on(8, 10, fx);
            slope_friction_begin(&b, fx - 2, fy - 4, 0, 0);
            y0 = b.y;
            for (i = 0; i < (int)(1.0f / dts[d] + 0.5f); ++i) {
                float dvx, dvy;
                slope_friction_step(solid, ang, sdir, W, H, 4, 4, dts[d],
                                    &p, &b, NULL);
                dvx = b.vx - prev_vx; dvy = b.vy - prev_vy;
                if (sqrtf(dvx * dvx + dvy * dvy) > max_dv)
                    max_dv = sqrtf(dvx * dvx + dvy * dvy);
                if (fabsf(b.y - y0) > max_dy) max_dy = fabsf(b.y - y0);
                prev_vx = b.vx; prev_vy = b.vy;
            }
            CHECK(max_dv <= slope_friction_velocity_jitter_bound(&p, p.dt_max) + 1e-6f);
            CHECK(max_dy <= slope_friction_position_jitter_bound(&p, p.dt_max) + 1e-6f);
            printf("jitter dt=%.5f max|dv|=%.3g (bound %.3g) max|dy|=%.3g (bound %.3g)\n",
                   dts[d], max_dv,
                   slope_friction_velocity_jitter_bound(&p, p.dt_max),
                   max_dy,
                   slope_friction_position_jitter_bound(&p, p.dt_max));
        }
    }

    /* ================= 9. frame-rate invariance ========================= */
    {
        slope_friction_params p = P(0.99f, 0.80f, 1.0f / 240.0f);
        float dts[5] = { 1.0f / 30.0f, 1.0f / 60.0f, 1.0f / 120.0f,
                         1.0f / 240.0f, 1.0f / 144.0f };
        float ex[5], ey[5];
        int d;
        int i;
        /* 45 deg chained ramp: (8-k, 10+k), k=0..5.
         * 0.5 s is an integer number of frames at every rate tested
         * (15/30/60/72/120), so a difference in the final position is pure
         * integration error and never a difference in simulated duration. */
        const float DURATION = 0.5f;
        reset_maps();
        for (i = 0; i <= 5; ++i) set_slope(8 - i, 10 + i, 45.0f, 1);
        for (d = 0; d < 5; ++d) {
            slope_friction_body b;
            float fx = 8 * 8 + 6, fy = foot_y_on(8, 10, fx);
            int frames = (int)(DURATION / dts[d] + 0.5f);
            slope_friction_begin(&b, fx - 2, fy - 4, 0, 0);
            for (i = 0; i < frames; ++i)
                slope_friction_step(solid, ang, sdir, W, H, 4, 4, dts[d],
                                    &p, &b, NULL);
            ex[d] = b.x; ey[d] = b.y;
        }
        for (d = 0; d < 4; ++d) {
            CHECK(NEAR(ex[d], ex[3], 1e-3f));
            CHECK(NEAR(ey[d], ey[3], 1e-3f));
            printf("frame-rate %.0f fps vs 240 fps: d=(%.6f,%.6f)\n",
                   1.0f / dts[d], ex[d] - ex[3], ey[d] - ey[3]);
        }
        /* 144 fps is not a multiple of 1/240, so the substep grid cannot be
         * the same; the drift is the O(dt_max) splitting error of the legacy
         * scheme, not a change of trajectory.  Two checks pin that down:
         * (a) it stays a small fraction of the path travelled, and
         * (b) halving dt_max roughly halves it -- the frame rate itself must
         *     not be what sets the error. */
        {
            float fx = 8 * 8 + 6, fy = foot_y_on(8, 10, fx);
            float path = sqrtf((ex[3] - (fx - 2)) * (ex[3] - (fx - 2)) +
                               (ey[3] - (fy - 4)) * (ey[3] - (fy - 4)));
            float err = sqrtf((ex[4] - ex[3]) * (ex[4] - ex[3]) +
                              (ey[4] - ey[3]) * (ey[4] - ey[3]));
            float err_half;
            slope_friction_params p2 = P(0.99f, 0.80f, 1.0f / 480.0f);
            slope_friction_body b2, b3;
            int frames_144 = (int)(DURATION / dts[4] + 0.5f);
            int frames_240 = (int)(DURATION / dts[3] + 0.5f);

            /* 30/60/120 fps are bit-identical to 240 fps; 144 fps can only
             * differ by the splitting error of the legacy slope_move step,
             * measured below at ~1.2% of the path after 0.5 s. */
            CHECK(err <= 2e-2f * path + 1e-4f);

            slope_friction_begin(&b2, fx - 2, fy - 4, 0, 0);
            for (i = 0; i < frames_144; ++i)
                slope_friction_step(solid, ang, sdir, W, H, 4, 4, dts[4],
                                    &p2, &b2, NULL);
            slope_friction_begin(&b3, fx - 2, fy - 4, 0, 0);
            for (i = 0; i < frames_240; ++i)
                slope_friction_step(solid, ang, sdir, W, H, 4, 4, dts[3],
                                    &p2, &b3, NULL);
            err_half = sqrtf((b2.x - b3.x) * (b2.x - b3.x) +
                             (b2.y - b3.y) * (b2.y - b3.y));
            printf("frame-rate 144 fps vs 240 fps: d=(%.6f,%.6f) |d|/path=%.2e\n",
                   ex[4] - ex[3], ey[4] - ey[3], err / (path > 0.0f ? path : 1.0f));
            printf("            same at dt_max/2: |d|=%.6f (ratio %.2f)\n",
                   err_half, err_half / (err > 0.0f ? err : 1.0f));
            CHECK(err_half < err * 0.75f);   /* error is bounded by dt_max */
        }
    }

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
