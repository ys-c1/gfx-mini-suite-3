/* 四组场景报告：合并段数、重叠区亮度、接缝跳变与耗时。
 * 亮度模型：每段遮挡 50%，b = max(0, 1 - 0.5*hits)。单条遮挡物基准亮度 0.5。 */
#define _POSIX_C_SOURCE 199309L
#include "../src/shadow.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>

#define MAXSEG 8192
#define MAXPROBE 64

static float g_segs[MAXSEG*4], g_merged[MAXSEG*4];
static float g_probe[MAXPROBE*2];
static int g_nseg, g_nprobe;
static float g_lx, g_ly, g_tol;

static double now_ms(void){
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts);
    return ts.tv_sec*1e3+ts.tv_nsec/1e6;
}
static float bright(float px,float py,const float* segs,int n){
    int h=shadow_hit_count(px,py,g_lx,g_ly,segs,n);
    if(h<0) h=0;
    float b=1.0f-0.5f*(float)h;
    return b<0.0f?0.0f:b;
}
static int load_scene(const char* path){
    FILE* f=fopen(path,"r");
    if(!f) return -1;
    char line[256];
    g_nseg=0; g_nprobe=0; g_tol=0.01f; g_lx=0; g_ly=0;
    while(fgets(line,sizeof line,f)){
        char key[32];
        if(sscanf(line,"%31s",key)!=1||key[0]=='#') continue;
        if(!strcmp(key,"light")) sscanf(line,"%*s %f %f",&g_lx,&g_ly);
        else if(!strcmp(key,"tol")) sscanf(line,"%*s %f",&g_tol);
        else if(!strcmp(key,"seg")&&g_nseg<MAXSEG){
            float* s=g_segs+g_nseg*4;
            if(sscanf(line,"%*s %f %f %f %f",s,s+1,s+2,s+3)==4) ++g_nseg;
        }else if(!strcmp(key,"probe")&&g_nprobe<MAXPROBE){
            float* p=g_probe+g_nprobe*2;
            if(sscanf(line,"%*s %f %f",p,p+1)==2) ++g_nprobe;
        }
    }
    fclose(f);
    return 0;
}
/* 网格统计。伪影区 = 合并前 hits 大于合并后 hits 的点（即可合并段造成的重复覆盖；
 * 两段独立墙的真实交叠 hits 前后不变，不计入）。统计伪影区最低亮度与
 * 同一逻辑遮挡物影区内（两侧合并后均 hits>=1 且至少一侧在伪影区）的最大亮度跳变。 */
static void grid_stats(const float* before,int nb,const float* after,int na,
                       float* omin_b,float* omin_a,float* jump_b,float* jump_a,int* zone_n){
    float x0=1e30f,y0=1e30f,x1=-1e30f,y1=-1e30f;
    for(int i=0;i<nb;++i){
        const float* s=before+i*4;
        if(s[0]<x0)x0=s[0];
        if(s[2]<x0)x0=s[2];
        if(s[0]>x1)x1=s[0];
        if(s[2]>x1)x1=s[2];
        if(s[1]<y0)y0=s[1];
        if(s[3]<y0)y0=s[3];
        if(s[1]>y1)y1=s[1];
        if(s[3]>y1)y1=s[3];
    }
    x0-=6;y0-=6;x1+=6;y1+=6;
    const int G=121;
    float mb=2, ma=2, jb=0, ja=0; int zn=0;
    for(int gy=0;gy<G;++gy){
        float py=y0+(y1-y0)*gy/(G-1);
        float pb_b=0,pa_b=0; int ph_a=0,pzone=0;
        for(int gx=0;gx<G;++gx){
            float px=x0+(x1-x0)*gx/(G-1);
            int hb=shadow_hit_count(px,py,g_lx,g_ly,before,nb);
            int ha=shadow_hit_count(px,py,g_lx,g_ly,after,na);
            float bb=bright(px,py,before,nb), ba=bright(px,py,after,na);
            int zone=hb>ha;
            if(zone){ ++zn; if(bb<mb)mb=bb; if(ba<ma)ma=ba; }
            if(gx>0&&ha>=1&&ph_a>=1&&(zone||pzone)){
                float db=fabsf(bb-pb_b); if(db>jb)jb=db;
                float da=fabsf(ba-pa_b); if(da>ja)ja=da;
            }
            pb_b=bb; pa_b=ba; ph_a=ha; pzone=zone;
        }
    }
    *omin_b=mb; *omin_a=ma; *jump_b=jb; *jump_a=ja; *zone_n=zn;
}
static void report(const char* name){
    float mb,ma,jb,ja; int zn;
    double t0,t1;
    t0=now_ms();
    int m0=shadow_merge_segments(g_segs,g_nseg,0.0f,g_merged,MAXSEG);
    t1=now_ms();
    double ms0=t1-t0;
    t0=now_ms();
    int mt=shadow_merge_segments(g_segs,g_nseg,g_tol,g_merged,MAXSEG);
    t1=now_ms();
    double mst=t1-t0;
    printf("== %s ==\n",name);
    printf("  段数: %d -> tol=0: %d -> tol=%.3g: %d\n",g_nseg,m0,g_tol,mt);
    printf("  合并耗时: tol=0 %.3f ms, tol=%.3g %.3f ms\n",ms0,g_tol,mst);
    grid_stats(g_segs,g_nseg,g_merged,mt,&mb,&ma,&jb,&ja,&zn);
    if(zn>0){
        printf("  重叠伪影区(%d 采样点)最低亮度: 合并前 %.2f -> 合并后 %.2f (单遮挡物基准 0.50)\n",zn,mb,ma);
        printf("  接缝处最大亮度跳变: 合并前 %.2f -> 合并后 %.2f\n",jb,ja);
    }else{
        printf("  重叠伪影区: 无（段间无冗余覆盖，见探针）\n");
    }
    for(int i=0;i<g_nprobe;++i){
        float px=g_probe[i*2],py=g_probe[i*2+1];
        printf("  探针(%.4g,%.4g): 亮度 %.2f -> %.2f\n",px,py,
               bright(px,py,g_segs,g_nseg),bright(px,py,g_merged,mt));
    }
}
int main(int argc,char** argv){
    const char* p1=argc>1?argv[1]:"data/scene_adjacent.dat";
    const char* p2=argc>2?argv[2]:"data/scene_overlap.dat";
    const char* p3=argc>3?argv[3]:"data/scene_mixed.dat";
    if(load_scene(p1)==0) report("S1 相邻接缝 (scene_adjacent.dat)");
    else printf("skip %s\n",p1);
    if(load_scene(p2)==0) report("S2 重叠链 (scene_overlap.dat)");
    else printf("skip %s\n",p2);
    if(load_scene(p3)==0) report("S3 混合轮廓 (scene_mixed.dat)");
    else printf("skip %s\n",p3);
    /* S4 生成场景：4096 段共线链，段长 1.0、间距 0.9（相邻重叠 0.1） */
    g_nseg=4096; g_nprobe=2; g_tol=0.05f; g_lx=1843.2f; g_ly=-500.0f;
    for(int i=0;i<g_nseg;++i){
        float* s=g_segs+i*4;
        s[0]=i*0.9f; s[1]=10.0f; s[2]=i*0.9f+1.0f; s[3]=10.0f;
    }
    g_probe[0]=1800.0f; g_probe[1]=20.0f;
    g_probe[2]=1843.2f; g_probe[3]=25.0f;
    report("S4 长链性能 (4096 段, 生成)");
    return 0;
}
