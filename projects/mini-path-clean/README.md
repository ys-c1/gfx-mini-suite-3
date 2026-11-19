# mini-path-clean

折线共线点清理、长度计算、以及拐角圆弧化。

## API

```c
int  path_remove_collinear(const float* pts, size_t n, float* out, size_t cap, size_t* outn, float eps);
float path_length(const float* pts, size_t n);

typedef struct {
    float radius;     /* 目标拐角半径；<=0 时原样返回输入折线 */
    float step_deg;   /* 圆弧采样步长（度）；<=0 视为 1e-3 */
    int   closed;     /* 0=开放折线；非 0=闭合（首尾相邻） */
} path_round_opts;

size_t path_round_capacity(size_t n, const path_round_opts* opt);
int    path_corner_round(const float* pts, size_t n, const path_round_opts* opt,
                         float* out, size_t cap, size_t* outn);
float  path_point_distance(const float* pts, size_t n, float px, float py);
```

`out` 可与 `pts` 相同（原地）。容量不够返回 `-2`；参数非法返回 `-1`。

## 圆角化做法

对每个内部顶点（闭合时含首尾）：

- 入方向 `u = normalize(P_i - P_{i-1})`，出方向 `v = normalize(P_{i+1} - P_i)`；
  内角 `alpha = acos(clamp(u·v))`，圆弧张角 `theta = pi - alpha`。
- 理想切线长 `t = r * tan(alpha/2)`。
- **自适应收缩**：`t_eff = min(t, 0.5*L_prev, 0.5*L_next)`，再反算
  `r_eff = t_eff / tan(alpha/2)`。半径只会缩小，不会放大。
- 采样数 `m = max(1, ceil(theta / step))`，写出 `m-1` 个内部点；两个切点
  `T1`、`T2` 与相邻直线段的端点一起写出。内部点用 Slerp 在两条单位径向之间
  插值再乘 `r_eff`，因此**严格落在圆上**（float 精度内）。

### 三种边界输入的确定结果

| 输入 | 结果 |
|---|---|
| `radius == 0` | 原折线逐位原样返回，`*outn == n` |
| `radius < 0` | 与 `radius == 0` 完全相同（逐位） |
| 相邻线段过短 | 该拐角 `t_eff` 收缩到两端线段一半中较小者；若收缩后 `t_eff == 0`（线段长度为零），该拐角保留尖角，不生成圆弧 |

另外：共线输入（`theta ~ 0`）不生成任何新点；严格折返（`theta -> pi`）
圆弧退化为一个点，结果仍与原折线同胚。

## 不自交保证

- 相邻两个拐角共享一条直线段，各自最多占用该段的一半，因此
  剩余直线长度 `L - t_i - t_{i+1} >= 0`，两段圆弧永远不会跨过同一条直线段。
- 每段圆弧是半径 `r_eff` 圆上的一段**凸弧**，且整段弧位于弦 `T1T2` 的同一侧，
  不会越过弦进入相邻区域。
- 直线段只被缩短、不新增交叉。因此只要原折线本身不自交，圆滑后也不会自交。
- 测试里对每条输出折线做 O(n²) 严格线段相交检查（共享端点不算），三组样例
  与 3000 条随机单调折线均为 0 自交。

## 原有功能不变

`path_remove_collinear` 与 `path_length` 的实现**未改动**（圆弧化是新增代码）。
`make bitcheck` 用 vendored 的 c2ff549 版本对拍 20000 条随机折线
（含重复点、共线点、`eps=0`），清理结果的返回码/点数/float 位模式、
长度返回值全部逐位一致。

## 三组样例（radius=2, step=10°）

| 样例 | 顶点数 | 最大偏离原折线 | 反向偏离 | 自交 | 耗时 |
|---|---|---|---|---|---|
| L 形 (3 点) | 3 → 12 | 0.467911 | 0.836038 | 0 | 0.15 µs |
| 锯齿 (13 点, 45°拐角) | 13 → 112 | 0.467912 | 0.836038 | 0 | 1.35 µs |
| 长短边混合 (8 点) | 8 → 62 | 0.233956 | 0.418019 | 0 | 0.70 µs |

- “偏离”＝圆滑后每个顶点到原折线的最大距离。
- “反向偏离”＝原折线每个顶点到圆滑后折线的最大距离（拐角被削掉的最大深度）。

`step=30°` 时三组分别为 3→6、13→46、8→26，耗时 0.10/0.71/0.33 µs。
数据由 `make bench` 现场生成（200 次取最小值）。

## 构建

```
make test      # 单元测试
make bitcheck  # 与旧实现逐位对拍
make bench     # 三组样例报告
make demo      # config 驱动的 CLI 示例
```
