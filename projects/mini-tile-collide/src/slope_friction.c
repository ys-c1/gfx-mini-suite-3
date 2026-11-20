#include "slope_friction.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define FRIC_DEG (3.14159265358979323846f / 180.0f)
/* Relative slack on the static-friction cone.  Without it the comparison at
 * exactly theta_c = atan(mu_s) is decided by the last bit of a float and the
 * body alternates between "held" and "slips" from frame to frame. */
#define FRIC_CONE_SLACK 1.0e-5f
#define FRIC_MAX_SUBSTEPS 1000000

void slope_friction_defaults(slope_friction_params* p) {
    if (!p) return;
    p->mu_static = 0.0f;
    p->mu_kinetic = 0.0f;
    p->dt_max = 1.0f / 240.0f;
    p->gravity = 300.0f;
}

int slope_friction_params_valid(const slope_friction_params* p) {
    if (!p) return -1;
    if (!isfinite(p->mu_static) || !isfinite(p->mu_kinetic) ||
        !isfinite(p->dt_max) || !isfinite(p->gravity)) return -1;
    if (!(p->mu_kinetic >= 0.0f)) return -1;
    if (!(p->mu_static >= p->mu_kinetic)) return -1;
    if (!(p->dt_max > 0.0f)) return -1;
    if (!(p->gravity >= 0.0f)) return -1;
    return 0;
}

/* Minimal key=value reader with the same syntax as config.c, kept local so
 * the friction unit tests do not need the application layer. */
static int cfg_f(const char* buf, const char* key, float* out) {
    size_t klen;
    const char* p;
    char tmp[64];
    size_t i;
    if (!buf || !key || !out) return -1;
    klen = strlen(key);
    p = buf;
    while ((p = strstr(p, key)) != NULL) {
        int line_start = (p == buf) || p[-1] == '\n' || p[-1] == '\r';
        if (line_start && p[klen] == '=') {
            double v;
            p += klen + 1;
            i = 0;
            while (*p && *p != '\n' && *p != '\r' && i + 1 < sizeof(tmp))
                tmp[i++] = *p++;
            tmp[i] = '\0';
            v = atof(tmp);
            if (!isfinite(v)) return -1;
            *out = (float)v;
            return 0;
        }
        p += klen;
    }
    return -1;
}

int slope_friction_params_from_config(const char* cfg_buf,
                                      slope_friction_params* p) {
    float v;
    if (!p) return -1;
    slope_friction_defaults(p);
    if (cfg_f(cfg_buf, "mu_static", &v) == 0) p->mu_static = v;
    if (cfg_f(cfg_buf, "mu_kinetic", &v) == 0) p->mu_kinetic = v;
    if (cfg_f(cfg_buf, "dt_max", &v) == 0) p->dt_max = v;
    if (cfg_f(cfg_buf, "gravity", &v) == 0) p->gravity = v;
    return slope_friction_params_valid(p);
}

float slope_friction_critical_angle(const slope_friction_params* p) {
    if (!p) return 0.0f;
    return (float)(atan((double)p->mu_static) / M_PI * 180.0);
}

float slope_friction_velocity_jitter_bound(const slope_friction_params* p,
                                           float dt) {
    if (!p || !(dt > 0.0f) || !(p->gravity >= 0.0f)) return 0.0f;
    /* A held body is zeroed every substep; the only speed it can pick up
     * before the next zeroing is one gravity increment g*dt, and the rest
     * test ignores speeds below V_EPS. */
    return p->gravity * dt + SLOPE_FRICTION_V_EPS;
}

float slope_friction_position_jitter_bound(const slope_friction_params* p,
                                           float dt) {
    if (!p || !(dt > 0.0f) || !(p->gravity >= 0.0f)) return 0.0f;
    /* Same argument applied to the semi-implicit position update. */
    return 0.5f * p->gravity * dt * dt + SLOPE_FRICTION_V_EPS * dt;
}

void slope_friction_begin(slope_friction_body* b,
                          float x, float y, float vx, float vy) {
    if (!b) return;
    memset(b, 0, sizeof(*b));
    b->x = x; b->y = y; b->vx = vx; b->vy = vy;
    b->last_tx = b->last_ty = -1;
    b->at_rest = (fabsf(vx) <= SLOPE_FRICTION_V_EPS &&
                  fabsf(vy) <= SLOPE_FRICTION_V_EPS) ? 1 : 0;
}

/* One substep of length h.
 *
 *   1. gravity:            v.y += g*h
 *   2. candidate motion:   p += v*h
 *   3. contact solver:     slope_move()  (unchanged; clamps the foot onto
 *                          the first surface crossed, removes v_n)
 *   4. friction:           static iff the body entered the substep at rest
 *                          AND the tangential speed left by this substep's
 *                          gravity increment fits in the static cone
 *                          mu_s*a_n*h; otherwise kinetic, removing
 *                          mu_k*a_n*h and clamping at zero (never reversing).
 *   5. held bodies get the tangential part of step 2 undone, so a body
 *      that static friction holds does not creep downhill by O(g*h^2).
 *
 * Both friction branches only ever shrink |v_t|, and step 3 only removes
 * v_n, so |v| cannot grow except through gravity; the scheme is
 * non-expansive for every h and for every static/kinetic switch pattern. */
static int substep(const unsigned char* solid, const float* ang,
                   const signed char* dir, int w, int h_map,
                   float bw, float bh, float h,
                   const slope_friction_params* p,
                   slope_friction_body* b, slope_contact_t* last) {
    float px = b->x, py = b->y;
    float vx0 = b->vx, vy0 = b->vy;
    int was_rest = b->at_rest;
    slope_contact_t ct;
    int hit;

    b->vy += p->gravity * h;
    b->x += b->vx * h;
    b->y += b->vy * h;

    memset(&ct, 0, sizeof(ct));
    hit = slope_move(solid, ang, dir, w, h_map, px, py,
                     &b->x, &b->y, bw, bh, &b->vx, &b->vy, &ct);
    if (hit < 0) return hit;

    if (hit == 1) {
        float nx = ct.nx, ny = ct.ny;
        float tx = -ny, ty = nx;              /* surface tangent, y down */
        float vn = b->vx * nx + b->vy * ny;   /* >= 0: never moving inward */
        float a_n = -p->gravity * ny;         /* support load, per unit mass */

        if (a_n < 0.0f) a_n = 0.0f;           /* ceiling: gravity pulls away */
        if (vn > SLOPE_FRICTION_V_EPS) a_n = 0.0f; /* separating: no load */

        if (a_n > 0.0f) {
            float vt = b->vx * tx + b->vy * ty;
            float vt0 = vx0 * tx + vy0 * ty;
            float vt_abs = fabsf(vt);
            float dv_static = p->mu_static * a_n * h;
            float dv_kinetic = p->mu_kinetic * a_n * h;
            int held = 0;

            if (fabsf(vt0) <= SLOPE_FRICTION_V_EPS &&
                vt_abs <= dv_static * (1.0f + FRIC_CONE_SLACK) +
                          SLOPE_FRICTION_V_EPS) {
                vt = 0.0f;
                held = 1;
            } else {
                float s = (vt > 0.0f) ? 1.0f : -1.0f;
                if (vt_abs <= dv_kinetic) vt = 0.0f;
                else vt -= s * dv_kinetic;
            }

            b->vx = vt * tx;
            b->vy = vt * ty;
            if (vt == 0.0f) { b->vx = 0.0f; b->vy = 0.0f; }
            b->at_rest = held;

            if (held && was_rest) {
                /* Undo the tangential drift accumulated while the body was
                 * already held.  The normal clamp stays, so the foot is
                 * still exactly on the surface. */
                float dx = b->x - px, dy = b->y - py;
                float dtt = dx * tx + dy * ty;
                b->x -= dtt * tx;
                b->y -= dtt * ty;
            }
        } else {
            b->at_rest = 0;
        }
        b->contacts++;
    } else {
        b->at_rest = 0;
    }

    b->substeps++;
    if (last && hit == 1) {
        last->nx = ct.nx; last->ny = ct.ny;
        last->depth = ct.depth;
        last->slide_vx = b->vx; last->slide_vy = b->vy;
        last->tx = ct.tx; last->ty = ct.ty;
        last->is_slope = ct.is_slope;
        b->last_tx = ct.tx;
        b->last_ty = ct.ty;
    }
    return hit;
}

int slope_friction_step(const unsigned char* solid,
                        const float* slope_angles,
                        const signed char* slope_dirs,
                        int w, int h_map,
                        float bw, float bh,
                        float dt,
                        const slope_friction_params* p,
                        slope_friction_body* b,
                        slope_contact_t* out) {
    float remaining;
    int any = 0;
    slope_contact_t last;
    if (!b || !p || slope_friction_params_valid(p) != 0) return -1;
    if (!(dt > 0.0f) || !isfinite(dt)) return -1;
    if (!(bw > 0.0f) || !(bh > 0.0f) || w <= 0 || h_map <= 0) return -1;

    memset(&last, 0, sizeof(last));
    remaining = dt;
    {
        /* Equal substeps, not a full step plus a ragged remainder: pick
         * n = round(dt / dt_max), bumped up by one if that would push h over
         * dt_max.  Frame lengths that are integer multiples of dt_max
         * (30/60/120/240 fps here) give exactly n steps of dt_max, so the
         * legacy flat-ground and wall results are reproduced bit for bit;
         * non-multiples (144 fps) split the frame evenly, keeping the error
         * against the 240 fps run far below a pixel. */
        double nd = (double)dt / (double)p->dt_max;
        int n = (int)(nd + 0.5);
        int k;
        if (n < 1) n = 1;
        if ((double)dt / (double)n > (double)p->dt_max * (1.0 + 1e-6)) ++n;
        if (n > FRIC_MAX_SUBSTEPS) return -1;
        for (k = 0; k < n; ++k) {
            float step = (k == n - 1) ? remaining : dt / (float)n;
            int r = substep(solid, slope_angles, slope_dirs, w, h_map,
                            bw, bh, step, p, b, &last);
            if (r < 0) return -1;
            if (r == 1) {
                if (!any && out) *out = last;
                any = 1;
            }
            remaining -= step;
        }
    }

    if (b->at_rest) b->rest_frames++;
    else b->rest_frames = 0;
    return any ? 1 : 0;
}
