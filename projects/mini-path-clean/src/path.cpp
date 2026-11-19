#include "path.h"
#include <cmath>
#include <vector>

int path_remove_collinear(const float* pts,size_t n,float* out,size_t cap,size_t* outn,float eps){
    if(!pts||!out||!outn||n<2) return -1;
    size_t k=0;
    for(size_t i=0;i<n;++i){
        if(i>0&&i+1<n){
            float ax=pts[(i-1)*2],ay=pts[(i-1)*2+1];
            float bx=pts[i*2],by=pts[i*2+1];
            float cx=pts[(i+1)*2],cy=pts[(i+1)*2+1];
            float cross=(bx-ax)*(cy-ay)-(by-ay)*(cx-ax);
            float len=std::sqrt((cx-ax)*(cx-ax)+(cy-ay)*(cy-ay));
            if(len>0.0f&&std::fabs(cross/len)<=eps) continue;
        }
        if(k>=cap) return -2;
        out[k*2]=pts[i*2]; out[k*2+1]=pts[i*2+1]; ++k;
    }
    *outn=k; return 0;
}
float path_length(const float* pts,size_t n){
    float s=0.0f;
    for(size_t i=1;i<n;++i){
        float dx=pts[i*2]-pts[(i-1)*2], dy=pts[i*2+1]-pts[(i-1)*2+1];
        s+=std::sqrt(dx*dx+dy*dy);
    }
    return s;
}

namespace {

struct dvec { double x, y; };

static dvec dpt(const float* pts,size_t i){
    dvec p; p.x=pts[i*2]; p.y=pts[i*2+1]; return p;
}
static bool same_point(dvec a,dvec b){
    return a.x==b.x && a.y==b.y;
}
static double dcross(double ax,double ay,double bx,double by){
    return ax*by-ay*bx;
}
static void clear_result(path_isect_result* r){
    r->pairs=0; r->exact_pairs=0; r->near_pairs=0; r->segments=0;
    r->has_first=0; r->first_is_near=0;
    r->first_x=0.0f; r->first_y=0.0f; r->first_distance=0.0f;
    r->first_i=-1; r->first_j=-1;
    r->has_exact=0;
    r->exact_x=0.0f; r->exact_y=0.0f; r->exact_i=-1; r->exact_j=-1;
}

/* 闭区间相交（含端点接触与共线重叠）。
 * 共线重叠时，交点取重叠区间在第一条线段上的起点。 */
static bool seg_intersect(dvec a,dvec b,dvec c,dvec d,dvec* pt){
    double rx=b.x-a.x, ry=b.y-a.y;
    double sx=d.x-c.x, sy=d.y-c.y;
    double qx=c.x-a.x, qy=c.y-a.y;
    double den=dcross(rx,ry,sx,sy);
    if(den!=0.0){
        double t=dcross(qx,qy,sx,sy)/den;
        double u=dcross(qx,qy,rx,ry)/den;
        if(t>=0.0&&t<=1.0&&u>=0.0&&u<=1.0){
            pt->x=a.x+t*rx; pt->y=a.y+t*ry;
            return true;
        }
        return false;
    }
    if(dcross(qx,qy,rx,ry)!=0.0) return false;
    /* 共线：把 c、d 投影到 a->b 的参数轴上 */
    double len2=rx*rx+ry*ry;
    double tc=((c.x-a.x)*rx+(c.y-a.y)*ry)/len2;
    double td=((d.x-a.x)*rx+(d.y-a.y)*ry)/len2;
    double lo=(tc<td?tc:td), hi=(tc<td?td:tc);
    if(lo<0.0) lo=0.0;
    if(hi>1.0) hi=1.0;
    if(lo>hi) return false;
    pt->x=a.x+lo*rx; pt->y=a.y+lo*ry;
    return true;
}

/* 点到线段最近点 */
static double point_seg_closest(dvec p,dvec a,dvec b,dvec* q){
    double rx=b.x-a.x, ry=b.y-a.y;
    double len2=rx*rx+ry*ry;
    double t=len2>0.0?((p.x-a.x)*rx+(p.y-a.y)*ry)/len2:0.0;
    if(t<0.0) t=0.0;
    if(t>1.0) t=1.0;
    q->x=a.x+t*rx; q->y=a.y+t*ry;
    double dx=p.x-q->x, dy=p.y-q->y;
    return std::sqrt(dx*dx+dy*dy);
}

/* 两线段最小距离及最近点对；调用方已排除严格相交的情况。 */
static double seg_seg_closest(dvec a,dvec b,dvec c,dvec d,dvec* pa,dvec* pb){
    dvec q;
    double best=point_seg_closest(a,c,d,&q);
    *pa=a; *pb=q;
    double cur=point_seg_closest(b,c,d,&q);
    if(cur<best){ best=cur; *pa=b; *pb=q; }
    cur=point_seg_closest(c,a,b,&q);
    if(cur<best){ best=cur; *pa=q; *pb=c; }
    cur=point_seg_closest(d,a,b,&q);
    if(cur<best){ best=cur; *pa=q; *pb=d; }
    return best;
}

} // namespace

int path_self_intersections(const float* pts,size_t n,float tol,path_isect_result* out){
    if(!pts||!out||n<2||!(tol>=0.0f)) return -1;
    clear_result(out);

    /* 折叠零长度线段：退化线段既无方向也不应把相邻关系链断掉 */
    struct seg { size_t src; dvec a, b; };
    std::vector<seg> segs;
    segs.reserve(n-1);
    for(size_t i=0;i+1<n;++i){
        dvec a=dpt(pts,i), b=dpt(pts,i+1);
        if(same_point(a,b)) continue;
        segs.push_back(seg{i,a,b});
    }

    /* 首尾顶点完全相等时按闭合折线处理，首段与末段的接缝不算自交 */
    const bool closed=(n>=3)&&same_point(dpt(pts,0),dpt(pts,n-1));
    const double dtol=(double)tol;
    std::vector<char> used(segs.size(),0);

    for(size_t i=0;i<segs.size();++i){
        for(size_t j=i+2;j<segs.size();++j){
            if(closed&&i==0&&j==segs.size()-1) continue;
            dvec a=segs[i].a, b=segs[i].b;
            dvec c=segs[j].a, d=segs[j].b;

            dvec pt{0.0,0.0};
            double dist=0.0;
            bool exact=seg_intersect(a,b,c,d,&pt);
            if(!exact){
                if(!(tol>0.0f)) continue;
                dvec pa,pb;
                dist=seg_seg_closest(a,b,c,d,&pa,&pb);
                if(dist>dtol) continue;
                pt.x=(pa.x+pb.x)*0.5;
                pt.y=(pa.y+pb.y)*0.5;
            }

            if(exact) ++out->exact_pairs;
            else      ++out->near_pairs;
            ++out->pairs;
            used[i]=1; used[j]=1;

            if(!out->has_first){
                out->has_first=1;
                out->first_is_near=exact?0:1;
                out->first_x=(float)pt.x;
                out->first_y=(float)pt.y;
                out->first_distance=(float)dist;
                out->first_i=(int)segs[i].src;
                out->first_j=(int)segs[j].src;
            }
            if(exact&&!out->has_exact){
                out->has_exact=1;
                out->exact_x=(float)pt.x;
                out->exact_y=(float)pt.y;
                out->exact_i=(int)segs[i].src;
                out->exact_j=(int)segs[j].src;
            }
        }
    }
    for(size_t i=0;i<segs.size();++i) if(used[i]) ++out->segments;
    return 0;
}

int path_clean_and_check(const float* pts,size_t n,float clean_eps,float tol,
                         float* clean_out,size_t cap,size_t* clean_n,
                         path_isect_result* out){
    if(!pts||!clean_out||!clean_n||!out||n<2||!(tol>=0.0f)) return -1;
    int rc=path_remove_collinear(pts,n,clean_out,cap,clean_n,clean_eps);
    if(rc!=0) return rc;
    if(path_self_intersections(clean_out,*clean_n,tol,out)!=0) return -3;
    return 0;
}
