#include "../src/oklab.h"
#include <stdio.h>
#include <math.h>
static int fails=0,checks=0;
#define CHECK(c) do{++checks; if(!(c)){++fails;printf("FAIL %d %s\n",__LINE__,#c);}}while(0)
int main(void){
    CHECK(fabsf(srgb_to_linear(0.0f))<1e-6f);
    CHECK(fabsf(srgb_to_linear(1.0f)-1.0f)<1e-5f);
    CHECK(fabsf(linear_to_srgb(srgb_to_linear(0.5f))-0.5f)<1e-4f);
    unsigned char in[4]={0,64,128,255}, out[4];
    CHECK(brightness_shift(in,4,1.0f,out)==0);
    CHECK(out[2]==in[2]||out[2]==in[2]+1);
    CHECK(brightness_shift(in,4,-1.0f,out)==-1);
    printf("%d checks, %d failures\n",checks,fails);
    return fails?1:0;
}
