#include "step.h"

#define TILE 8.0f

void step_stats_reset(StepStats* st) {
    if (st) { st->lifts = 0; st->max_lift = 0.0f; }
}

/* 仅值 1 视为实心（整格碰撞）；值 2 单向平台不阻挡水平移动。 */
static int box_hits_solid(const unsigned char* map, int w, int h,
                          float x, float y, float bw, float bh) {
    if (!map) return 0;
    int x0 = (int)(x / TILE), y0 = (int)(y / TILE);
    int x1 = (int)((x + bw - 1e-3f) / TILE), y1 = (int)((y + bh - 1e-3f) / TILE);
    for (int ty = y0; ty <= y1; ++ty)
        for (int tx = x0; tx <= x1; ++tx) {
            if (tx < 0 || ty < 0 || tx >= w || ty >= h) continue;
            if (map[ty * w + tx] == 1) return 1;
        }
    return 0;
}

int step_box_in_solid(const unsigned char* map, int w, int h,
                      float x, float y, float bw, float bh) {
    return box_hits_solid(map, w, h, x, y, bw, bh);
}

/* 支撑判定：脚下 1px 内有实心格，或脚底与单向平台顶对齐（下落 1px 会穿过平台顶）。 */
static int box_supported(const unsigned char* map, int w, int h,
                         float x, float y, float bw, float bh) {
    if (box_hits_solid(map, w, h, x, y + 1.0f, bw, bh)) return 1;
    float bottom = y + bh;
    int ty = (int)(bottom / TILE);
    int x0 = (int)(x / TILE), x1 = (int)((x + bw - 1e-3f) / TILE);
    if (ty < 0 || ty >= h) return 0;
    for (int tx = x0; tx <= x1; ++tx) {
        if (tx < 0 || tx >= w) continue;
        if (map[ty * w + tx] == 2) {
            float top = ty * TILE;
            if (bottom <= top + 1e-3f && bottom + 1.0f > top + 1e-3f) return 1;
        }
    }
    return 0;
}

int step_move_x(const unsigned char* map, int w, int h,
                float* px, float* py, float bw, float bh,
                float dx, float max_step, StepStats* st) {
    if (!map || !px || !py || dx == 0.0f) return 0;
    float x = *px, y = *py;
    float nx = x + dx;
    if (!box_hits_solid(map, w, h, nx, y, bw, bh)) { *px = nx; return 0; }
    if (max_step >= 1.0f) {
        int maxl = (int)max_step;
        for (int L = 1; L <= maxl; ++L) {
            float ly = y - (float)L;
            /* 抬升位被头顶实心挡住：更高抬升必然穿过该格，终止搜索（防穿墙） */
            if (box_hits_solid(map, w, h, x, ly, bw, bh)) break;
            /* 抬升后重新检测：水平目标位不得嵌入墙体 */
            if (box_hits_solid(map, w, h, nx, ly, bw, bh)) continue;
            /* 落点搜索：自抬升高度向下逐像素找第一个被支撑的位置，深度 <= L */
            for (int d = 0; d <= L; ++d) {
                float yc = ly + (float)d;
                if (box_hits_solid(map, w, h, nx, yc, bw, bh)) break;
                if (box_supported(map, w, h, nx, yc, bw, bh)) {
                    *px = nx; *py = yc;
                    if (st) {
                        st->lifts++;
                        float lift = y - yc;
                        if (lift > st->max_lift) st->max_lift = lift;
                    }
                    return 1;
                }
            }
        }
    }
    /* 无有效抬升：撞停，贴到墙边 */
    float dir = dx > 0.0f ? 1.0f : -1.0f;
    float cx = x;
    while ((dir > 0.0f ? cx + dir <= nx : cx + dir >= nx) &&
           !box_hits_solid(map, w, h, cx + dir, y, bw, bh)) cx += dir;
    *px = cx;
    return 0;
}
