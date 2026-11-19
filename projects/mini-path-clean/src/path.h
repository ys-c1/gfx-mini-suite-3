#ifndef PATH_H
#define PATH_H
#include <stddef.h>

/* 共线点清理。原实现，输出必须逐位保持不变。 */
int path_remove_collinear(const float* pts, size_t n, float* out, size_t cap, size_t* outn, float eps);
float path_length(const float* pts, size_t n);

/* 圆角化参数。 */
typedef struct {
    float radius;     /* 目标拐角半径，<=0 时直接返回原折线（0 个拐角） */
    float step_deg;   /* 圆弧采样步长（度），<=0 视为 1e-3（超细，仅限小输入） */
    int   closed;     /* 0=开放折线；非 0=闭合（首尾视为相邻） */
} path_round_opts;

/* 圆角化所需输出容量上限（多变元逐点圆弧，保守上界）。cap 不够时 -2。 */
size_t path_round_capacity(size_t n, const path_round_opts* opt);

/* 圆角化。out 与 pts 可相同（in-place）。失败 -1/-2/-3。 */
int path_corner_round(const float* pts, size_t n, const path_round_opts* opt,
                      float* out, size_t cap, size_t* outn);

/* 最近距离：点 p 到折线 pts（逐段投影）。 */
float path_point_distance(const float* pts, size_t n, float px, float py);

#endif
