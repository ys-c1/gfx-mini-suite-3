# gfx-mini-suite-3

第三批图形与多媒体小项目集合，每个子目录独立可构建，各自带 `make test`。

| 子项目 | 语言 | 内容 |
|---|---|---|
| projects/mini-shadow-cast | C11 | 2D 线段投影与遮挡判定 |
| projects/mini-tile-collide | C11 | 图块碰撞与 AABB 判定 |
| projects/mini-path-clean | C++17 | 折线共线点清理与长度 |
| projects/mini-envelope | Python 3 | 分帧 RMS 包络 |
| projects/mini-oklab-shift | C11 | sRGB 线性化与亮度偏移 |

进入任一子目录执行 `make test` 即可。

## mini-shadow-cast 多光源合成

每盏灯各自对同一遮挡线段做 2D 投影与遮挡判定，逐点线性加权合成亮度：

    B = Σ(lit_i · max(0, w_i)) / Σ max(0, w_i)

- 权重和为 1 时，所有灯都遮挡的全背光区 B=0（全暗）；重叠照亮区 B=1，
  不会出现相乘式重复变暗或接缝。
- 灯数与权重走配置（`light_count`、`light_x_i/light_y_i/light_w_i`）。
  不含 `light_count` 的旧配置走原单灯路径，`out/state.bin`（GSF1）逐字节不变；
  多灯模式写 GSF2 状态，含合成亮度统计（均值/极值/点亮数/钳制与重合计数），
  不含计时数据，重复运行结果确定。
- 边界：零灯 → 全黑；负权重 → 钳为 0 并在状态中计数；重合灯 → 结果与单灯
  一致并计数。配置示例见 `projects/mini-shadow-cast/tests/configs/`，
  `make bench` 输出三组布光耗时。
