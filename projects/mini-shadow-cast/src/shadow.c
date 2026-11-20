#include "shadow.h"
#include <math.h>
#include <stdlib.h>
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

int shadow_brightness(float px,float py,const shadow_light* lights,int nlight,
                      const float* segs,int nseg,double* out_brightness){
    if(!out_brightness||!segs||nseg<=0) return -1;
    if(nlight<0) return -1;
    double wsum=0.0, wlit=0.0;
    for(int i=0;i<nlight;++i){
        double w=(double)lights[i].weight;
        if(w<0.0) w=0.0;
        int lit=shadow_point_lit(px,py,lights[i].x,lights[i].y,segs,nseg);
        if(lit<0) return -1;
        wsum+=w;
        if(lit) wlit+=w;
    }
    *out_brightness=(wsum>0.0)?(wlit/wsum):0.0;
    return 0;
}

int shadow_sample_grid(double x0,double y0,double x1,double y1,int nx,int ny,
                       const shadow_light* lights,int nlight,
                       const float* segs,int nseg,
                       double* grid,shadow_report* rep){
    if(nx<=0||ny<=0||!segs||nseg<=0) return -1;
    if(nlight<0) return -1;
    if(nlight>0&&!lights) return -1;
    if(rep){
        rep->samples=nx*ny; rep->lit=0; rep->clamped=0;
        rep->coincident=0; rep->wsum=0.0;
        rep->mean=0.0; rep->vmin=1.0; rep->vmax=0.0; rep->lit_ratio=0.0;
    }
    /* Effective weights: negatives are clamped to zero deterministically. */
    double* weff=NULL;
    if(nlight>0){
        weff=(double*)malloc((size_t)nlight*sizeof(double));
        if(!weff) return -1;
        for(int i=0;i<nlight;++i){
            double w=(double)lights[i].weight;
            weff[i]=(w<0.0)?0.0:w;
            if(rep&&w<0.0) rep->clamped++;
        }
        /* Coincident lights: ordered pairs within a fixed distance. */
        if(rep){
            for(int i=0;i<nlight;++i)
                for(int j=i+1;j<nlight;++j){
                    float dx=lights[i].x-lights[j].x;
                    float dy=lights[i].y-lights[j].y;
                    if(dx*dx+dy*dy<1e-18f) rep->coincident++;
                }
            for(int i=0;i<nlight;++i) rep->wsum+=weff[i];
        }
    }
    double acc=0.0, vmin=1.0, vmax=0.0; int lit=0;
    double dxg=(x1-x0)/(double)nx, dyg=(y1-y0)/(double)ny;
    for(int row=0;row<ny;++row){
        double py=y0+(row+0.5)*dyg;
        for(int col=0;col<nx;++col){
            double px=x0+(col+0.5)*dxg;
            double wlit=0.0, wsum=0.0;
            for(int i=0;i<nlight;++i){
                int s=shadow_point_lit((float)px,(float)py,
                                       lights[i].x,lights[i].y,segs,nseg);
                if(s<0){ free(weff); return -1; }
                wsum+=weff[i];
                if(s) wlit+=weff[i];
            }
            double b=(wsum>0.0)?(wlit/wsum):0.0;
            if(grid) grid[row*nx+col]=b;
            acc+=b;
            if(b>0.0) lit++;
            if(b<vmin) vmin=b;
            if(b>vmax) vmax=b;
        }
    }
    if(rep){
        rep->mean=acc/(double)(nx*ny);
        rep->vmin=(nx*ny>0)?vmin:0.0;
        rep->vmax=vmax;
        rep->lit=lit;
        rep->lit_ratio=(double)lit/(double)(nx*ny);
    }
    free(weff);
    return 0;
}
