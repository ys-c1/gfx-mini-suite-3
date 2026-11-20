# gfx-mini-suite-3

第三批图形与多媒体小项目集合，每个子目录独立可构建，各自带 `make test`。

| 子项目 | 语言 | 内容 |
|---|---|---|
| projects/mini-shadow-cast | C11 | 2D 线段投影与遮挡判定 |
| projects/mini-tile-collide | C11 | 图块碰撞与 AABB 判定 |
| projects/mini-path-clean | C++17 | 折线共线点清理与长度 |
| projects/mini-envelope | Python 3 | 分帧 RMS 包络 |
| projects/mini-oklab-shift | C11 | sRGB 线性化、亮度偏移与参考白色适应 |

进入任一子目录执行 `make test` 即可。

## mini-oklab-shift 参考白色适应

冷暖光下灰拍出来偏色，手工调增益会带偏其他颜色。本项目在原有亮度调整路径
（`oklab.c::brightness_shift`，保持不变）之外新增 `adapt.c`，实现基于参考白的
von Kries 色适应：

1. sRGB 解码到**线性光 RGB 适应空间**；
2. 按配置中的场景参考白做三通道对角缩放，参考白映射为等比 D65 白；
3. 重新编码回 sRGB。

性质与约束：

- 中性严格保持：等比灰 `(g,g,g)` 经全强度适应后仍是等比灰（浮点域严格相等，
  8bit 域仅量化误差，实测通道极差 ≤2 LSB）；
- 色相偏移有上界 `ADAPT_HUE_BOUND_DEG`（默认 10°），单测与基准均强制校验；
- 参考白非法（全零、通道越界、非有限值）或强度不在 `[0,1]` 时返回负错误码，
  CLI 拒绝执行；
- `strength=0` 时增益全为 1，`apply` 直接 `memcpy`，逐值原样还原；
- 参考白与强度均从配置读入（`ref_white=r,g,b`、`strength`），见 `cfg/` 与
  `demo.cfg`。

`make bench` 在 2700K 暖光 / 6500K 日光 / 9000K 冷光三组光线下，用 24 色
ColorChecker 合成偏色照片，输出：

- `out/bench_report.txt`：每组光线的 ΔE76 均值/最大、色相最大偏移、中性灰偏差、
  配置参考白与物理计算值的漂移、耗时；
- `out/color_diff.csv`：24 色逐块色差表（含加偏/适应后的 RGB、ΔE、色相偏移）。
