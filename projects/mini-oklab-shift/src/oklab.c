#include "oklab.h"
#include <math.h>
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
