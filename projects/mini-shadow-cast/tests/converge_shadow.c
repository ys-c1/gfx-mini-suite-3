/* Grid-refinement convergence: per-cell sampling area vs exact polygon
 * area, against the proved error bound, on every benchmark case. */
#define _POSIX_C_SOURCE 200809L
#include "../src/shadow.h"
#include <math.h>
#include <stdio.h>

static shadow_occluder_t mkoc(float ax,float ay,float bx,float by,float depth,int id){
    shadow_occluder_t o;
    o.ax=ax; o.ay=ay; o.bx=bx; o.by=by; o.depth=depth; o.id=id;
    return o;
}

static void converge(const char* tag, const shadow_occluder_t* a,
                     const shadow_occluder_t* b, float lx, float ly, float reach){
    shadow_polygon_t qa,qb;
    shadow_occluder_quad(a,lx,ly,reach,&qa);
    shadow_occluder_quad(b,lx,ly,reach,&qb);
    shadow_clip_result_t r;
    shadow_clip_two(a,b,lx,ly,reach,&r);
    double aa,ab,ao=0.0;
    shadow_polygon_area(&qa,&aa); shadow_polygon_area(&qb,&ab);
    for(int i=0;i<r.count;++i) if(r.piece[i].is_overlap)
        shadow_polygon_area(&r.piece[i],&ao);
    double exact=aa+ab-ao;

    float xn=1e30f,yn=1e30f,xx=-1e30f,yx=-1e30f;
    shadow_polygon_t qs[2]={qa,qb};
    for(int k=0;k<2;++k) for(int i=0;i<qs[k].n;++i){
        float x=qs[k].x[i],y=qs[k].y[i];
        if(x<xn) xn=x;
        if(x>xx) xx=x;
        if(y<yn) yn=y;
        if(y>yx) yx=y;
    }
    float padx=(xx-xn)*0.05f+1e-3f, pady=(yx-yn)*0.05f+1e-3f;
    xn-=padx; xx+=padx; yn-=pady; yx+=pady;

    printf("[%s] exact union area = %.6f\n",tag,exact);
    printf("  %6s %12s %12s %12s %10s\n","grid","raster_area","|gap|","bound","within");
    int grids[5]={32,64,128,256,512};
    for(int g=0;g<5;++g){
        int grid=grids[g];
        long cells;
        shadow_raster_count(qs,2,xn,yn,xx,yx,grid,&cells);
        double hx=(double)(xx-xn)/grid, hy=(double)(yx-yn)/grid;
        double rast=(double)cells*hx*hy;
        double bound;
        shadow_area_error_bound(r.piece,r.count,xn,yn,xx,yx,grid,&bound);
        double gap=fabs(rast-exact);
        printf("  %6d %12.6f %12.3e %12.3e %10s\n",
               grid,rast,gap,bound,gap<=bound*(1.0+1e-12)?"yes":"NO");
    }
}

int main(void){
    shadow_occluder_t s1a=mkoc(0,10,10,10,5.0f,0),  s1b=mkoc(3,12,13,12,2.0f,1);
    converge("S1",&s1a,&s1b,5,0,20);
    shadow_occluder_t s2a=mkoc(0,8,8,8,1.0f,10),    s2b=mkoc(6,8,14,8,4.0f,11);
    converge("S2",&s2a,&s2b,12,0,24);
    shadow_occluder_t s3a=mkoc(0,10,10,10,3.0f,20), s3b=mkoc(2,10,8,10,3.5f,21);
    converge("S3",&s3a,&s3b,5,6,18);
    shadow_occluder_t s4a=mkoc(1,9,9,9,7.0f,30),    s4b=mkoc(4,11,12,11,2.5f,31);
    converge("S4",&s4a,&s4b,8,-4,30);
    return 0;
}
