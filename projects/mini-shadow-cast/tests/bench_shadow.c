/* Benchmark / report driver for depth-aware overlap clipping.
 * Runs four lighting setups plus the three required edge cases, prints
 * overlap polygon vertex count, areas (exact vs per-cell sampling with the
 * proved error bound) and timing. No third-party deps, C11 only. */
#define _POSIX_C_SOURCE 200809L
#include "../src/shadow.h"
#include <math.h>
#include <stdio.h>
#include <time.h>

#define BENCH_ITERS 50000
#define RASTER_GRID 256

static shadow_occluder_t mkoc(float ax,float ay,float bx,float by,float depth,int id){
    shadow_occluder_t o;
    o.ax=ax; o.ay=ay; o.bx=bx; o.by=by; o.depth=depth; o.id=id;
    return o;
}

static double now_sec(void){
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC,&ts);
    return ts.tv_sec + ts.tv_nsec*1e-9;
}

static void bbox_of(const shadow_polygon_t* qa,const shadow_polygon_t* qb,
                    float* xmin,float* ymin,float* xmax,float* ymax){
    float xn=1e30f,yn=1e30f,xx=-1e30f,yx=-1e30f;
    const shadow_polygon_t* qs[2]={qa,qb};
    for(int k=0;k<2;++k) for(int i=0;i<qs[k]->n;++i){
        float x=qs[k]->x[i],y=qs[k]->y[i];
        if(x<xn) xn=x;
        if(x>xx) xx=x;
        if(y<yn) yn=y;
        if(y>yx) yx=y;
    }
    float padx=(xx-xn)*0.05f+1e-3f, pady=(yx-yn)*0.05f+1e-3f;
    *xmin=xn-padx; *xmax=xx+padx; *ymin=yn-pady; *ymax=yx+pady;
}

static void run_case(const char* tag, const shadow_occluder_t* a,
                     const shadow_occluder_t* b, float lx, float ly, float reach){
    shadow_polygon_t qa,qb;
    shadow_occluder_quad(a,lx,ly,reach,&qa);
    shadow_occluder_quad(b,lx,ly,reach,&qb);

    /* timing: full depth-resolve + clip path, many iterations */
    shadow_clip_result_t r;
    double t0=now_sec();
    int rc=0;
    for(int it=0;it<BENCH_ITERS;++it)
        rc=shadow_clip_two(a,b,lx,ly,reach,&r);
    double t1=now_sec();
    double total_ms=(t1-t0)*1e3;
    double avg_ns=(t1-t0)*1e9/BENCH_ITERS;

    double area_a,area_b,area_overlap=0.0,area_pieces=0.0,tmp;
    shadow_polygon_area(&qa,&area_a); shadow_polygon_area(&qb,&area_b);
    int overlap_verts=0;
    for(int i=0;i<r.count;++i){
        shadow_polygon_area(&r.piece[i],&tmp); area_pieces+=tmp;
        if(r.piece[i].is_overlap){ overlap_verts=r.piece[i].n; area_overlap=tmp; }
    }
    double exact_union=area_a+area_b-area_overlap;

    float xmin,ymin,xmax,ymax;
    bbox_of(&qa,&qb,&xmin,&ymin,&xmax,&ymax);
    shadow_polygon_t qs[2]={qa,qb};
    long cells_u=0,cells_p=0;
    shadow_raster_count(qs,2,xmin,ymin,xmax,ymax,RASTER_GRID,&cells_u);
    shadow_raster_count(r.piece,r.count,xmin,ymin,xmax,ymax,RASTER_GRID,&cells_p);
    double hx=(double)(xmax-xmin)/RASTER_GRID, hy=(double)(ymax-ymin)/RASTER_GRID;
    double raster_area=(double)cells_u*hx*hy;
    double bound;
    shadow_area_error_bound(r.piece,r.count,xmin,ymin,xmax,ymax,RASTER_GRID,&bound);
    double actual_gap=fabs(raster_area-exact_union);

    printf("--------------------------------------------------------------------\n");
    printf("[%s] light=(%.1f,%.1f) reach=%.1f  rc=%d\n",tag,lx,ly,reach,rc);
    printf("  occluder A id=%d depth=%g seg=(%.0f,%.0f)-(%.0f,%.0f)\n",
           a->id,isnan(a->depth)?0.0:a->depth,a->ax,a->ay,a->bx,a->by);
    printf("  occluder B id=%d depth=%g seg=(%.0f,%.0f)-(%.0f,%.0f)\n",
           b->id,isnan(b->depth)?0.0:b->depth,b->ax,b->ay,b->bx,b->by);
    printf("  winner=%d tie=%d depth_missing=%d pieces=%d overlap_verts=%d\n",
           r.winner,r.tie,r.depth_missing,r.count,overlap_verts);
    printf("  area  quadA=%.6f quadB=%.6f overlap=%.6f union(exact)=%.6f partition=%.6f\n",
           area_a,area_b,area_overlap,exact_union,area_pieces);
    printf("  raster grid=%d cells_union=%ld cells_partition=%ld match=%s\n",
           RASTER_GRID,cells_u,cells_p,cells_u==cells_p?"yes":"NO");
    printf("  raster area=%.6f  |gap|=%.3e  error bound=%.3e  within=%s\n",
           raster_area,actual_gap,bound,actual_gap<=bound*(1.0+1e-12)?"yes":"NO");
    printf("  timing iters=%d total=%.3f ms avg=%.1f ns/call\n",
           BENCH_ITERS,total_ms,avg_ns);
    printf("  pieces detail:\n");
    for(int i=0;i<r.count;++i){
        double ar; shadow_polygon_area(&r.piece[i],&ar);
        printf("    #%d owner=%d %-8s n=%2d area=%.6f verts=",
               i,r.piece[i].owner,r.piece[i].is_overlap?"OVERLAP":"diff",
               r.piece[i].n,ar);
        for(int v=0;v<r.piece[i].n;++v)
            printf("(%.3f,%.3f)%s",r.piece[i].x[v],r.piece[i].y[v],
                   v+1<r.piece[i].n?" ":"");
        printf("\n");
    }
}

int main(void){
    printf("mini-shadow-cast overlap clipping benchmark\n");

    /* ---- four lighting setups ---- */
    shadow_occluder_t s1a=mkoc(0,10,10,10, 5.0f,0),  s1b=mkoc(3,12,13,12, 2.0f,1);
    run_case("S1 top-centre light", &s1a,&s1b, 5,0,20);

    shadow_occluder_t s2a=mkoc(0,8,8,8, 1.0f,10),    s2b=mkoc(6,8,14,8, 4.0f,11);
    run_case("S2 off-centre right light", &s2a,&s2b, 12,0,24);

    shadow_occluder_t s3a=mkoc(0,10,10,10, 3.0f,20), s3b=mkoc(2,10,8,10, 3.5f,21);
    run_case("S3 light above light row", &s3a,&s3b, 5,6,18);

    shadow_occluder_t s4a=mkoc(1,9,9,9, 7.0f,30),    s4b=mkoc(4,11,12,11, 2.5f,31);
    run_case("S4 low side light, nested shadows", &s4a,&s4b, 8,-4,30);

    /* ---- three required edge-case inputs ---- */
    shadow_occluder_t e1a=mkoc(0,10,10,10, 3.0f,5),  e1b=mkoc(3,10,13,10, 3.0f,6);
    run_case("E1 equal depth (lower-id tie-break)", &e1a,&e1b, 5,0,20);

    shadow_occluder_t e2a=mkoc(0,10,10,10, SHADOW_DEPTH_MISSING,2),
                      e2b=mkoc(3,10,13,10, 3.0f,1);
    run_case("E2 missing depth (NaN = +infinity)", &e2a,&e2b, 5,0,20);

    shadow_occluder_t e3a=mkoc(0,10,10,10, 2.0f,0),  e3b=mkoc(0,10,10,10, 8.0f,1);
    run_case("E3 fully coincident occluders", &e3a,&e3b, 5,0,20);
    return 0;
}
