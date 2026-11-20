# mini-tile-collide

图块碰撞（AABB vs 8px 图块）与可开关的门。C11，无外部依赖。

## 构建与运行

```sh
make test   # 旧碰撞测试 + 门开关测试（共 73 项断言）
make bench  # 64 格 / 256 格两档基准：命中差、回滚误差、切换耗时
make demo   # 读 demo.cfg 跑一次 AABB 判定并写 out/state.bin
```

## 门模块（src/door.h / door.c）

在旧的实心标记 `unsigned char map[]` 之上叠加门表，不改 `tile.c`：

- 关门 → `map[i]=1`，开门 → `map[i]=0`，因此关门时 `tile_solid()` /
  `aabb_vs_tiles()` 的碰撞结果与旧实心格完全相同。
- 三段式：
  1. `doorgrid_apply`：按开关状态更新单个实心标记；
  2. `doorgrid_toggle_batch`：先全量校验再整体落地，写绝对目标态，
     同批顺序无关；
  3. 每批切换前对 `map/state/frame_toggled` 三表打快照，
     `doorgrid_rollback_last()` 或外部 `doorgrid_snapshot/restore` 回滚。
- 同帧重复切换由 `frame_toggled` 记录帧号拦截；错误一律显式返回码，
  校验失败的批次整批不落、无静默。

| 返回码 | 含义 |
|---|---|
| 0 `DOOR_OK` | 成功 |
| -1 `DOOR_E_ARG` | 空指针 / 尺寸非法 / 空批 |
| -2 `DOOR_E_BOUNDS` | 格越界 |
| -3 `DOOR_E_STATE` | 状态不是 0(关)/1(开) |
| -4 `DOOR_E_NOT_DOOR` | 目标格未注册为门 |
| -5 `DOOR_E_DUP_BATCH` | 同一批内同格重复 |
| -6 `DOOR_E_DUP_FRAME` | 同一帧内同格重复切换 |
| -7 `DOOR_E_FRAME` | 帧号为负 |
| -8 `DOOR_E_ALLOC` | 快照内存分配失败 |
| -9 `DOOR_E_NO_SNAPSHOT` | 无检查点可回滚 |
| -10 `DOOR_E_SHAPE` | 快照与网格尺寸不一致 |

状态表的纸笔推导见 `docs/door-state-table.md`，基准读数见
`docs/bench-report.md`。
