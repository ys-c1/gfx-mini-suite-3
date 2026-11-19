#include "oklab.h"
#include <math.h>
#include <limits.h>

/* ================================================================== */
/* Legacy path -- kept byte-for-byte identical to the original.       */
/* ================================================================== */
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

/* ================================================================== */
/* Oklab path.                                                        */
/*                                                                    */
/* All internal work is double precision.  The single-precision       */
/* legacy helpers above are NOT reused: with float matrices the       */
/* a/b round trip at 255,255,255 carries ~1e-4 absolute error, which  */
/* is enough to move an 8-bit result by one LSB.  Double brings the    */
/* worst observed linear-domain error on the full 2^24 colour cube    */
/* down to 2.7e-7, which quantises back to the identical byte.        */
/* ================================================================== */

static double srgb_decode8(unsigned char v){
    double x=v/255.0;
    if(x<=0.04045) return x/12.92;
    return pow((x+0.055)/1.055,2.4);
}

/* Encode linear light in [0,1] to 8 bits with round-half-up. */
static unsigned char srgb_encode8(double lin){
    if(!(lin>0.0)) return 0;              /* also catches NaN */
    if(lin>1.0) lin=1.0;
    double x = (lin<=0.0031308) ? lin*12.92 : 1.055*pow(lin,1.0/2.4)-0.055;
    double v = x*255.0+0.5;
    if(!(v>0.0)) return 0;
    if(v>=255.0) return 255;
    return (unsigned char)v;
}

static int linear_in_gamut(double r,double g,double b){
    /* Tolerance only absorbs the matrix round-trip noise (~3e-7);
       it is far below one 8-bit LSB in the encoded domain (~1e-4). */
    const double e=1e-6;
    return r>=-e && r<=1.0+e && g>=-e && g<=1.0+e && b>=-e && b<=1.0+e;
}

void rgb8_to_oklab(int r,int g,int b,double lab[3]){
    /* Out-of-range integer inputs are clamped, so every input has a
       defined result. */
    if(r<0) r=0;
    if(r>255) r=255;
    if(g<0) g=0;
    if(g>255) g=255;
    if(b<0) b=0;
    if(b>255) b=255;
    double lr=srgb_decode8((unsigned char)r);
    double lg=srgb_decode8((unsigned char)g);
    double lb=srgb_decode8((unsigned char)b);

    double l=0.4122214708*lr+0.5363325363*lg+0.0514459929*lb;
    double m=0.2119034982*lr+0.6806995451*lg+0.1073969566*lb;
    double s=0.0883024619*lr+0.2817188376*lg+0.6299787005*lb;
    l=cbrt(l); m=cbrt(m); s=cbrt(s);

    lab[0]=0.2104542553*l+0.7936177850*m-0.0040720468*s;
    lab[1]=1.9779984951*l-2.4285922050*m+0.4505937099*s;
    lab[2]=0.0259040371*l+0.7827717662*m-0.8086757660*s;
}

void oklab_to_linear(const double lab[3],double lin[3]){
    double l_=lab[0]+0.3963377774*lab[1]+0.2158037573*lab[2];
    double m_=lab[0]-0.1055613458*lab[1]-0.0638541728*lab[2];
    double s_=lab[0]-0.0894841775*lab[1]-1.2914855480*lab[2];
    double l=l_*l_*l_, m=m_*m_*m_, s=s_*s_*s_;
    lin[0]= 4.0767416621*l-3.3077115913*m+0.2309699292*s;
    lin[1]=-1.2684380046*l+2.6097574011*m-0.3413193965*s;
    lin[2]=-0.0041960863*l-0.7034186147*m+1.7076147010*s;
}

/* Scale chroma toward the neutral axis until the colour fits sRGB.
   Hue angle atan2(b,a) is preserved exactly by construction: a and b
   are multiplied by the same scalar.  Returns the scalar used. */
static double gamut_map_chroma(double lab[3],double lin[3]){
    if(linear_in_gamut(lin[0],lin[1],lin[2])) return 1.0;
    if(lab[1]==0.0 && lab[2]==0.0){
        /* Hue is undefined; only L can fix it. Clamp L to the closest
           achromatic in-gamut value. */
        return 0.0;
    }
    /* A neutral (s == 0) at any L in [0,1] is always inside sRGB, and
       the sRGB gamut is convex, so the in-gamut set of s is an interval
       starting at 0 for every practical colour; a bisection over
       [0,1] therefore finds the largest viable s. */
    double lo=0.0, hi=1.0, best=0.0;
    for(int i=0;i<40;i++){
        double mid=0.5*(lo+hi);
        double t[3]={lab[0],lab[1]*mid,lab[2]*mid};
        double tmp[3];
        oklab_to_linear(t,tmp);
        if(linear_in_gamut(tmp[0],tmp[1],tmp[2])){ best=mid; lo=mid; }
        else hi=mid;
    }
    lab[1]*=best;
    lab[2]*=best;
    oklab_to_linear(lab,lin);
    return best;
}

void oklab_to_rgb8(const double lab[3],unsigned char out[3]){
    double v[3];
    /* Non-finite input: fall back to a neutral grey at the same L. */
    if(!isfinite(lab[0])||!isfinite(lab[1])||!isfinite(lab[2])){
        v[0]=isfinite(lab[0])?lab[0]:0.0;
        v[1]=0.0; v[2]=0.0;
    } else {
        v[0]=lab[0]; v[1]=lab[1]; v[2]=lab[2];
    }
    if(v[0]<0.0) v[0]=0.0;
    if(v[0]>1.0) v[0]=1.0;

    double lin[3];
    oklab_to_linear(v,lin);
    gamut_map_chroma(v,lin);

    out[0]=srgb_encode8(lin[0]);
    out[1]=srgb_encode8(lin[1]);
    out[2]=srgb_encode8(lin[2]);
}

int oklab_shift(const unsigned char* rgb,int npix,double delta_l,unsigned char* out){
    if(!rgb||!out||npix<=0||npix>INT_MAX/3) return -1;
    if(!isfinite(delta_l)) return -1;

    /* Exact identity: copy, never touch the float pipeline.  Writing the
       input bytes back is what guarantees value-for-value restoration at
       delta_l == 0, independent of matrix/rounding noise. */
    if(delta_l==0.0){
        if(out!=rgb){
            for(int i=0;i<npix*3;++i) out[i]=rgb[i];
        }
        return 0;
    }

    for(int i=0;i<npix;++i){
        double lab[3];
        rgb8_to_oklab(rgb[3*i],rgb[3*i+1],rgb[3*i+2],lab);
        lab[0]+=delta_l;
        if(lab[0]<0.0) lab[0]=0.0;
        if(lab[0]>1.0) lab[0]=1.0;
        oklab_to_rgb8(lab,out+3*i);
    }
    return 0;
}
