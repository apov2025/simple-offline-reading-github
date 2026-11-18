# 小纸 Pico 桌面模拟器

在 Mac 上直接运行本仓库的固件，不需要真机，也不需要安装 ESP-IDF。窗口里是一台用 3D 模型渲染的小纸 Pico 正面，屏幕、三个触摸键和电源键都能用鼠标点，还能随机换配色。

固件的应用层（`main/`）和绘图库原样编译，只把最底层的硬件驱动换成桌面上的替身，所以在这里看到的界面、排版和交互逻辑，就是真机上会运行的那份代码。

## 运行

需要 Xcode 命令行工具和 SDL2（`brew install sdl2`）。

```bash
make -C sim run
```

第一次运行会把仓库 `sdcard/` 里的字体放进模拟的 TF 卡，并预先选好这个字体。

## 操作

| 操作 | 作用 |
| --- | --- |
| 鼠标点击、拖动屏幕 | 触摸 |
| 点外壳下方的三个标记，或按 `1` `2` `3`、`←` `↓` `→` | 三个触摸键 |
| 点外壳右侧的电源键，或按 `P` | 电源键短按（锁屏、唤醒） |
| 菜单栏"外观 → 随机配色"，或 `⌘R`，或 `C` | 随机配色 |
| 菜单栏"外观 → 原机配色"，或 `⌘0`，或 `O` | 恢复原机配色 |
| `F` | 切换"按真实耗时刷新"和"瞬间刷新" |
| 把 EPUB 或 TTF 文件拖进窗口 | 放进模拟 TF 卡的 `books` 或 `fonts`，并自动拔卡再插卡，书库随即刷新 |
| 菜单栏"TF 卡 → 在访达中打开 TF 卡文件夹"，或 `⌘O` | 打开模拟 TF 卡对应的文件夹 |
| 菜单栏"TF 卡 → 拔出 / 插入 TF 卡"，或 `⌘E`，或 `M` | 插拔 TF 卡 |
| `S` | 把当前屏幕截图存到 `sim/data/shots/` |

固件本身两分钟不操作会自动锁屏，这时按电源键唤醒。

窗口标题栏显示最近一次刷新用的模式、波形相位数和估算耗时，以及开机以来的刷新次数。

## 模拟设备的数据

都在 `sim/data/` 下，删掉这个目录就等于恢复出厂。

| 目录 | 对应真机上的 |
| --- | --- |
| `sim/data/sdcard/` | TF 卡。书放 `books/`，字体放 `fonts/`；这两个文件夹开机时由固件自动建 |
| `sim/data/flash/` | 机身内置存储。每次 `make run` 会按 `builtin/storage/` 补齐自带的字体和书（只补缺的，不覆盖） |
| `sim/data/nvs/` | 设置和阅读进度 |
| `sim/data/shots/` | 截图 |

## 模拟到什么程度

| 部分 | 情况 |
| --- | --- |
| 界面、排版、书籍解析、交互逻辑 | 与真机是同一份代码 |
| 屏幕内容 | 16 级灰度，逐像素一致 |
| 刷新模式 | 按真实波形表判断哪些像素被驱动；快刷（DU）只出黑白 |
| 刷新耗时 | 按波形相位数 × 帧周期估算，与真机计算方式相同 |
| 残影 | 不模拟，必须在真机上调 |
| 触摸 | 单点（鼠标） |
| 电池、充电 | 固定为 78%、未充电 |
| 加速度计 | 固定为平放、屏幕朝上 |
| 关机、软休眠 | 退化为浅睡，按电源键唤醒 |
| Wi-Fi 传书、蓝牙 | 未实现，书直接放进 `sim/data/sdcard/books/` |
| 蜂鸣器 | 只打印日志 |
| 内存限制 | 不模拟，真机只有 8 MB PSRAM |

## 自动化脚本

用环境变量 `SIM_SCRIPT` 指向一个文本文件，模拟器会逐行执行，适合做回归截图。配合 `SDL_VIDEODRIVER=dummy` 可以不弹窗口。

```
wait 1500
key 3
wait 800
tap 340 897
wait 2000
shot shelf
quit
```

| 命令 | 含义 |
| --- | --- |
| `wait <毫秒>` | 等待 |
| `tap <x> <y>` | 点一下，坐标是屏幕坐标（0..683，0..1215） |
| `swipe <x0> <y0> <x1> <y1> <毫秒>` | 滑动 |
| `key <1-3> [毫秒]` | 按触摸键，可指定按住多久 |
| `power` | 电源键短按 |
| `card <0\|1>` | 拔出或插入 TF 卡 |
| `look <种子>` | 按种子生成一套配色，`look 0` 是原机配色 |
| `shot <名字>` | 只截屏幕（纯灰度） |
| `shell <名字>` | 连外壳一起截 |
| `quit` | 退出 |

`sim/scripts/reader_v1.txt` 是阅读器第一版的整套回归脚本，改完阅读器后跑一遍再看截图。

其他环境变量：`SIM_FAST=1` 启动时就用瞬间刷新；`SIM_DATA=<目录>` 换一个数据目录；`SIM_SCALE=0.8` 指定窗口缩放；`SIM_LOG=debug` 打开详细日志。

## 结构

| 路径 | 内容 |
| --- | --- |
| `sim/idf/` | ESP-IDF 接口的替身头文件，以及把 `/sdcard`、`/flash` 路径重定向到 `sim/data/` 的头文件 |
| `sim/src/sim_idf.c` | 日志、计时、内存、任务与信号量、设置存储、文件路径、休眠 |
| `sim/src/sim_epd.c` | 墨水屏面板：差分、按波形驱动像素、耗时估算 |
| `sim/src/sim_board.c` | 触摸、电源芯片、TF 卡、蜂鸣器、传书、假 I2C 总线 |
| `sim/src/sim_shell.c` | 真机外壳：3D 模型的软件渲染、配色、点击映射 |
| `sim/src/sim_menu.m` | macOS 菜单栏里的"外观"菜单 |
| `sim/assets/read_pico.mesh` | 外壳网格，由 `sim/tools/glb_to_mesh.py` 从 GLB 模型转出 |
| `sim/src/sim_main.c` | 窗口、输入、脚本 |

固件新增源文件时，`main/` 下的 `.c` 会被自动编译进来，不用改 `sim/Makefile`。如果用到了新的 ESP-IDF 接口，在 `sim/idf/sim_idf.h` 声明、在 `sim/src/` 里实现即可。

## 外壳模型

外壳固定为正视，在 CPU 上渲染，用透视投影（相当于 catalog 查看器的 32° 视角）。渲染器本身支持任意视角，但窗口里不提供旋转。灯光和材质是手调的近似，和网页上的查看器不会完全一样。

模型是 apov catalog 里小纸 Pico 页面的 3D 模型（`catalog/product/mindreset-read-pico/models/device.glb`），由小纸的 STEP 文件制作。屏幕窗口、三个触摸键和电源键的位置都是开机时从模型里量出来的，所以边框比例和真机一致。随机配色的算法与 catalog 页面的"随机搭配"相同。

模型更新后重新生成网格：

```bash
python3 sim/tools/glb_to_mesh.py <device.glb 的路径> sim/assets/read_pico.mesh
```

## 续读与性能回归

`python3 tools/run_reader_perf_tests.py` 从仓库根运行：自动构建模拟器，在临时目录内验证续读等待期间的输入、无效翻页计时，以及普通开机、成功续读、缺书和坏书回退；不会使用或修改 `sim/data`。成功续读在首屏刷新前打开书，正常开机保留开机图。该测试只证明状态与刷新次数，不证明真机速度、残影或耗电。

`python3 tools/run_sleep_wake_tests.py` 验证真实浅睡循环：共用中断被清除时到达的按键、无关中断醒来后到达的按键，以及只有卡中断时不能解锁。硬件边界用替身，不能据此确认真机故障根因或测量待机功耗。阅读器测试另含异步卡恢复时已选字体先于完整书库恢复，以及字体文件丢失/选择机身字体的兜底。

`python3 tools/run_sleep_wake_tests.py` exercises the real light-sleep loop with simulated hardware boundaries: keys arriving during IRQ clearing or after an unrelated wake, and no unlock from a card-only IRQ. It does not identify the device failure or measure standby power. Reader tests also cover saved-font restoration before the recovered library and missing/flash-font fallback.

## Metalio 主机检查

`python3 tools/run_reader_metalio_tests.py` 使用真实阅读页验证 Metalio 的 HOME、设置、前光和 SD 重试。设置 `READER_MENU_PREVIEW` 为绝对目录时会额外绘制菜单 BMP。`python3 tools/run_metalio_sleep_tests.py` 验证共享中断、按键保持、I2C/唤醒配置失败、挂载忙和完整休眠/恢复调用顺序。它们使用硬件替身，不能证明真实引脚、SD 休眠保留或功耗。

`sh tools/run_reader_controls_tests.sh` 检查输入去抖和前光范围，`sh tools/run_reader_screenshot_tests.sh` 检查 BMP 方向、唯一命名以及真实文件长度受限时的清理。`python3 tools/run_reader_target_tests.py` 拒绝不匹配芯片或未知接线配置。
