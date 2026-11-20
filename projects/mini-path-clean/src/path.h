#include <stddef.h>
int path_remove_collinear(const float* pts,size_t n,float* out,size_t cap,size_t* outn,float eps);
float path_length(const float* pts,size_t n);
/*
 * 细分式(Chaikin 切角)折线圆滑。
 * 每轮把每个内拐角替换为相邻两段上按比例 ratio 截取的两个点,
 * 开放折线首尾端点保留。点数: n_r = 2 + 2^rounds * (n - 2) (n >= 2)。
 *
 * 约定:
 *  - rounds == 0: 输出与输入逐点相同。
 *  - rounds <  0: 返回 -3, 不写输出。
 *  - n == 1 或 n == 2: 无拐角可切, 输出与输入逐点相同, 返回 0。
 *  - 两段共线的拐角: 照常取截点, 结果仍共线, 长度严格不变。
 *  - ratio 被钳制到 [0, 0.5]; 每个拐角的实际比例还会按到非相邻
 *    线段的净空距离进一步收缩, 保证简单(不自交)折线圆滑后仍不自交。
 *  - 长度只减不增(三角不等式), 即长度变化 <= 0 且 |dL| <= 原长。
 * 返回: 0 成功; -1 参数非法; -2 输出容量不足; -3 rounds 为负。
 */
int path_smooth_chaikin(const float* pts,size_t n,int rounds,float ratio,float* out,size_t cap,size_t* outn);
