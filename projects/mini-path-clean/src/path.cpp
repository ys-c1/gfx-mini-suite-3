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

/* 点 p 到线段 ab 的距离 */
static float pt_seg_dist(float px,float py,float ax,float ay,float bx,float by){
    float dx=bx-ax, dy=by-ay;
    float l2=dx*dx+dy*dy;
    float t=0.0f;
    if(l2>0.0f){
        t=((px-ax)*dx+(py-ay)*dy)/l2;
        if(t<0.0f) t=0.0f; else if(t>1.0f) t=1.0f;
    }
    float ex=ax+t*dx-px, ey=ay+t*dy-py;
    return std::sqrt(ex*ex+ey*ey);
}

/* 顶点 i 到所有非关联线段的最小距离(无则返回一个很大的值) */
static float corner_clearance(const std::vector<float>& p,size_t m,size_t i){
    float best=3.0e38f;
    for(size_t j=0;j+1<m;++j){
        if(j==i-1||j==i) continue; /* 跳过关联线段 (i-1,i) 与 (i,i+1) */
        float d=pt_seg_dist(p[i*2],p[i*2+1],p[j*2],p[j*2+1],p[j*2+2],p[j*2+3]);
        if(d<best) best=d;
    }
    return best;
}

/* 一轮切角: 每个内拐角 B 用 BA、BC 上距 B 为 t 比例的两点替换 */
static void chaikin_round(const std::vector<float>& in,size_t m,const std::vector<float>& ts,std::vector<float>& out){
    out.clear();
    out.reserve((2*m-2)*2);
    out.push_back(in[0]); out.push_back(in[1]);
    for(size_t i=1;i+1<m;++i){
        float ax=in[(i-1)*2],ay=in[(i-1)*2+1];
        float bx=in[i*2],by=in[i*2+1];
        float cx=in[(i+1)*2],cy=in[(i+1)*2+1];
        float t=ts[i];
        out.push_back(bx+t*(ax-bx)); out.push_back(by+t*(ay-by));
        out.push_back(bx+t*(cx-bx)); out.push_back(by+t*(cy-by));
    }
    out.push_back(in[(m-1)*2]); out.push_back(in[(m-1)*2+1]);
}

int path_smooth_chaikin(const float* pts,size_t n,int rounds,float ratio,float* out,size_t cap,size_t* outn){
    if(!pts||!out||!outn||n<1) return -1;
    if(rounds<0) return -3;
    float r=ratio;
    if(r<0.0f) r=0.0f;
    if(r>0.5f) r=0.5f;
    if(n<3||rounds==0||r==0.0f){
        if(n>cap) return -2;
        for(size_t i=0;i<n*2;++i) out[i]=pts[i];
        *outn=n;
        return 0;
    }
    std::vector<float> cur(pts,pts+n*2), nxt, ts;
    size_t m=n;
    for(int k=0;k<rounds;++k){
        ts.assign(m,0.0f);
        for(size_t i=1;i+1<m;++i){
            float ax=cur[(i-1)*2],ay=cur[(i-1)*2+1];
            float bx=cur[i*2],by=cur[i*2+1];
            float cx=cur[(i+1)*2],cy=cur[(i+1)*2+1];
            float la=std::sqrt((bx-ax)*(bx-ax)+(by-ay)*(by-ay));
            float lb=std::sqrt((cx-bx)*(cx-bx)+(cy-by)*(cy-by));
            float lm=la>lb?la:lb;
            if(lm<=0.0f){ ts[i]=0.0f; continue; }
            /* 切角三角形落在以 B 为圆心、t*lm 为半径的圆盘内;
               收缩 t 使圆盘不触及任何非相邻线段, 保证不自交 */
            float tmax=0.9f*corner_clearance(cur,m,i)/lm;
            float t=r<tmax?r:tmax;
            ts[i]=t<0.0f?0.0f:t;
        }
        chaikin_round(cur,m,ts,nxt);
        cur.swap(nxt);
        m=cur.size()/2;
    }
    if(m>cap) return -2;
    for(size_t i=0;i<m*2;++i) out[i]=cur[i];
    *outn=m;
    return 0;
}
