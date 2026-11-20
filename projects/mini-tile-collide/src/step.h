#ifndef STEP_H
#define STEP_H

/* 自动上台阶移动层。不改动 tile.c 的整格碰撞判定，在其上提供水平移动 + 抬升。
   图块约定：0 空，1 实心（整格碰撞），2 单向平台（仅承接下落，不阻挡水平移动）。 */

typedef struct {
    int lifts;      /* 抬升次数 */
    float max_lift; /* 最大单帧抬升（像素） */
} StepStats;

void step_stats_reset(StepStats* st);

/* 水平移动 dx，被实心格挡住时按 [1, floor(max_step)] 逐像素搜索抬升量：
   - 抬升阈值 max_step 来自配置；<= 0 时退化为原来的撞停；
   - 抬升后必须重新检测：抬升位 (x, y-L) 与水平目标位 (x+dx, y-L) 都不得嵌入实心格，
     抬升路径被头顶实心挡住即终止搜索（防止阈值过高时穿墙）；
   - 落点搜索规则：自抬升高度向下逐像素扫描，取第一个“被支撑”的位置
     （脚下 1px 内有实心格，或脚底与单向平台顶对齐），扫描深度不超过 L，
     找不到落点则该 L 无效，继续尝试更大的 L；
   - 所有 L 都失败时撞停，贴到墙边。
   返回 1 表示本帧发生了抬升。 */
int step_move_x(const unsigned char* map, int w, int h,
                float* px, float* py, float bw, float bh,
                float dx, float max_step, StepStats* st);

/* 查询：某位置是否嵌入实心格（供测试与调试断言）。 */
int step_box_in_solid(const unsigned char* map, int w, int h,
                      float x, float y, float bw, float bh);

#endif
