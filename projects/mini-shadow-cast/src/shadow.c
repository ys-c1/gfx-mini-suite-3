#include "shadow.h"
#include <math.h>
#include <stddef.h>

#define SH_EPS       1e-7f  /* geometric tolerance, input coords are O(1..100) */
#define SH_DEPTH_EPS 1e-6f  /* relative depth-equality tolerance             */

/* ============================ legacy API (frozen) ============================ */
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

/* ============================ polygon helpers ============================ */
static double signed_area(const shadow_polygon_t* p){
    if(p->n < 3) return 0.0;
    double s = 0.0;
    for(int i=0;i<p->n;++i){
        int j=(i+1)%p->n;
        s += (double)p->x[i]*p->y[j] - (double)p->x[j]*p->y[i];
    }
    return s*0.5;
}

int shadow_polygon_area(const shadow_polygon_t* p, double* area){
    if(!p || !area) return -1;
    *area = fabs(signed_area(p));
    return 0;
}

void shadow_polygon_clean(shadow_polygon_t* p){
    if(!p) return;
    /* drop consecutive duplicates (ring-closed) */
    shadow_polygon_t q;
    q.n = 0;
    for(int i=0;i<p->n;++i){
        int prev = q.n ? q.n-1 : -1;
        if(prev >= 0){
            float dx=p->x[i]-q.x[prev], dy=p->y[i]-q.y[prev];
            if(dx*dx+dy*dy <= SH_EPS*SH_EPS) continue;
        }
        if(q.n >= SHADOW_MAX_VERTS){ p->n=0; return; }
        q.x[q.n]=p->x[i]; q.y[q.n]=p->y[i]; ++q.n;
    }
    if(q.n >= 2){
        float dx=q.x[q.n-1]-q.x[0], dy=q.y[q.n-1]-q.y[0];
        if(dx*dx+dy*dy <= SH_EPS*SH_EPS) --q.n;
    }
    if(q.n < 3){ q.n=0; }
    *p = q;
}

static int make_ccw(shadow_polygon_t* p){
    if(signed_area(p) < 0.0){
        for(int i=1, j=p->n-1; i<j; ++i,--j){
            float tx=p->x[i], ty=p->y[i];
            p->x[i]=p->x[j]; p->y[i]=p->y[j];
            p->x[j]=tx;     p->y[j]=ty;
        }
    }
    return 0;
}

int shadow_occluder_quad(const shadow_occluder_t* oc, float lx, float ly,
                         float reach, shadow_polygon_t* q){
    if(!oc || !q) return -1;
    float ax,bx,ay,by;
    if(shadow_project(oc->ax,oc->ay,oc->bx,oc->by,lx,ly,reach,
                      &ax,&ay,&bx,&by) != 0) return -1;
    /* ring A -> B -> B' -> A' */
    q->n = 4;
    q->x[0]=oc->ax; q->y[0]=oc->ay;
    q->x[1]=oc->bx; q->y[1]=oc->by;
    q->x[2]=bx;     q->y[2]=by;
    q->x[3]=ax;     q->y[3]=ay;
    q->is_overlap = 0;
    q->owner = oc->id;
    make_ccw(q);
    shadow_polygon_clean(q);
    return q->n ? 0 : -1;
}

/* Clip convex CCW polygon `in` to the closed half-plane n.x >= c. */
static int clip_halfplane(const shadow_polygon_t* in,
                          float nx, float ny, float c,
                          shadow_polygon_t* out){
    out->n = 0;
    if(in->n == 0) return 0;
    for(int i=0;i<in->n;++i){
        int k = (i+1) % in->n;
        float sx=in->x[i], sy=in->y[i], ex=in->x[k], ey=in->y[k];
        float fs = nx*sx+ny*sy-c, fe = nx*ex+ny*ey-c;
        int sin = fs >= -SH_EPS, ein = fe >= -SH_EPS;
        if(sin){
            if(out->n >= SHADOW_MAX_VERTS) return -2;
            out->x[out->n]=sx; out->y[out->n]=sy; ++out->n;
        }
        if(sin != ein){
            float denom = fs-fe;
            if(fabsf(denom) < SH_EPS) continue; /* parallel: no crossing */
            float t = fs/denom;
            if(out->n >= SHADOW_MAX_VERTS) return -2;
            out->x[out->n]=sx+t*(ex-sx);
            out->y[out->n]=sy+t*(ey-sy);
            ++out->n;
        }
    }
    shadow_polygon_clean(out);
    return 0;
}

/* Convex intersection of two CCW polygons: keep points inside every
 * left half-plane of `b`. Ping-pong buffers, never clipped in place. */
static int poly_intersect(const shadow_polygon_t* a, const shadow_polygon_t* b,
                          shadow_polygon_t* r){
    shadow_polygon_t buf[2];
    buf[0] = *a; buf[1].n = 0;
    int src = 0;
    for(int i=0;i<b->n;++i){
        int j=(i+1)%b->n;
        float dx=b->x[j]-b->x[i], dy=b->y[j]-b->y[i];
        float nx=-dy, ny=dx;                 /* inward (left) normal */
        float c = nx*b->x[i]+ny*b->y[i];
        int dst = src ^ 1;
        if(clip_halfplane(&buf[src],nx,ny,c,&buf[dst]) != 0){ r->n=0; return -2; }
        src = dst;
    }
    *r = buf[src];
    return 0;
}

/* a minus b, both convex CCW. Walk b's half-planes in order: every current
 * work fragment is split into its inner part (clipped further) and outer
 * part (final fragment). A point of a\b leaves the work set at exactly one
 * edge, so the emitted fragments have pairwise disjoint interiors. */
static int poly_difference(const shadow_polygon_t* a, const shadow_polygon_t* b,
                           int owner, shadow_polygon_t* out, int maxpieces,
                           int* nout){
    *nout = 0;
    if(a->n == 0) return 0;
    shadow_polygon_t work, inpart, outpart;
    work = *a;
    for(int i=0;i<b->n;++i){
        int j=(i+1)%b->n;
        float dx=b->x[j]-b->x[i], dy=b->y[j]-b->y[i];
        float nx=-dy, ny=dx;
        float c = nx*b->x[i]+ny*b->y[i];
        if(work.n < 3) break;
        if(clip_halfplane(&work, -nx,-ny,-c, &outpart) != 0) return -2; /* n.x <= c */
        if(clip_halfplane(&work,  nx, ny, c, &inpart)  != 0) return -2; /* n.x >= c */
        if(outpart.n >= 3){
            double ar; shadow_polygon_area(&outpart,&ar);
            if(ar >= 1e-9){
                if(*nout >= maxpieces) return -2;
                outpart.owner = owner;
                outpart.is_overlap = 0;
                out[(*nout)++] = outpart;
            }
        }
        work = inpart;
    }
    /* what remains in `work` is a intersect b and is discarded */
    return 0;
}

/* ============================ depth-aware merge ============================ */
static int depth_resolve(const shadow_occluder_t* a, const shadow_occluder_t* b,
                         int* winner_id, int* tie, int* missing){
    int am = isnan(a->depth), bm = isnan(b->depth);
    *missing = am || bm;
    *tie = 0;
    if(am && bm){ *tie = 1; *winner_id = (a->id < b->id) ? a->id : b->id; return 0; }
    if(am){ *winner_id = b->id; return 0; }
    if(bm){ *winner_id = a->id; return 0; }
    float scale = 1.0f + fmaxf(fabsf(a->depth), fabsf(b->depth));
    if(fabsf(a->depth-b->depth) <= SH_DEPTH_EPS*scale){
        *tie = 1;
        *winner_id = (a->id < b->id) ? a->id : b->id;
    } else {
        *winner_id = (a->depth < b->depth) ? a->id : b->id;
    }
    return 0;
}

int shadow_clip_two(const shadow_occluder_t* a, const shadow_occluder_t* b,
                    float lx, float ly, float reach,
                    shadow_clip_result_t* out){
    if(!a || !b || !out || reach<=0.0f) return -1;
    if(a->id < 0 || b->id < 0 || a->id == b->id) return -1;
    for(int i=0;i<SHADOW_MAX_PIECES;++i) out->piece[i].n=0;
    out->count=0; out->winner=-1; out->tie=0; out->depth_missing=0;

    shadow_polygon_t qa, qb;
    if(shadow_occluder_quad(a,lx,ly,reach,&qa) != 0) return -1;
    if(shadow_occluder_quad(b,lx,ly,reach,&qb) != 0) return -1;

    int winner_id, tie, missing;
    depth_resolve(a,b,&winner_id,&tie,&missing);
    out->tie=tie; out->depth_missing=missing;

    shadow_polygon_t inter;
    inter.n=0;
    if(poly_intersect(&qa,&qb,&inter) != 0) return -2;
    if(inter.n < 3){
        double ai; shadow_polygon_area(&inter,&ai);
        if(ai < 1e-9){
            /* disjoint shadows: render each quad whole */
            qa.owner=a->id; qb.owner=b->id;
            out->piece[0]=qa; out->piece[1]=qb; out->count=2;
            return 0;
        }
    }
    double area_i, area_a, area_b;
    shadow_polygon_area(&inter,&area_i);
    shadow_polygon_area(&qa,&area_a);
    shadow_polygon_area(&qb,&area_b);

    const shadow_occluder_t* near_o = (a->id==winner_id) ? a : b;
    const shadow_polygon_t* qn = (a->id==winner_id) ? &qa : &qb;
    const shadow_polygon_t* qf = (a->id==winner_id) ? &qb : &qa;

    inter.owner = winner_id;
    inter.is_overlap = 1;
    out->winner = winner_id;

    /* exact geometric coincidence -> one polygon, no difference pieces */
    if(area_a > 0.0 && area_b > 0.0 &&
       fabs(area_i-area_a)/area_a < 1e-5 &&
       fabs(area_i-area_b)/area_b < 1e-5){
        out->piece[0]=inter; out->count=1;
        return 0;
    }

    shadow_polygon_t near_parts[SHADOW_MAX_PIECES];
    shadow_polygon_t far_parts[SHADOW_MAX_PIECES];
    int nn=0, nf=0;
    if(poly_difference(qn,qf,near_o->id,near_parts,SHADOW_MAX_PIECES,&nn) != 0) return -2;
    if(poly_difference(qf,qn,(a->id==winner_id)?b->id:a->id,
                       far_parts,SHADOW_MAX_PIECES,&nf) != 0) return -2;
    if(1+nn+nf > SHADOW_MAX_PIECES) return -2;

    int k=0;
    out->piece[k++]=inter;
    for(int i=0;i<nn;++i) out->piece[k++]=near_parts[i];
    for(int i=0;i<nf;++i) out->piece[k++]=far_parts[i];
    out->count=k;
    return 0;
}

/* ============================ reference sampling ============================ */
int shadow_point_in_poly(float px, float py, const shadow_polygon_t* p){
    if(!p || p->n < 3) return 0;
    int inside=0;
    for(int i=0, j=p->n-1; i<p->n; j=i++){
        float xi=p->x[i], yi=p->y[i], xj=p->x[j], yj=p->y[j];
        if(((yi>py)!=(yj>py)) &&
           (px < (xj-xi)*(py-yi)/(yj-yi)+xi))
            inside ^= 1;
    }
    if(inside) return 1;
    /* boundary check: within distance SH_EPS of any edge */
    for(int i=0;i<p->n;++i){
        int j=(i+1)%p->n;
        float ex=p->x[j]-p->x[i], ey=p->y[j]-p->y[i];
        float len2=ex*ex+ey*ey;
        if(len2 < SH_EPS*SH_EPS) continue;
        float t=((px-p->x[i])*ex+(py-p->y[i])*ey)/len2;
        if(t < 0.0f) t=0.0f; else if(t > 1.0f) t=1.0f;
        float cx=p->x[i]+t*ex, cy=p->y[i]+t*ey;
        float dx=px-cx, dy=py-cy;
        if(dx*dx+dy*dy <= SH_EPS*SH_EPS) return 1;
    }
    return 0;
}

/* Strict interior predicate for cell-centre sampling: parity rule only,
 * no boundary tolerance. */
static int point_inside_strict(float px, float py, const shadow_polygon_t* p){
    int inside=0;
    for(int i=0, j=p->n-1; i<p->n; j=i++){
        float xi=p->x[i], yi=p->y[i], xj=p->x[j], yj=p->y[j];
        if(((yi>py)!=(yj>py)) &&
           (px < (xj-xi)*(py-yi)/(yj-yi)+xi))
            inside ^= 1;
    }
    return inside;
}

int shadow_raster_count(const shadow_polygon_t* polys, int npoly,
                        float xmin, float ymin, float xmax, float ymax,
                        int grid, long* inside_cells){
    if(!polys || npoly<=0 || !inside_cells || grid<=0 ||
       !(xmax>xmin) || !(ymax>ymin)) return -1;
    float hx=(xmax-xmin)/grid, hy=(ymax-ymin)/grid;
    long cnt=0;
    for(int gy=0; gy<grid; ++gy){
        float py = ymin + (gy+0.5f)*hy;
        for(int gx=0; gx<grid; ++gx){
            float px = xmin + (gx+0.5f)*hx;
            /* plain OR containment: correct for a union of overlapping
             * quads and for an interior-disjoint partition alike. The two
             * samplings can only differ on boundary cells (ulp-level seam
             * placement), which shadow_area_error_bound counts explicitly. */
            for(int k=0;k<npoly;++k){
                if(point_inside_strict(px,py,&polys[k])){ ++cnt; break; }
            }
        }
    }
    *inside_cells=cnt;
    return 0;
}

int shadow_area_error_bound(const shadow_polygon_t* polys, int npoly,
                            float xmin, float ymin, float xmax, float ymax,
                            int grid, double* bound){
    if(!polys || npoly<=0 || !bound || grid<=0 ||
       !(xmax>xmin) || !(ymax>ymin)) return -1;
    double hx=(double)(xmax-xmin)/grid, hy=(double)(ymax-ymin)/grid;
    double disputed=0.0;
    for(int k=0;k<npoly;++k){
        const shadow_polygon_t* p=&polys[k];
        for(int i=0;i<p->n;++i){
            int j=(i+1)%p->n;
            double dx=fabs((double)p->x[j]-p->x[i]);
            double dy=fabs((double)p->y[j]-p->y[i]);
            disputed += dx/hx + dy/hy + 1.0;
        }
    }
    *bound = disputed*hx*hy;
    return 0;
}
