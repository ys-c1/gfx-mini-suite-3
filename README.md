# gfx-mini-suite-3

第三批图形与多媒体小项目集合，每个子目录独立可构建，各自带 `make test`。

| 子项目 | 语言 | 内容 |
|---|---|---|
| projects/mini-shadow-cast | C11 | 2D 线段投影与遮挡判定 |
| projects/mini-tile-collide | C11 | 图块碰撞与 AABB 判定 |
| projects/mini-path-clean | C++17 | 折线共线点清理、长度与自交检测 |
| projects/mini-envelope | Python 3 | 分帧 RMS 包络 |
| projects/mini-oklab-shift | C11 | sRGB 线性化与亮度偏移 |

进入任一子目录执行 `make test` 即可。
