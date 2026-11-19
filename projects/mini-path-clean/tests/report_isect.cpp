/* 自交检测报告工具。
 *
 * 对四组折线输出：顶点数、严格相交段对数、首个交点、
 * 容差 1e-3 下的近似报告数、检测耗时。
 * 另对每组折线扫描一组容差，展示误报（纯近似对）如何随容差变化。
 *
 * 另附三组对照数据：在同一输入上分别用
 *   1. 本工具（新实现）
 *   2. 冻结的基线清理 + 本工具的长度
 *   3. Python 独立参考实现（shapely 缺失时用内置几何）
 * 核对清理结果、长度与几何结论是否一致。
 */
#include "../src/path.h"
#include "ref_baseline.h"
#include <cstdio>
#include <cmath>
#include <chrono>
#include <vector>
#include <cstring>
#include <cstdlib>

static const float kTol=1e-3f;

struct Case { const char* name; std::vector<float> pts; };

static std::vector<Case> cases(){
    return {
        /* 1. 清理掉 (4,0) 与 (4,2) 两个共线点后，s2 仍横穿 s0 */
        {"case1-zigzag-open",
         {0,0, 4,0, 8,0, 8,6, 4,2, 0,-2, 2,-2}},
        /* 2. 清理后自交多次的折线 */
        {"case2-multi-cross",
         {0,0, 10,0, 10,10, 5,-5, 5,15, 0,10}},
        /* 3. 首尾重合的闭合蝴蝶结（接缝不算自交） */
        {"case3-closed-bowtie",
         {0,0, 4,0, 0,4, 4,4, 0,0}},
        /* 4. 清理后近距但不相交（容差敏感性） */
        {"case4-near-miss",
         {0,0, 5,0, 10,0, 10,0.5, 0,0.2}},
    };
}

static const int kReps=20000;

static void report_case(const Case& c,std::vector<float>& cleaned,size_t& cn){
    path_isect_result r;
    int rc=0;
    /* 清理 + 检测，取 kReps 次重复的单次平均耗时 */
    auto t0=std::chrono::steady_clock::now();
    for(int k=0;k<kReps;++k)
        rc=path_clean_and_check(c.pts.data(),c.pts.size()/2,1e-3f,kTol,
                                cleaned.data(),cleaned.size(),&cn,&r);
    auto t1=std::chrono::steady_clock::now();
    double ms=std::chrono::duration<double,std::milli>(t1-t0).count()/kReps;

    std::printf("%s\n",c.name);
    std::printf("  vertices_raw=%zu vertices_clean=%zu rc=%d\n",
                c.pts.size()/2,cn,rc);
    std::printf("  intersecting_segments=%zu exact_pairs=%zu near_pairs=%zu tol=%g\n",
                r.segments,r.exact_pairs,r.near_pairs,(double)kTol);
    if(r.has_first){
        std::printf("  first_intersection=(%.6f, %.6f) between segments %d and %d%s\n",
                    (double)r.first_x,(double)r.first_y,r.first_i,r.first_j,
                    r.first_is_near?" (near)":"");
    }else{
        std::printf("  first_intersection=none\n");
    }
    if(r.has_exact){
        std::printf("  first_exact=(%.6f, %.6f) between segments %d and %d\n",
                    (double)r.exact_x,(double)r.exact_y,r.exact_i,r.exact_j);
    }
    std::printf("  time_us_per_call=%.3f (%d reps)\n",ms*1000.0,kReps);
}

static void tolerance_sweep(){
    const float tols[]={0.0f,0.05f,0.1f,0.2f,0.5f,1.0f,2.0f,5.0f};
    std::printf("\ntolerance_sweep (false positives = near_pairs, exact unchanged)\n");
    std::printf("%-22s %6s %8s %7s %7s %16s\n",
                "case","tol","exact","near","segments","time_us_per_call");
    for(const Case& c : cases()){
        std::vector<float> cleaned(c.pts.size()+2);
        size_t cn=0;
        for(float tol : tols){
            path_isect_result r;
            std::vector<float> buf(c.pts.size()+2);
            auto t0=std::chrono::steady_clock::now();
            for(int k=0;k<kReps;++k)
                path_clean_and_check(c.pts.data(),c.pts.size()/2,1e-3f,tol,
                                     buf.data(),buf.size(),&cn,&r);
            auto t1=std::chrono::steady_clock::now();
            double ms=std::chrono::duration<double,std::milli>(t1-t0).count()/kReps;
            std::printf("%-22s %6.3f %8zu %7zu %7zu %16.3f\n",
                        c.name,(double)tol,r.exact_pairs,r.near_pairs,r.segments,ms*1000.0);
        }
    }
}

/* 三组对照数据：清理结果、长度、检测计数三方核对。 */
static void crosscheck(){
    std::printf("\ncrosscheck vs frozen baseline and independent reference\n");
    std::printf("%-22s %8s %8s %10s %10s %s\n",
                "case","n_raw","n_clean","len_new","len_ref","clean_bitwise / length_bitwise");
    for(const Case& c : cases()){
        size_t n=c.pts.size()/2;
        std::vector<float> a(n*2+2),b(n*2+2);
        size_t na=0,nb=0;
        int ra=path_remove_collinear(c.pts.data(),n,a.data(),n+1,&na,1e-3f);
        int rb=ref_remove_collinear(c.pts.data(),n,b.data(),n+1,&nb,1e-3f);
        int clean_same=(ra==rb&&na==nb&&std::memcmp(a.data(),b.data(),nb*2*sizeof(float))==0);
        float la=path_length(c.pts.data(),n);
        float lb=ref_length(c.pts.data(),n);
        int len_same=(la==lb);
        std::printf("%-22s %8zu %8zu %10.4f %10.4f %s / %s\n",
                    c.name,n,na,(double)la,(double)lb,
                    clean_same?"identical":"DIFF",len_same?"identical":"DIFF");
    }
}

/* 机器可读输出，供 compare_three.sh 与 Python 精确参考比对。 */
static void json_out(){
    std::printf("[\n");
    const auto& cs=cases();
    for(size_t k=0;k<cs.size();++k){
        const Case& c=cs[k];
        size_t n=c.pts.size()/2;
        std::vector<float> out(n*2+2);
        size_t cn=0;
        path_isect_result r;
        path_clean_and_check(c.pts.data(),n,1e-3f,kTol,out.data(),n+1,&cn,&r);
        std::printf("  {\"name\":\"%s\",\"n_clean\":%zu,\"length\":%.9g,"
                    "\"pairs\":%zu,\"exact_pairs\":%zu,\"near_pairs\":%zu,"
                    "\"segments\":%zu,\"has_first\":%d,\"first_x\":%.9g,"
                    "\"first_y\":%.9g,\"first_i\":%d,\"first_j\":%d,"
                    "\"first_is_near\":%d,\"first_distance\":%.9g}%s\n",
                    c.name,cn,(double)path_length(c.pts.data(),n),
                    r.pairs,r.exact_pairs,r.near_pairs,r.segments,
                    r.has_first,(double)r.first_x,(double)r.first_y,
                    r.first_i,r.first_j,r.first_is_near,(double)r.first_distance,
                    k+1<cs.size()?",":"");
    }
    std::printf("]\n");
}

/* 三种退化输入的确定结果。 */
static void degenerate(){
    struct D { const char* name; std::vector<float> pts; float tol; };
    std::vector<D> ds={
        {"two-vertices",        {1,2, 7,9},            0.0f},
        {"all-collinear",       {0,0, 1,0, 2,0, 3,0},  0.0f},
        {"first-eq-last-closed",{0,0, 4,0, 4,4, 0,4, 0,0}, 0.0f},
    };
    std::printf("\ndegenerate inputs (tol=0, no approximation)\n");
    std::printf("%-22s %6s %6s %7s %8s %s\n",
                "input","pairs","exact","near","segments","first");
    for(const D& d : ds){
        path_isect_result r;
        int rc=path_self_intersections(d.pts.data(),d.pts.size()/2,d.tol,&r);
        std::printf("%-22s %6zu %6zu %7zu %8zu ",d.name,r.pairs,r.exact_pairs,
                    r.near_pairs,r.segments);
        if(r.has_first) std::printf("(%.6f,%.6f) seg(%d,%d) rc=%d\n",
                                    (double)r.first_x,(double)r.first_y,
                                    r.first_i,r.first_j,rc);
        else std::printf("none rc=%d\n",rc);
    }
}

int main(int argc,char** argv){
    if(argc>1&&std::strcmp(argv[1],"--json")==0){ json_out(); return 0; }
    if(argc>1&&std::strcmp(argv[1],"--sweep")==0){ tolerance_sweep(); return 0; }
    if(argc>1&&std::strcmp(argv[1],"--crosscheck")==0){ crosscheck(); return 0; }

    std::printf("self-intersection report (clean then check)\n");
    std::vector<float> cleaned;
    for(const Case& c : cases()){
        cleaned.assign(c.pts.size()+2,0.0f);
        size_t cn=0;
        report_case(c,cleaned,cn);
    }
    degenerate();
    tolerance_sweep();
    crosscheck();
    return 0;
}
