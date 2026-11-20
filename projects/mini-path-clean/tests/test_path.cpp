#include "../src/path.h"
#include <cstdio>
#include <cmath>
#include <vector>

static int fails=0,checks=0;
#define CHECK(c) do{++checks; if(!(c)){++fails;std::printf("FAIL %d %s\n",__LINE__,#c);}}while(0)

/* Helper: build a polyline from x,y pairs. */
static std::vector<float> poly(std::initializer_list<double> v) {
    std::vector<float> out;
    for (double d : v) out.push_back((float)d);
    return out;
}

static void test_existing_behaviour() {
    float pts[8]={0,0, 5,0, 10,0, 10,5};
    float out[16]; size_t n=0;
    CHECK(path_remove_collinear(pts,4,out,8,&n,1e-3f)==0);
    CHECK(n==3);
    CHECK(out[2]==10.0f&&out[3]==0.0f);
    CHECK(std::fabs(path_length(pts,4)-15.0f)<1e-4f);
    CHECK(path_remove_collinear(pts,4,out,1,&n,1e-3f)==-2);
}

/* Target zero: the caller allowed no points.  The resampler reports the
 * minimum a representation needs rather than claiming success with
 * nothing drawn (deviation would be unbounded, or infinite for n==0). */
static void test_target_zero() {
    auto p = poly({0,0, 10,0});
    float out[16]; size_t n=99;
    CHECK(path_resample(p.data(),2,out,8,0,0.5f,&n)==-3);
    CHECK(n==2);
    /* A single vertex would need exactly one point. */
    float one[2]={1.0f,2.0f};
    CHECK(path_resample(one,1,out,8,0,0.5f,&n)==-3);
    CHECK(n==1);
    /* Degenerate input with target zero is still an empty success. */
    CHECK(path_resample(nullptr,0,out,8,0,0.5f,&n)==0 && n==0);
}

/* A single vertex is copied through. */
static void test_single_vertex() {
    float p[2]={3.0f,-4.0f};
    float out[2]={0,0}; size_t n=0;
    CHECK(path_resample(p,1,out,1,64,0.5f,&n)==0);
    CHECK(n==1 && out[0]==3.0f && out[1]==-4.0f);
    float dev = path_max_deviation(p,1,out,n);
    CHECK(dev==0.0f);
}

/* Perfectly straight input: two endpoints, zero deviation, any target >= 2. */
static void test_straight_line() {
    std::vector<float> p;
    for (int i=0;i<=1000;++i) { p.push_back((float)i); p.push_back(0.0f); }
    std::vector<float> out(4000);
    size_t n=0;
    CHECK(path_resample(p.data(),1001,out.data(),2000,64,0.05f,&n)==0);
    CHECK(n==2);
    CHECK(out[0]==0.0f && out[1]==0.0f);
    CHECK(out[2]==1000.0f && out[3]==0.0f);
    CHECK(path_max_deviation(p.data(),1001,out.data(),n)==0.0f);
    CHECK(path_has_self_intersection(out.data(),n)==0);
}

/* Bends get more points than straight runs: density follows curvature. */
static void test_curvature_density() {
    /* 400 points along x (straight), then a half circle of radius 20 with
     * 400 points, then 400 more straight points. */
    std::vector<float> p;
    for (int i=0;i<400;++i) { p.push_back((float)i*0.25f); p.push_back(0.0f); }
    float cx=100.0f+25.0f, cy=0.0f;
    for (int i=1;i<=400;++i) {
        double t = M_PI * i / 400.0;
        p.push_back((float)(cx - 25.0*std::cos(t)));
        p.push_back((float)(cy + 25.0*std::sin(t)));
    }
    float ex=p.back(), ey=p.back(); (void)ex; (void)ey;
    for (int i=1;i<=400;++i) { p.push_back(100.0f+50.0f+(float)i*0.25f); p.push_back(0.0f); }
    size_t m_in = p.size()/2;

    std::vector<float> out(p.size()), idx(p.size()/2);
    size_t m=0;
    CHECK(path_resample(p.data(),m_in,out.data(),p.size(),1000,0.05f,&m)==0);
    CHECK(m<=1000);
    CHECK(path_has_self_intersection(out.data(),m)==0);
    float dev=path_max_deviation(p.data(),m_in,out.data(),m);
    CHECK(dev<=0.05f+1e-5f);

    /* Count output vertices that lie on the arc (x in [-?], arc spans
     * x in [100,150]) versus on the straight tail beyond x>150. */
    int arc_pts=0, tail_pts=0;
    for (size_t k=0;k<m;++k) {
        float x=out[k*2], y=out[k*2+1];
        if (x>150.5f && y>-0.001f && y<0.001f) ++tail_pts;
        else if (x>=100.0f && x<=150.0f && y>=-0.001f) ++arc_pts;
    }
    /* Arc length is ~78.5 over 400 input samples; the two straight runs are
     * ~100 long each.  The arc must be sampled far more densely per unit
     * length than the straight tail. */
    CHECK(arc_pts>=3);
    CHECK(tail_pts<=1);
    (void)idx;
}

/* A tight budget that cannot meet max_dev reports the required count. */
static void test_budget_too_small() {
    std::vector<float> p;
    for (int i=0;i<=100;++i) {
        double t = 2.0*M_PI*i/100.0;
        p.push_back((float)(10.0*std::cos(t)));
        p.push_back((float)(10.0*std::sin(t)));
    }
    float out[400]; size_t n=0;
    int rc=path_resample(p.data(),101,out,200,4,0.001f,&n);
    CHECK(rc==-3);
    CHECK(n>4);
    /* Same request with a sufficient budget succeeds and respects both
     * bounds. */
    std::vector<float> big(800);
    size_t m=0;
    CHECK(path_resample(p.data(),101,big.data(),400,n,0.001f,&m)==0);
    CHECK(m==n);
    CHECK(m<=n);
    CHECK(path_max_deviation(p.data(),101,big.data(),m)<=0.001f+1e-6f);
}

/* Degenerate targets and arguments. */
static void test_degenerate() {
    auto p = poly({0,0, 10,0});
    float out[8]; size_t n=7;
    CHECK(path_resample(p.data(),2,out,8,1,1.0f,&n)==-3);   /* one point cannot hold a segment */
    CHECK(n==2);
    CHECK(path_resample(p.data(),2,out,1,4,1.0f,&n)==-2);   /* cap too small */
    CHECK(path_resample(p.data(),2,out,8,4,-1.0f,&n)==-1);  /* negative tolerance */
    CHECK(path_resample(nullptr,0,out,8,4,1.0f,&n)==0 && n==0);
    CHECK(path_resample(nullptr,2,out,8,4,1.0f,&n)==-1);

    float one=1.0f;
    CHECK(path_resample(&one,1,out,0,1,1.0f,&n)==-2);       /* output cap too small */
    CHECK(path_resample(&one,1,out,1,1,1.0f,&n)==0 && n==1);
}

/* Simplification must not introduce a self-intersection. */
static void test_no_new_self_intersection() {
    /* Spiral: DP across tight inner turns can cut across the turns. */
    std::vector<float> p;
    const int turns=2400;
    for (int i=0;i<turns;++i) {
        double t=i*0.02;
        double r=1.0+0.35*t;
        p.push_back((float)(r*std::cos(t)));
        p.push_back((float)(r*std::sin(t)));
    }
    size_t m_in=p.size()/2;
    CHECK(path_has_self_intersection(p.data(),m_in)==0);

    std::vector<float> out(p.size());
    size_t m=0;
    int rc=path_resample(p.data(),m_in,out.data(),p.size(),m_in,0.02f,&m);
    CHECK(rc==0);
    CHECK(path_has_self_intersection(out.data(),m)==0);
    CHECK(path_max_deviation(p.data(),m_in,out.data(),m)<=0.02f+1e-5f);
}

/* A plain Douglas-Peucker pass over this short simple path crosses itself;
 * the repair step has to put a vertex back.  With only the DP count
 * available the request must be refused rather than return a crossing. */
static void test_repair_required() {
    float pts[]={0,0, 0,-2, 0,1, 1,2, 3,2, 4,3, 6,3, 2,-1, 3,-2, 2,-1,
                 3,-2, 4,-3, 0,-7, -3,-10, -3,-12, -7,-16, -3,-20, -5,-20,
                 -7,-22, -5,-20, -5,-23, -5,-27, -1,-27, -1,-23, 1,-25, 5,-25};
    const size_t n=sizeof(pts)/sizeof(pts[0])/2;
    CHECK(path_has_self_intersection(pts,n)==0);

    std::vector<float> out(64);
    size_t idx[64], m=0;

    /* DP alone needs 15; the repaired result needs 16. */
    int rc=path_resample_map(pts,n,out.data(),32,15,1.2f,&m,idx);
    CHECK(rc==-5);

    rc=path_resample_map(pts,n,out.data(),32,16,1.2f,&m,idx);
    CHECK(rc==0);
    CHECK(m==16);
    CHECK(path_has_self_intersection(out.data(),m)==0);
    CHECK(path_max_deviation(pts,n,out.data(),m)<=1.2f+1e-6f);
    /* The inserted vertex (input index 17) is present. */
    bool has17=false;
    for(size_t k=0;k<m;++k) if(idx[k]==17) has17=true;
    CHECK(has17);
}

/* Resampling with a small tolerance never loses the end points and never
 * exceeds the caller's target. */
static void test_endpoints_and_target() {
    std::vector<float> p;
    for (int i=0;i<=200;++i) {
        double t=2.0*M_PI*i/200.0;
        p.push_back((float)(5.0*std::cos(t)));
        p.push_back((float)(5.0*std::sin(t)));
    }
    std::vector<float> out(500);
    size_t m=0;
    CHECK(path_resample(p.data(),201,out.data(),250,40,0.05f,&m)==0);
    CHECK(m<=40);
    CHECK(out[0]==p[0] && out[1]==p[1]);
    CHECK(out[(m-1)*2]==p[400] && out[(m-1)*2+1]==p[401]);
}

int main(){
    test_existing_behaviour();
    test_target_zero();
    test_single_vertex();
    test_straight_line();
    test_curvature_density();
    test_budget_too_small();
    test_degenerate();
    test_no_new_self_intersection();
    test_repair_required();
    test_endpoints_and_target();
    std::printf("%d checks, %d failures\n",checks,fails);
    return fails?1:0;
}
