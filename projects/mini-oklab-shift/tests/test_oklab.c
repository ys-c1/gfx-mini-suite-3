#include "../src/oklab.h"
#include "../src/adapt.h"
#include <stdio.h>
#include <math.h>
static int fails=0,checks=0;
#define CHECK(c) do{++checks; if(!(c)){++fails;printf("FAIL %d %s\n",__LINE__,#c);}}while(0)

/* 线性 sRGB -> XYZ(D65)，用于测试构造光源 */
static void lin_to_xyz(const float lin[3], float xyz[3]) {
    static const float M[3][3] = {
        {0.4123908f,0.3575843f,0.1804808f},
        {0.2126390f,0.7151687f,0.0721923f},
        {0.0193308f,0.1191948f,0.9505322f}};
    for (int i=0;i<3;++i)
        xyz[i]=M[i][0]*lin[0]+M[i][1]*lin[1]+M[i][2]*lin[2];
}
/* 线性 sRGB -> Lab（参考白 D65） */
static void lin_to_lab(const float lin[3], float lab[3]) {
    float xyz[3]; lin_to_xyz(lin, xyz);
    static const float w[3]={0.95047f,1.0f,1.08883f};
    float f[3];
    for (int i=0;i<3;++i) {
        float t=xyz[i]/w[i];
        f[i] = t>0.008856f ? cbrtf(t) : (7.787f*t+16.0f/116.0f);
    }
    lab[0]=116.0f*f[1]-16.0f; lab[1]=500.0f*(f[0]-f[1]); lab[2]=200.0f*(f[1]-f[2]);
}
static float lab_hue(const float lab[3]) {
    if (fabsf(lab[1])<1e-6f && fabsf(lab[2])<1e-6f) return 0.0f;
    float h=atan2f(lab[2],lab[1])*180.0f/3.14159265358979f;
    return h<0.0f ? h+360.0f : h;
}
static float hue_delta(float h1, float h2) {
    float d=fabsf(h1-h2);
    return d>180.0f ? 360.0f-d : d;
}

/* 暖光/冷光参考白，直接给线性光 RGB（不等比） */
static const float WARM_LIN[3]={0.95f,0.62f,0.30f};
static const float REF_W[2][3] = {{0.95f,0.62f,0.30f},{0.55f,0.78f,1.00f}};

int main(void){
    /* ---- 既有亮度路径回归（不允许行为变化） ---- */
    CHECK(fabsf(srgb_to_linear(0.0f))<1e-6f);
    CHECK(fabsf(srgb_to_linear(1.0f)-1.0f)<1e-5f);
    CHECK(fabsf(linear_to_srgb(srgb_to_linear(0.5f))-0.5f)<1e-4f);
    unsigned char in[4]={0,64,128,255}, out[4];
    CHECK(brightness_shift(in,4,1.0f,out)==0);
    CHECK(out[2]==in[2]||out[2]==in[2]+1);
    CHECK(brightness_shift(in,4,-1.0f,out)==-1);

    /* ---- 参考白非法必须拒绝 ---- */
    adapt_ctx_t ctx;
    unsigned char zero[3]={0,0,0};
    CHECK(adapt_init8(&ctx,zero,1.0f)==ADAPT_E_WHITE);
    CHECK(adapt_init8(NULL,zero,1.0f)==ADAPT_E_ARG);
    float bad_ref[3]={0.5f,1.2f,0.1f};
    CHECK(adapt_init_f(&ctx,bad_ref,1.0f)==ADAPT_E_WHITE);
    float neg_ref[3]={-0.1f,0.5f,0.5f};
    CHECK(adapt_init_f(&ctx,neg_ref,1.0f)==ADAPT_E_WHITE);
    float bad_lin[3]={0.5f,-0.2f,0.1f};
    CHECK(adapt_init_linear(&ctx,bad_lin,1.0f)==ADAPT_E_WHITE);
    float zero_lin[3]={0.0f,0.0f,0.0f};
    CHECK(adapt_init_linear(&ctx,zero_lin,1.0f)==ADAPT_E_WHITE);
    CHECK(adapt_init_linear(&ctx,WARM_LIN,-0.01f)==ADAPT_E_STRENGTH);
    CHECK(adapt_init_linear(&ctx,WARM_LIN,1.01f)==ADAPT_E_STRENGTH);
    CHECK(adapt_apply8(&ctx,NULL,1,out)==ADAPT_E_ARG);
    CHECK(adapt_apply8(&ctx,in,0,out)==ADAPT_E_ARG);

    /* ---- 强度为零：逐值原样还原 ---- */
    CHECK(adapt_init_linear(&ctx,WARM_LIN,0.0f)==ADAPT_OK);
    unsigned char patch[6]={12,99,201,255,0,128}, got[6];
    CHECK(adapt_apply8(&ctx,patch,2,got)==ADAPT_OK);
    for (int i=0;i<6;++i) CHECK(got[i]==patch[i]);
    float lin_in[3]={0.31f,0.02f,0.77f}, lin_out[3];
    CHECK(adapt_apply_linear(&ctx,lin_in,lin_out)==ADAPT_OK);
    for (int i=0;i<3;++i) CHECK(lin_out[i]==lin_in[i]);

    /* ---- 中性灰必须回到中性 ----
     * 物理模型：反射率 rho 的灰卡在色光 W 下成像为 (rho*Wr,rho*Wg,rho*Wb)，
     * 适应后必须回到等比 (rho,rho,rho)。浮点域严格相等，8bit 域仅量化误差。 */
    static const float grays[]={0.05f,0.18f,0.35f,0.5f,0.72f,0.93f};
    for (int k=0;k<2;++k) {
        CHECK(adapt_init_linear(&ctx,REF_W[k],1.0f)==ADAPT_OK);
        for (unsigned gi=0; gi<sizeof(grays)/sizeof(grays[0]); ++gi) {
            float rho=grays[gi], g_in[3], g_lin_out[3];
            for (int c=0;c<3;++c) g_in[c]=rho*REF_W[k][c]; /* 色光下的灰卡 */
            CHECK(adapt_apply_linear(&ctx,g_in,g_lin_out)==ADAPT_OK);
            /* 对角缩放：三通道输出严格相等，且还原为反射率 rho */
            CHECK(fabsf(g_lin_out[0]-g_lin_out[1])<1e-6f);
            CHECK(fabsf(g_lin_out[1]-g_lin_out[2])<1e-6f);
            CHECK(fabsf(g_lin_out[0]-rho)<1e-6f);
            /* 8bit：逐通道编码偏色灰卡，适应后通道极差 <=2 LSB */
            unsigned char g8[3], g8out[3];
            for (int c=0;c<3;++c) {
                float v=linear_to_srgb(g_in[c])*255.0f+0.5f;
                g8[c]=(unsigned char)fminf(255.0f,fmaxf(0.0f,v));
            }
            CHECK(adapt_apply8(&ctx,g8,1,g8out)==ADAPT_OK);
            int span=(int)g8out[0]-(int)g8out[1]; if(span<0)span=-span;
            int span2=(int)g8out[1]-(int)g8out[2]; if(span2<0)span2=-span2;
            if (span2>span) span=span2;
            CHECK(span<=2);
        }
    }

    /* ---- 色卡色相偏移上界：样色先经光源对角矩阵加偏，再适应，与原色比较 ---- */
    static const float samples_lin[][3] = {
        {0.60f,0.03f,0.02f}, {0.85f,0.28f,0.02f}, {0.90f,0.78f,0.05f},
        {0.04f,0.38f,0.04f}, {0.02f,0.22f,0.70f}, {0.18f,0.05f,0.45f},
        {0.70f,0.18f,0.33f}, {0.05f,0.52f,0.45f}, {0.33f,0.33f,0.05f},
        {0.09f,0.09f,0.09f}, {0.005f,0.005f,0.005f}, {0.78f,0.78f,0.78f},
    };
    for (int k=0;k<2;++k) {
        CHECK(adapt_init_linear(&ctx,REF_W[k],1.0f)==ADAPT_OK);
        float worst=0.0f;
        for (unsigned s=0;s<sizeof(samples_lin)/sizeof(samples_lin[0]);++s) {
            float cast[3], lo[3];
            for (int c=0;c<3;++c) cast[c]=samples_lin[s][c]*REF_W[k][c];
            adapt_apply_linear(&ctx,cast,lo);
            float lab1[3],lab2[3];
            lin_to_lab(samples_lin[s],lab1);
            lin_to_lab(lo,lab2);
            if (lab1[1]*lab1[1]+lab1[2]*lab1[2] > 4.0f) { /* 跳过近中性 */
                float d=hue_delta(lab_hue(lab1),lab_hue(lab2));
                if (d>worst) worst=d;
            }
        }
        CHECK(worst<=ADAPT_HUE_BOUND_DEG);
    }

    /* ---- 强度插值：同一输入在 0 与 1 之间逐通道单调，参考白本身映射到 D65 白 ---- */
    adapt_init_linear(&ctx,WARM_LIN,0.0f);
    float a0[3]; adapt_apply_linear(&ctx,lin_in,a0);
    adapt_init_linear(&ctx,WARM_LIN,1.0f);
    float a1[3]; adapt_apply_linear(&ctx,lin_in,a1);
    /* 全强度下，参考白各通道均映射为 1.0（等比 D65 白） */
    float w_out[3]; adapt_apply_linear(&ctx,WARM_LIN,w_out);
    CHECK(fabsf(w_out[0]-1.0f)<1e-6f && fabsf(w_out[1]-1.0f)<1e-6f && fabsf(w_out[2]-1.0f)<1e-6f);
    adapt_init_linear(&ctx,WARM_LIN,0.5f);
    float ah[3]; adapt_apply_linear(&ctx,lin_in,ah);
    for (int i=0;i<3;++i) {
        float lo=fminf(a0[i],a1[i])-1e-6f, hi=fmaxf(a0[i],a1[i])+1e-6f;
        CHECK(ah[i]>=lo && ah[i]<=hi);
    }

    /* ---- sRGB 编码域初始化与 8bit 往返：D65 白参考下近似恒等 ---- */
    float d65_f[3]={1.0f,1.0f,1.0f};
    CHECK(adapt_init_f(&ctx,d65_f,1.0f)==ADAPT_OK);
    CHECK(adapt_apply8(&ctx,patch,2,got)==ADAPT_OK);
    for (int i=0;i<6;++i) { int d=(int)got[i]-(int)patch[i]; if(d<0)d=-d; CHECK(d<=1); }
    unsigned char d65[3]={255,255,255};
    CHECK(adapt_init8(&ctx,d65,1.0f)==ADAPT_OK);
    CHECK(adapt_apply8(&ctx,patch,2,got)==ADAPT_OK);
    for (int i=0;i<6;++i) { int d=(int)got[i]-(int)patch[i]; if(d<0)d=-d; CHECK(d<=1); }

    printf("%d checks, %d failures\n",checks,fails);
    return fails?1:0;
}
