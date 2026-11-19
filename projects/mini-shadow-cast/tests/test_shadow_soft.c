#include "../src/shadow.h"
#include <stdio.h>
#include <math.h>
static int fails=0,checks=0;
#define CHECK(c) do{++checks; if(!(c)){++fails;printf("FAIL %d %s\n",__LINE__,#c);}}while(0)

/* scene shared with the benchmark:
 * light (5,0), wall segment (0,10)-(10,10), receiver rows y=20 and y=11.
 * Hard shadow at y=20 spans x in [-5,15] (rays through the two endpoints);
 * the penumbra lives immediately outside those two boundaries. */
#define LX 5.0f
#define AX 0.0f
#define AY 10.0f
#define BX 10.0f
#define BY 10.0f

int main(void){
    shadow_soft_t cfg={2.0f,0.5f};
    float v;

    /* ---- distinguishable failure inputs ---- */
    shadow_soft_t zero_size={0.0f,0.5f};
    shadow_soft_t neg_width={2.0f,-0.25f};
    CHECK(shadow_soft_factor(5,20,LX,0, AX,AY,BX,BY, &zero_size,&v)==-2);
    CHECK(shadow_soft_factor(5,20,LX,0, AX,AY,BX,BY, &neg_width,&v)==-3);
    CHECK(shadow_soft_factor(5,20,LX,0, 4,10,4,10, &cfg,&v)==-4); /* degenerate seg */
    CHECK(shadow_soft_factor(5,20,LX,0, AX,AY,BX,BY, NULL,&v)==-1);
    shadow_soft_t bad_nan={NAN,0.5f};
    CHECK(shadow_soft_factor(5,20,LX,0, AX,AY,BX,BY, &bad_nan,&v)==-1);

    /* ---- full backlight region stays exactly dark ---- */
    CHECK(shadow_soft_factor(5,20,LX,0, AX,AY,BX,BY,&cfg,&v)==0 && v==0.0f);
    CHECK(shadow_soft_factor(0.5f,20,LX,0, AX,AY,BX,BY,&cfg,&v)==0 && v==0.0f);
    CHECK(shadow_soft_factor(9.5f,20,LX,0, AX,AY,BX,BY,&cfg,&v)==0 && v==0.0f);
    CHECK(shadow_soft_factor(14.9f,20,LX,0, AX,AY,BX,BY,&cfg,&v)==0 && v==0.0f);
    /* near contact (f->0, geometric term pinches): still exact dark */
    CHECK(shadow_soft_factor(5,10.01f,LX,0, AX,AY,BX,BY,&cfg,&v)==0 && v==0.0f);

    /* ---- fully lit outside the band ---- */
    CHECK(shadow_soft_factor(40,20,LX,0, AX,AY,BX,BY,&cfg,&v)==0 && v==1.0f);
    CHECK(shadow_soft_factor(-40,20,LX,0, AX,AY,BX,BY,&cfg,&v)==0 && v==1.0f);

    /* ---- analytic smoothstep values at known points (right edge, y=20)
     * f=0.5, wu=2*0.25+0.5=1.0; lateral w = (1-f)*|x-5| - 5 = |x-5|/2 - 5
     * x=18 -> C=11.5 -> w=1.5 -> clamped -> 1
     * x=17 -> C=11.0 -> w=1.0 -> smoothstep(1)=1
     * x=16.3333 -> w=0.6667 -> smoothstep = 0.74074 */
    CHECK(shadow_soft_factor(18,20,LX,0, AX,AY,BX,BY,&cfg,&v)==0 && v==1.0f);
    CHECK(shadow_soft_factor(17,20,LX,0, AX,AY,BX,BY,&cfg,&v)==0 && v==1.0f);
    CHECK(shadow_soft_factor(16.3333f,20,LX,0, AX,AY,BX,BY,&cfg,&v)==0
          && fabsf(v-0.74074f)<2e-3f);

    /* ---- dense 4001-point sweeps: monotone edges, no banding steps ---- */
    const int N=4001;
    float prev,maxjump=0.0f;
    int mono=1;
    /* right edge: walk from the umbra boundary outward -> non-decreasing */
    prev=-1.0f; maxjump=0.0f; mono=1;
    for(int i=0;i<N;++i){
        float x=15.0f+15.0f*i/(N-1);
        int rc=shadow_soft_factor(x,20,LX,0, AX,AY,BX,BY,&cfg,&v);
        CHECK(rc==0);
        if(rc!=0){mono=0;continue;}
        if(v<prev-1e-6f) mono=0;
        if(i>0 && v-prev>maxjump) maxjump=v-prev;
        prev=v;
    }
    CHECK(mono==1);
    CHECK(maxjump<0.01f);
    /* left edge: walk outward -> visibility non-decreasing as |x| grows */
    prev=-1.0f; mono=1;
    for(int i=0;i<N;++i){
        float x=-5.0f-15.0f*i/(N-1);
        CHECK(shadow_soft_factor(x,20,LX,0, AX,AY,BX,BY,&cfg,&v)==0);
        if(v<prev-1e-6f) mono=0;
        prev=v;
    }
    CHECK(mono==1);
    /* umbra floor is flat exact zero across the whole blocked interval,
     * including interior points where FP rounding used to leak denormals */
    float leak_x[5]={-0.15f,4.6f,8.4f,9.35f,13.15f};
    for(int i=0;i<5;++i)
        CHECK(shadow_soft_factor(leak_x[i],20,LX,0, AX,AY,BX,BY,&cfg,&v)==0
              && v==0.0f);
    for(int i=0;i<21;++i){
        float x=-4.9f+19.0f*i/20;
        CHECK(shadow_soft_factor(x,20,LX,0, AX,AY,BX,BY,&cfg,&v)==0 && v==0.0f);
    }

    /* tight band (trans_width=0): monotone, no divide blow-up, umbra dark */
    shadow_soft_t tight={0.2f,0.0f};
    prev=-1.0f; mono=1;
    for(int i=0;i<N;++i){
        float x=15.0f+15.0f*i/(N-1);
        CHECK(shadow_soft_factor(x,20,LX,0, AX,AY,BX,BY,&tight,&v)==0);
        if(v<prev-1e-6f) mono=0;
        prev=v;
    }
    CHECK(mono==1);
    CHECK(shadow_soft_factor(5,20,LX,0, AX,AY,BX,BY,&tight,&v)==0 && v==0.0f);

    /* ---- multi-segment scene: min combine, any umbra dominates ---- */
    float segs[8]={AX,AY,BX,BY, /* second wall far away, never blocks */
                   100,100,110,100};
    CHECK(shadow_soft_scene(5,20,LX,0,segs,2,&cfg,&v)==0 && v==0.0f);
    CHECK(shadow_soft_scene(40,20,LX,0,segs,2,&cfg,&v)==0 && v==1.0f);
    CHECK(shadow_soft_scene(5,20,LX,0,segs,0,&cfg,&v)==-1);
    /* degenerate segment inside a scene propagates its own code */
    float dseg[8]={4,10,4,10, AX,AY,BX,BY};
    CHECK(shadow_soft_scene(5,20,LX,0,dseg,2,&cfg,&v)==-4);

    /* ---- both tunables widen the band: at a fixed penumbra point,
     * growing light_size or trans_width must lower visibility ---- */
    float v1,v2,v3;
    shadow_soft_t c1={1.0f,0.1f}, c2={4.0f,0.1f}, c3={1.0f,1.5f};
    CHECK(shadow_soft_factor(16.0f,20,LX,0,AX,AY,BX,BY,&c1,&v1)==0);
    CHECK(shadow_soft_factor(16.0f,20,LX,0,AX,AY,BX,BY,&c2,&v2)==0);
    CHECK(shadow_soft_factor(16.0f,20,LX,0,AX,AY,BX,BY,&c3,&v3)==0);
    CHECK(v2<v1 && v3<v1);

    /* hard-path functions still answer exactly as before */
    float o1x,o1y,o2x,o2y;
    CHECK(shadow_project(0,10,10,10,5,0,20,&o1x,&o1y,&o2x,&o2y)==0);
    CHECK(o1y>10.0f && o2y>10.0f);
    float hs[4]={0,10,10,10};
    CHECK(shadow_point_lit(5,20,5,0,hs,1)==0);
    CHECK(shadow_point_lit(20,20,5,0,hs,1)==1);
    CHECK(shadow_project(0,0,0,0,0,0,10,&o1x,&o1y,&o2x,&o2y)==-1);

    printf("%d checks, %d failures (max dense-sweep jump %.5f)\n",
           checks,fails,maxjump);
    return fails?1:0;
}
