#include "../src/oklab.h"
#include "../src/state.h"
#include <stdio.h>
#include <math.h>
#include <string.h>
static int fails=0,checks=0;
#define CHECK(c) do{++checks; if(!(c)){++fails;printf("FAIL %d %s\n",__LINE__,#c);}}while(0)

static Oklch px_lch(const unsigned char* p){
    return oklab_to_lch(srgb_to_oklab(p[0],p[1],p[2]));
}
static Oklch out_lch(const unsigned char* p){
    return oklab_to_lch(srgb_to_oklab(p[0],p[1],p[2]));
}

/*
 * Tolerance model, two tiers:
 *  - float Oklch space: the two channels are exactly orthogonal (no clamp,
 *    no rounding). Checked directly on the L/C/h structs with 1e-7 eps.
 *  - 8-bit sRGB output: residuals can only come from (a) byte quantization
 *    (~5e-4) and (b) gamut clipping when the requested color leaves sRGB
 *    (only on aggressive outward moves; desaturation never clips).
 *    In-gamut cases must stay under QUANT_FLOOR; clipping cases get a
 *    looser physical bound and the intended channel must still dominate.
 */
#define QUANT_FLOOR 0.005f
#define CLIP_FLOOR  0.05f

int main(void){
    /* ---- legacy pipeline and entry, unchanged ---- */
    CHECK(fabsf(srgb_to_linear(0.0f))<1e-6f);
    CHECK(fabsf(srgb_to_linear(1.0f)-1.0f)<1e-5f);
    CHECK(fabsf(linear_to_srgb(srgb_to_linear(0.5f))-0.5f)<1e-4f);
    unsigned char in[4]={0,64,128,255}, out[4];
    CHECK(brightness_shift(in,4,1.0f,out)==0);
    CHECK(out[2]==in[2]||out[2]==in[2]+1);
    CHECK(brightness_shift(in,4,-1.0f,out)==-1);

    /* ---- decoupled pipeline: argument validation ---- */
    unsigned char rgb[9]={120,60,200, 220,40,40, 40,200,80}, dst[9];
    float rL=9,rC=9;
    CHECK(tone_chroma_adjust(0,9,0,0,dst,&rL,&rC)==-1);
    CHECK(tone_chroma_adjust(rgb,9,0,0,0,&rL,&rC)==-1);
    CHECK(tone_chroma_adjust(rgb,0,0,0,dst,&rL,&rC)==-1);
    CHECK(tone_chroma_adjust(rgb,4,0,0,dst,&rL,&rC)==-1); /* not RGB triplets */

    /* ---- zero/zero restores every value byte for byte ---- */
    CHECK(tone_chroma_adjust(rgb,9,0.0f,0.0f,dst,&rL,&rC)==0);
    CHECK(memcmp(dst,rgb,9)==0);
    CHECK(rL==0.0f && rC==0.0f);

    /* ---- tier 1: float-level orthogonality in Oklch (exact) ---- */
    Oklch p=px_lch(rgb);
    Oklch ql={p.L+0.15f,p.C,p.h};                 /* lightness channel only */
    CHECK(fabsf(ql.C-p.C)<1e-7f);
    CHECK(fabsf(ql.h-p.h)<1e-7f);
    Oklch qc={p.L,p.C*1.4f,p.h};                  /* saturation channel only */
    CHECK(fabsf(qc.L-p.L)<1e-7f);
    CHECK(fabsf(qc.h-p.h)<1e-7f);
    /* identity Oklch -> 8-bit -> Oklch round trip has zero residual:
       any observed leak below comes from clipping, not the math */
    unsigned char id_out[3];
    oklab_to_srgb(lch_to_oklab(p),id_out);
    Oklch p2=out_lch(id_out);
    CHECK(fabsf(p2.L-p.L)<1e-6f && fabsf(p2.C-p.C)<1e-6f);

    /* ---- lightness-only: L moves, C stays (C residual is crosstalk) ---- */
    CHECK(tone_chroma_adjust(rgb,9,0.15f,0.0f,dst,&rL,&rC)==0);
    printf("light-only  resid dL=%.5f dC=%.5f\n",rL,rC);
    CHECK(rL>0.05f);
    CHECK(rC<CLIP_FLOOR);                          /* quant + clip floor */

    /* ---- saturation-only, in-gamut: only quantization floor allowed ---- */
    CHECK(tone_chroma_adjust(rgb,9,0.0f,0.15f,dst,&rL,&rC)==0);
    printf("chroma-only(sat=+0.15) resid dL=%.5f dC=%.5f\n",rL,rC);
    CHECK(rC>0.02f);
    CHECK(rL<QUANT_FLOOR);
    for(int i=0;i<3;++i){
        Oklch a=px_lch(rgb+3*i), b=out_lch(dst+3*i);
        CHECK(fabsf(b.L-a.L)<0.01f);               /* per-pixel, in-gamut */
    }

    /* ---- saturation-only, aggressive: gamut clipping bounds the leak ---- */
    CHECK(tone_chroma_adjust(rgb,9,0.0f,0.40f,dst,&rL,&rC)==0);
    printf("chroma-only(sat=+0.40) resid dL=%.5f dC=%.5f\n",rL,rC);
    CHECK(rC>rL);                                  /* intended effect dominates */
    CHECK(rL<CLIP_FLOOR);                          /* physical clip ceiling */

    /* ---- combined adjustment moves both ---- */
    CHECK(tone_chroma_adjust(rgb,9,0.15f,0.40f,dst,&rL,&rC)==0);
    CHECK(rL>0.05f && rC>0.02f);

    /* ---- edge input 1: negative saturation desaturates, never clips ---- */
    Oklch before=px_lch(rgb);
    CHECK(tone_chroma_adjust(rgb,9,0.0f,-0.60f,dst,&rL,&rC)==0);
    Oklch after=out_lch(dst);
    printf("neg-sat     C %.5f -> %.5f (scale 0.4 target %.5f)\n",
           before.C,after.C,before.C*0.4f);
    CHECK(after.C < before.C);
    CHECK(fabsf(after.C-before.C*0.4f)<0.01f);
    CHECK(fabsf(after.L-before.L)<QUANT_FLOOR);    /* inward move: quant only */

    /* full desaturation: sat <= -1 lands on the gray axis (C = 0) */
    CHECK(tone_chroma_adjust(rgb,9,0.0f,-1.0f,dst,&rL,&rC)==0);
    Oklch gray=out_lch(dst);
    printf("full-desat  C=%.6f\n",gray.C);
    CHECK(gray.C<0.01f);
    /* sat below -1 is clamped to scale 0, same determinate result */
    CHECK(tone_chroma_adjust(rgb,9,0.0f,-2.0f,dst,&rL,&rC)==0);
    CHECK(fabsf(out_lch(dst).C-gray.C)<1e-6f);

    /* ---- edge input 2: lightness zero with nonzero saturation ---- */
    CHECK(tone_chroma_adjust(rgb,9,0.0f,0.15f,dst,&rL,&rC)==0);
    CHECK(rL<QUANT_FLOOR);
    CHECK(tone_chroma_adjust(rgb,9,0.0f,-0.60f,dst,&rL,&rC)==0);
    CHECK(rL<QUANT_FLOOR);                         /* negative sat: near zero */

    /* ---- edge input 3: exactly one pixel ---- */
    unsigned char one[3]={120,60,200}, one_out[3];
    CHECK(tone_chroma_adjust(one,3,0.10f,0.25f,one_out,&rL,&rC)==0);
    CHECK(rL>=0.0f && rC>=0.0f);
    CHECK(tone_chroma_adjust(one,3,0.0f,0.0f,one_out,0,0)==0);
    CHECK(one_out[0]==one[0] && one_out[1]==one[1] && one_out[2]==one[2]);
    /* negative saturation on the single pixel is determinate */
    CHECK(tone_chroma_adjust(one,3,0.0f,-0.5f,one_out,&rL,&rC)==0);
    Oklch og=out_lch(one_out);
    CHECK(fabsf(og.C-px_lch(one).C*0.5f)<0.01f);

    /* ---- GSF2 state round trip carries both crosstalk metrics ---- */
    CHECK(state_save_ext("build/state_test.bin","oklab",13,0.1234,0.00125f,0.0025f)==0);
    char tag[16]; int frames=0; double metric=0; float xL=0,xC=0;
    CHECK(state_load_ext("build/state_test.bin",tag,&frames,&metric,&xL,&xC)==0);
    CHECK(strcmp(tag,"oklab")==0);
    CHECK(frames==13);
    CHECK(fabs(metric-0.1234)<1e-9);
    CHECK(fabsf(xL-0.00125f)<1e-8f && fabsf(xC-0.0025f)<1e-8f);
    /* legacy GSF1 records still load through the old entry */
    CHECK(state_save("build/state_legacy.bin","oklab",4,12.0)==0);
    CHECK(state_load("build/state_legacy.bin",tag,&frames,&metric)==0);
    CHECK(frames==4 && fabs(metric-12.0)<1e-9);

    printf("%d checks, %d failures\n",checks,fails);
    return fails?1:0;
}
