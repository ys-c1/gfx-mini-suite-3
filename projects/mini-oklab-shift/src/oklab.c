#include "oklab.h"
#include <math.h>
#include <string.h>

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

/* ------------------------------------------------------------------ */
/* Oklab (Björn Ottosson's matrices), used for reporting hue shifts.   */
/* ------------------------------------------------------------------ */

void linear_rgb_to_oklab(const float lin[3], float lab[3]){
    float r=lin[0], g=lin[1], b=lin[2];
    float l=0.4122214708f*r+0.5363325363f*g+0.0514459929f*b;
    float m=0.2119034982f*r+0.6806995451f*g+0.1073969566f*b;
    float s=0.0883024619f*r+0.2817188376f*g+0.6299787005f*b;
    l=cbrtf(l);
    m=cbrtf(m);
    s=cbrtf(s);
    lab[0]=0.2104542553f*l+0.7936177850f*m-0.0040720468f*s;
    lab[1]=1.9779984951f*l-2.4285922050f*m+0.4505937099f*s;
    lab[2]=0.0259040371f*l+0.7827717662f*m-0.8086757660f*s;
}

void srgb8_to_oklab(const unsigned char rgb[3], float lab[3]){
    float lin[3];
    lin[0]=srgb_to_linear(rgb[0]/255.0f);
    lin[1]=srgb_to_linear(rgb[1]/255.0f);
    lin[2]=srgb_to_linear(rgb[2]/255.0f);
    linear_rgb_to_oklab(lin,lab);
}

float oklab_hue_deg(const float lab[3]){
    float c=hypotf(lab[1],lab[2]);
    if(!(c>=OKLAB_HUE_MIN_CHROMA)) return NAN; /* also rejects NAN */
    float h=atan2f(lab[2],lab[1])*(180.0f/3.14159265358979323846f);
    if(h<0.0f) h+=360.0f;
    return h;
}

float oklab_hue_diff_deg(float h1,float h2){
    if(isnan(h1)||isnan(h2)) return NAN;
    float d=fabsf(h1-h2);
    if(d>180.0f) d=360.0f-d;
    return d;
}

float oklab_delta_e(const float lab1[3],const float lab2[3]){
    float dl=lab1[0]-lab2[0];
    float da=lab1[1]-lab2[1];
    float db=lab1[2]-lab2[2];
    return sqrtf(dl*dl+da*da+db*db);
}

/* ------------------------------------------------------------------ */
/* Hue-preserving gamut pullback.                                      */
/* ------------------------------------------------------------------ */

/* Byte encoding identical to brightness_shift(): transfer, *255+0.5,
 * truncate, clamp.  Shared by both paths so their in-gamut bytes agree. */
static unsigned char encode_byte(float linear){
    float v=linear_to_srgb(linear);
    v=v*255.0f+0.5f;
    return (unsigned char)(v<0.0f?0.0f:(v>255.0f?255.0f:v));
}

int brightness_shift_gamut(const unsigned char* rgb,int n,float factor,
                           float strength,unsigned char* out,
                           gamut_stats* stats){
    if(!rgb||!out||n<=0||n%3!=0) return -1;
    if(!isfinite(factor)||factor<=0.0f) return -1;
    /* Written so that NAN fails the comparison and is rejected. */
    if(!(strength>=0.0f&&strength<=1.0f)) return -1;

    gamut_stats acc;
    memset(&acc,0,sizeof acc);
    acc.components=n;
    acc.pixels=n/3;
    /* Global worst case, used as the cap on the per-image bound below: on
     * [0,1] the sRGB encoding is monotone, and every mixed channel lies in
     * [leg*(1-strength), leg], so one channel can move at most the encoded
     * span 255*(linear_to_srgb(leg)-linear_to_srgb(leg*(1-strength))) <=
     * 255*(1-linear_to_srgb(1-strength)); +1 covers truncation. */
    double global_bound=0.0;
    if(stats){
        global_bound=255.0*(1.0-(double)linear_to_srgb(1.0f-strength))+1.0;
        if(global_bound>255.0) global_bound=255.0;
    }
    double observed_bound=0.0;   /* max of the exact per-pixel pre-rounding
                                  * span over pixels actually out of gamut */
    acc.legacy_hue_mean_deg=0.0;
    acc.legacy_hue_max_deg=0.0;
    acc.oog_hue_max_deg=0.0;

    double hue_sum=0.0, legacy_hue_sum=0.0;
    float  hue_max=0.0f, legacy_hue_max=0.0f, oog_hue_max=0.0f;
    double de_max=0.0;

    for(int i=0;i<n;i+=3){
        float lin[3],leg[3];
        unsigned char oldpx[3],newpx[3];

        /* Exactly the legacy target, channel by channel. */
        for(int c=0;c<3;++c)
            lin[c]=srgb_to_linear(rgb[i+c]/255.0f)*factor;

        float tmax=lin[0];
        if(lin[1]>tmax) tmax=lin[1];
        if(lin[2]>tmax) tmax=lin[2];

        int in_gamut=(tmax<=1.0f);

        for(int c=0;c<3;++c){
            leg[c]=lin[c]>1.0f?1.0f:(lin[c]<0.0f?0.0f:lin[c]);
            oldpx[c]=encode_byte(leg[c]);
        }

        if(in_gamut){
            /* Nothing was out of gamut: unchanged from the legacy path. */
            for(int c=0;c<3;++c) newpx[c]=oldpx[c];
        }else{
            /* tmax>1.0 here, so it is strictly positive: no division by
             * zero and the all-black pixel never reaches this branch. */
            ++acc.out_of_gamut;
            for(int c=0;c<3;++c){
                float radial=lin[c]/tmax;      /* scale so max == 1 */
                float mixed=leg[c]+strength*(radial-leg[c]);
                if(mixed<0.0f) mixed=0.0f;
                if(mixed>1.0f) mixed=1.0f;
                newpx[c]=encode_byte(mixed);
                if(stats){
                    /* Rigorous pre-truncation span for this channel, from
                     * the actual leg and mixed values this call produced.
                     * Only needed when stats are requested. */
                    double span=255.0*((double)linear_to_srgb(leg[c])
                                      -(double)linear_to_srgb(mixed));
                    if(span>observed_bound) observed_bound=span;
                }
            }
        }

        int changed=0;
        for(int c=0;c<3;++c){
            int d=(int)newpx[c]-(int)oldpx[c];
            if(d<0) d=-d;
            if(d>acc.max_delta) acc.max_delta=d;
            if(newpx[c]!=oldpx[c]) changed=1;
            out[i+c]=newpx[c];
        }
        if(changed) ++acc.changed;

        if(stats){
            /* Measure hue against the ideal target.  A pixel enters the
             * statistics only when the target and BOTH outputs have chroma
             * above OKLAB_HUE_MIN_CHROMA, so every mean shares one
             * denominator and neutral pixels cannot inject hue noise. */
            float ideal_lab[3],new_lab[3],old_lab[3];
            linear_rgb_to_oklab(lin,ideal_lab);
            srgb8_to_oklab(newpx,new_lab);
            srgb8_to_oklab(oldpx,old_lab);
            float h_ideal=oklab_hue_deg(ideal_lab);
            float h_new=oklab_hue_deg(new_lab);
            float h_old=oklab_hue_deg(old_lab);

            /* All three have chroma above the threshold; full angle wrap. */
            if(!isnan(h_ideal)&&!isnan(h_new)&&!isnan(h_old)){
                float d_new=oklab_hue_diff_deg(h_ideal,h_new);
                float d_old=oklab_hue_diff_deg(h_ideal,h_old);
                hue_sum+=(double)d_new;
                legacy_hue_sum+=(double)d_old;
                if(d_new>hue_max) hue_max=d_new;
                if(d_old>legacy_hue_max) legacy_hue_max=d_old;
                ++acc.hue_samples;
                if(!in_gamut){
                    ++acc.oog_hue_samples;
                    if(d_new>oog_hue_max) oog_hue_max=d_new;
                }
            }
            if(changed){
                double de=(double)oklab_delta_e(new_lab,old_lab)*100.0;
                if(de>de_max) de_max=de;
            }
        }
    }

    if(stats){
        double bound=observed_bound+1.0;
        if(bound>global_bound) bound=global_bound;
        acc.max_delta_bound=bound;
        acc.max_de=de_max;
        if(acc.hue_samples>0) acc.hue_mean_deg=hue_sum/(double)acc.hue_samples;
        acc.hue_max_deg=(double)hue_max;
        acc.legacy_hue_max_deg=(double)legacy_hue_max;
        acc.legacy_hue_mean_deg=acc.hue_samples>0
            ? legacy_hue_sum/(double)acc.hue_samples : 0.0;
        acc.oog_hue_max_deg=(double)oog_hue_max;
        *stats=acc;
    }
    return 0;
}
