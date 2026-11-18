# 构建与验证

## 环境

使用 [ESP-IDF v6.1](https://github.com/espressif/esp-idf/tree/v6.1)。此分享包构建环境的 IDF 提交为 `fff9895c82d744c7237be8847347bdd1b07c6643`，SDK 工作区无本地修改；ESP32-S31 需要预览目标支持。发布时使用 esptool 5.6.0。

按 ESP-IDF 官方安装流程准备目标工具链，激活 `export.sh` 或对应 Windows 环境。首次配置时组件管理器按 `dependencies.lock` 下载依赖，需要联网；固件日常阅读不需要联网。

源码包已包含字体、波形和内置书籍，不必先重新生成资源。`configs/` 保存本次构建的完整配置，`sdkconfig*.defaults` 是用于新构建的默认配置。不要混用两机缓存，也不要把 CI 用的配置当作产品配置。

## 小纸 Pico

在源码根目录：

```bash
cp configs/pico.sdkconfig sdkconfig
idf.py -B build -DREADER_BOARD=pico build
```

产物 `build/Read_Pico.bin` 只是程序，不能单独用于首次安装。此配置启用针对原板 Flash 的 HPM 和 120 MHz 运行时设置；串行烧录参数／镜像头记录的 80 MHz 与运行时 HPM 不是同一概念。不能据此推定兼容任意 ESP32-S3 板。

## Metalio E-Ink 4 Plus

仅在确认与本包相同接线和屏幕参数时使用下列命令。`1200` 是本包所针对的 VCOM 参数，不是所有面板的通用值。

```bash
mkdir -p build-metalio
cp configs/metalio.sdkconfig build-metalio/sdkconfig
idf.py --preview -B build-metalio \
  -DREADER_BOARD=metalio \
  -DMETALIO_PIN_PROFILE=plus-xstl4-mode19 \
  -DMETALIO_VCOM_MV=1200 build
```

产物 `build-metalio/Read_Metalio.bin`。板型选择固定 ESP32-S31，不通过猜测 PCB 版本号选择引脚。若需要适配其他面板，应先取得原厂接线和标定信息，再开发独立配置。

## 合并完整镜像

使用构建生成的 `flasher_args.json`，其中记录实际偏移。下面示例与本次发布布局一致，均输出从 `0x0` 开始的 16 MB 完整镜像：

```bash
python -m esptool --chip esp32s3 merge-bin \
  --target-offset 0 --pad-to-size 16MB -o pico-full.bin \
  0x0 build/bootloader/bootloader.bin \
  0x8000 build/partition_table/partition-table.bin \
  0x10000 build/Read_Pico.bin \
  0x410000 build/storage.bin

python -m esptool --chip esp32s31 merge-bin \
  --target-offset 0 --pad-to-size 16MB -o metalio-full.bin \
  0x2000 build-metalio/bootloader/bootloader.bin \
  0x8000 build-metalio/partition_table/partition-table.bin \
  0x10000 build-metalio/Read_Metalio.bin \
  0x410000 build-metalio/storage.bin
```

使用 `python` 或当前 ESP-IDF 环境中的 Python。不得把设备读取的完整 Flash dump 当成分享镜像，它可能包含用户设置和内容。合并镜像使用干净构建产物，间隙用 `0xFF` 填充。

镜像组成：NVS 从 `0x9000` 开始，完整镜像在该处为空白；程序从 `0x10000` 开始、容量 4 MB；机内存储从 `0x410000` 开始、容量 11 MB。具体以分区表 CSV 和 `manifest.json` 为准。全量刷写会重置机内数据；仅同布局更新才使用 app-only。

发布版不承诺逐字节可重复构建：ESP-IDF 会把编译时间、项目版本和 ELF 校验信息写入程序。源码文件清单、配置、SDK 提交和镜像 SHA-256 用于追踪版本，不把重新编译后哈希不同误判为源码不同。

## 回归验证入口

下面是现有测试，不要求设备连接：

```bash
bash tools/run_book_host_tests.sh
bash tools/run_epub_host_tests.sh
python3 tools/run_reader_perf_tests.py
python3 tools/run_reader_metalio_tests.py
python3 tools/run_metalio_display_tests.py
python3 tools/run_metalio_light_tests.py
python3 tools/run_metalio_sleep_tests.py
python3 tools/run_metalio_usb_route_tests.py
python3 tools/run_sleep_wake_tests.py
python3 tools/run_reader_target_tests.py
python3 tools/run_fatfs_filename_tests.py \
  --idf-path "$IDF_PATH" \
  --sdkconfig-header build-metalio/config/sdkconfig.h
```

依赖 C/C++ 编译器、zlib 开发文件；模拟器相关测试还需要 SDL2 和 `sdl2-config`。默认命令面向 macOS/Linux。运行前看对应测试脚本，测试不等价于真机电气验证。

模拟器说明见 [sim/README.md](../sim/README.md)。主阅读器在 `main/apps/app_reader.c`，Metalio 硬件封装在 `main/board/`；上游来源和本地修改记录在 `boards/metalio/upstream/SOURCE.json`。

## 本次分享包验证范围

- 两目标均完成编译；Metalio 标志和书籍封面显示已获用户确认。本次最后修改为移除锁屏底部说明、统一两机“已关机”留屏；睡眠回归及模拟器预览通过，Metalio 新程序已烧录，用户确认封面无遮挡、短按唤醒正常、关机留屏显示正确；Pico 本次程序也已刷入并确认启动及 SD 读取正常，用户确认翻页和锁屏正常，软件关机入口及画面仍未实机验收。
- Pico 从当前源码重新构建，未为分享包重新刷写设备；既有阅读、SD 恢复和唤醒反馈来自先前同一开发分支。
- 两个全量镜像按段比较启动程序、分区、app、内置存储，确认偏移和内容，并检查 NVS／未使用区为全 `0xFF`。
- metadata／封面步骤以本地合成 EPUB 验证，不把个人书籍纳入测试交付。
- Metalio 已知 SDK 弃用提示涉及 `esp_sleep_get_wakeup_cause()`，构建成功；未因此更改本次实测过的固件。
- 不提供真实设备 MAC、USB 序列号、完整备份、NVS 读取结果或私人日志。校验清单只记录发布文件自身的信息。
