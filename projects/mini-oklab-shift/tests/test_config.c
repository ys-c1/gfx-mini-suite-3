#include "../src/config.h"
#include <stdio.h>
static int fails=0,checks=0;
#define CHECK(c) do{++checks; if(!(c)){++fails;printf("FAIL %d %s\n",__LINE__,#c);}}while(0)

int main(void){
    const char* buf =
        "factor=1.0\n"
        "ref_white=255,173,89\n"
        "spaced= 12 , 34 , 56 \n"
        "zero=0,0,0\n";
    unsigned char rgb[3];

    /* 正常解析 */
    CHECK(config_get_rgb(buf,"ref_white",rgb)==0);
    CHECK(rgb[0]==255 && rgb[1]==173 && rgb[2]==89);
    CHECK(config_get_rgb(buf,"spaced",rgb)==0);
    CHECK(rgb[0]==12 && rgb[1]==34 && rgb[2]==56);

    /* 全零在配置层合法（由适应层拒绝） */
    CHECK(config_get_rgb(buf,"zero",rgb)==0);
    CHECK(rgb[0]==0 && rgb[1]==0 && rgb[2]==0);

    /* 键缺失返回 1，且不写 rgb */
    rgb[0]=rgb[1]=rgb[2]=77;
    CHECK(config_get_rgb(buf,"nope",rgb)==1);
    CHECK(rgb[0]==77 && rgb[1]==77 && rgb[2]==77);

    /* 非法格式/范围 */
    CHECK(config_get_rgb("a=255,300,10\n","a",rgb)==-1);
    CHECK(config_get_rgb("a=-1,0,0\n","a",rgb)==-1);
    CHECK(config_get_rgb("a=255,abc\n","a",rgb)==-1);
    CHECK(config_get_rgb("a=1,2,3x\n","a",rgb)==-1);
    CHECK(config_get_rgb("a=1,2\n","a",rgb)==-1);
    CHECK(config_get_rgb("a=1,2,3,4\n","a",rgb)==-1);

    /* 空参数防御 */
    CHECK(config_get_rgb(NULL,"a",rgb)==-1);
    CHECK(config_get_rgb(buf,NULL,rgb)==-1);
    CHECK(config_get_rgb(buf,"a",NULL)==-1);

    /* 既有数值接口不回归 */
    CHECK(config_get_f(buf,"factor",9.0)==1.0);
    CHECK(config_get_f(buf,"missing",9.0)==9.0);

    /* 文件加载：不存在返回负值 */
    char tmp[64];
    CHECK(config_load("/no/such/path/xyz.cfg",tmp,sizeof(tmp))<0);

    printf("%d checks, %d failures\n",checks,fails);
    return fails?1:0;
}
