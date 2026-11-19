#include "tile.h"
int tile_solid(const unsigned char* map,int w,int h,int tx,int ty){
    if(!map||tx<0||ty<0||tx>=w||ty>=h) return 0;
    return map[ty*w+tx]!=0;
}
int aabb_vs_tiles(const unsigned char* map,int w,int h,float x,float y,float bw,float bh,int* hit_tx,int* hit_ty){
    if(!map||bw<=0.0f||bh<=0.0f) return -1;
    int x0=(int)(x/8.0f), y0=(int)(y/8.0f);
    int x1=(int)((x+bw-1e-3f)/8.0f), y1=(int)((y+bh-1e-3f)/8.0f);
    for(int ty=y0;ty<=y1;++ty)
        for(int tx=x0;tx<=x1;++tx)
            if(tile_solid(map,w,h,tx,ty)){
                if(hit_tx) *hit_tx=tx;
                if(hit_ty) *hit_ty=ty;
                return 1;
            }
    return 0;
}
