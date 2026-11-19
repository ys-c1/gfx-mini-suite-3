#include "../src/tile.h"
#include <stdio.h>
static int fails=0,checks=0;
#define CHECK(c) do{++checks; if(!(c)){++fails;printf("FAIL %d %s\n",__LINE__,#c);}}while(0)
int main(void){
    enum{W=4,H=4};
    unsigned char map[W*H]={0};
    map[1*W+1]=1;
    CHECK(tile_solid(map,W,H,1,1)==1);
    CHECK(tile_solid(map,W,H,5,5)==0);
    int tx=-1,ty=-1;
    CHECK(aabb_vs_tiles(map,W,H,4,4,10,10,&tx,&ty)==1);
    CHECK(tx==1&&ty==1);
    CHECK(aabb_vs_tiles(map,W,H,24,24,6,6,&tx,&ty)==0);
    CHECK(aabb_vs_tiles(map,W,H,0,0,0,5,&tx,&ty)==-1);
    printf("%d checks, %d failures\n",checks,fails);
    return fails?1:0;
}
