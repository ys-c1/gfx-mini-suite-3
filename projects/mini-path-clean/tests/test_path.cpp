#include "../src/path.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <cstring>
static int fails=0,checks=0;
#define CHECK(c) do{++checks; if(!(c)){++fails;std::printf("FAIL %d %s\n",__LINE__,#c);}}while(0)

static bool same_bits(float a, float b) { return std::memcmp(&a, &b, sizeof(float)) == 0; }

/* a 的每个顶点到折线 b 的最大距离。 */
static float max_dev(const float* a, size_t na, const float* b, size_t nb) {
    float m = 0.0f;
    for (size_t i = 0; i < na; ++i) {
        float d = path_point_distance(b, nb, a[i*2], a[i*2+1]);
        if (d > m) m = d;
    }
    return m;
}

/* 线段严格相交（共享端点不算）。 */
static bool seg_cross(float ax,float ay,float bx,float by,float cx,float cy,float dx,float dy){
    auto cr=[](float ox,float oy,float px,float py,float qx,float qy){
        float ux=px-ox, uy=py-oy, vx=qx-ox, vy=qy-oy;
        return ux*vy-uy*vx;
    };
    float d1=cr(cx,cy,dx,dy,ax,ay), d2=cr(cx,cy,dx,dy,bx,by);
    float d3=cr(ax,ay,bx,by,cx,cy), d4=cr(ax,ay,bx,by,dx,dy);
    return ((d1>0&&d2<0)||(d1<0&&d2>0)) && ((d3>0&&d4<0)||(d3<0&&d4>0));
}

static bool self_intersects(const float* p, size_t n) {
    for (size_t i = 0; i + 1 < n; ++i)
        for (size_t j = i + 2; j + 1 < n; ++j)
            if (seg_cross(p[i*2],p[i*2+1],p[(i+1)*2],p[(i+1)*2+1],
                          p[j*2],p[j*2+1],p[(j+1)*2],p[(j+1)*2+1])) return true;
    return false;
}

static bool on_circle(float px,float py,float cx,float cy,float r){
    float dx=px-cx, dy=py-cy;
    return std::fabs(std::sqrt(dx*dx+dy*dy)-r) <= 1e-4f*r + 1e-5f;
}

/* (顶点数, 最大偏离原折线距离, 微秒/k 次) —— 供三组样例报告用。 */
struct Sample { const char* name; const float* pts; size_t n; };

int main(){
    /* ---------- 原有测试：逐位一致 ---------- */
    float pts[8]={0,0, 5,0, 10,0, 10,5};
    float out[16]; size_t n=0;
    CHECK(path_remove_collinear(pts,4,out,8,&n,1e-3f)==0);
    CHECK(n==3);
    CHECK(out[2]==10.0f&&out[3]==0.0f);
    CHECK(std::fabs(path_length(pts,4)-15.0f)<1e-4f);
    CHECK(path_remove_collinear(pts,4,out,1,&n,1e-3f)==-2);

    /* 清理结果手算为 4 个顶点；长度用同一公式独立重算并逐位比较。 */
    {
        float big[20] = {0,0, 1,1, 2,2, 3,3, 3,4, 3,5, 4,5, 5,5, 6,5, 7,5};
        float o[24]; size_t m=0;
        CHECK(path_remove_collinear(big,10,o,12,&m,1e-3f)==0);
        CHECK(m==4);
        CHECK(o[2]==3.0f&&o[3]==3.0f);
        CHECK(o[6]==7.0f&&o[7]==5.0f);
        float want_len = 0;
        for (size_t i=1;i<m;++i){
            float dx=o[i*2]-o[(i-1)*2], dy=o[i*2+1]-o[(i-1)*2+1];
            want_len += std::sqrt(dx*dx+dy*dy);
        }
        CHECK(same_bits(path_length(o,m), want_len));
    }

    /* ---------- 半径为 0：确定性地返回原折线 ---------- */
    {
        path_round_opts ro={0.0f, 15.0f, 0};
        size_t cap = path_round_capacity(4, &ro);
        std::vector<float> r(cap*2);
        size_t rn=0;
        CHECK(path_corner_round(pts,4,&ro,r.data(),cap,&rn)==0);
        CHECK(rn==4);
        CHECK(std::memcmp(r.data(), pts, sizeof(float)*8)==0);
    }

    /* ---------- 半径为负：与 0 完全相同 ---------- */
    {
        path_round_opts ro={-2.0f, 15.0f, 0};
        size_t cap = path_round_capacity(4, &ro);
        std::vector<float> r(cap*2);
        size_t rn=0;
        CHECK(path_corner_round(pts,4,&ro,r.data(),cap,&rn)==0);
        CHECK(rn==4);
        CHECK(std::memcmp(r.data(), pts, sizeof(float)*8)==0);
    }

    /* ---------- 相邻线段过短：切线长收缩到半段 ---------- */
    {
        float shortp[8] = {0,0, 1,0, 2,0, 2,10};
        path_round_opts ro={100.0f, 10.0f, 0};
        size_t cap = path_round_capacity(4, &ro);
        std::vector<float> r(cap*2);
        size_t rn=0;
        CHECK(path_corner_round(shortp,4,&ro,r.data(),cap,&rn)==0);
        CHECK(rn > 4);
        CHECK(!self_intersects(r.data(), rn));
        /* 拐角 (2,0) 两侧线段各 1，t=0.5：T1=(1.5,0)，T2=(2,0.5) */
        CHECK(std::fabs(r[2*2]-1.5f)<1e-5f && std::fabs(r[2*2+1])<1e-5f);
        CHECK(std::fabs(r[(rn-2)*2]-2.0f)<1e-5f && std::fabs(r[(rn-2)*2+1]-0.5f)<1e-5f);
        CHECK(std::fabs(max_dev(r.data(), rn, shortp, 4)) < 0.5f);
    }

    /* ---------- 直角圆弧：半径 2，采样点严格在圆上 ---------- */
    {
        float L[6] = {0,0, 10,0, 10,10};
        path_round_opts ro={2.0f, 10.0f, 0};
        size_t cap = path_round_capacity(3, &ro);
        std::vector<float> r(cap*2);
        size_t rn=0;
        CHECK(path_corner_round(L,3,&ro,r.data(),cap,&rn)==0);
        /* 起点 + T1 + 8 内部点 + T2 + 终点 = 12；圆心 (8,2)，半径 2 */
        CHECK(rn == 12);
        bool interior_ok = true;
        for (size_t i=2;i+2<rn;i++)
            if (!on_circle(r[i*2],r[i*2+1],8.0f,2.0f,2.0f)) { interior_ok=false; break; }
        CHECK(interior_ok);
        CHECK(std::fabs(r[1*2]-8.0f)<1e-5f && std::fabs(r[1*2+1])<1e-5f);      /* T1 */
        CHECK(std::fabs(r[(rn-2)*2]-10.0f)<1e-5f && std::fabs(r[(rn-2)*2+1]-2.0f)<1e-5f); /* T2 */
        /* 采样点到原折线的最大距离落在弧中点附近，必小于 r*(1 - 1/sqrt2)。 */
        float dev = max_dev(r.data(), rn, L, 3);
        CHECK(dev < 2.0f*(1.0f - 1.0f/std::sqrt(2.0f)) + 1e-4f);
        CHECK(dev > 0.3f);
        CHECK(!self_intersects(r.data(), rn));
    }

    /* ---------- 半径自适应：短边比理想半径小 ---------- */
    {
        float L[6] = {0,0, 1,0, 1,1};
        path_round_opts ro={5.0f, 10.0f, 0};
        size_t cap = path_round_capacity(3, &ro);
        std::vector<float> r(cap*2);
        size_t rn=0;
        CHECK(path_corner_round(L,3,&ro,r.data(),cap,&rn)==0);
        CHECK(std::fabs(r[1*2]-0.5f)<1e-5f);                       /* T1 在 (0.5,0) */
        CHECK(std::fabs(r[(rn-2)*2]-1.0f)<1e-5f &&
              std::fabs(r[(rn-2)*2+1]-0.5f)<1e-5f);                /* T2 在 (1,0.5) */
        CHECK(!self_intersects(r.data(), rn));
    }

    /* ---------- 采样密度可配 ---------- */
    {
        float L[6] = {0,0, 10,0, 10,10};
        path_round_opts a={2.0f, 10.0f, 0}, b={2.0f, 90.0f, 0};
        size_t ca=path_round_capacity(3,&a), cb=path_round_capacity(3,&b);
        std::vector<float> ra(ca*2), rb(cb*2);
        size_t na=0, nb=0;
        CHECK(path_corner_round(L,3,&a,ra.data(),ca,&na)==0);
        CHECK(path_corner_round(L,3,&b,rb.data(),cb,&nb)==0);
        CHECK(na > nb);
        CHECK(nb == 4); /* 90 度步长 -> 弧内 0 个内部点：起点 + T1 + T2 + 终点 */

        /* 10 度步长：内部点等角分布，相邻点张角等于步长 */
        size_t got=0; float prev=0, diff=0;
        for (size_t i=2;i+2<na;i++) {
            float a = std::atan2(ra[i*2+1]-2.0f, ra[i*2]-8.0f);
            if (got) diff = a - prev;
            prev = a; ++got;
        }
        CHECK(got == 8);
        CHECK(std::fabs(std::fabs(diff) - 10.0f*3.14159265f/180.0f) < 1e-3f);
    }

    /* ---------- 共线输入：不新增顶点 ---------- */
    {
        float L[6] = {0,0, 5,0, 10,0};
        path_round_opts ro={3.0f, 10.0f, 0};
        size_t cap = path_round_capacity(3, &ro);
        std::vector<float> r(cap*2);
        size_t rn=0;
        CHECK(path_corner_round(L,3,&ro,r.data(),cap,&rn)==0);
        CHECK(rn==3);
    }

    /* ---------- 闭合折线：所有顶点都是拐角，首尾相接 ---------- */
    {
        float sq[8] = {0,0, 4,0, 4,4, 0,4};
        path_round_opts ro={0.5f, 10.0f, 1};
        size_t cap = path_round_capacity(4, &ro);
        std::vector<float> r(cap*2);
        size_t rn=0;
        CHECK(path_corner_round(sq,4,&ro,r.data(),cap,&rn)==0);
        CHECK(rn > 4);
        /* 每个顶点写 T1 + 8 内部点 + T2 = 10；4 个拐角共 40 */
        CHECK(rn == 40);
        CHECK(!self_intersects(r.data(), rn));
        /* 首点 = 顶点 (0,0) 的 T1 在 (0,0.5)；尾点 = (0,4) 的 T2 在 (0,3.5) */
        CHECK(std::fabs(r[0])<1e-5f && std::fabs(r[1]-0.5f)<1e-5f);
        CHECK(std::fabs(r[(rn-1)*2])<1e-5f && std::fabs(r[(rn-1)*2+1]-3.5f)<1e-5f);
    }

    /* ---------- 容量不足 ---------- */
    {
        path_round_opts ro={2.0f, 10.0f, 0};
        float r[8];
        size_t rn=0;
        CHECK(path_corner_round(pts,4,&ro,r,4,&rn)==-2);
    }

    /* ---------- in-place 与 out-of-place 逐位一致 ---------- */
    {
        float work[16]; std::memcpy(work, pts, sizeof(float)*8);
        path_round_opts ro={2.0f, 10.0f, 0};
        size_t cap = path_round_capacity(4, &ro);
        std::vector<float> sep(cap*2);
        size_t ns=0, ni=0;
        CHECK(path_corner_round(pts,4,&ro,sep.data(),cap,&ns)==0);
        CHECK(path_corner_round(work,4,&ro,work,cap,&ni)==0);
        CHECK(ns==ni);
        CHECK(std::memcmp(sep.data(), work, sizeof(float)*ni*2)==0);
    }

    std::printf("%d checks, %d failures\n",checks,fails);
    return fails?1:0;
}
