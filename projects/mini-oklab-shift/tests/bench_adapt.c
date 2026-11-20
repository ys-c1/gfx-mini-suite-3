/*
 * bench_adapt —— 三种光线（2700K 暖光 / 6500K 日光 / 9000K 冷光）下的
 * 参考白色适应基准：
 *   1. CCT -> xy（Planckian / CIE daylight 近似）-> XYZ -> 线性 sRGB，
 *      归一化后编码为 8bit 参考白，并与 cfg 目录各 .cfg 中配置的参考白交叉校验；
 *   2. 用 24 色 ColorChecker 经典 sRGB 参考值，按光源对角矩阵合成偏色照片；
 *   3. 报告适应前后的 ΔE76、Lab 色相偏移、中性灰通道偏差与耗时。
 * 用法：bench_adapt <project_dir>
 */
#define _POSIX_C_SOURCE 199309L
#include "../src/adapt.h"
#include "../src/oklab.h"
#include "../src/config.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <time.h>

/* ---------------- 颜色科学工具 ---------------- */

static const float M_S2X[3][3] = {
    {0.4123908f,0.3575843f,0.1804808f},
    {0.2126390f,0.7151687f,0.0721923f},
    {0.0193308f,0.1191948f,0.9505322f}};
static const float M_X2S[3][3] = {
    { 3.24096994f,-1.53738318f,-0.49861076f},
    {-0.96924364f, 1.87596750f, 0.04155506f},
    { 0.05563008f,-0.20397696f, 1.05697151f}};

static void mv(const float m[3][3], const float v[3], float o[3]) {
    for (int i=0;i<3;++i) o[i]=m[i][0]*v[0]+m[i][1]*v[1]+m[i][2]*v[2];
}
static void srgb8_to_lab(const unsigned char s[3], float lab[3]) {
    float lin[3], xyz[3];
    for (int i=0;i<3;++i) lin[i]=srgb_to_linear(s[i]/255.0f);
    mv(M_S2X, lin, xyz);
    static const float w[3]={0.95047f,1.0f,1.08883f};
    float f[3];
    for (int i=0;i<3;++i) {
        float t=xyz[i]/w[i];
        f[i] = t>0.008856f ? cbrtf(t) : (7.787f*t+16.0f/116.0f);
    }
    lab[0]=116.0f*f[1]-16.0f; lab[1]=500.0f*(f[0]-f[1]); lab[2]=200.0f*(f[1]-f[2]);
}
static float de76(const float a[3], const float b[3]) {
    float d0=a[0]-b[0],d1=a[1]-b[1],d2=a[2]-b[2];
    return sqrtf(d0*d0+d1*d1+d2*d2);
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

/* CCT -> 色度坐标 xy（Wikipedia: Color temperature 近似公式） */
static void cct_to_xy(float T, float xy[2]) {
    if (T >= 4000.0f && T <= 25000.0f) {
        float t=T/1000.0f;
        float x = ((-4.6070f/(t*t*t))+(2.9678f/(t*t))+(0.09911f/t)+0.244063f);
        float y = -3.000f*x*x+2.870f*x-0.275f;
        xy[0]=x; xy[1]=y;
    } else if (T >= 1667.0f && T < 4000.0f) {
        float x = ((-0.2661239f*1e9f/(T*T*T))-(0.2343589f*1e6f/(T*T))
                   +(0.8776956f*1e3f/T)+0.179910f);
        if (T <= 2222.0f)
            xy[1] = -1.1063814f*x*x*x-1.34811020f*x*x+2.18555832f*x-0.20219683f;
        else
            xy[1] = -0.9549476f*x*x*x-1.37418593f*x*x+2.09137015f*x-0.16748867f;
        xy[0]=x;
    } else {
        xy[0]=xy[1]=-1.0f;
    }
}

/* 由 CCT 计算 8bit sRGB 参考白（最大通道归一到 255，模拟曝光定标） */
static void cct_ref_white(float T, unsigned char ref[3], float ref_lin[3]) {
    float xy[2]; cct_to_xy(T, xy);
    float xyz[3]={xy[0]/xy[1], 1.0f, (1.0f-xy[0]-xy[1])/xy[1]};
    float lin[3]; mv(M_X2S, xyz, lin);
    float mx=fmaxf(fmaxf(lin[0],lin[1]),lin[2]);
    for (int i=0;i<3;++i) {
        lin[i]/=mx;
        ref_lin[i]=lin[i];
        float v=linear_to_srgb(lin[i])*255.0f+0.5f;
        ref[i]=(unsigned char)fminf(255.0f,fmaxf(0.0f,v));
    }
}

/* ---------------- 24 色 ColorChecker（经典 sRGB 0..255 参考值） ---------------- */
static const unsigned char CHART[24][3] = {
    {115, 82, 68},{194,150,130},{ 98,122,157},{ 87,108, 67},{133,128,177},{103,189,170},
    {214,126, 44},{ 80, 91,166},{193, 90, 99},{ 94, 60,108},{157,188, 64},{224,163, 46},
    { 56, 61,150},{ 70,148, 73},{175, 54, 60},{231,199, 31},{187, 86,149},{  8,133,161},
    {243,243,242},{200,200,200},{160,160,160},{122,122,121},{ 85, 85, 85},{ 52, 52, 52},
};
static const char* NAMES[24] = {
    "dark-skin","light-skin","blue-sky","foliage","blue-flower","bluish-green",
    "orange","purplish-blue","moderate-red","purple","yellow-green","orange-yellow",
    "blue","green","red","yellow","magenta","cyan",
    "white 9.5","neutral 8","neutral 6.5","neutral 5","neutral 3.5","neutral 2",
};
#define GRAY_FIRST 18  /* 18..23 为 6 个中性灰/白色块 */

#define NPIX 100000
#define ITERS 20

typedef struct {
    const char* name;
    float cct;
    const char* cfg;
} light_t;

int main(int argc, char** argv) {
    if (argc < 2) { printf("usage: bench_adapt <project_dir>\n"); return 2; }
    const char* root = argv[1];
    light_t lights[3] = {
        {"warm_2700k", 2700.0f, "cfg/warm_2700k.cfg"},
        {"day_6500k",  6500.0f, "cfg/day_6500k.cfg"},
        {"cool_9000k", 9000.0f, "cfg/cool_9000k.cfg"},
    };

    char path[512], csv_path[512], report_path[512];
    snprintf(csv_path, sizeof(csv_path), "%s/out/color_diff.csv", root);
    snprintf(report_path, sizeof(report_path), "%s/out/bench_report.txt", root);
    FILE* csv = fopen(csv_path, "wb");
    FILE* rep = fopen(report_path, "wb");
    if (!csv || !rep) { printf("cannot open output files\n"); return 3; }
    fprintf(csv, "light,patch,r_in,g_in,b_in,r_cast,g_cast,b_cast,r_adapt,g_adapt,b_adapt,dE_cast,dE_adapt,hue_shift_deg,gray_dev\n");

    unsigned char* img = malloc((size_t)NPIX*3);
    unsigned char* out = malloc((size_t)NPIX*3);
    if (!img || !out) { printf("oom\n"); return 4; }

    int bound_violations = 0;
    printf("== chromatic adaptation bench ==\n");
    printf("%-10s %7s | %14s %14s %12s %10s %10s %10s\n",
           "light","CCT(K)","dE76 mean/max","hue deg max","gray dev max",
           "cfg drift","us/iter","us/pixel");
    for (int li=0; li<3; ++li) {
        light_t* L=&lights[li];
        unsigned char calc_ref[3], cfg_ref[3];
        float ref_lin[3];
        cct_ref_white(L->cct, calc_ref, ref_lin);

        /* 与配置文件交叉校验 */
        snprintf(path, sizeof(path), "%s/%s", root, L->cfg);
        char cbuf[512];
        int drift = -1;
        if (config_load(path, cbuf, sizeof(cbuf)) >= 0 &&
            config_get_rgb(cbuf, "ref_white", cfg_ref) == 0) {
            drift = 0;
            for (int i=0;i<3;++i) {
                int d=(int)cfg_ref[i]-(int)calc_ref[i]; if(d<0)d=-d;
                if (d>drift) drift=d;
            }
        }
        double strength = config_get_f(cbuf, "strength", 1.0);

        adapt_ctx_t ctx;
        int rc = adapt_init8(&ctx, cfg_ref, (float)strength);
        if (rc != ADAPT_OK) { printf("%s adapt init failed rc=%d\n", L->name, rc); return 5; }

        float sum_cast=0,sum_adapt=0,max_cast=0,max_adapt=0,max_hue=0,max_gray=0;
        for (int p=0; p<24; ++p) {
            /* 合成偏色照片：线性域乘光源对角矩阵（参考白即光源的线性 RGB） */
            unsigned char cast[3], adapted[3];
            for (int c=0;c<3;++c) {
                float v=srgb_to_linear(CHART[p][c]/255.0f)*ref_lin[c];
                if (v>1.0f) v=1.0f;
                cast[c]=(unsigned char)(linear_to_srgb(v)*255.0f+0.5f);
            }
            adapt_apply8(&ctx, cast, 1, adapted);

            float lab_ref[3],lab_cast[3],lab_ad[3];
            srgb8_to_lab(CHART[p], lab_ref);
            srgb8_to_lab(cast, lab_cast);
            srgb8_to_lab(adapted, lab_ad);
            float de_c=de76(lab_cast,lab_ref), de_a=de76(lab_ad,lab_ref);
            /* 色相上界以“相对真实色卡”的偏移衡量 */
            float hd_ref = (lab_ref[1]*lab_ref[1]+lab_ref[2]*lab_ref[2]>4.0f)
                       ? hue_delta(lab_hue(lab_ref),lab_hue(lab_ad)) : 0.0f;
            if (hd_ref > max_hue) max_hue = hd_ref;
            if (hd_ref > ADAPT_HUE_BOUND_DEG) ++bound_violations;
            int gray_dev=0;
            if (p>=GRAY_FIRST) {
                int a=adapted[0],b=adapted[1],cc=adapted[2];
                int hi=a>b?(a>cc?a:cc):(b>cc?b:cc);
                int lo=a<b?(a<cc?a:cc):(b<cc?b:cc);
                gray_dev=hi-lo;
                if ((float)gray_dev>max_gray) max_gray=(float)gray_dev;
            }
            sum_cast+=de_c; sum_adapt+=de_a;
            if (de_c>max_cast)max_cast=de_c;
            if (de_a>max_adapt)max_adapt=de_a;
            fprintf(csv,"%s,%s,%d,%d,%d,%d,%d,%d,%d,%d,%d,%.3f,%.3f,%.3f,%d\n",
                    L->name,NAMES[p],
                    CHART[p][0],CHART[p][1],CHART[p][2],
                    cast[0],cast[1],cast[2],adapted[0],adapted[1],adapted[2],
                    de_c,de_a,hd_ref,gray_dev);
        }

        /* 耗时：NPIX 像素图像，ITERS 次 */
        for (int i=0;i<NPIX;++i) { img[3*i]=CHART[i%24][0];img[3*i+1]=CHART[i%24][1];img[3*i+2]=CHART[i%24][2]; }
        struct timespec t0,t1;
        clock_gettime(CLOCK_MONOTONIC,&t0);
        for (int it=0;it<ITERS;++it) adapt_apply8(&ctx,img,NPIX,out);
        clock_gettime(CLOCK_MONOTONIC,&t1);
        double us=(t1.tv_sec-t0.tv_sec)*1e6+(t1.tv_nsec-t0.tv_nsec)/1e3;
        double us_iter=us/ITERS, us_px=us_iter/NPIX;

        printf("%-10s %7.0f | %7.2f / %5.2f      %7.2f       %8.0f     %8d   %8.1f   %.4f\n",
               L->name,L->cct,sum_adapt/24.0,max_adapt,max_hue,max_gray,
               drift,us_iter,us_px);
        fprintf(rep,"light=%s cct=%.0f ref_calc=%d,%d,%d ref_cfg=%d,%d,%d drift=%d "
                    "dE_cast_mean=%.2f dE_adapt_mean=%.2f dE_adapt_max=%.2f "
                    "hue_shift_max=%.2f gray_dev_max=%.0f us_per_iter=%.1f us_per_pixel=%.4f\n",
                L->name,L->cct,calc_ref[0],calc_ref[1],calc_ref[2],
                cfg_ref[0],cfg_ref[1],cfg_ref[2],drift,
                sum_cast/24.0,sum_adapt/24.0,max_adapt,max_hue,max_gray,us_iter,us_px);
    }
    printf("hue bound = %.1f deg, violations = %d\n", ADAPT_HUE_BOUND_DEG, bound_violations);
    printf("wrote %s and %s\n", csv_path, report_path);
    fclose(csv); fclose(rep);
    free(img); free(out);
    return bound_violations ? 1 : 0;
}
