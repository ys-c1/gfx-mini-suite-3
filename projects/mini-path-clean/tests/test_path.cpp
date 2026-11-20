#include "../src/path.h"
#include <cstdio>
#include <cmath>
static int fails=0,checks=0;
#define CHECK(c) do{++checks; if(!(c)){++fails;std::printf("FAIL %d %s\n",__LINE__,#c);}}while(0)

static float cross2(float ax,float ay,float bx,float by){return ax*by-ay*bx;}
/* 严格(非擦边)线段相交判定 */
static bool seg_proper_intersect(float ax,float ay,float bx,float by,
                                 float cx,float cy,float dx,float dy){
    float d1=cross2(bx-ax,by-ay,cx-ax,cy-ay);
    float d2=cross2(bx-ax,by-ay,dx-ax,dy-ay);
    float d3=cross2(dx-cx,dy-cy,ax-cx,ay-cy);
    float d4=cross2(dx-cx,dy-cy,bx-cx,by-cy);
    const float e=1e-6f;
    return ((d1>e&&d2<-e)||(d1<-e&&d2>e))&&((d3>e&&d4<-e)||(d3<-e&&d4>e));
}
static bool self_intersects(const float* p,size_t n){
    for(size_t i=0;i+1<n;++i)
        for(size_t j=i+2;j+1<n;++j)
            if(seg_proper_intersect(p[i*2],p[i*2+1],p[i*2+2],p[i*2+3],
                                    p[j*2],p[j*2+1],p[j*2+2],p[j*2+3])) return true;
    return false;
}

int main(){
    /* ---- 原有清理与长度测试, 不变 ---- */
    float pts[8]={0,0, 5,0, 10,0, 10,5};
    float out[16]; size_t n=0;
    CHECK(path_remove_collinear(pts,4,out,8,&n,1e-3f)==0);
    CHECK(n==3);
    CHECK(out[2]==10.0f&&out[3]==0.0f);
    CHECK(std::fabs(path_length(pts,4)-15.0f)<1e-4f);
    CHECK(path_remove_collinear(pts,4,out,1,&n,1e-3f)==-2);

    /* ---- 细分式圆滑 ---- */
    float sm[512]; size_t sn=0;
    float z[10]={0,0, 4,3, 8,0, 12,3, 16,0};

    /* 轮数为零: 与原折线逐点相同 */
    CHECK(path_smooth_chaikin(z,5,0,0.25f,sm,256,&sn)==0);
    CHECK(sn==5);
    { bool same=true; for(int i=0;i<10;++i) if(sm[i]!=z[i]) same=false; CHECK(same); }

    /* 轮数为负: 明确返回 -3 */
    CHECK(path_smooth_chaikin(z,5,-1,0.25f,sm,256,&sn)==-3);

    /* 只有一个点: 原样输出该点 */
    float one[2]={3,4};
    CHECK(path_smooth_chaikin(one,1,3,0.25f,sm,256,&sn)==0);
    CHECK(sn==1&&sm[0]==3.0f&&sm[1]==4.0f);

    /* 两个点: 无拐角, 原样输出 */
    float two[4]={0,0, 10,0};
    CHECK(path_smooth_chaikin(two,2,3,0.25f,sm,256,&sn)==0);
    CHECK(sn==2&&sm[2]==10.0f&&sm[3]==0.0f);

    /* 两段共线: 照常切角, 结果仍共线且长度严格不变 */
    float col[6]={0,0, 5,0, 10,0};
    CHECK(path_smooth_chaikin(col,3,2,0.25f,sm,256,&sn)==0);
    CHECK(sn==6); /* 2 + 2^2*(3-2) */
    { bool ally=true; for(size_t i=0;i<sn;++i) if(sm[i*2+1]!=0.0f) ally=false; CHECK(ally); }
    CHECK(std::fabs(path_length(sm,sn)-10.0f)<1e-4f);

    /* 直角拐角一轮: 截点位置精确, 端点保留, 长度变短 */
    float ra[6]={0,0, 10,0, 10,10};
    CHECK(path_smooth_chaikin(ra,3,1,0.25f,sm,256,&sn)==0);
    CHECK(sn==4);
    CHECK(sm[0]==0.0f&&sm[1]==0.0f);
    CHECK(sm[2]==7.5f&&sm[3]==0.0f);
    CHECK(sm[4]==10.0f&&sm[5]==2.5f);
    CHECK(sm[6]==10.0f&&sm[7]==10.0f);
    CHECK(path_length(sm,sn)<20.0f);

    /* ratio 超界被钳到 0.5 */
    CHECK(path_smooth_chaikin(ra,3,1,0.9f,sm,256,&sn)==0);
    CHECK(sm[2]==5.0f&&sm[5]==5.0f);

    /* 点数公式 n_r = 2 + 2^r*(n-2) */
    CHECK(path_smooth_chaikin(z,5,3,0.25f,sm,256,&sn)==0);
    CHECK(sn==2+8*3);

    /* 长度只减不增(变化有上界: dL<=0 且 |dL|<=原长) */
    { float prev=path_length(z,5); bool mono=true;
      for(int k=1;k<=4;++k){
          CHECK(path_smooth_chaikin(z,5,k,0.25f,sm,256,&sn)==0);
          float L=path_length(sm,sn);
          if(L>prev+1e-4f) mono=false;
          prev=L;
      }
      CHECK(mono); }

    /* 容量不足: -2 */
    CHECK(path_smooth_chaikin(z,5,3,0.25f,sm,10,&sn)==-2);

    /* 紧螺旋多轮圆滑后不得自交, 长度不超原长 */
    float sp[20]={0,0, 10,0, 10,10, 2,10, 2,2, 8,2, 8,8, 4,8, 4,4, 6,4};
    CHECK(!self_intersects(sp,10));
    CHECK(path_smooth_chaikin(sp,10,4,0.25f,sm,256,&sn)==0);
    CHECK(sn==2+16*8);
    CHECK(!self_intersects(sm,sn));
    CHECK(path_length(sm,sn)<=path_length(sp,10)+1e-4f);

    std::printf("%d checks, %d failures\n",checks,fails);
    return fails?1:0;
}
