#include "shadow.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
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

int shadow_hit_count(float px,float py,float lx,float ly,const float* segs,int nseg){
    if(!segs||nseg<0) return -1;
    int hits=0;
    for(int i=0;i<nseg;++i){
        float ax=segs[i*4],ay=segs[i*4+1],bx=segs[i*4+2],by=segs[i*4+3];
        float rx=px-lx, ry=py-ly, sx=bx-ax, sy=by-ay;
        float den=rx*sy-ry*sx;
        if(fabsf(den)<1e-9f) continue;
        float t=((ax-lx)*sy-(ay-ly)*sx)/den;
        float u=((ax-lx)*ry-(ay-ly)*rx)/den;
        if(t>0.0f&&t<1.0f&&u>=0.0f&&u<=1.0f) ++hits;
    }
    return hits;
}

/* ---- 相邻遮挡段合并 ---- */

typedef struct {
    float ax,ay,bx,by;   /* 规范化后的端点（方向已统一） */
    float dx,dy,len;     /* 单位方向与长度 */
    int idx;             /* 原始输入下标 */
    int swapped;         /* 规范化时是否交换过端点 */
} mseg;

static int uf_find(int* p,int i){ while(p[i]!=i){ p[i]=p[p[i]]; i=p[i]; } return i; }

/* 取成员 j 的原始坐标：end=0 为规范化后的 a 端，end=1 为 b 端。
 * 直接回读输入数组，保证合并结果的端点永远是原始顶点，不引入新坐标。 */
static void orig_pt(const float* segs,const mseg* s,int end,float* x,float* y){
    int base=s->idx*4;
    int off=(end==0)?(s->swapped?2:0):(s->swapped?0:2);
    *x=segs[base+off]; *y=segs[base+off+1];
}

int shadow_merge_segments(const float* segs,int nseg,float tol,float* out,int out_cap){
    if(nseg<0||!(tol>=0.0f)) return -1;      /* tol<0 或 NaN：拒绝 */
    if(nseg==0) return 0;                     /* 零输入：确定的空结果 */
    if(!segs||!out||out_cap<nseg) return -1;

    mseg* s=(mseg*)malloc(sizeof(mseg)*(size_t)nseg);
    int* parent=(int*)malloc(sizeof(int)*(size_t)nseg);
    char* seen=(char*)calloc((size_t)nseg,1);
    if(!s||!parent||!seen){ free(s); free(parent); free(seen); return -1; }

    /* 规范化：丢弃零长度段，方向统一为字典序 */
    int n=0;
    for(int i=0;i<nseg;++i){
        float ax=segs[i*4],ay=segs[i*4+1],bx=segs[i*4+2],by=segs[i*4+3];
        float dx=bx-ax,dy=by-ay;
        float len=sqrtf(dx*dx+dy*dy);
        if(len<1e-12f) continue;              /* 退化段：丢弃 */
        int sw=0;
        if(bx<ax||(bx==ax&&by<ay)){ float tx=ax,ty=ay; ax=bx; ay=by; bx=tx; by=ty; dx=-dx; dy=-dy; sw=1; }
        s[n].ax=ax; s[n].ay=ay; s[n].bx=bx; s[n].by=by;
        s[n].dx=dx/len; s[n].dy=dy/len; s[n].len=len;
        s[n].idx=i; s[n].swapped=sw;
        parent[n]=n; ++n;
    }
    if(n==0){ free(s); free(parent); free(seen); return 0; }

    /* 并查集：方向共线（固定角阈值）+ 垂直距离<=tol + 区间间隙<=tol */
    for(int i=0;i<n;++i){
        for(int j=i+1;j<n;++j){
            float cross=s[i].dx*s[j].dy-s[i].dy*s[j].dx;
            if(fabsf(cross)>1e-6f) continue;  /* 角度不随 tol 放宽 */
            float rax=s[j].ax-s[i].ax, ray=s[j].ay-s[i].ay;
            float rbx=s[j].bx-s[i].ax, rby=s[j].by-s[i].ay;
            float pa=fabsf(s[i].dx*ray-s[i].dy*rax);
            float pb=fabsf(s[i].dx*rby-s[i].dy*rbx);
            if(pa>tol||pb>tol) continue;
            float t0=s[i].dx*rax+s[i].dy*ray;
            float t1=s[i].dx*rbx+s[i].dy*rby;
            float lo=t0<t1?t0:t1, hi=t0>t1?t0:t1;
            float gap=0.0f;
            if(lo>s[i].len) gap=lo-s[i].len;
            else if(hi<0.0f) gap=-hi;
            if(gap>tol) continue;
            int ri=uf_find(parent,i), rj=uf_find(parent,j);
            if(ri!=rj) parent[rj]=ri;
        }
    }

    /* 按“最小组内下标”的顺序输出，保证对同一输入结果确定 */
    int count=0;
    for(int i=0;i<n;++i){
        int r=uf_find(parent,i);
        if(seen[r]) continue;
        seen[r]=1;
        int first=-1,members=0;
        for(int j=0;j<n;++j) if(uf_find(parent,j)==r){ if(first<0) first=j; ++members; }
        if(members==1){
            /* 单段：逐位拷贝原始输入，保证合并前后完全一致 */
            memcpy(out+(size_t)count*4,segs+(size_t)s[first].idx*4,4*sizeof(float));
            ++count; continue;
        }
        float dx=s[first].dx, dy=s[first].dy, ox=s[first].ax, oy=s[first].ay;
        float tmin=0.0f,tmax=0.0f; int emin=-1,emin_end=0,emax=-1,emax_end=0;
        for(int j=0;j<n;++j){
            if(uf_find(parent,j)!=r) continue;
            float ta=dx*(s[j].ax-ox)+dy*(s[j].ay-oy);
            float tb=dx*(s[j].bx-ox)+dy*(s[j].by-oy);
            if(emin<0||ta<tmin){ tmin=ta; emin=j; emin_end=0; }
            if(tb<tmin){ tmin=tb; emin=j; emin_end=1; }
            if(emax<0||ta>tmax){ tmax=ta; emax=j; emax_end=0; }
            if(tb>tmax){ tmax=tb; emax=j; emax_end=1; }
        }
        float x1,y1,x2,y2;
        orig_pt(segs,&s[emin],emin_end,&x1,&y1);
        orig_pt(segs,&s[emax],emax_end,&x2,&y2);
        out[count*4]=x1; out[count*4+1]=y1; out[count*4+2]=x2; out[count*4+3]=y2;
        ++count;
    }
    free(s); free(parent); free(seen);
    return count;
}
