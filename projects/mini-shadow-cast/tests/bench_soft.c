#define _POSIX_C_SOURCE 199309L
#include "../src/shadow.h"
#include <stdio.h>
#include <time.h>
#include <math.h>

/* Scene (self-made):
 *   light at (5,0); occluder wall segment (0,10)-(10,10);
 *   receiver row y=20 for the edge curves, near-contact row y=11.
 * Three configs exercise the two tunables independently. */
#define LX 5.0f
#define AX 0.0f
#define AY 10.0f
#define BX 10.0f
#define BY 10.0f
#define RY 20.0f
#define NCOL 21
#define ITER 1000000

static const struct { const char* name; shadow_soft_t cfg; } CONFIGS[3]={
    {"A point-like narrow band", {0.5f, 0.10f}},
    {"B broad area light",       {4.0f, 0.20f}},
    {"C art-directed wide band", {1.0f, 1.50f}},
};

static double now_sec(void){
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC,&ts);
    return ts.tv_sec+ts.tv_nsec*1e-9;
}

int main(void){
    for(int c=0;c<3;++c){
        shadow_soft_t cfg=CONFIGS[c].cfg;
        printf("=== config %s: light_size=%.2f trans_width=%.2f ===\n",
               CONFIGS[c].name,cfg.light_size,cfg.trans_width);

        /* edge brightness curve: 21 samples spanning the right penumbra.
         * Hard umbra boundary is x=15 on the far row (f=0.5) and x=10.9
         * on the near-contact row (f=0.909); each sweep starts inside the
         * umbra (vis must read exact 0 there) and ends beyond the band. */
        float wuf=cfg.light_size*0.25f+cfg.trans_width;
        float wun=cfg.light_size*(0.1f*0.9f)+cfg.trans_width;
        printf("x_far,vis(y=20),x_near,vis(y=11)\n");
        for(int i=0;i<NCOL;++i){
            float xf=15.0f+2.0f*wuf*i/(NCOL-1);
            float xn=10.0f+(0.9f+2.0f*wun)*i/(NCOL-1);
            float vfar,vnear;
            shadow_soft_factor(xf,RY,  LX,0,AX,AY,BX,BY,&cfg,&vfar);
            shadow_soft_factor(xn,11.0f,LX,0,AX,AY,BX,BY,&cfg,&vnear);
            printf("%7.3f,%9.5f,%8.3f,%9.5f\n",xf,vfar,xn,vnear);
        }

        /* timing: ITER calls over the same sweep pattern */
        float acc=0.0f;
        double t0=now_sec();
        for(int k=0;k<ITER;++k){
            float x=-10.0f+30.0f*(k%257)/256.0f;
            float v;
            shadow_soft_factor(x,RY,LX,0,AX,AY,BX,BY,&cfg,&v);
            acc+=v;
        }
        double dt=now_sec()-t0;
        printf("time: %.4f s for %d calls, %.1f ns/call (acc=%.3f)\n\n",
               dt,ITER,dt*1e9/ITER,acc);
    }

    /* hard-path timing for reference */
    float segs[4]={AX,AY,BX,BY};
    float acc=0.0f;
    double t0=now_sec();
    for(int k=0;k<ITER;++k){
        float x=-10.0f+30.0f*(k%257)/256.0f;
        acc+=(float)shadow_point_lit(x,RY,LX,0,segs,1);
    }
    double dt=now_sec()-t0;
    printf("hard shadow_point_lit: %.4f s for %d calls, %.1f ns/call (acc=%.3f)\n",
           dt,ITER,dt*1e9/ITER,acc);
    return 0;
}
