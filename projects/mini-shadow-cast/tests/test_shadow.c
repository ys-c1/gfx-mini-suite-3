#include "../src/shadow.h"
#include <stdio.h>
#include <math.h>
#include <string.h>
static int fails=0,checks=0;
#define CHECK(c) do{++checks; if(!(c)){++fails;printf("FAIL %d %s\n",__LINE__,#c);}}while(0)
int main(void){
    float o1x,o1y,o2x,o2y;
    CHECK(shadow_project(0,10,10,10,5,0,20,&o1x,&o1y,&o2x,&o2y)==0);
    CHECK(o1y>10.0f && o2y>10.0f);
    float segs[4]={0,10,10,10};
    CHECK(shadow_point_lit(5,20,5,0,segs,1)==0);
    CHECK(shadow_point_lit(20,20,5,0,segs,1)==1);
    CHECK(shadow_project(0,0,0,0,0,0,10,&o1x,&o1y,&o2x,&o2y)==-1);

    /* ---- 合并：单条遮挡物位级一致 ---- */
    float mo[16];
    float one[4]={1,2,3,4};
    CHECK(shadow_merge_segments(one,1,0.0f,mo,1)==1);
    CHECK(memcmp(one,mo,sizeof one)==0);
    float rev[4]={3,4,1,2};                 /* 端点逆序也原样返回 */
    CHECK(shadow_merge_segments(rev,1,0.5f,mo,1)==1);
    CHECK(memcmp(rev,mo,sizeof rev)==0);
    /* 单条合并前后投影/遮挡结果一致 */
    CHECK(shadow_merge_segments(segs,1,0.0f,mo,1)==1);
    CHECK(shadow_project(mo[0],mo[1],mo[2],mo[3],5,0,20,&o1x,&o1y,&o2x,&o2y)==0);
    CHECK(shadow_point_lit(5,20,5,0,mo,1)==0);
    CHECK(shadow_point_lit(20,20,5,0,mo,1)==1);

    /* ---- 相邻共线段合并 ---- */
    float adj[8]={0,10,5,10, 5,10,10,10};   /* 端点相接 */
    CHECK(shadow_merge_segments(adj,2,0.0f,mo,2)==1);
    CHECK(mo[0]==0&&mo[1]==10&&mo[2]==10&&mo[3]==10);
    float ov[8]={0,10,6,10, 4,10,10,10};    /* 区间重叠 */
    CHECK(shadow_merge_segments(ov,2,0.0f,mo,2)==1);
    CHECK(mo[0]==0&&mo[1]==10&&mo[2]==10&&mo[3]==10);
    /* 容差边界：间隙 0.5，tol 0.1 不合，tol 0.5 合（含边界） */
    float gap[8]={0,10,4,10, 4.5f,10,10,10};
    CHECK(shadow_merge_segments(gap,2,0.1f,mo,2)==2);
    CHECK(shadow_merge_segments(gap,2,0.5f,mo,2)==1);
    /* 平行错位：垂直距离 0.05，tol 0.1 合，tol 0.01 不合 */
    float off[8]={0,0,4,0, 0,0.05f,4,0.05f};
    CHECK(shadow_merge_segments(off,2,0.1f,mo,2)==1);
    CHECK(shadow_merge_segments(off,2,0.01f,mo,2)==2);

    /* ---- 边界一：tol==0，精确相接才合并 ---- */
    CHECK(shadow_merge_segments(adj,2,0.0f,mo,2)==1);
    CHECK(shadow_merge_segments(gap,2,0.0f,mo,2)==2);

    /* ---- 边界二：tol 过大，轮廓退化但结果确定 ---- */
    float perp[8]={0,0,4,0, 4,0,4,4};       /* 非共线：任意 tol 不合 */
    CHECK(shadow_merge_segments(perp,2,1e30f,mo,2)==2);
    float far[8]={0,0,1,0, 100,0,101,0};    /* 共线远距：并成跨极值段 */
    CHECK(shadow_merge_segments(far,2,1e30f,mo,2)==1);
    CHECK(mo[0]==0&&mo[1]==0&&mo[2]==101&&mo[3]==0);

    /* ---- 边界三：零输入与退化段 ---- */
    CHECK(shadow_merge_segments(NULL,0,0.0f,mo,2)==0);
    CHECK(shadow_merge_segments(adj,0,0.0f,mo,2)==0);
    float zl[12]={0,0,0,0, 1,1,1,1, 0,5,4,5}; /* 零长度段丢弃 */
    CHECK(shadow_merge_segments(zl,3,0.0f,mo,3)==1);
    CHECK(mo[0]==0&&mo[1]==5&&mo[2]==4&&mo[3]==5);
    float zz[8]={1,1,1,1, 2,2,2,2};          /* 全部退化 -> 0 */
    CHECK(shadow_merge_segments(zz,2,0.0f,mo,2)==0);

    /* ---- 非法参数 ---- */
    CHECK(shadow_merge_segments(NULL,2,0.0f,mo,2)==-1);
    CHECK(shadow_merge_segments(adj,2,-0.1f,mo,2)==-1);
    CHECK(shadow_merge_segments(adj,2,NAN,mo,2)==-1);
    CHECK(shadow_merge_segments(adj,2,0.0f,NULL,2)==-1);
    CHECK(shadow_merge_segments(adj,2,0.0f,mo,1)==-1);

    /* ---- 重叠区只投影一次：接缝/重叠处亮度无跳变 ---- */
    CHECK(shadow_hit_count(5,20,5,0,adj,2)==2);      /* 接缝射线被计两次 */
    int m=shadow_merge_segments(adj,2,0.0f,mo,2);
    CHECK(m==1);
    CHECK(shadow_hit_count(5,20,5,0,mo,1)==1);       /* 合并后只计一次 */
    CHECK(shadow_hit_count(5,20,5,0,ov,2)==2);       /* 重叠区中心 */
    m=shadow_merge_segments(ov,2,0.0f,mo,2);
    CHECK(m==1);
    CHECK(shadow_hit_count(5,20,5,0,mo,1)==1);
    /* 合并后整条墙亮度均匀：接缝点与墙中点 hits 相同 */
    CHECK(shadow_hit_count(5,20,5,0,mo,1)==shadow_hit_count(2.5f,20,5,0,mo,1));
    CHECK(shadow_hit_count(5,20,5,0,mo,1)==shadow_hit_count(7.5f,20,5,0,mo,1));
    /* hit_count 参数约定 */
    CHECK(shadow_hit_count(5,20,5,0,adj,0)==0);
    CHECK(shadow_hit_count(5,20,5,0,NULL,1)==-1);

    /* ---- 合并不改变遮挡判定（精确共线数据） ---- */
    {
        float big[12]={0,10,4,10, 3,10,7,10, 7,10,10,10};
        float bm[12];
        CHECK(shadow_merge_segments(big,3,0.0f,bm,3)==1);
        for(int k=0;k<=20;++k){
            float px=-5.0f+k*1.0f, py=20.0f;
            CHECK(shadow_point_lit(px,py,5,0,big,3)==shadow_point_lit(px,py,5,0,bm,1));
        }
    }
    printf("%d checks, %d failures\n",checks,fails);
    return fails?1:0;
}
