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
