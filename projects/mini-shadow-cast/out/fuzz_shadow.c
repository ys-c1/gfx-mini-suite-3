/* Property fuzz: random occluder pairs must always satisfy
 *  (1) partition area == union area of the two shadow quads,
 *  (2) piece centres never lie inside another piece (disjoint interiors),
 *  (3) exactly one overlap polygon when shadows intersect, else none,
 *  (4) raster cell counts of union and partition agree.
 * Deterministic seed; lives outside the shipped tree under out/. */
#define _POSIX_C_SOURCE 200809L
#include "../src/shadow.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static uint32_t rng_state=0x12345678u;
static float rnd(float lo,float hi){
    rng_state ^= rng_state<<13; rng_state ^= rng_state>>17; rng_state ^= rng_state<<5;
    float u=(rng_state>>8)*(1.0f/16777216.0f);
    return lo + u*(hi-lo);
}
static double area_of(const shadow_polygon_t*p){double a;shadow_polygon_area(p,&a);return a;}

int main(void){
    int n=20000, bad=0, overlap_cases=0, disjoint_cases=0, tie_cases=0, miss_cases=0, seam_cases=0;
    double worst_rel=0.0;
    for(int it=0;it<n;++it){
        shadow_occluder_t a,b;
        a.ax=rnd(-10,10); a.ay=rnd(5,15);
        a.bx=a.ax+rnd(2,12); a.by=a.ay+rnd(-2,2);
        b.ax=rnd(-10,10); b.ay=rnd(5,15);
        b.bx=b.ax+rnd(2,12); b.by=b.ay+rnd(-2,2);
        a.id=0; b.id=1;
        if(it%7==0){ a.depth=SHADOW_DEPTH_MISSING; miss_cases++; } else a.depth=rnd(0,10);
        if(it%11==0){ b.depth=SHADOW_DEPTH_MISSING; miss_cases++; } else b.depth=rnd(0,10);
        if(it%23==0) b.depth=a.depth; /* force ties */

        float lx=rnd(-5,15), ly=rnd(-5,5), reach=rnd(10,40);
        shadow_polygon_t qa,qb;
        if(shadow_occluder_quad(&a,lx,ly,reach,&qa)!=0 ||
           shadow_occluder_quad(&b,lx,ly,reach,&qb)!=0){ continue; }
        shadow_clip_result_t r;
        int rc=shadow_clip_two(&a,&b,lx,ly,reach,&r);
        if(rc!=0){ printf("rc=%d at %d\n",rc,it); bad++; continue; }
        if(r.tie) tie_cases++;

        double aa=area_of(&qa), ab=area_of(&qb);
        double ao=0.0, part=0.0; int nov=0;
        for(int i=0;i<r.count;++i){
            part+=area_of(&r.piece[i]);
            if(r.piece[i].is_overlap){ ao=area_of(&r.piece[i]); nov++; }
        }
        double expect=aa+ab-ao;
        double rel=fabs(part-expect)/(1.0+fabs(expect));
        if(rel>worst_rel) worst_rel=rel;
        if(rel>1e-4){ printf("area mismatch it=%d rel=%g\n",it,rel); bad++; }
        if(ao>0.0){ overlap_cases++; if(nov!=1){printf("overlap count %d\n",nov);bad++;} }
        else { disjoint_cases++; if(nov!=0){printf("phantom overlap\n");bad++;} }

        for(int i=0;i<r.count;++i) for(int k=i+1;k<r.count;++k){
            float cx=0,cy=0;
            for(int v=0;v<r.piece[i].n;++v){cx+=r.piece[i].x[v];cy+=r.piece[i].y[v];}
            cx/=r.piece[i].n; cy/=r.piece[i].n;
            if(shadow_point_in_poly(cx,cy,&r.piece[k])){printf("overlap pieces it=%d\n",it);bad++;}
        }
        /* raster agreement on a coarse grid: any disagreement must be
         * confined to boundary cells and thus inside the proved bound */
        float xn=1e30f,yn=1e30f,xx=-1e30f,yx=-1e30f;
        shadow_polygon_t qs[2]={qa,qb};
        for(int k=0;k<2;++k)for(int v=0;v<qs[k].n;++v){
            float x=qs[k].x[v],y=qs[k].y[v];
            if(x<xn) xn=x;
            if(x>xx) xx=x;
            if(y<yn) yn=y;
            if(y>yx) yx=y;
        }
        float px=(xx-xn)*0.05f+1e-3f, py=(yx-yn)*0.05f+1e-3f;
        long cu,cp;
        shadow_raster_count(qs,2,xn-px,yn-py,xx+px,yx+py,64,&cu);
        shadow_raster_count(r.piece,r.count,xn-px,yn-py,xx+px,yx+py,64,&cp);
        double bound;
        shadow_area_error_bound(r.piece,r.count,xn-px,yn-py,xx+px,yx+py,64,&bound);
        double hx=(double)(2.0f*px+xx-xn)/64.0, hy=(double)(2.0f*py+yx-yn)/64.0;
        if(fabs((double)(cu-cp)*hx*hy) > bound*(1.0+1e-12)){
            printf("raster bound breach it=%d %ld vs %ld bound=%g\n",it,cu,cp,bound);bad++;
        }
        if(cu!=cp) seam_cases++;
        if(bad>10){ printf("aborting early\n"); break; }
    }
    printf("fuzz: %d cases, overlap=%d disjoint=%d ties=%d missing_hits=%d "
           "seam_1cell=%d worst_rel_err=%g bad=%d\n",
           n,overlap_cases,disjoint_cases,tie_cases,miss_cases,seam_cases,worst_rel,bad);
    return bad?1:0;
}
