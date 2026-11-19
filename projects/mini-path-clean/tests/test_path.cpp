#include "../src/path.h"
#include <cstdio>
#include <cmath>
static int fails=0,checks=0;
#define CHECK(c) do{++checks; if(!(c)){++fails;std::printf("FAIL %d %s\n",__LINE__,#c);}}while(0)
int main(){
    float pts[8]={0,0, 5,0, 10,0, 10,5};
    float out[16]; size_t n=0;
    CHECK(path_remove_collinear(pts,4,out,8,&n,1e-3f)==0);
    CHECK(n==3);
    CHECK(out[2]==10.0f&&out[3]==0.0f);
    CHECK(std::fabs(path_length(pts,4)-15.0f)<1e-4f);
    CHECK(path_remove_collinear(pts,4,out,1,&n,1e-3f)==-2);
    std::printf("%d checks, %d failures\n",checks,fails);
    return fails?1:0;
}
