#ifndef MINI_SLOPE_FRICTION_H
#define MINI_SLOPE_FRICTION_H

/* Friction-aware, variable-step slope integrator (add-on for slope.c).
 *
 * The original slope_move() is left byte-for-byte untouched: it remains the
 * geometric contact solver.  This layer wraps it with
 *
 *   - variable external frame dt, subdivided so no internal step exceeds
 *     params.dt_max;
 *   - Coulomb friction with an explicit static / kinetic switch:
 *       static  : cancels the whole tangential motion of the substep when
 *                 |a_t| <= mu_s * a_n and the body was already at rest,
 *       kinetic : removes mu_k * a_n * h from |v_t|, clamped at zero so a
 *                 single substep can never reverse the slide direction;
 *   - an "at rest" flag that latches as soon as a static contact holds and
 *     stays latched while the body remains on a supporting surface.
 *
 * Stability properties (proved in REPORT.md, checked in tests/test_friction.c):
 *   - both friction branches are non-expansive in |v| and can only remove
 *     energy, so no step size and no switch of branch can diverge;
 *   - a body that can be held by static friction reports at_rest on the
 *     first substep that has it, and repeatedly in the same frame, so the
 *     rest decision converges in a finite, bounded number of frames;
 *   - near the critical angle the tangential speed ripple is bounded by
 *     slope_friction_velocity_jitter_bound(); as h -> 0 the bound -> 0.
 *
 * All quantities are per unit mass: "force" is acceleration, "mass" is 1.
 * Coordinates match tile.c / slope.c: y grows downward, tiles are 8x8 px.
 */

#include "slope.h"

#define SLOPE_FRICTION_V_EPS 1.0e-4f  /* px/s: speed treated as "already at rest" */

typedef struct {
    float mu_static;   /* coefficient of static friction, >= mu_kinetic */
    float mu_kinetic;  /* coefficient of kinetic friction, >= 0 */
    float dt_max;      /* largest internal step, seconds */
    float gravity;     /* acceleration along +y, px/s^2 */
} slope_friction_params;

typedef struct {
    float x, y;        /* AABB top-left, world px */
    float vx, vy;      /* px/s */
    int at_rest;       /* 1 while a static contact holds this frame */
    int rest_frames;   /* consecutive frames ending at rest */
    int contacts;      /* substeps that had a contact */
    int substeps;      /* substeps executed since begin() */
    int last_tx, last_ty;
} slope_friction_body;

/* Defaults are frictionless with dt_max = 1/240 s, i.e. exactly the old
 * slope_move behaviour for any caller that does not opt in. */
void slope_friction_defaults(slope_friction_params* p);

/* Read mu_static, mu_kinetic, dt_max and gravity from a config buffer.
 * Missing keys keep their default (frictionless), so legacy configs are
 * unchanged.  Returns 0 when the resulting parameters are valid, -1 else. */
int slope_friction_params_from_config(const char* cfg_buf,
                                      slope_friction_params* p);

/* 0 when 0 <= mu_kinetic <= mu_static, dt_max > 0, gravity >= 0; -1 else. */
int slope_friction_params_valid(const slope_friction_params* p);

/* atan(mu_s), in degrees: slopes below it can be held from rest. */
float slope_friction_critical_angle(const slope_friction_params* p);

/* Rigorous upper bounds on what a single substep can still move a body
 * that is held by static friction:
 *   velocity: g * dt + V_EPS            (px/s)
 *   position: 0.5 * g * dt^2 + V_EPS*dt (px)
 * Both go to zero as dt -> 0. */
float slope_friction_velocity_jitter_bound(const slope_friction_params* p,
                                           float dt);
float slope_friction_position_jitter_bound(const slope_friction_params* p,
                                           float dt);

void slope_friction_begin(slope_friction_body* b,
                          float x, float y, float vx, float vy);

/* Advance the body by one external frame of length dt (any value > 0).
 * Returns 1 if any substep touched a surface, 0 if airborne throughout,
 * -1 on bad arguments.  `out` (optional) receives the last contact. */
int slope_friction_step(const unsigned char* solid,
                        const float* slope_angles,
                        const signed char* slope_dirs,
                        int w, int h,
                        float bw, float bh,
                        float dt,
                        const slope_friction_params* p,
                        slope_friction_body* b,
                        slope_contact_t* out);

#endif
