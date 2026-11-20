#include "../src/shadow.h"
#include "../src/state.h"
#include <stdio.h>
#include <math.h>
#include <string.h>
static int fails=0,checks=0;
#define CHECK(c) do{++checks; if(!(c)){++fails;printf("FAIL %d %s\n",__LINE__,#c);}}while(0)
static int close_enough(double a,double b){ return fabs(a-b)<1e-12; }
/* weights arrive as float, so weight-derived brightness has float tolerance */
static int closef(double a,double b){ return fabs(a-b)<1e-6; }

int main(void){
    /* ---- legacy behavior, unchanged ---- */
    float o1x,o1y,o2x,o2y;
    CHECK(shadow_project(0,10,10,10,5,0,20,&o1x,&o1y,&o2x,&o2y)==0);
    CHECK(o1y>10.0f && o2y>10.0f);
    float segs[4]={0,10,10,10};
    CHECK(shadow_point_lit(5,20,5,0,segs,1)==0);
    CHECK(shadow_point_lit(20,20,5,0,segs,1)==1);
    CHECK(shadow_project(0,0,0,0,0,0,10,&o1x,&o1y,&o2x,&o2y)==-1);

    /* ---- multi-light linear blending ---- */
    shadow_light two[2]={{5,0,0.5f},{15,0,0.5f}};
    double b;
    /* fully back-lit region: blocked by both, weights sum to 1 -> exactly 0 */
    CHECK(shadow_brightness(2,20,two,2,segs,1,&b)==0 && close_enough(b,0.0));
    /* half shadow: blocked by L1, lit by L2 -> 0.5, no multiplicative darkening */
    CHECK(shadow_brightness(10,20,two,2,segs,1,&b)==0 && close_enough(b,0.5));
    /* fully lit overlap -> 1 (not brighter than 1, no seam) */
    CHECK(shadow_brightness(20,20,two,2,segs,1,&b)==0 && close_enough(b,1.0));

    /* configurable unequal weights: 0.7 blocked / 0.3 lit -> 0.3 */
    shadow_light uw[2]={{5,0,0.7f},{15,0,0.3f}};
    CHECK(shadow_brightness(10,20,uw,2,segs,1,&b)==0 && closef(b,0.3));
    CHECK(shadow_brightness(20,20,uw,2,segs,1,&b)==0 && close_enough(b,1.0));

    /* single light in the new API matches 0/1 point lighting exactly */
    shadow_light one[1]={{5,0,1.0f}};
    CHECK(shadow_brightness(5,20,one,1,segs,1,&b)==0 && close_enough(b,0.0));
    CHECK(shadow_brightness(20,20,one,1,segs,1,&b)==0 && close_enough(b,1.0));

    /* edge: zero lights -> deterministic full darkness */
    CHECK(shadow_brightness(20,20,NULL,0,segs,1,&b)==0 && close_enough(b,0.0));
    CHECK(shadow_brightness(20,20,two,-1,segs,1,&b)==-1);
    CHECK(shadow_brightness(20,20,two,2,NULL,1,&b)==-1);

    /* edge: negative weights are clamped to 0 */
    shadow_light neg[2]={{5,0,-1.0f},{15,0,1.0f}};
    CHECK(shadow_brightness(10,20,neg,2,segs,1,&b)==0 && close_enough(b,1.0));
    CHECK(shadow_brightness(20,20,neg,2,segs,1,&b)==0 && close_enough(b,1.0));
    /* all weights zero: zero denominator -> 0, never NaN/inf */
    shadow_light zw[2]={{5,0,0.0f},{15,0,0.0f}};
    CHECK(shadow_brightness(20,20,zw,2,segs,1,&b)==0 && close_enough(b,0.0));

    /* edge: coincident lights behave as one, never double-darken */
    shadow_light coin[2]={{5,0,0.5f},{5,0,0.5f}};
    double bc1,bc2,bs1,bs2;
    CHECK(shadow_brightness(10,20,coin,2,segs,1,&bc1)==0);
    CHECK(shadow_brightness(20,20,coin,2,segs,1,&bc2)==0);
    CHECK(shadow_brightness(10,20,one,1,segs,1,&bs1)==0);
    CHECK(shadow_brightness(20,20,one,1,segs,1,&bs2)==0);
    CHECK(close_enough(bc1,bs1) && close_enough(bc2,bs2));

    /* ---- grid sampling and report counters ---- */
    double grid[36]; shadow_report rep;
    CHECK(shadow_sample_grid(0,0,20,30,6,6,two,2,segs,1,grid,&rep)==0);
    CHECK(rep.samples==36);
    CHECK(rep.clamped==0 && rep.coincident==0);
    CHECK(rep.lit>0 && rep.lit<36);
    CHECK(close_enough(rep.vmin,0.0));
    CHECK(rep.vmax<=1.0+1e-12 && rep.mean>0.0 && rep.mean<1.0);
    CHECK(close_enough(rep.lit_ratio,(double)rep.lit/36.0));
    CHECK(close_enough(rep.wsum,1.0));
    for(int i=0;i<36;++i) CHECK(grid[i]>=0.0 && grid[i]<=1.0);

    shadow_report rneg;
    CHECK(shadow_sample_grid(0,0,20,30,4,4,neg,2,segs,1,NULL,&rneg)==0);
    CHECK(rneg.clamped==1);
    shadow_report rcoin;
    CHECK(shadow_sample_grid(0,0,20,30,4,4,coin,2,segs,1,NULL,&rcoin)==0);
    CHECK(rcoin.coincident==1);
    shadow_report rzero;
    CHECK(shadow_sample_grid(0,0,20,30,4,4,NULL,0,segs,1,NULL,&rzero)==0);
    CHECK(rzero.lit==0 && close_enough(rzero.mean,0.0) && close_enough(rzero.vmax,0.0));
    CHECK(shadow_sample_grid(0,0,20,30,0,4,two,2,segs,1,NULL,NULL)==-1);

    /* ---- GSF2 state round trip ---- */
    const char* p="/tmp/mini_shadow_state_test.bin";
    state_report s={2,6,6,20,1,1,1.0,27.889,0.416,0.0,1.0,0.555};
    CHECK(state_save_report(p,"shadow",1,&s)==0);
    char tag[16]; int frames; state_report s2;
    CHECK(state_load_report(p,tag,&frames,&s2)==0);
    CHECK(frames==1 && strcmp(tag,"shadow")==0);
    CHECK(s2.nlight==2 && s2.grid_nx==6 && s2.grid_ny==6 && s2.lit==20);
    CHECK(s2.clamped==1 && s2.coincident==1);
    CHECK(close_enough(s2.wsum,1.0) && close_enough(s2.span,27.889));
    CHECK(close_enough(s2.mean,0.416) && close_enough(s2.vmin,0.0));
    CHECK(close_enough(s2.vmax,1.0) && close_enough(s2.lit_ratio,0.555));
    CHECK(state_load_report("/tmp/does_not_exist.bin",tag,&frames,&s2)==-2);

    /* ---- legacy GSF1 state still works ---- */
    CHECK(state_save("/tmp/mini_shadow_gsf1.bin","shadow",1,27.889)==0);
    char tg[16]; int fr; double metric;
    CHECK(state_load("/tmp/mini_shadow_gsf1.bin",tg,&fr,&metric)==0);
    CHECK(fr==1 && close_enough(metric,27.889));

    printf("%d checks, %d failures\n",checks,fails);
    return fails?1:0;
}
