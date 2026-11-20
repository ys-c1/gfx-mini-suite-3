#include "../src/tile.h"
#include "../src/slope.h"
#include "../src/slope_friction.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Four slope scenarios x four frame rates.
 *
 * Prints, per slope and per external frame length:
 *   - convergence frame   : first frame after which the body is at rest for
 *                           every remaining frame (or -1 if it never rests)
 *   - max jitter          : max |v| and max |dy| over the run
 *   - energy growth       : max per-frame increase of kinetic+potential
 *   - elapsed wall time   : CPU time spent in slope_friction_step
 * and writes a CSV trace for tools/friction_plot.py.
 *
 * Everything here is read from friction.cfg; nothing is hard-coded. */

enum { W = 24, H = 24 };
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

static int read_f(const char* buf, const char* key, float* out) {
    size_t klen = strlen(key);
    const char* p = buf;
    while ((p = strstr(p, key)) != NULL) {
        if ((p == buf || p[-1] == '\n' || p[-1] == '\r') && p[klen] == '=') {
            *out = (float)atof(p + klen + 1);
            return 1;
        }
        p += klen;
    }
    return 0;
}

static int read_list(const char* buf, const char* key, float* out, int cap) {
    size_t klen = strlen(key);
    const char* p = buf;
    int n = 0;
    while ((p = strstr(p, key)) != NULL) {
        if ((p == buf || p[-1] == '\n' || p[-1] == '\r') && p[klen] == '=') {
            const char* q = p + klen + 1;
            while (n < cap) {
                char* end;
                double v = strtod(q, &end);
                if (end == q) break;
                out[n++] = (float)v;
                q = end;
                if (*q != ',') break;
                ++q;
            }
            return n;
        }
        p += klen;
    }
    return 0;
}

/* ---------------- the four scenarios (`kind` picks the map) ---------------- */
typedef struct {
    const char* name;
    const char* slug;
    float theta;      /* for slope scenarios */
    int kind;         /* 0 = plain slope, 1 = chained ramp, 2 = slope->floor,
                         3 = near critical */
    float vx0, vy0;
} scenario;

enum { SC_PLAIN = 0, SC_CHAIN = 1, SC_MIXED = 2, SC_NEAR = 3 };

static void build_map(const scenario* s, float fx, float* out_fy) {
    reset_maps();
    switch (s->kind) {
    case SC_PLAIN:
        /* single 20 deg tile, below the critical angle: from rest, static
         * friction must hold the body exactly still forever. */
        set_slope(12, 10, s->theta, 1);
        *out_fy = slope_surface_y(solid, ang, sdir, W, H, 12, 10, fx);
        break;
    case SC_CHAIN: {
        int i;
        /* Any 45 deg chain is one continuous plane (adjacent tiles offset by
         * exactly the tile size on both axes).  This one runs the full map
         * diagonal, (20,0) down to (0,20), so the body stays on it. */
        for (i = 0; i <= 20; ++i) set_slope(20 - i, 0 + i, 45.0f, 1);
        *out_fy = slope_surface_y(solid, ang, sdir, W, H, 20, 0, fx);
        break;
    }
    case SC_MIXED: {
        int i;
        /* 45 deg chained ramp (16,11)..(12,15) whose last corner (96,128)
         * is the top-left corner of the solid floor at row 16.  The body
         * accelerates down the ramp, then slides along the flat floor until
         * kinetic friction stops it and static friction holds it there. */
        for (i = 0; i <= 4; ++i) set_slope(16 - i, 11 + i, 45.0f, 1);
        for (i = 0; i < W; ++i) solid[16 * W + i] = 1;
        *out_fy = slope_surface_y(solid, ang, sdir, W, H, 16, 11, fx);
        break;
    }
    default:
        set_slope(12, 10, s->theta, 1);
        *out_fy = slope_surface_y(solid, ang, sdir, W, H, 12, 10, fx);
        break;
    }
}

/* Start point per scenario: SC_MIXED starts on the slope's upper tile. */
static float start_fx(const scenario* s) {
    if (s->kind == SC_MIXED) return 16 * 8 + 6.0f;  /* foot x = 134 */
    if (s->kind == SC_CHAIN) return 20 * 8 + 4.0f;
    return 12 * 8 + 4.0f;
}

/* ---- naive reference: one friction decision per external frame ----------
 * This is the "compute the speed per frame" scheme the friction layer
 * replaces: gravity, one ballistic move, slope_move's projection, then a
 * single static/kinetic test for the whole frame (no substepping, no
 * static-cone slack, no drift undo).  It is kept here only so the report can
 * show what it does at the critical angle; production code never calls it. */
static void naive_frame(float* x, float* y, float* vx, float* vy,
                        float h, const slope_friction_params* p,
                        int* at_rest) {
    float px = *x, py = *y;
    slope_contact_t ct;
    *vy += p->gravity * h;
    *x += *vx * h;
    *y += *vy * h;
    memset(&ct, 0, sizeof(ct));
    if (slope_move(solid, ang, sdir, W, H, px, py, x, y, 4, 4, vx, vy, &ct) == 1) {
        float tx = -ct.ny, ty = ct.nx;
        float vt = *vx * tx + *vy * ty;
        float a_n = -p->gravity * ct.ny;
        if (a_n < 0.0f) a_n = 0.0f;
        if (fabsf(vt) <= p->mu_static * a_n * h) vt = 0.0f;
        else {
            float dv = p->mu_kinetic * a_n * h;
            float s = (vt > 0.0f) ? 1.0f : -1.0f;
            vt = (fabsf(vt) <= dv) ? 0.0f : vt - s * dv;
        }
        *vx = vt * tx;
        *vy = vt * ty;
        *at_rest = (vt == 0.0f);
    } else {
        *at_rest = 0;
    }
}

typedef struct {
    int converge_frame;
    double converge_time;
    float max_speed, max_dy;
    float max_jitter_v, max_jitter_y;   /* frame-to-frame ripple */
    double max_energy_gain;
    double seconds;
    double energy0, energy_end;
    int frames;
    int switches;
    int rest_frames;
    float final_x, final_y;
    int rest_ever;
    int flips;              /* at_rest toggles over the run */
    float naive_maxdv;      /* frame-to-frame |dv| of the naive reference */
    float nx, ny;           /* naive reference final position */
} result;

/* Runs one scenario at one external dt; appends the trace to `trace`
 * (may be NULL).  Returns 0 on success. */
static int run_case(const scenario* sc, float dt, const slope_friction_params* p,
                    float seconds, FILE* trace, result* r,
                    int trace_stride, int trace_id) {
    slope_friction_body b;
    float fx = start_fx(sc), fy = 0.0f;
    int i, frames, last_rest = -1, rest_run = 0, prev_rest = -1;
    double e_prev;
    float prev_vx = 0.0f, prev_vy = 0.0f, prev_y = 0.0f;
    double prev_ax = 0.0, prev_ay = 0.0, jvm = 0.0, jym = 0.0;
    int naive_rest = 0, naive_prev = -1;
    float nx = 0.0f, ny = 0.0f, nvx = 0.0f, nvy = 0.0f, npvx = 0.0f, npvy = 0.0f;
    slope_contact_t ct;

    memset(r, 0, sizeof(*r));
    build_map(sc, fx, &fy);
    if (!isfinite(fy)) return -1;
    slope_friction_begin(&b, fx - 2.0f, fy - 4.0f, sc->vx0, sc->vy0);
    nx = fx - 2.0f; ny = fy - 4.0f; nvx = sc->vx0; nvy = sc->vy0;
    frames = (int)(seconds / dt + 0.5f);
    if (frames < 1) frames = 1;
    r->frames = frames;
    r->max_dy = 0.0f;
    r->max_speed = 0.0f;
    r->max_energy_gain = 0.0;
    r->max_jitter_v = 0.0f;
    r->max_jitter_y = 0.0f;
    prev_ax = prev_ay = jvm = jym = 0.0;
    r->energy0 = 0.5 * ((double)b.vx * b.vx + (double)b.vy * b.vy)
                 - (double)p->gravity * b.y;
    {
        double t0 = (double)clock();
        int contacts = 0;
        float fy0 = b.y;
        prev_vx = b.vx; prev_vy = b.vy; prev_y = b.y;
        e_prev = r->energy0;
        for (i = 0; i < frames; ++i) {
            int hit;
            double e;
            hit = slope_friction_step(solid, ang, sdir, W, H, 4, 4, dt, p, &b, &ct);
            if (hit < 0) return -1;
            if (hit == 1) ++contacts;
            naive_frame(&nx, &ny, &nvx, &nvy, dt, p, &naive_rest);
            if (naive_prev >= 0 && naive_prev != naive_rest) ++r->flips;
            naive_prev = naive_rest;
            if (i > 2) {
                float dv = sqrtf((nvx - npvx) * (nvx - npvx) +
                                 (nvy - npvy) * (nvy - npvy));
                if (dv > r->naive_maxdv) r->naive_maxdv = dv;
            }
            npvx = nvx; npvy = nvy;
            if (b.at_rest) r->rest_ever = 1;
            e = 0.5 * ((double)b.vx * b.vx + (double)b.vy * b.vy)
                - (double)p->gravity * b.y;
            if (e - e_prev > r->max_energy_gain) r->max_energy_gain = e - e_prev;
            e_prev = e;
            if (fabsf(b.x) > 1e6f || fabsf(b.y) > 1e6f ||
                !isfinite(b.vx) || !isfinite(b.vy)) return -2;
            if (b.at_rest) {
                if (rest_run == 0) last_rest = i;
                ++rest_run;
            } else {
                rest_run = 0;
            }
            if (prev_rest >= 0 && prev_rest != b.at_rest) ++r->switches;
            prev_rest = b.at_rest;
            {
                float sp = sqrtf(b.vx * b.vx + b.vy * b.vy);
                float dvx = b.vx - prev_vx, dvy = b.vy - prev_vy;
                float jv = sqrtf(dvx * dvx + dvy * dvy);
                float jy = fabsf(b.y - prev_y);
                if (sp > r->max_speed) r->max_speed = sp;
                if (fabsf(b.y - fy0) > r->max_dy) r->max_dy = fabsf(b.y - fy0);
                /* Jitter is the ripple AROUND a smooth slide: remove the
                 * one-frame velocity change the integrator applies to every
                 * body (gravity, plus the normal projection on contact) and
                 * keep only what the static/kinetic switch adds.  Computed
                 * here in double from the per-frame acceleration; exact for
                 * a smooth trajectory, so its max is the switch's ripple. */
                if (i > 0) {
                    double ax = ((double)b.vx - (double)prev_vx) / (double)dt;
                    double ay = ((double)b.vy - (double)prev_vy) / (double)dt;
                    prev_ax = ax; prev_ay = ay;
                }
                if (i > 2) {
                    double jax = fabs(((double)b.vx - (double)prev_vx) / (double)dt - prev_ax);
                    double jay = fabs(((double)b.vy - (double)prev_vy) / (double)dt - prev_ay);
                    if (jax > jvm) jvm = jax;
                    if (jay > jym) jym = jay;
                }
                (void)jv; (void)jy;
                prev_vx = b.vx; prev_vy = b.vy; prev_y = b.y;
            }
            if (trace && (i % trace_stride == 0 || i == frames - 1)) {
                fprintf(trace, "%d,%.9g,%d,%.9g,%.9g,%.9g,%.9g,%.9g,%d,%.9g,%.9g,%d\n",
                        trace_id, (double)i * dt, b.at_rest, (double)b.x, (double)b.y,
                        (double)b.vx, (double)b.vy, e, contacts,
                        (double)nx, (double)ny, naive_rest);
            }
        }
        r->seconds = ((double)clock() - t0) / (double)CLOCKS_PER_SEC;
        r->max_jitter_v = (float)jvm;
        r->max_jitter_y = (float)(jym * (double)dt * (double)dt);
        (void)contacts;
    }
    /* convergence: the first frame from which at_rest never drops again */
    r->converge_frame = (rest_run > 0) ? last_rest + 1 : -1;
    r->converge_time = (rest_run > 0) ? (double)(last_rest + 1) * (double)dt : -1.0;
    r->rest_frames = rest_run;
    r->final_x = b.x;
    r->final_y = b.y;
    r->nx = nx;
    r->ny = ny;
    r->energy_end = 0.5 * ((double)b.vx * b.vx + (double)b.vy * b.vy)
                    - (double)p->gravity * b.y;
    return 0;
}

int main(int argc, char** argv) {
    const char* cfg_path = (argc > 1) ? argv[1] : "friction.cfg";
    FILE* cfg;
    char buf[4096];
    size_t n;
    slope_friction_params p;
    float dts[8];
    int ndts, i;
    float seconds = 1.5f, critical_window = 0.25f, tcrit;
    FILE* trace;
    scenario scs[4];

    if (!(cfg = fopen(cfg_path, "rb"))) { printf("config error\n"); return 3; }
    n = fread(buf, 1, sizeof(buf) - 1, cfg);
    fclose(cfg);
    buf[n] = '\0';

    if (slope_friction_params_from_config(buf, &p) != 0) {
        printf("bad friction params\n");
        return 4;
    }
    ndts = read_list(buf, "frame_dts", dts, 8);
    if (ndts < 1) { printf("frame_dts missing\n"); return 4; }
    if (read_f(buf, "duration", &seconds) && !(seconds > 0.0f)) return 4;
    if (!read_f(buf, "critical_window", &critical_window)) critical_window = 0.25f;
    tcrit = slope_friction_critical_angle(&p);

    {
        float vx0 = 0.0f, vy0 = 0.0f;
        read_f(buf, "v0x", &vx0);
        read_f(buf, "v0y", &vy0);
        scs[0].name = "S1 20deg slope below critical, from rest";
        scs[0].slug = "s1_hold20";
        scs[0].theta = 20.0f; scs[0].kind = SC_PLAIN;
        scs[0].vx0 = vx0; scs[0].vy0 = vy0;
        scs[1].name = "S2 chained 45deg ramp, from rest";
        scs[1].slug = "s2_chain45";
        scs[1].theta = 45.0f; scs[1].kind = SC_CHAIN;
        scs[1].vx0 = vx0; scs[1].vy0 = vy0;
        scs[2].name = "S3 45deg ramp onto solid floor";
        scs[2].slug = "s3_mixed45";
        scs[2].theta = 45.0f; scs[2].kind = SC_MIXED;
        scs[2].vx0 = vx0; scs[2].vy0 = vy0;
        scs[3].name = "S4 at critical (theta = atan mu_s)";
        scs[3].slug = "s4_critical";
        scs[3].theta = tcrit; scs[3].kind = SC_NEAR;
        scs[3].vx0 = vx0; scs[3].vy0 = vy0;
    }

    (void)critical_window;
    printf("# friction: mu_s=%.4f mu_k=%.4f dt_max=%.6f g=%.1f critical=%.4f deg\n",
           p.mu_static, p.mu_kinetic, p.dt_max, p.gravity, tcrit);
    printf("# scenario,dt,fps,converge_frame,rest_frames,max_speed,max_dy,max_jitter_v,max_jitter_y,energy_gain,switches,elapsed_s,rest_ever,naive_flips,naive_maxdv,rest_x,rest_y,naive_x,naive_y,conv_time_s\n");

    if (argc > 2) {
        trace = fopen(argv[2], "wb+");
        if (!trace) { printf("trace open failed\n"); return 5; }
        fprintf(trace, "id,t,at_rest,x,y,vx,vy,energy,contacts,naive_x,naive_y,naive_rest\n");
    } else {
        trace = NULL;
    }

    for (i = 0; i < 4; ++i) {
        int d;
        for (d = ndts - 1; d >= 0; --d) {   /* slowest -> fastest frame */
            result r;
            int id = i * 16 + d;
            if (run_case(&scs[i], dts[d], &p, seconds, trace, &r, 1, id) != 0) {
                printf("run failed: %s dt=%.6f\n", scs[i].slug, dts[d]);
                if (trace) fclose(trace);
                return 6;
            }
            printf("%-16s,%.8f,%.2f,%d,%d,%.6f,%.6f,%.6f,%.6f,%.3e,%d,%.6f,%d,%d,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n",
                   scs[i].slug, (double)dts[d], 1.0 / (double)dts[d],
                   r.converge_frame, r.rest_frames, (double)r.max_speed,
                   (double)r.max_dy, (double)r.max_jitter_v, (double)r.max_jitter_y,
                   r.max_energy_gain, r.switches, r.seconds,
                   r.rest_ever, r.flips, (double)r.naive_maxdv,
                   (double)r.final_x, (double)r.final_y,
                   (double)r.nx, (double)r.ny, r.converge_time);
            (void)id;
        }
    }
    if (trace) fclose(trace);
    return 0;
}
