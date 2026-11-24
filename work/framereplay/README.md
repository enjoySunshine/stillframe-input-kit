# framereplay — 帧索引、确定性的输入/手势静帧回放

解决导演停在某一帧反复核对时，**窗口画面与内存画面不一致、按住时长跟着墙钟走、
重看静帧会冒出新按下/增量变化、长回放帧号漂移**的问题。

核心思路：不再依赖 raylib 那套“按循环次数推进 + 墙钟计时 + 硬件回调喂手势”的实时模型，
而是把整段素材存成**按帧号索引的不可变时间线**。所有可显示的量（按下边缘、指针增量、
按住毫秒、手势、捏合距离/角度）都只从帧自身的时间戳和相邻帧数据计算一次并固化，
读取任何一帧都是纯函数查询，没有 `GetTime()`、没有实时输入轮询、没有可变的重读状态。

## 三个根因与对应处理

| 现象（你描述的） | 根因 | 本模块的处理 |
| --- | --- | --- |
| 按住秒数变成盯着画面发呆的墙钟 | `rgestures.h` 的 `GetGestureHoldDuration()` 用 `GetTime()` 实时墙钟 | 按住时长由帧时间戳差值派生（`FrameSample.holdMs`），静帧重读数值恒定 |
| 内存画面灌不进键盘/鼠标键，位置写了也不变按住/拖动 | 桌面端鼠标→手势的转换在 GLFW 硬件回调里；`PlayAutomationEvent` 只改 `CORE.Input`，从不调用 `ProcessGestureEvent`；内存后端无输入回调 | 回放不经过实时输入/回调，直接以帧数据驱动；窗口与内存两条渲染路径读同一份帧 |
| 第二次看按下不见了 / 冒出新按下 / 划动方向丢失 | 回放按“循环执行次数”推进，重放同一帧会再次跑状态机、重发边缘事件、重算增量 | 边缘（`buttonPressed/Released`、`keyPressed`）和增量在录制时由相邻帧算好固化；按帧号 O(1) 直读，seek 回去不重跑任何状态机 |
| 回放 200 帧帧号漂开 | 帧号依赖循环计数而非显式索引 | 帧号即数组下标，`FrGetFrame(tl, n)` 永远返回第 n 帧，已用 200 次 seek 测试 |

## 文件

- `framereplay.h / framereplay.c` — 确定性时间线、纯函数手势分类、双后端画布与帧渲染。
  **不修改任何 raylib 库文件**，零回归风险；不碰手柄震动、音频、模型、字体（仅默认字体）和玩法。
- `scenario.c` — 精确还原需求中的第 10/11/12/13/14 帧与捏合帧（见下）。
- `replay_viewer.c` — 窗口查看器（桌面 GL/GLFW 构建），静帧/逐帧/正常播放，按 S 存同帧内存图。
- `test_framereplay.c` — 33 条无窗口断言，覆盖静帧三次一致、增量、按住、捏合、200 帧不漂移等。
- `test_wallclock.c` — 真实 `usleep` 插入墙钟停顿，证明停顿绝不计入按住时长。
- `test_backend_contract.c` — 捕获窗口分支会收到的图元指令流并与内存 Image 后端逐图元比对。
- `export_frames.c` — 把每个关键帧渲染成无窗口 PNG。

## 场景与需求逐条对应（`scenario.c`，帧步长 100ms）

- 第 10 帧：手指 (80,60)，未按，键盘未按。
- 第 11 帧：手指仍 (80,60)，左键按下（`buttonPressed` 仅此一帧为真），E 键按下，增量 (0,0)，
  手势 `Hold`，`holdMs=0`。连看三次结果完全相同（`test_framereplay`「three reads…」）。
- 第 12 帧：晚 100ms，手指 (140,60)，仍按住，增量 (+60,0)，手势 `Drag`，`holdMs=100`，
  **不会**产生新按下；连看两次仍是 100ms，不会变 200ms。
- 第 13 帧：原地保持，`holdMs=200`，增量 (0,0)——只累加本帧自己的 100ms。
- 帧间手动停两秒：`test_wallclock` 用真实 sleep 证明这两秒不进入 `holdMs`。
- 第 20/21 帧：双指捏合；停在第 20 帧连看两次，距离（80px）与角度不变；第 21 帧分类为 `Pinch Out`。

## 窗口 vs 内存：同一帧、同一手势、同一坐标

`FrRenderFrame(canvas, sample, …)` 是唯一的渲染例程。`FrCanvas.target == NULL` 时每条图元走
`DrawCircleV/DrawText/…`（窗口/GPU）；指向一个 `Image` 时走完全对称的
`ImageDrawCircleV/ImageDrawText/…`（内存）。因此圆点和文字在两端落在相同坐标：

- 内存端：`FrRenderFrameToImage()` 直接产出同尺寸 PNG，测试断言 (80,60)/(140,60) 处确有圆点、
  标签文字存在，且重复渲染像素级一致。
- 窗口端：`test_backend_contract` 捕获其将收到的图元流（圆心、线端点、文字锚点），
  证明与内存端同坐标；查看器按 S 可把当前帧另存为内存图直接核对。

> 说明：本机无显示器且未安装 GLFW/GL，窗口程序无法在此链接运行，故窗口路径以
> “图元契约 + 对称路由 + 语法编译”验证；内存路径在 raylib 的 `PLATFORM_MEMORY`
> （内置 rlsw 软件渲染、无 OS 窗口）下完整编译并运行。另发现一个**既有的、与本次需求无关**
> 的上游缺口：rlsw 软件后端的 `rlDrawRenderBatch()` 没有 GL 1.1 风格的 QUADS 提交分支，
> 因此内存后端的实时屏幕帧缓冲 `LoadImageFromScreen()` 取不到 2D 形状；本模块的内存画面
> 走 `Image*` CPU 光栅化路径，不受该缺口影响。

## 正常播放与既有输入

非静帧时窗口查看器按每渲染帧推进一条记录帧并循环；点击、拖动、滚轮的实时处理保持 raylib
原样，本模块不替换它们。手柄震动本次不动。

## 构建与运行

无窗口（本机可跑，需要先用 `PLATFORM_MEMORY` 编出 `libraylib.a`）：

```sh
make -C /workspace/src PLATFORM=PLATFORM_MEMORY          # 已在本工作区完成
gcc -std=c99 -DPLATFORM_MEMORY -DGRAPHICS_API_OPENGL_SOFTWARE \
    -I/workspace/src -I. test_framereplay.c framereplay.c \
    /workspace/src/libraylib.a -lm -lpthread -o test_framereplay && ./test_framereplay
# 其余两个测试与 export_frames 同理；或参考 Makefile（沙箱若拦截 make 调编译器，请直接用 gcc）
```

桌面窗口（有 GL/GLFW 的机器）：

```sh
gcc replay_viewer.c framereplay.c scenario.c -lraylib -lm -o replay_viewer
# 空格 冻结/播放，左右 逐帧，R 回零，S 当前帧存内存 PNG
```

## 测试结果

- `test_framereplay`：33/33 通过
- `test_wallclock`：4/4 通过（含真实 sleep）
- `test_backend_contract`：11/11 通过
- `export_frames`：成功导出 9 张关键帧 PNG，坐标契约通过
