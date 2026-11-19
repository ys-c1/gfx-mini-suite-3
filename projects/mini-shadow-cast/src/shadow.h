int shadow_project(float x1,float y1,float x2,float y2,float lx,float ly,float reach,float* ox1,float* oy1,float* ox2,float* oy2);
int shadow_point_lit(float px,float py,float lx,float ly,const float* segs,int nseg);

/* 合并相邻、共线的遮挡段为一条轮廓，使重叠区只投影一次。
 * segs: 输入，nseg*4 个 float，每段 (x1,y1,x2,y2)。
 * out:  调用方缓冲区，容量 out_cap*4 个 float，必须 out_cap>=nseg。
 * tol:  合并容差（距离单位）。端点间距与线间垂直距离不超过 tol 且方向
 *       共线（固定角阈值 1e-6 rad，不随 tol 放宽）的段才会合并。
 * 返回合并后的段数（>=0），参数非法返回 -1。
 * 确定性约定：
 *  - nseg==0            -> 返回 0，out 不改动；
 *  - nseg==1            -> 原样拷贝（端点浮点位级一致），返回 1；
 *  - tol==0             -> 仅精确共线且精确相接/重叠的段合并；
 *  - tol 为 NaN 或 <0   -> 返回 -1；
 *  - tol 过大           -> 所有共线段并成一条跨极值端的段（轮廓退化），
 *                          非共线段任意 tol 下都不合并，结果确定；
 *  - 零长度段           -> 视为退化，一律丢弃。 */
int shadow_merge_segments(const float* segs,int nseg,float tol,float* out,int out_cap);

/* 统计覆盖某点的遮挡段数（0=完全受光）。segs 为 NULL 或 nseg<0 返回 -1，
 * nseg==0 返回 0。重叠区 hits>=2，即“影子更深”的来源。 */
int shadow_hit_count(float px,float py,float lx,float ly,const float* segs,int nseg);
