#include "../src/shadow.h"
#include <stdio.h>
#include <math.h>
static int fails=0,checks=0;
#define CHECK(c) do{++checks; if(!(c)){++fails;printf("FAIL %d %s\n",__LINE__,#c);}}while(0)
int main(void){
    float o1x,o1y,o2x,o2y;
    CHECK(shadow_project(0,10,10,10,5,0,20,&o1x,&o1y,&o2x,&o2y)==0);
    CHECK(o1y>10.0f && o2y>10.0f);
    float segs[4]={0,10,10,10};
    CHECK(shadow_point_lit(5,20,5,0,segs,1)==0);
    CHECK(shadow_point_lit(20,20,5,0,segs,1)==1);
    CHECK(shadow_project(0,0,0,0,0,0,10,&o1x,&o1y,&o2x,&o2y)==-1);
    printf("%d checks, %d failures\n",checks,fails);
    return fails?1:0;
}
