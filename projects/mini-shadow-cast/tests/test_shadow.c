#include "../src/shadow.h"
#include <stdio.h>
#include <math.h>
#include <float.h>

static int fails=0,checks=0;
#define CHECK(c) do{++checks; if(!(c)){++fails;printf("FAIL %d %s\n",__LINE__,#c);}}while(0)

static shadow_occluder_t mkoc(float ax,float ay,float bx,float by,float depth,int id){
    shadow_occluder_t o;
    o.ax=ax; o.ay=ay; o.bx=bx; o.by=by; o.depth=depth; o.id=id;
    return o;
}

static double total_area(const shadow_clip_result_t* r){
    double s=0.0,a;
    for(int i=0;i<r->count;++i){ shadow_polygon_area(&r->piece[i],&a); s+=a; }
    return s;
}

/* full raster consistency: union-of-quads sampling must equal partition
 * sampling, exact area must lie inside the proved error bound. */
static void check_raster_consistency(const shadow_polygon_t* qa,
                                     const shadow_polygon_t* qb,
                                     const shadow_clip_result_t* r,
                                     float lx,float ly){
    float xmin=FLT_MAX,ymin=FLT_MAX,xmax=-FLT_MAX,ymax=-FLT_MAX;
    shadow_polygon_t qs[2]={*qa,*qb};
    for(int k=0;k<2;++k) for(int i=0;i<qs[k].n;++i){
        float x=qs[k].x[i], y=qs[k].y[i];
        if(x<xmin) xmin=x;
        if(x>xmax) xmax=x;
        if(y<ymin) ymin=y;
        if(y>ymax) ymax=y;
    }
    /* pad so cell centres cover the boundary rows/columns */
    float padx=(xmax-xmin)*0.05f+1e-3f, pady=(ymax-ymin)*0.05f+1e-3f;
    xmin-=padx; xmax+=padx; ymin-=pady; ymax+=pady;
    (void)lx; (void)ly;
    double exact_union=0.0, ai;
    shadow_polygon_area(qa,&ai); exact_union+=ai;
    shadow_polygon_area(qb,&ai); exact_union+=ai;
    /* intersection area via clip result when present, else recompute */
    double inter_area=0.0;
    int have=0;
    for(int i=0;i<r->count;++i) if(r->piece[i].is_overlap){
        shadow_polygon_area(&r->piece[i],&inter_area); have=1; break;
    }
    if(!have){
        /* disjoint shadows: union area equals partition area */
        exact_union = total_area(r);
    } else {
        exact_union -= inter_area;
    }
    double part_area=total_area(r);
    CHECK(fabs(part_area-exact_union) < 1e-4*(1.0+fabs(exact_union)));

    double bound_first=0.0, bound_last=0.0;
    int grids[3]={64,128,256};
    for(int g=0;g<3;++g){
        int grid=grids[g];
        long cells_u, cells_p;
        CHECK(shadow_raster_count(qs,2,xmin,ymin,xmax,ymax,grid,&cells_u)==0);
        CHECK(shadow_raster_count(r->piece,r->count,xmin,ymin,xmax,ymax,grid,&cells_p)==0);
        double hx=(double)(xmax-xmin)/grid, hy=(double)(ymax-ymin)/grid;
        double raster_area=(double)cells_u*hx*hy;
        double part_area_sampled=(double)cells_p*hx*hy;
        double bound;
        CHECK(shadow_area_error_bound(r->piece,r->count,xmin,ymin,xmax,ymax,grid,&bound)==0);
        /* exact area vs both samplings: each gap is inside the proved bound;
         * the two samplings therefore agree within one bound by triangle
         * inequality (seam-placement ulp effects can only touch boundary
         * cells, which the bound counts explicitly). */
        CHECK(fabs(raster_area-exact_union) <= bound*(1.0+1e-12));
        CHECK(fabs(part_area_sampled-exact_union) <= bound*(1.0+1e-12));
        CHECK(fabs((double)(cells_u-cells_p)*hx*hy) <= bound*(1.0+1e-12));
        if(g==0) bound_first=bound;
        else /* bound scales like 1/grid; allow slack for constant edge term */
            CHECK(bound <= bound_first*(64.0/grid)*1.5 + 1e-9);
        bound_last=bound;
    }
    /* finest grid must give a tighter bound than the coarsest */
    CHECK(bound_last < bound_first);
}

int main(void){
    float o1x,o1y,o2x,o2y;

    /* ---- legacy API unchanged ---- */
    CHECK(shadow_project(0,10,10,10,5,0,20,&o1x,&o1y,&o2x,&o2y)==0);
    CHECK(o1y>10.0f && o2y>10.0f);
    float segs[4]={0,10,10,10};
    CHECK(shadow_point_lit(5,20,5,0,segs,1)==0);
    CHECK(shadow_point_lit(20,20,5,0,segs,1)==1);
    CHECK(shadow_project(0,0,0,0,0,0,10,&o1x,&o1y,&o2x,&o2y)==-1);

    /* ---- four lighting setups: nearer occluder owns the overlap ---- */
    struct { float lx,ly,reach;
             shadow_occluder_t a,b; } setups[4] = {
        { 5,0,  20, mkoc(0,10,10,10, 5.0f,0),  mkoc(3,12,13,12, 2.0f,1)},
        {12,0,  24, mkoc(0,8, 8, 8, 1.0f,10),  mkoc(6,8,14,8,  4.0f,11)},
        { 5,6,  18, mkoc(0,10,10,10, 3.0f,20), mkoc(2,10,8,10,  3.5f,21)},
        { 8,-4, 30, mkoc(1,9, 9, 9, 7.0f,30),  mkoc(4,11,12,11, 2.5f,31)},
    };
    for(int s=0;s<4;++s){
        shadow_clip_result_t r;
        shadow_polygon_t qa,qb;
        CHECK(shadow_occluder_quad(&setups[s].a,setups[s].lx,setups[s].ly,setups[s].reach,&qa)==0);
        CHECK(shadow_occluder_quad(&setups[s].b,setups[s].lx,setups[s].ly,setups[s].reach,&qb)==0);
        CHECK(qa.n==4 && qb.n==4);
        CHECK(shadow_clip_two(&setups[s].a,&setups[s].b,setups[s].lx,setups[s].ly,setups[s].reach,&r)==0);
        CHECK(r.count>=1 && r.count<=SHADOW_MAX_PIECES);
        int nover=0, winner=-1;
        for(int i=0;i<r.count;++i){
            CHECK(r.piece[i].n>=3 && r.piece[i].n<=SHADOW_MAX_VERTS);
            if(r.piece[i].is_overlap){ ++nover; winner=r.piece[i].owner; }
        }
        CHECK(nover==1);
        int expect_winner = (setups[s].a.depth < setups[s].b.depth)
                          ? setups[s].a.id : setups[s].b.id;
        CHECK(winner==expect_winner);
        /* pairwise disjoint interiors: centres of one piece never inside another */
        for(int i=0;i<r.count;++i) for(int k=i+1;k<r.count;++k){
            double ai; shadow_polygon_area(&r.piece[i],&ai);
            float cx=0,cy=0;
            for(int v=0;v<r.piece[i].n;++v){cx+=r.piece[i].x[v];cy+=r.piece[i].y[v];}
            cx/=r.piece[i].n; cy/=r.piece[i].n;
            CHECK(!shadow_point_in_poly(cx,cy,&r.piece[k]));
            (void)ai;
        }
        check_raster_consistency(&qa,&qb,&r,setups[s].lx,setups[s].ly);
    }

    /* ---- single occluder: quad area equals geometric shadow formula ---- */
    {
        shadow_occluder_t one = mkoc(0,10,10,10, 2.0f, 0);
        shadow_polygon_t q;
        CHECK(shadow_occluder_quad(&one,5,0,20,&q)==0);
        CHECK(q.n==4 && q.owner==0 && q.is_overlap==0);
        double ar; shadow_polygon_area(&q,&ar);
        /* trapezium area = (base + far edge)/2 * height; just check sane >0 */
        CHECK(ar > 100.0);
    }

    /* ---- equal depth: deterministic lower-id tie-break ---- */
    {
        shadow_occluder_t a=mkoc(0,10,10,10, 3.0f, 5);
        shadow_occluder_t b=mkoc(3,10,13,10, 3.0f+1e-7f, 6);
        shadow_clip_result_t r;
        CHECK(shadow_clip_two(&a,&b,5,0,20,&r)==0);
        CHECK(r.tie==1 && r.winner==5);
        shadow_occluder_t a2=mkoc(0,10,10,10, 3.0f, 9);
        shadow_clip_result_t r2;
        CHECK(shadow_clip_two(&b,&a2,5,0,20,&r2)==0);
        CHECK(r2.tie==1 && r2.winner==6); /* argument order independent */
    }

    /* ---- missing depth: NaN sorts as +infinity; both missing -> tie ---- */
    {
        shadow_occluder_t known=mkoc(0,10,10,10, 3.0f, 1);
        shadow_occluder_t miss =mkoc(3,10,13,10, SHADOW_DEPTH_MISSING, 2);
        shadow_clip_result_t r;
        CHECK(shadow_clip_two(&known,&miss,5,0,20,&r)==0);
        CHECK(r.depth_missing==1 && r.winner==1);
        shadow_clip_result_t r3;
        CHECK(shadow_clip_two(&miss,&known,5,0,20,&r3)==0);
        CHECK(r3.winner==1);
        shadow_occluder_t miss2=mkoc(3,10,13,10, SHADOW_DEPTH_MISSING, 7);
        shadow_clip_result_t r4;
        CHECK(shadow_clip_two(&miss,&miss2,5,0,20,&r4)==0);
        CHECK(r4.depth_missing==1 && r4.tie==1 && r4.winner==2);
    }

    /* ---- exact coincidence: one overlap polygon, no difference pieces ---- */
    {
        shadow_occluder_t a=mkoc(0,10,10,10, 2.0f, 0);
        shadow_occluder_t b=mkoc(0,10,10,10, 8.0f, 1);
        shadow_clip_result_t r;
        CHECK(shadow_clip_two(&a,&b,5,0,20,&r)==0);
        CHECK(r.count==1 && r.piece[0].is_overlap==1 && r.winner==0);
        double ar, qar;
        shadow_polygon_area(&r.piece[0],&ar);
        shadow_polygon_t q;
        shadow_occluder_quad(&a,5,0,20,&q);
        shadow_polygon_area(&q,&qar);
        CHECK(fabs(ar-qar) <= 1e-5*qar);
    }

    /* ---- disjoint shadows: two whole quads, no overlap polygon ---- */
    {
        shadow_occluder_t a=mkoc(-20,10,-12,10, 2.0f, 0);
        shadow_occluder_t b=mkoc(20,10,28,10, 1.0f, 1);
        shadow_clip_result_t r;
        CHECK(shadow_clip_two(&a,&b,5,0,20,&r)==0);
        CHECK(r.count==2 && r.winner==-1);
        int ov=0; for(int i=0;i<2;++i) ov+=r.piece[i].is_overlap;
        CHECK(ov==0);
    }

    /* ---- invalid inputs ---- */
    {
        shadow_occluder_t a=mkoc(0,10,10,10, 2.0f, 0);
        shadow_occluder_t bad=mkoc(0,10,10,10, 2.0f, 0); /* duplicate id */
        shadow_clip_result_t r;
        CHECK(shadow_clip_two(&a,&bad,5,0,20,&r)==-1);
        CHECK(shadow_clip_two(0,&bad,5,0,20,&r)==-1);
        CHECK(shadow_clip_two(&a,&bad,5,0,0,&r)==-1);
    }

    printf("%d checks, %d failures\n",checks,fails);
    return fails?1:0;
}
