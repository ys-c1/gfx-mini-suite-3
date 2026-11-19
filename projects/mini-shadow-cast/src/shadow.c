#include "shadow.h"
#include <math.h>
int shadow_project(float x1,float y1,float x2,float y2,float lx,float ly,float reach,
                   float* ox1,float* oy1,float* ox2,float* oy2){
    if(!ox1||!oy1||!ox2||!oy2||reach<=0.0f) return -1;
    float d1x=x1-lx, d1y=y1-ly, d2x=x2-lx, d2y=y2-ly;
    float n1=sqrtf(d1x*d1x+d1y*d1y), n2=sqrtf(d2x*d2x+d2y*d2y);
    if(n1<1e-6f||n2<1e-6f) return -1;
    *ox1=x1+d1x/n1*reach; *oy1=y1+d1y/n1*reach;
    *ox2=x2+d2x/n2*reach; *oy2=y2+d2y/n2*reach;
    return 0;
}
int shadow_point_lit(float px,float py,float lx,float ly,const float* segs,int nseg){
    if(!segs||nseg<=0) return -1;
    int lit=1;
    for(int i=0;i<nseg;++i){
        float ax=segs[i*4],ay=segs[i*4+1],bx=segs[i*4+2],by=segs[i*4+3];
        float rx=px-lx, ry=py-ly, sx=bx-ax, sy=by-ay;
        float den=rx*sy-ry*sx;
        if(fabsf(den)<1e-9f) continue;
        float t=((ax-lx)*sy-(ay-ly)*sx)/den;
        float u=((ax-lx)*ry-(ay-ly)*rx)/den;
        if(t>0.0f&&t<1.0f&&u>=0.0f&&u<=1.0f){ lit=0; break; }
    }
    return lit;
}

/* ---- soft area-light penumbra ----------------------------------------
 * Geometry: intersect the light ray L->P with the occluder's supporting
 * line (parameter t), clamp the intersection depth to the segment so C is
 * the point at P's ray depth on the segment. w is the perpendicular
 * distance from C to the segment plane in cross-ray units, i.e. how far P
 * sits laterally from the umbra boundary at that depth.
 *
 * Band half-width:  wu = light_size*f*(1-f) + trans_width,
 * f = |C-L| / |P-L| in (0,1). The geometric term vanishes at the contact
 * point (f->0) and at the light plane (f->1); trans_width keeps the band
 * finite there. visibility = smoothstep(w / wu):
 *   - w == 0 (ray truly blocked, full backlight region) -> exact 0.0f,
 *     the band only widens the penumbra outside the umbra, never leaks
 *     light into it;
 *   - smoothstep is C1 with zero slope at both ends -> no visible banding
 *     steps compared with a piecewise-linear ramp;
 *   - monotone non-decreasing in w, so edge brightness varies
 *     monotonically with distance from the occluder boundary.
 */
static int finite_f(float v){
    return isfinite(v);
}
int shadow_soft_factor(float px,float py,float lx,float ly,
                       float ax,float ay,float bx,float by,
                       const shadow_soft_t* cfg,float* vis){
    if(!cfg||!vis) return -1;
    if(!finite_f(px)||!finite_f(py)||!finite_f(lx)||!finite_f(ly)||
       !finite_f(ax)||!finite_f(ay)||!finite_f(bx)||!finite_f(by)||
       !finite_f(cfg->light_size)||!finite_f(cfg->trans_width))
        return -1;
    if(cfg->light_size<=0.0f) return -2;
    if(cfg->trans_width<0.0f) return -3;
    float sx=bx-ax, sy=by-ay;
    float slen2=sx*sx+sy*sy;
    if(slen2<1e-12f) return -4;

    float rx=px-lx, ry=py-ly;
    float rlen2=rx*rx+ry*ry;
    if(rlen2<1e-12f){ *vis=1.0f; return 0; }      /* P sits on the light */
    float den=rx*sy-ry*sx;
    if(fabsf(den)<1e-9f){ *vis=1.0f; return 0; }  /* parallel: cannot block */
    float t=((ax-lx)*sy-(ay-ly)*sx)/den;
    if(t<=0.0f){ *vis=1.0f; return 0; }           /* occluder behind light */

    float tc=t; if(tc>1.0f) tc=1.0f;
    float cx=lx+rx*tc, cy=ly+ry*tc;
    float u=((cx-ax)*sx+(cy-ay)*sy)/slen2;
    if(u<0.0f) u=0.0f; else if(u>1.0f) u=1.0f;
    float qx=ax+sx*u, qy=ay+sy*u;
    float dx=cx-qx, dy=cy-qy;
    float w=sqrtf(dx*dx+dy*dy);
    /* Snap FP noise inside the geometric umbra to exact zero so the full
     * backlight region cannot pick up denormal light through the ramp. */
    float w_eps=1e-6f*sqrtf(slen2);
    if(w<w_eps){ *vis=0.0f; return 0; }

    float f=tc;
    float wu=cfg->light_size*f*(1.0f-f)+cfg->trans_width;
    float k;
    if(wu<=1e-12f){
        k=(w<=1e-12f)?0.0f:1.0f;                  /* degenerate band: hard */
    }else{
        k=w/wu;
        if(k<0.0f) k=0.0f; else if(k>1.0f) k=1.0f;
        k=k*k*(3.0f-2.0f*k);                       /* smoothstep */
    }
    *vis=k;
    return 0;
}
int shadow_soft_scene(float px,float py,float lx,float ly,
                      const float* segs,int nseg,
                      const shadow_soft_t* cfg,float* vis){
    if(!segs||nseg<=0||!cfg||!vis) return -1;
    float best=1.0f;
    for(int i=0;i<nseg;++i){
        float v;
        int rc=shadow_soft_factor(px,py,lx,ly,
                                  segs[i*4],segs[i*4+1],segs[i*4+2],segs[i*4+3],
                                  cfg,&v);
        if(rc!=0) return rc;
        if(v<best) best=v;
    }
    *vis=best;
    return 0;
}
