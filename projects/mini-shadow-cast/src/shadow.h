#ifndef SHADOW_H
#define SHADOW_H

typedef struct {
    float x, y;       /* light position */
    float weight;     /* blending weight, negative values are clamped to 0 */
} shadow_light;

typedef struct {
    int    samples;     /* number of grid samples (nx*ny) */
    int    lit;         /* samples whose composite brightness is > 0 */
    int    clamped;     /* number of lights whose weight was clamped from <0 to 0 */
    int    coincident;  /* number of light pairs closer than 1e-9 */
    double wsum;        /* sum of effective (clamped) weights */
    double mean;        /* mean composite brightness over the grid */
    double vmin;        /* min composite brightness */
    double vmax;        /* max composite brightness */
    double lit_ratio;   /* lit / samples */
} shadow_report;

int shadow_project(float x1,float y1,float x2,float y2,float lx,float ly,float reach,float* ox1,float* oy1,float* ox2,float* oy2);
int shadow_point_lit(float px,float py,float lx,float ly,const float* segs,int nseg);

/*
 * Composite brightness at one point.
 * Each light casts its own shadow via shadow_point_lit; per-light 0/1 lit
 * flags are blended linearly with clamped weights:
 *     B = sum(lit_i * max(0,w_i)) / sum(max(0,w_i))
 * A zero-light configuration or a zero effective weight sum yields B = 0
 * deterministically. Returns 0 on success, -1 on bad arguments.
 */
int shadow_brightness(float px,float py,const shadow_light* lights,int nlight,
                      const float* segs,int nseg,double* out_brightness);

/*
 * Sample the composite brightness on a nx*ny cell-center grid over
 * [x0,x1] x [y0,y1], row-major (ny rows, nx columns). grid may be NULL when
 * only the report is wanted; rep may be NULL. Returns 0 on success, -1 on
 * bad arguments. A zero-light configuration is valid and fills the grid
 * with zeros.
 */
int shadow_sample_grid(double x0,double y0,double x1,double y1,int nx,int ny,
                       const shadow_light* lights,int nlight,
                       const float* segs,int nseg,
                       double* grid,shadow_report* rep);
#endif
