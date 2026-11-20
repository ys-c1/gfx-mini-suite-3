#define _POSIX_C_SOURCE 199309L
#include "../src/tile.h"
#include "../src/step.h"
#include <stdio.h>
#include <time.h>

static int fails = 0, checks = 0;
#define CHECK(c) do{++checks; if(!(c)){++fails;printf("FAIL %d %s\n",__LINE__,#c);}}while(0)

enum { W = 16, H = 16 };

static double now_us(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}

static void floor_row(unsigned char* m) { for (int tx = 0; tx < W; ++tx) m[15 * W + tx] = 1; }

int main(void) {
    /* 组 1：四级楼梯，阈值 8 —— 每级抬升 8px，全部爬上 */
    {
        unsigned char m[W * H] = {0}; floor_row(m);
        for (int i = 0; i < 4; ++i) for (int ty = 14 - i; ty <= 14; ++ty) m[ty * W + (5 + i)] = 1;
        float x = 8.0f, y = 112.0f; StepStats st; step_stats_reset(&st);
        double t0 = now_us();
        for (int f = 0; f < 12; ++f) step_move_x(m, W, H, &x, &y, 8, 8, 8.0f, 8.0f, &st);
        double us = now_us() - t0;
        printf("group1 stairs      : lifts=%d max_lift=%.1f time_us=%.1f\n", st.lifts, st.max_lift, us);
        CHECK(st.lifts == 4);
        CHECK(st.max_lift == 8.0f);
        CHECK(y == 80.0f);
        CHECK(x == 104.0f);
        CHECK(step_box_in_solid(m, W, H, x, y, 8, 8) == 0);
    }
    /* 组 2：阈值为零 —— 关闭自动上台阶，确定结果：撞停在墙边，0 次抬升 */
    {
        unsigned char m[W * H] = {0}; floor_row(m);
        m[14 * W + 5] = 1;
        float x = 8.0f, y = 112.0f; StepStats st; step_stats_reset(&st);
        double t0 = now_us();
        for (int f = 0; f < 12; ++f) step_move_x(m, W, H, &x, &y, 8, 8, 8.0f, 0.0f, &st);
        double us = now_us() - t0;
        printf("group2 zero_thresh : lifts=%d max_lift=%.1f time_us=%.1f\n", st.lifts, st.max_lift, us);
        CHECK(st.lifts == 0);
        CHECK(st.max_lift == 0.0f);
        CHECK(x == 32.0f && y == 112.0f);
    }
    /* 组 3：阈值过高(64) 对 9 格高墙 —— 抬升后重检测拦截，确定结果：
       任何一帧都不嵌入墙体，0 次抬升，撞停（朴素实现会在 L=64 时穿进墙里） */
    {
        unsigned char m[W * H] = {0}; floor_row(m);
        for (int ty = 6; ty <= 14; ++ty) m[ty * W + 5] = 1;
        float x = 8.0f, y = 112.0f; StepStats st; step_stats_reset(&st);
        double t0 = now_us();
        for (int f = 0; f < 12; ++f) {
            step_move_x(m, W, H, &x, &y, 8, 8, 8.0f, 64.0f, &st);
            CHECK(step_box_in_solid(m, W, H, x, y, 8, 8) == 0);
        }
        double us = now_us() - t0;
        printf("group3 high_thresh : lifts=%d max_lift=%.1f time_us=%.1f\n", st.lifts, st.max_lift, us);
        CHECK(st.lifts == 0);
        CHECK(x == 32.0f && y == 112.0f);
    }
    /* 组 4：台阶上方有实心格 —— 抬升路径被头顶挡住，确定结果：0 次抬升，撞停 */
    {
        unsigned char m[W * H] = {0}; floor_row(m);
        m[14 * W + 5] = 1; m[13 * W + 5] = 1;
        float x = 8.0f, y = 112.0f; StepStats st; step_stats_reset(&st);
        double t0 = now_us();
        for (int f = 0; f < 12; ++f) step_move_x(m, W, H, &x, &y, 8, 8, 8.0f, 8.0f, &st);
        double us = now_us() - t0;
        printf("group4 low_ceiling : lifts=%d max_lift=%.1f time_us=%.1f\n", st.lifts, st.max_lift, us);
        CHECK(st.lifts == 0);
        CHECK(x == 32.0f && y == 112.0f);
        CHECK(step_box_in_solid(m, W, H, x, y, 8, 8) == 0);
    }
    /* 单向平台：水平通行不阻挡、不触发抬升；原有 aabb_vs_tiles 判定不变 */
    {
        unsigned char m[W * H] = {0}; floor_row(m);
        m[14 * W + 5] = 2;
        float x = 8.0f, y = 112.0f; StepStats st; step_stats_reset(&st);
        for (int f = 0; f < 8; ++f) step_move_x(m, W, H, &x, &y, 8, 8, 8.0f, 8.0f, &st);
        CHECK(st.lifts == 0);
        CHECK(x == 72.0f);
        int tx = -1, ty = -1;
        CHECK(aabb_vs_tiles(m, W, H, 40, 112, 8, 8, &tx, &ty) == 1); /* 旧函数仍把非零当实心 */
        CHECK(tx == 5 && ty == 14);
    }
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
