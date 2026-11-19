#include "../src/path.h"
#include "ref_baseline.h"
#include <cstdio>
#include <cmath>
#include <cstring>
#include <vector>

static int fails=0,checks=0;
#define CHECK(c) do{++checks; if(!(c)){++fails;std::printf("FAIL %d %s\n",__LINE__,#c);}}while(0)
#define CHECK_NEAR(a,b,t) do{++checks; double _a=(a),_b=(b),_t=(t); \
    if(!(std::fabs(_a-_b)<=_t)){++fails;std::printf("FAIL %d |%g-%g|>%g\n",__LINE__,_a,_b,_t);}}while(0)

static void test_original(void){
    float pts[8]={0,0, 5,0, 10,0, 10,5};
    float out[16]; size_t n=0;
    CHECK(path_remove_collinear(pts,4,out,8,&n,1e-3f)==0);
    CHECK(n==3);
    CHECK(out[2]==10.0f&&out[3]==0.0f);
    CHECK(std::fabs(path_length(pts,4)-15.0f)<1e-4f);
    CHECK(path_remove_collinear(pts,4,out,1,&n,1e-3f)==-2);
}

/* 回归：新实现的清理与长度必须与冻结基线逐位一致。 */
static void test_regression_matches_baseline(void){
    unsigned state=0x12345678u;
    auto rnd=[&](){ state^=state<<13; state^=state>>17; state^=state<<5; return state; };
    const float eplist[3]={0.0f,1e-4f,1e-2f};
    int cases=0;
    for(int it=0;it<400;++it){
        size_t n=(size_t)(2+rnd()%39);
        std::vector<float> pts(n*2);
        for(size_t i=0;i<n*2;++i){
            /* 混合整点与小数，制造共线/近似共线/重复点 */
            pts[i]=(float)((int)(rnd()%41)-20) * ((rnd()%3)?1.0f:0.25f);
        }
        for(int e=0;e<3;++e){
            float eps=eplist[e];
            std::vector<float> a(n*2+2), b(n*2+2);
            size_t na=0,nb=0;
            int ra=path_remove_collinear(pts.data(),n,a.data(),n+1,&na,eps);
            int rb=ref_remove_collinear(pts.data(),n,b.data(),n+1,&nb,eps);
            CHECK(ra==rb);
            CHECK(na==nb);
            CHECK(std::memcmp(a.data(),b.data(),nb*2*sizeof(float))==0);
            CHECK(path_length(pts.data(),n)==ref_length(pts.data(),n));
            ++cases;
        }
    }
    std::printf("regression cases: %d\n",cases);
}

/* 检测不得改变任何顶点坐标。 */
static void test_no_mutation(void){
    float pts[10]={0,0, 10,10, 0,10, 10,0, 5,-3};
    float copy[10]; std::memcpy(copy,pts,sizeof pts);
    path_isect_result r;
    CHECK(path_self_intersections(pts,5,0.5f,&r)==0);
    CHECK(std::memcmp(pts,copy,sizeof pts)==0);
    CHECK(path_self_intersections(pts,5,0.0f,&r)==0);
    CHECK(std::memcmp(pts,copy,sizeof pts)==0);
    CHECK(r.pairs>0);
}

/* 退化输入：顶点数为二、全共线、首尾重合。 */
static void test_degenerate(void){
    path_isect_result r;

    /* 顶点数为二（不同点）：一条线段，确定结果 0 对 */
    float two[4]={1,2, 7,9};
    CHECK(path_self_intersections(two,2,0.0f,&r)==0);
    CHECK(r.pairs==0&&r.exact_pairs==0&&r.near_pairs==0&&r.segments==0);
    CHECK(r.has_first==0&&r.has_exact==0&&r.first_i==-1&&r.first_j==-1);
    CHECK(path_self_intersections(two,2,100.0f,&r)==0);
    CHECK(r.pairs==0);

    /* 顶点数为二（重合点）：仍是一条零长度线段，不参与检测 */
    float two_same[4]={3,3, 3,3};
    CHECK(path_self_intersections(two_same,2,0.0f,&r)==0);
    CHECK(r.pairs==0&&r.segments==0);
    CHECK(path_self_intersections(two_same,2,100.0f,&r)==0);
    CHECK(r.pairs==0);

    /* 全共线：单调。非相邻段之间相隔 1，容差越过间隔时会作为近似对报出 */
    float col[8]={0,0, 1,0, 2,0, 3,0};
    CHECK(path_self_intersections(col,4,0.0f,&r)==0);
    CHECK(r.pairs==0&&r.exact_pairs==0);
    CHECK(path_self_intersections(col,4,0.5f,&r)==0);
    CHECK(r.pairs==0);
    CHECK(path_self_intersections(col,4,2.0f,&r)==0);
    CHECK(r.pairs==1&&r.near_pairs==1&&r.exact_pairs==0);
    CHECK(r.first_i==0&&r.first_j==2);
    CHECK_NEAR(r.first_distance,1.0,1e-6);
    CHECK_NEAR(r.first_x,1.5,1e-6);
    CHECK_NEAR(r.first_y,0.0,1e-6);
    /* 全共线：返回原路的往返段会与首段共线重叠，按严格相交报 1 对 */
    float back[8]={0,0, 3,0, 1,0, 4,0};
    CHECK(path_self_intersections(back,4,0.0f,&r)==0);
    CHECK(r.pairs==1&&r.exact_pairs==1&&r.near_pairs==0);
    CHECK(r.first_i==0&&r.first_j==2);
    CHECK_NEAR(r.first_x,1.0,1e-6);
    CHECK_NEAR(r.first_y,0.0,1e-6);
    /* 容差增大不改变严格相交的计数；三段折线只有 (0,2) 一对非相邻段 */
    CHECK(path_self_intersections(back,4,100.0f,&r)==0);
    CHECK(r.pairs==1&&r.exact_pairs==1&&r.near_pairs==0);
    /* 全共线清理后应只剩两端点，长度不变 */
    float out[16]; size_t n=0;
    CHECK(path_remove_collinear(col,4,out,16,&n,1e-3f)==0);
    CHECK(n==2);
    CHECK(out[0]==0&&out[1]==0&&out[2]==3&&out[3]==0);
    CHECK(path_length(col,4)==path_length(out,n));

    /* 首尾重合：闭合方形，接缝不报自交；容差越过间隔后才报近似对 */
    float square[10]={0,0, 4,0, 4,4, 0,4, 0,0};
    CHECK(path_self_intersections(square,5,0.0f,&r)==0);
    CHECK(r.pairs==0&&r.has_first==0);
    CHECK(path_self_intersections(square,5,3.0f,&r)==0);
    CHECK(r.pairs==0);
    CHECK(path_self_intersections(square,5,5.0f,&r)==0);
    CHECK(r.pairs==2);              /* 非相邻的两条对边 */
    CHECK(r.exact_pairs==0&&r.near_pairs==2);
    CHECK(r.first_i==0&&r.first_j==2);
    /* 首尾重合：闭合蝴蝶结，只剩一对真实交叉 */
    float bow[10]={0,0, 4,0, 0,4, 4,4, 0,0};
    CHECK(path_self_intersections(bow,5,0.0f,&r)==0);
    CHECK(r.pairs==1);
    CHECK(r.exact_pairs==1);
    CHECK(r.first_i==1&&r.first_j==3);
    CHECK_NEAR(r.first_x,2.0,1e-6);
    CHECK_NEAR(r.first_y,2.0,1e-6);
    /* 清理不改变首尾相等的判定 */
    size_t bn=0; float bout[10];
    path_isect_result rb;
    CHECK(path_clean_and_check(bow,5,1e-3f,0.0f,bout,5,&bn,&rb)==0);
    CHECK(bn==5);
    CHECK(rb.pairs==1&&rb.first_i==1&&rb.first_j==3);
}

/* 严格相交：位置与计数。 */
static void test_exact(void){
    path_isect_result r;

    float cross1[8]={0,0, 10,10, 0,10, 10,0};
    CHECK(path_self_intersections(cross1,4,0.0f,&r)==0);
    CHECK(r.pairs==1&&r.exact_pairs==1&&r.near_pairs==0);
    CHECK(r.segments==2);
    CHECK(r.first_i==0&&r.first_j==2);
    CHECK(r.has_first==1&&r.first_is_near==0);
    CHECK_NEAR(r.first_x,5.0,1e-6);
    CHECK_NEAR(r.first_y,5.0,1e-6);
    CHECK(r.first_distance==0.0f);

    /* 多对相交，首个按 (i,j) 字典序 */
    float multi[10]={0,0, 10,0, 10,10, 5,-5, 5,15};
    CHECK(path_self_intersections(multi,5,0.0f,&r)==0);
    CHECK(r.pairs==2&&r.exact_pairs==2);
    CHECK(r.segments==3);
    CHECK(r.first_i==0&&r.first_j==2);
    CHECK_NEAR(r.first_x,20.0/3.0,1e-5);
    CHECK_NEAR(r.first_y,0.0,1e-6);
    CHECK(r.exact_i==0&&r.exact_j==2);

    /* 端点接触与共线重叠都按严格相交计，共线重叠取重叠段起点 */
    float touch[10]={0,0, 10,0, 10,5, 5,0, 15,0};
    CHECK(path_self_intersections(touch,5,0.0f,&r)==0);
    CHECK(r.pairs==3&&r.exact_pairs==3);   /* (0,2) (0,3) (1,3) */
    CHECK(r.first_i==0&&r.first_j==2);
    CHECK_NEAR(r.first_x,5.0,1e-6);
    CHECK_NEAR(r.first_y,0.0,1e-6);
    float coll[10]={0,0, 10,0, 10,-1, 5,0, 1,0};
    CHECK(path_self_intersections(coll,5,0.0f,&r)==0);
    CHECK(r.pairs==2&&r.exact_pairs==2);
    CHECK(r.first_i==0&&r.first_j==2);
    CHECK_NEAR(r.first_x,5.0,1e-6);
    CHECK_NEAR(r.first_y,0.0,1e-6);

    /* 合法输入下的错误参数 */
    CHECK(path_self_intersections(nullptr,4,0.0f,&r)==-1);
    CHECK(path_self_intersections(cross1,4,-1.0f,&r)==-1);
    CHECK(path_self_intersections(cross1,4,0.0f,nullptr)==-1);
    CHECK(path_self_intersections(cross1,1,0.0f,&r)==-1);
}

/* 容差：只增报近似相交，严格结果与首个严格交点不受影响。 */
static void test_tolerance(void){
    path_isect_result r;
    /* s0=(0,0)-(10,0)；s2=(10,0.5)-(0,0.2)，最小距离 0.2 */
    float pts[8]={0,0, 10,0, 10,0.5, 0,0.2};

    CHECK(path_self_intersections(pts,4,0.0f,&r)==0);
    CHECK(r.pairs==0&&r.near_pairs==0);
    CHECK(path_self_intersections(pts,4,0.1f,&r)==0);
    CHECK(r.pairs==0&&r.near_pairs==0);
    CHECK(path_self_intersections(pts,4,0.25f,&r)==0);
    CHECK(r.pairs==1&&r.near_pairs==1&&r.exact_pairs==0);
    CHECK(r.has_first==1&&r.first_is_near==1);
    CHECK(r.has_exact==0);
    CHECK(r.first_i==0&&r.first_j==2);
    CHECK_NEAR(r.first_x,0.0,1e-6);
    CHECK_NEAR(r.first_y,0.1,1e-6);
    CHECK_NEAR(r.first_distance,0.2,1e-5);

    /* 容差再大也不改变严格相交的计数，近似对只增不减 */
    float mixed[10]={0,0, 10,10, 0,10, 10,0, 10,0.3};
    size_t prev_near=0; size_t prev_exact=(size_t)-1;
    for(float tol : {0.0f,0.05f,0.2f,1.0f}){
        CHECK(path_self_intersections(mixed,5,tol,&r)==0);
        CHECK(r.near_pairs>=prev_near);
        if(prev_exact!=(size_t)-1) CHECK(r.exact_pairs==prev_exact);
        prev_near=r.near_pairs; prev_exact=r.exact_pairs;
        if(r.has_first) CHECK(r.first_i<=r.first_j);
    }
    /* 近似对排在严格对之前时，first 为近似，但 exact_* 仍给出真实交点 */
    float nearfirst[10]={0,0, 10,0, 10,10, 5,10.2, 5,-5};
    CHECK(path_self_intersections(nearfirst,5,0.5f,&r)==0);
    CHECK(r.has_exact==1);
    CHECK(r.first_i==0);
    if(r.first_is_near){
        CHECK(r.exact_i==0&&r.exact_j==4);
    }
}

/* 组合清理流程：清理结果与基线一致，检测作用于清理后的折线。 */
static void test_clean_and_check(void){
    /* 原始路径含共线中间点，清理后交叉仍存在 */
    float pts[12]={0,0, 5,0, 10,0, 10,10, 5,5, 0,10};
    float out[12]; size_t n=0;
    path_isect_result r;
    CHECK(path_clean_and_check(pts,6,1e-3f,0.0f,out,6,&n,&r)==0);
    CHECK(n==5);
    CHECK(out[0]==0&&out[1]==0);
    CHECK(out[8]==0&&out[9]==10);

    float refout[12]; size_t rn=0;
    CHECK(ref_remove_collinear(pts,6,refout,6,&rn,1e-3f)==0);
    CHECK(n==rn);
    CHECK(std::memcmp(out,refout,rn*2*sizeof(float))==0);
    CHECK(path_length(pts,6)==ref_length(pts,6));
    CHECK(path_length(out,n)==path_length(pts,6));

    /* 清理后被剔除的共线点若本来是"交点"，应随清理一起消失 */
    float grad[10]={0,0, 2,0, 4,0, 4,4, 0,4};
    float gout[10]; size_t gn=0; path_isect_result gr;
    CHECK(path_clean_and_check(grad,5,1e-3f,0.0f,gout,5,&gn,&gr)==0);
    CHECK(gn==4);
    CHECK(gr.pairs==0);

    /* 容量不足与非法参数 */
    CHECK(path_clean_and_check(pts,6,1e-3f,0.0f,out,2,&n,&r)==-2);
    CHECK(path_clean_and_check(pts,6,1e-3f,0.0f,out,6,&n,nullptr)==-1);
    CHECK(path_clean_and_check(nullptr,6,1e-3f,0.0f,out,6,&n,&r)==-1);
}

/* 清理流程中的检测必须在清理后的坐标上工作：线段下标指向清理后序列。 */
static void test_clean_then_detect_indices(void){
    /* (5,5) 在 (10,0)-(0,10) 上共线被清理，交叉对从原始 (0,3) 变为清理后 (0,2) */
    float pts[10]={0,0, 10,10, 10,0, 5,5, 0,10};
    float out[10]; size_t n=0; path_isect_result r;
    CHECK(path_clean_and_check(pts,5,1e-3f,0.0f,out,5,&n,&r)==0);
    CHECK(n==4);
    CHECK(out[4]==10.0f&&out[5]==0.0f);
    CHECK(out[6]==0.0f&&out[7]==10.0f);
    CHECK(r.pairs==1&&r.exact_pairs==1);
    CHECK(r.first_i==0&&r.first_j==2);
    CHECK_NEAR(r.first_x,5.0,1e-6);
    CHECK_NEAR(r.first_y,5.0,1e-6);
    /* 直接用清理结果复算，结果必须一致 */
    path_isect_result r2;
    CHECK(path_self_intersections(out,n,0.0f,&r2)==0);
    CHECK(r.pairs==r2.pairs);
    CHECK(r.first_i==r2.first_i&&r.first_j==r2.first_j);
}

int main(){
    test_original();
    test_regression_matches_baseline();
    test_no_mutation();
    test_degenerate();
    test_exact();
    test_tolerance();
    test_clean_and_check();
    test_clean_then_detect_indices();
    std::printf("%d checks, %d failures\n",checks,fails);
    return fails?1:0;
}
