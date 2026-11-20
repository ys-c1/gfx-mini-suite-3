# gfx-mini-suite-3

第三批图形与多媒体小项目集合，每个子目录独立可构建，各自带 `make test`。

| 子项目 | 语言 | 内容 |
|---|---|---|
| projects/mini-shadow-cast | C11 | 2D 线段投影与遮挡判定 |
| projects/mini-tile-collide | C11 | 图块碰撞与 AABB 判定 |
| projects/mini-path-clean | C++17 | 折线共线点清理与长度 |
| projects/mini-envelope | Python 3 | 分帧 RMS 包络与亚采样峰值重建（保持/回落、误差上界、降级） |
| projects/mini-oklab-shift | C11 | sRGB 线性化与亮度偏移 |

进入任一子目录执行 `make test` 即可。

mini-envelope 还提供 `make bench`：用三组含尖峰信号评估亚采样重建，
输出 `out/bench_results.csv` 与 `out/error_table.md`（重建误差、峰值保持
时长、定时误差与耗时）。窗口、保持样本数、回落时间常数从 `demo.cfg` 的
`window` / `hold` / `decay_tau` 读取。
