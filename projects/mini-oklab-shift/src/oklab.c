#include "oklab.h"
#include <math.h>
#include <string.h>

/* ================= legacy sRGB helpers and entry (unchanged) ================= */

float srgb_to_linear(float v){
    if(v<=0.04045f) return v/12.92f;
    return powf((v+0.055f)/1.055f,2.4f);
}
float linear_to_srgb(float v){
    if(v<=0.0031308f) return v*12.92f;
    return 1.055f*powf(v,1.0f/2.4f)-0.055f;
}
int brightness_shift(const unsigned char* rgb,int n,float factor,unsigned char* out){
    if(!rgb||!out||n<=0||factor<=0.0f) return -1;
    for(int i=0;i<n;++i){
        float v=linear_to_srgb(srgb_to_linear(rgb[i]/255.0f)*factor);
        v=v*255.0f+0.5f;
        out[i]=(unsigned char)(v<0.0f?0.0f:(v>255.0f?255.0f:v));
    }
    return 0;
}

/* ================= Oklab / Oklch decoupled pipeline ================= */

static float clampf(float v, float lo, float hi){
    return v < lo ? lo : (v > hi ? hi : v);
}

/* sRGB (0..255) -> linear sRGB -> Oklab (Bjorn Ottosson matrices) */
Oklab srgb_to_oklab(unsigned char r, unsigned char g, unsigned char b){
    float lr = srgb_to_linear(r / 255.0f);
    float lg = srgb_to_linear(g / 255.0f);
    float lb = srgb_to_linear(b / 255.0f);
    float l = cbrtf(0.4122214708f*lr + 0.5363325363f*lg + 0.0514459929f*lb);
    float m = cbrtf(0.2119034982f*lr + 0.6806995451f*lg + 0.1073969566f*lb);
    float s = cbrtf(0.0883024619f*lr + 0.2817188376f*lg + 0.6299787005f*lb);
    Oklab o;
    o.L = 0.2104542553f*l + 0.7936177850f*m - 0.0040720468f*s;
    o.a = 1.9779984951f*l - 2.4285922050f*m + 0.4505937099f*s;
    o.b = 0.0259040371f*l + 0.7827717662f*m - 0.8086757660f*s;
    return o;
}

/* Oklab -> linear sRGB -> sRGB (0..255), gamut-clamped in linear light */
void oklab_to_srgb(Oklab c, unsigned char rgb[3]){
    float l = c.L + 0.3963377774f*c.a + 0.2158037573f*c.b;
    float m = c.L - 0.1055613458f*c.a - 0.0638541728f*c.b;
    float s = c.L - 0.0894841775f*c.a - 1.2914855480f*c.b;
    float l3 = l*l*l, m3 = m*m*m, s3 = s*s*s;
    float lr =  4.0767416621f*l3 - 3.3077115913f*m3 + 0.2309699292f*s3;
    float lg = -1.2684380046f*l3 + 2.6097574011f*m3 - 0.3413193965f*s3;
    float lb = -0.0041960863f*l3 - 0.7034186147f*m3 + 1.7076147010f*s3;
    lr = clampf(lr, 0.0f, 1.0f);
    lg = clampf(lg, 0.0f, 1.0f);
    lb = clampf(lb, 0.0f, 1.0f);
    float v;
    v = clampf(linear_to_srgb(lr), 0.0f, 1.0f); rgb[0] = (unsigned char)(v*255.0f + 0.5f);
    v = clampf(linear_to_srgb(lg), 0.0f, 1.0f); rgb[1] = (unsigned char)(v*255.0f + 0.5f);
    v = clampf(linear_to_srgb(lb), 0.0f, 1.0f); rgb[2] = (unsigned char)(v*255.0f + 0.5f);
}

Oklch oklab_to_lch(Oklab c){
    Oklch p;
    p.L = c.L;
    p.C = sqrtf(c.a*c.a + c.b*c.b);
    p.h = p.C > 1e-8f ? atan2f(c.b, c.a) : 0.0f;
    return p;
}

Oklab lch_to_oklab(Oklch p){
    Oklab c;
    c.L = p.L;
    c.a = p.C * cosf(p.h);
    c.b = p.C * sinf(p.h);
    return c;
}

int tone_chroma_adjust(const unsigned char* rgb, int n, float dL, float sat,
                       unsigned char* out, float* resid_dL, float* resid_dC){
    if(!rgb || !out || n <= 0 || (n % 3) != 0) return -1;

    /* Zero parameters: byte-exact restoration, no math on the pixels. */
    if(dL == 0.0f && sat == 0.0f){
        memcpy(out, rgb, (size_t)n);
        if(resid_dL) *resid_dL = 0.0f;
        if(resid_dC) *resid_dC = 0.0f;
        return 0;
    }

    /* Saturation is a relative chroma change: C' = (1 + sat) * C. */
    float cscale = 1.0f + sat;
    if(cscale < 0.0f) cscale = 0.0f;

    int pixels = n / 3;
    double sum_dL = 0.0, sum_dC = 0.0;

    for(int i = 0; i < pixels; ++i){
        const unsigned char* in = rgb + 3*i;
        unsigned char* o = out + 3*i;

        Oklch p = oklab_to_lch(srgb_to_oklab(in[0], in[1], in[2]));

        Oklch q;
        q.L = clampf(p.L + dL, 0.0f, 1.0f);   /* channel 1: lightness only */
        q.C = p.C * cscale;                   /* channel 2: chroma only     */
        q.h = p.h;                            /* hue is shared, untouched   */

        oklab_to_srgb(lch_to_oklab(q), o);

        /* Residuals measured after 8-bit quantization of the output. */
        Oklch r = oklab_to_lch(srgb_to_oklab(o[0], o[1], o[2]));
        sum_dL += fabsf(r.L - p.L);
        sum_dC += fabsf(r.C - p.C);
    }

    if(resid_dL) *resid_dL = (float)(sum_dL / pixels);
    if(resid_dC) *resid_dC = (float)(sum_dC / pixels);
    return 0;
}
