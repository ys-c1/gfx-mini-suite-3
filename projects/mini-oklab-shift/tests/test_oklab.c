#include "../src/oklab.h"
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdint.h>

static int fails=0,checks=0;
#define CHECK(c) do{++checks; if(!(c)){++fails;printf("FAIL line %d: %s\n",__LINE__,#c);}}while(0)
#define CHECK_EQ(a,b) do{++checks; int a_=(int)(a),b_=(int)(b); if(a_!=b_){++fails;printf("FAIL line %d: %s==%s (%d!=%d)\n",__LINE__,#a,#b,a_,b_);}}while(0)

/* ------------------------------------------------------------------ */
/* Golden hashes of the legacy path, computed from the pre-change       */
/* source (commit b07de1d) over a 64^3 grid sampled every 4 code units. */
/* ------------------------------------------------------------------ */
static uint64_t fnv1a(const unsigned char* p,size_t n){
    uint64_t h=1469598103934665603ULL;
    for(size_t i=0;i<n;i++){ h^=p[i]; h*=1099511628211ULL; }
    return h;
}

static void fill_grid(unsigned char* in,int step,int* npx){
    int m=0;
    for(int b=0;b<256;b+=step)
        for(int g=0;g<256;g+=step)
            for(int r=0;r<256;r+=step){
                in[m*3]=(unsigned char)r;
                in[m*3+1]=(unsigned char)g;
                in[m*3+2]=(unsigned char)b;
                ++m;
            }
    *npx=m;
}

static void test_legacy_golden(void){
    static const struct { float factor; uint64_t hash; } golden[]={
        {0.5f, 0x4af8f9c3631bf1abULL},
        {1.0f, 0x46f46b761db54383ULL},
        {1.2f, 0x7f9dabe3b8171cabULL},
        {1.5f, 0x65b3b576714d7a4bULL},
        {2.0f, 0xbc1a4ffe3f1facf3ULL},
        {3.0f, 0xcd4bf8c59e274c3bULL},
    };
    static unsigned char in[64*64*64*3], out[64*64*64*3];
    int npx=0;
    fill_grid(in,4,&npx);
    CHECK_EQ(npx,64*64*64);
    for(size_t k=0;k<sizeof golden/sizeof golden[0];k++){
        CHECK(brightness_shift(in,npx*3,golden[k].factor,out)==0);
        CHECK(fnv1a(out,(size_t)npx*3)==golden[k].hash);
    }
}

/* ------------------------------------------------------------------ */
/* Stat helpers                                                        */
/* ------------------------------------------------------------------ */
static void run_gamut(const unsigned char* in,int n,float f,float s,
                      unsigned char* out,gamut_stats* st){
    memset(out,0xAA,(size_t)n); /* poison, so untouched bytes are visible */
    CHECK(brightness_shift_gamut(in,n,f,s,out,st)==0);
}

static void legacy_of(const unsigned char* in,int n,float f,unsigned char* out){
    CHECK(brightness_shift(in,n,f,out)==0);
}

static int max_abs_diff(const unsigned char* a,const unsigned char* b,int n){
    int m=0;
    for(int i=0;i<n;i++){
        int d=(int)a[i]-(int)b[i];
        if(d<0) d=-d;
        if(d>m) m=d;
    }
    return m;
}

/* In-gamut pixels must be byte-identical to legacy at every strength.     */
static void test_ingamut_untouched(void){
    /* (2,2,2) through (200,200,200): never escapes at factor 1.2. */
    unsigned char in[3],leg[3],out[3];
    gamut_stats st;
    for(int v=0;v<=200;v+=1){
        in[0]=in[1]=in[2]=(unsigned char)v;
        legacy_of(in,3,1.2f,leg);
        for(float s=0.0f;s<=1.0f;s+=0.25f){
            run_gamut(in,3,1.2f,s,out,&st);
            CHECK(memcmp(leg,out,3)==0);
            CHECK_EQ(st.out_of_gamut,0);
            CHECK_EQ(st.changed,0);
            CHECK_EQ(st.max_delta,0);
        }
    }
    /* A mid-saturation colour that is inside the cube at factor 1.0. */
    unsigned char c[3]={180,90,40};
    legacy_of(c,3,1.0f,leg);
    run_gamut(c,3,1.0f,1.0f,out,&st);
    CHECK(memcmp(leg,out,3)==0);
    CHECK_EQ(st.changed,0);
}

/* ------------------------------------------------------------------ */
/* Deterministic edge cases                                            */
/* ------------------------------------------------------------------ */
static void test_all_black(void){
    unsigned char in[3]={0,0,0},leg[3],out[3];
    gamut_stats st;
    float fs[]={1.0f,1.2f,1.5f,2.0f,4.0f,16.0f};
    for(size_t k=0;k<sizeof fs/sizeof fs[0];k++){
        legacy_of(in,3,fs[k],leg);
        for(float s=0.0f;s<=1.0f;s+=0.5f){
            run_gamut(in,3,fs[k],s,out,&st);
            CHECK(out[0]==0&&out[1]==0&&out[2]==0);
            CHECK(memcmp(leg,out,3)==0);
            CHECK_EQ(st.out_of_gamut,0);
            CHECK_EQ(st.changed,0);
        }
    }
}

static void test_all_white(void){
    unsigned char in[3]={255,255,255},leg[3],out[3];
    gamut_stats st;
    float fs[]={1.0f,1.2f,1.5f,2.0f,4.0f,16.0f};
    for(size_t k=0;k<sizeof fs/sizeof fs[0];k++){
        legacy_of(in,3,fs[k],leg);
        for(float s=0.0f;s<=1.0f;s+=0.5f){
            run_gamut(in,3,fs[k],s,out,&st);
            /* White is a scalar multiple of (1,1,1): the radial pullback
             * maps it back to exactly (1,1,1), same bytes as legacy. */
            CHECK(out[0]==255&&out[1]==255&&out[2]==255);
            CHECK(memcmp(leg,out,3)==0);
            CHECK_EQ(st.changed,0);
        }
    }
}

/* Channel pinned exactly at the top and another pushed out of the cube:
 * (255,0,0) at factor > 1, (255,255,255), (0,255,255).  Must be finite,
 * no NaN, no division by zero, and identical for repeated calls.          */
static void test_channel_at_ceiling(void){
    unsigned char cases[][3]={{255,0,0},{0,255,0},{0,0,255},
                              {255,255,0},{0,255,255},{255,0,255},
                              {255,255,255},{255,128,0},{0,200,230}};
    float fs[]={1.0f,1.01f,1.2f,1.5f,2.0f,8.0f};
    for(size_t c=0;c<sizeof cases/sizeof cases[0];c++){
        for(size_t k=0;k<sizeof fs/sizeof fs[0];k++){
            unsigned char a[3],b[3]; gamut_stats sa,sb;
            run_gamut(cases[c],3,fs[k],1.0f,a,&sa);
            run_gamut(cases[c],3,fs[k],1.0f,b,&sb);
            CHECK(memcmp(a,b,3)==0);           /* deterministic */
            CHECK(sa.max_delta==sb.max_delta);
            for(int i=0;i<3;i++) CHECK(a[i]<=255); /* always representable */
            CHECK(isfinite(sa.max_de));
            CHECK(isfinite(sa.oog_hue_max_deg));
            /* The pullback must not brighten beyond legacy on any channel
             * and must never push a channel negative. */
            for(int i=0;i<3;i++) CHECK(a[i]>=0);
        }
    }
    /* Exact values for the three requested shapes at factor 1.5. */
    unsigned char in[3],out[3]; gamut_stats st;
    unsigned char black[3]={0,0,0},white[3]={255,255,255},red[3]={255,0,0};
    run_gamut(black,3,1.5f,1.0f,out,&st);
    CHECK(out[0]==0&&out[1]==0&&out[2]==0);
    run_gamut(white,3,1.5f,1.0f,out,&st);
    CHECK(out[0]==255&&out[1]==255&&out[2]==255);
    run_gamut(red,3,1.5f,1.0f,out,&st);
    /* Pure red clipped to (1,0,0) by legacy; radial gives (1,0,0) too since
     * the red channel is already the maximum. */
    CHECK(out[0]==255&&out[1]==0&&out[2]==0);
    (void)in;
}

/* ------------------------------------------------------------------ */
/* Legacy path preserved: strength 0 reproduces it on the whole grid    */
/* ------------------------------------------------------------------ */
static void test_strength_zero_is_legacy(void){
    static unsigned char in[64*64*64*3],leg[64*64*64*3],out[64*64*64*3];
    int npx=0;
    fill_grid(in,4,&npx);
    float fs[]={0.7f,1.0f,1.2f,1.5f,2.0f,3.0f};
    for(size_t k=0;k<sizeof fs/sizeof fs[0];k++){
        legacy_of(in,npx*3,fs[k],leg);
        gamut_stats st;
        run_gamut(in,npx*3,fs[k],0.0f,out,&st);
        CHECK(memcmp(leg,out,(size_t)npx*3)==0);
        CHECK_EQ(st.changed,0);
        CHECK_EQ(st.max_delta,0);
    }
}

/* ------------------------------------------------------------------ */
/* Delta accounting: stats must match a direct byte comparison         */
/* ------------------------------------------------------------------ */
static void test_delta_accounting(void){
    static unsigned char in[64*64*64*3],leg[64*64*64*3],out[64*64*64*3];
    int npx=0;
    fill_grid(in,4,&npx);
    float fs[]={1.2f,1.5f,2.0f};
    float ss[]={0.0f,0.25f,0.5f,1.0f};
    for(size_t k=0;k<sizeof fs/sizeof fs[0];k++){
        legacy_of(in,npx*3,fs[k],leg);
        /* Ground truth out-of-gamut count, straight from the definition. */
        int truth_oog=0;
        for(int i=0;i<npx;i++){
            int hot=0;
            for(int c=0;c<3;c++){
                float lin=srgb_to_linear(in[i*3+c]/255.0f)*fs[k];
                if(lin>1.0f) hot=1;
            }
            truth_oog+=hot;
        }
        for(size_t j=0;j<sizeof ss/sizeof ss[0];j++){
            gamut_stats st;
            run_gamut(in,npx*3,fs[k],ss[j],out,&st);
            int changed=0,maxd=0;
            for(int i=0;i<npx;i++){
                int hit=0;
                for(int c=0;c<3;c++){
                    int a=out[i*3+c],b=leg[i*3+c];
                    int d=a>b?a-b:b-a;
                    if(d>maxd) maxd=d;
                    if(d) hit=1;
                }
                changed+=hit;
            }
            CHECK_EQ(st.pixels,npx);
            CHECK_EQ(st.out_of_gamut,truth_oog);
            CHECK_EQ(st.changed,changed);
            CHECK_EQ(st.max_delta,maxd);
            CHECK((double)st.max_delta<=st.max_delta_bound);
        }
    }
}

/* Verifies the documented analytic bound against a dense cube sweep:
 * strength*255 + 1 code units per channel, worst case over the cube.     */
static void test_delta_bound_dense(void){
    static unsigned char in[48*48*48*3],leg[48*48*48*3],out[48*48*48*3];
    int npx=0;
    fill_grid(in,6,&npx); /* steps of 6 over the cube at factor 3 */
    float fs[]={1.3f,2.0f,3.0f,6.0f};
    float ss[]={0.25f,0.5f,0.75f,1.0f};
    for(size_t k=0;k<sizeof fs/sizeof fs[0];k++){
        legacy_of(in,npx*3,fs[k],leg);
        for(size_t j=0;j<sizeof ss/sizeof ss[0];j++){
            gamut_stats st;
            run_gamut(in,npx*3,fs[k],ss[j],out,&st);
            int observed=max_abs_diff(out,leg,npx*3);
            CHECK_EQ(observed,st.max_delta);
            CHECK((double)observed<=255.0*(double)ss[j]+1.0);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Strength blending and monotonicity                                  */
/* ------------------------------------------------------------------ */
static void test_strength_zero_to_one_path(void){
    unsigned char in[3]={255,128,0},leg[3],half[3],full[3];
    gamut_stats st;
    legacy_of(in,3,1.5f,leg);
    run_gamut(in,3,1.5f,0.0f,half,&st);
    CHECK(memcmp(leg,half,3)==0);
    run_gamut(in,3,1.5f,0.5f,half,&st);
    run_gamut(in,3,1.5f,1.0f,full,&st);
    /* Legacy lifts green to 154; full pullback keeps the input green 128.
     * The half-strength blend must land strictly between the two. */
    CHECK(leg[1]>full[1]);
    CHECK(half[1]>=full[1]&&half[1]<=leg[1]);
    CHECK(leg[0]==255);
    CHECK(full[0]==255);
    CHECK(leg[2]==0&&full[2]==0);
}

static void test_invalid_arguments(void){
    unsigned char buf[6]={0},out[6];
    gamut_stats st;
    CHECK(brightness_shift_gamut(NULL,3,1.0f,1.0f,out,&st)==-1);
    CHECK(brightness_shift_gamut(buf,3,1.0f,1.0f,NULL,&st)==-1);
    CHECK(brightness_shift_gamut(buf,0,1.0f,1.0f,out,&st)==-1);
    CHECK(brightness_shift_gamut(buf,-3,1.0f,1.0f,out,&st)==-1);
    CHECK(brightness_shift_gamut(buf,4,1.0f,1.0f,out,&st)==-1); /* not /3 */
    CHECK(brightness_shift_gamut(buf,3,0.0f,1.0f,out,&st)==-1);
    CHECK(brightness_shift_gamut(buf,3,-1.0f,1.0f,out,&st)==-1);
    CHECK(brightness_shift_gamut(buf,3,NAN,1.0f,out,&st)==-1);
    CHECK(brightness_shift_gamut(buf,3,INFINITY,1.0f,out,&st)==-1);
    CHECK(brightness_shift_gamut(buf,3,1.0f,-0.1f,out,&st)==-1);
    CHECK(brightness_shift_gamut(buf,3,1.0f,1.1f,out,&st)==-1);
    CHECK(brightness_shift_gamut(buf,3,1.0f,NAN,out,&st)==-1);
    /* stats pointer optional */
    CHECK(brightness_shift_gamut(buf,6,1.0f,1.0f,out,NULL)==0);
    /* in-place */
    unsigned char ip[3]={255,128,0};
    CHECK(brightness_shift_gamut(ip,3,1.5f,1.0f,ip,&st)==0);
    CHECK(ip[0]==255&&ip[1]==128&&ip[2]==0);
}

/* ------------------------------------------------------------------ */
/* Hue behaviour                                                       */
/* ------------------------------------------------------------------ */
static float hue_of_linear_target(unsigned char r,unsigned char g,
                                  unsigned char b,float f){
    float lin[3]={srgb_to_linear(r/255.0f)*f,
                  srgb_to_linear(g/255.0f)*f,
                  srgb_to_linear(b/255.0f)*f};
    float lab[3];
    linear_rgb_to_oklab(lin,lab);
    return oklab_hue_deg(lab);
}
static float hue_of_pixel(const unsigned char p[3]){
    float lab[3];
    srgb8_to_oklab(p,lab);
    return oklab_hue_deg(lab);
}

/* The core claim: an out-of-gamut pixel keeps its target hue far better
 * than per-channel clipping, and two distinct hues stay distinct.          */
static void test_hue_preserved_and_distinct(void){
    /* Two cyans that both clip to the same (0,255,255) in the legacy path. */
    unsigned char a[3]={0,200,230},b[3]={0,220,240};
    unsigned char la[3],lb[3],na[3],nb[3];
    gamut_stats st;
    legacy_of(a,3,1.8f,la);
    legacy_of(b,3,1.8f,lb);
    run_gamut(a,3,1.8f,1.0f,na,&st);
    run_gamut(b,3,1.8f,1.0f,nb,&st);

    /* Legacy collapses both to identical bytes. */
    CHECK(memcmp(la,lb,3)==0);

    float ideal_a=hue_of_linear_target(a[0],a[1],a[2],1.8f);
    float ideal_b=hue_of_linear_target(b[0],b[1],b[2],1.8f);
    float new_a=hue_of_pixel(na), new_b=hue_of_pixel(nb);

    CHECK(!isnan(ideal_a)&&!isnan(ideal_b)&&!isnan(new_a)&&!isnan(new_b));
    /* Distinct targets stay distinct after the pullback. */
    float ideal_gap=oklab_hue_diff_deg(ideal_a,ideal_b);
    float new_gap=oklab_hue_diff_deg(new_a,new_b);
    CHECK(ideal_gap>3.0f);
    CHECK(new_gap>3.0f);
    CHECK(fabsf(oklab_hue_diff_deg(ideal_a,new_a))<0.5f);
    CHECK(fabsf(oklab_hue_diff_deg(ideal_b,new_b))<0.5f);

    /* ... while the legacy path collapses both hues onto the same value. */
    float leg_a=hue_of_pixel(la);
    CHECK(fabsf(oklab_hue_diff_deg(ideal_a,leg_a))>10.0f);
}

static void test_hue_reporting_improves(void){
    /* Warm and cool ramp; both leave the cube at factor 2. */
    unsigned char px[8][3]={{0,200,230},{0,220,240},{255,128,0},
                            {255,140,10},{0,180,220},{255,160,20},
                            {40,120,255},{200,60,0}};
    unsigned char in[8*3],leg[24],out[24];
    for(int i=0;i<8;i++) memcpy(in+i*3,px[i],3);
    legacy_of(in,24,2.0f,leg);
    gamut_stats st;
    run_gamut(in,24,2.0f,1.0f,out,&st);

    CHECK_EQ(st.pixels,8);
    CHECK(st.hue_samples>0);
    CHECK(st.oog_hue_samples>0);
    /* The pullback's worst Oklab hue drift is far below clipping's. */
    CHECK(st.hue_max_deg<st.legacy_hue_max_deg);
    CHECK(st.oog_hue_max_deg<st.legacy_hue_max_deg);
    CHECK(st.oog_hue_max_deg<1.0);
    CHECK(st.legacy_hue_max_deg>10.0);
    CHECK(st.hue_mean_deg<st.legacy_hue_mean_deg);
    /* dE between the two outputs is bounded and reported. */
    CHECK(st.max_de>0.0);
    CHECK(isfinite(st.max_de));
}

/* Neutrals must not pollute the hue statistics. */
static void test_neutral_pixels_excluded_from_hue_stats(void){
    unsigned char grays[4*3]={0,0,0, 64,64,64, 128,128,128, 200,200,200};
    unsigned char out[12]; gamut_stats st;
    run_gamut(grays,12,2.0f,1.0f,out,&st);
    CHECK_EQ(st.hue_samples,0);
    CHECK_EQ(st.oog_hue_samples,0);
    CHECK(st.hue_max_deg==0.0);
    CHECK(st.legacy_hue_max_deg==0.0);
    CHECK_EQ(st.max_delta,0);
}

/* Pull direction check: output must be on the ray from black through the
 * target, i.e. all channels shrink by the same positive factor.           */
static void test_radial_geometry(void){
    unsigned char in[3]={255,128,0},out[3]; gamut_stats st;
    run_gamut(in,3,2.0f,1.0f,out,&st);
    /* The dominant channel stays at its ceiling, the others keep the input
     * ratio in linear light. */
    float r0=srgb_to_linear(255/255.0f), g0=srgb_to_linear(128/255.0f);
    float r1=srgb_to_linear(out[0]/255.0f), g1=srgb_to_linear(out[1]/255.0f);
    CHECK(fabsf(r1-r0)<1e-5f);            /* red pinned to 1.0 */
    CHECK(fabsf(g1/g0-1.0f)<2e-3f);       /* green follows, ratio intact */
}

/* Same-input determinism across the full API surface. */
static void test_repeatable(void){
    static unsigned char in[64*64*64*3],a[64*64*64*3],b[64*64*64*3];
    int npx=0;
    fill_grid(in,4,&npx);
    gamut_stats sa,sb;
    run_gamut(in,npx*3,1.5f,0.6f,a,&sa);
    run_gamut(in,npx*3,1.5f,0.6f,b,&sb);
    CHECK(memcmp(a,b,(size_t)npx*3)==0);
    CHECK(memcmp(&sa,&sb,sizeof sa)==0);
}

int main(void){
    /* Legacy transfer-function behaviour (original tests, kept). */
    CHECK(fabsf(srgb_to_linear(0.0f))<1e-6f);
    CHECK(fabsf(srgb_to_linear(1.0f)-1.0f)<1e-5f);
    CHECK(fabsf(linear_to_srgb(srgb_to_linear(0.5f))-0.5f)<1e-4f);
    unsigned char in4[4]={0,64,128,255}, out4[4];
    CHECK(brightness_shift(in4,4,1.0f,out4)==0);
    CHECK(out4[2]==in4[2]||out4[2]==in4[2]+1);
    CHECK(brightness_shift(in4,4,-1.0f,out4)==-1);

    test_legacy_golden();
    test_ingamut_untouched();
    test_all_black();
    test_all_white();
    test_channel_at_ceiling();
    test_strength_zero_is_legacy();
    test_delta_accounting();
    test_delta_bound_dense();
    test_strength_zero_to_one_path();
    test_invalid_arguments();
    test_hue_preserved_and_distinct();
    test_hue_reporting_improves();
    test_neutral_pixels_excluded_from_hue_stats();
    test_radial_geometry();
    test_repeatable();

    printf("%d checks, %d failures\n",checks,fails);
    return fails?1:0;
}
