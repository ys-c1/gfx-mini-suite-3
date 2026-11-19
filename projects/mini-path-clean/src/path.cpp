#include "path.h"
#include <cmath>
int path_remove_collinear(const float* pts,size_t n,float* out,size_t cap,size_t* outn,float eps){
    if(!pts||!out||!outn||n<2) return -1;
    size_t k=0;
    for(size_t i=0;i<n;++i){
        if(i>0&&i+1<n){
            float ax=pts[(i-1)*2],ay=pts[(i-1)*2+1];
            float bx=pts[i*2],by=pts[i*2+1];
            float cx=pts[(i+1)*2],cy=pts[(i+1)*2+1];
            float cross=(bx-ax)*(cy-ay)-(by-ay)*(cx-ax);
            float len=std::sqrt((cx-ax)*(cx-ax)+(cy-ay)*(cy-ay));
            if(len>0.0f&&std::fabs(cross/len)<=eps) continue;
        }
        if(k>=cap) return -2;
        out[k*2]=pts[i*2]; out[k*2+1]=pts[i*2+1]; ++k;
    }
    *outn=k; return 0;
}
float path_length(const float* pts,size_t n){
    float s=0.0f;
    for(size_t i=1;i<n;++i){
        float dx=pts[i*2]-pts[(i-1)*2], dy=pts[i*2+1]-pts[(i-1)*2+1];
        s+=std::sqrt(dx*dx+dy*dy);
    }
    return s;
}
