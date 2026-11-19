#ifndef PATH_H
#define PATH_H

#include <stddef.h>

/* 自交检测结果。
 *
 * pairs         报告的非相邻线段对数（exact_pairs + near_pairs）
 * exact_pairs   严格几何相交的线段对数（与 tol 无关）
 * near_pairs    仅因距离 <= tol 而被报告为"近似相交"的线段对数
 * segments      所有被报告线段对中涉及的不同线段条数
 * has_first     是否存在首个报告交点（pairs > 0）
 * first_is_near 首个报告对是否只是近似相交（1）而非严格相交（0）
 * first_*       首个报告交点；近似相交时为最近点对的中点
 * first_distance 首个报告对的两线段最小距离（严格相交为 0）
 * first_i/j     首个报告对的线段下标（按下标升序扫描的第一对）
 * has_exact     是否存在严格交点
 * exact_*       第一个严格交点及其线段下标（即使容差另有近似报告也单独给出）
 */
typedef struct {
    size_t pairs;
    size_t exact_pairs;
    size_t near_pairs;
    size_t segments;
    int has_first;
    int first_is_near;
    float first_x;
    float first_y;
    float first_distance;
    int first_i;
    int first_j;
    int has_exact;
    float exact_x;
    float exact_y;
    int exact_i;
    int exact_j;
} path_isect_result;

int path_remove_collinear(const float* pts,size_t n,float* out,size_t cap,size_t* outn,float eps);
float path_length(const float* pts,size_t n);

/* 检测折线上的自交。
 *
 * 规则：
 *  - 输入为 x0,y0,x1,y1,... 形式的顶点数组，只读，绝不修改坐标。
 *  - 相邻线段（下标相差 1）不参与检测；若首尾顶点完全相等，则视为闭合
 *    折线，首段与末段也视为相邻，检测时跳过接缝。
 *  - 零长度线段在检测前被折叠掉，相邻关系按折叠后的线段序列判定；
 *    报告的线段下标始终是原始下标。
 *  - n == 2 时只有一条线段，结果确定：0 对、无交点。
 *  - 共线线段重叠或端点相接按严格相交处理，交点取重叠区间在第一条
 *    线段上的起点。
 *  - tol <= 0 时只报告严格相交；tol > 0 时，未严格相交但两线段最小
 *    距离 <= tol 的对也会报告为近似相交。近似交点取最近点对的中点。
 *  - "首个"均按 (i, j) 字典序最小的一对确定，扫描顺序固定。
 *
 * 返回 0 成功；-1 参数非法（空指针、n < 2、tol < 0）。
 */
int path_self_intersections(const float* pts,size_t n,float tol,path_isect_result* out);

/* 清理流程：先按 clean_eps 去除共线点，再对清理后的折线做自交检测。
 * 清理结果写入 clean_out，*clean_n 为清理后顶点数。
 * 返回 0 成功；-1 参数非法；-2 clean_out 容量不足；-3 清理结果无法检测。
 */
int path_clean_and_check(const float* pts,size_t n,float clean_eps,float tol,
                         float* clean_out,size_t cap,size_t* clean_n,
                         path_isect_result* out);

#endif
