# 构建与验证

本次更新小纸 **2026.11.21-preview.20**，编译和电脑回归已通过，已烧录、完整读回校验并正常启动，用户确认划线／查词切换、跨行选字、隐藏书库、锁屏唤醒及新版说明书正常；Wi-Fi传书本版尚未测试。无名继续发布 **2026.11.18-preview** 原 BIN 和原源码归档；下面的最新源码无名构建只用于兼容检查，不能作为旧 BIN 的同版本复现说明。

## 环境

使用 [ESP-IDF v6.1](https://github.com/espressif/esp-idf/tree/v6.1)。记录的构建环境 IDF 提交为 `fff9895c82d744c7237be8847347bdd1b07c6643`，SDK 工作区无本地修改；ESP32-S31 需要预览目标支持。打包环境使用 esptool 5.6.0。

按 ESP-IDF 官方安装流程准备目标工具链，激活 `export.sh` 或对应 Windows 环境。首次配置时组件管理器按 `dependencies.lock` 下载依赖，需要联网；日常阅读和查词不需要联网。小纸主动 Wi-Fi 传书，以及已配网后的每日首次开机／手动校时，会短时联网。

源码包包含字体、波形和内置书籍资源。小纸新版机内书籍为《梦溪笔谈》和 The Time Machine，并有独立使用说明；无名旧源码保留原内置书。修改说明书原稿后须用 `tools/build_pico_guide.py` 更新机内说明书，再构建 storage。离线词库是独立 Release ZIP，不纳入 Git 源码或固件的机内存储；解压后放到 SD 卡根目录。`configs/` 保存本次构建的完整配置，`sdkconfig*.defaults` 是用于新构建的默认配置。不要混用两机缓存，也不要把 CI 用的配置当作产品配置。

## 小纸 Pico

在源码根目录：

```bash
cp configs/pico.sdkconfig sdkconfig
idf.py -B build -DREADER_BOARD=pico build
```

产物 `build/Read_Pico.bin` 只是程序，不能单独用于首次安装。此配置启用针对原板 Flash 的 HPM 和 120 MHz 运行时设置；串行烧录参数／镜像头记录的 80 MHz 与运行时 HPM 不是同一概念。不能据此推定兼容任意 ESP32-S3 板。

## Metalio E-Ink 4 Plus

最新源码此目标仅做兼容检查，不发布新的无名固件。需要维护已发布旧版，请先切换到 `simple-offline-reading-source-2026.11.18-preview.zip` 原归档。仅在确认与本包相同接线和屏幕参数时使用下列命令。`1200` 是本包所针对的 VCOM 参数，不是所有面板的通用值。

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

使用构建生成的 `flasher_args.json`，其中记录实际偏移。下面是现有布局示例；必须先核对对应版本分区表和构建结果。均输出从 `0x0` 开始的 16 MB 完整镜像：

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

镜像组成：NVS 从 `0x9000` 开始，完整镜像在该处为空白；程序从 `0x10000` 开始、容量 4 MB；机内存储从 `0x410000` 开始、容量 11 MB。具体以分区表 CSV 和 `manifest.json` 为准。全量刷写会重置机内数据；仅同布局更新才使用 app-only。只刷 app 不会更新 storage 中的内置书和说明书；需要同步资源时，按该固件包指南选择明确包含资源的更新方式。不能用旧 storage 搭配新版本说明后声称完整更新。

发布版不承诺逐字节可重复构建：ESP-IDF 会把编译时间、项目版本和 ELF 校验信息写入程序。源码文件清单、配置、SDK 提交和镜像 SHA-256 用于追踪版本，不把重新编译后哈希不同误判为源码不同。

## 回归验证入口

下面是现有测试，不要求设备连接，但新下载源码不保证能直接启动模拟器：`sim/Makefile` 和部分主机测试依赖 `managed_components/espressif__cjson/cJSON`。先激活 ESP-IDF，按上方小纸配置准备 `sdkconfig`，运行 `idf.py -B build -DREADER_BOARD=pico reconfigure`，让组件管理器取得锁定依赖；或按 Makefile 明确配置依赖路径，再运行对应测试。此准备可能需要联网。

```bash
bash tools/run_book_host_tests.sh
bash tools/run_epub_host_tests.sh
bash tools/run_reader_dictionary_rows_tests.sh
bash tools/run_reader_sentence_tests.sh
bash tools/run_reader_clock_tests.sh
bash tools/run_app_loop_host_tests.sh
bash tools/run_reader_wallpaper_tests.sh
python3 tools/run_reader_dictionary_tests.py
python3 tools/run_reader_highlight_tests.py
python3 tools/run_reader_highlight_ui_tests.py
python3 tools/run_book_visibility_tests.py
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

- 小纸 preview.20 编译通过；导航、划线、模式切换与隐藏书库、睡眠、事件循环、书库可见性及跨行拖动等电脑回归通过。已烧录并正常启动；用户确认本轮划线／查词切换、跨行选字、隐藏书库、锁屏唤醒及新版说明书正常。Wi-Fi传书本版尚未测试；不将本次反馈扩展为USB、校时、续航或长期稳定性的专项验收。
- 最新源码通过无名界面兼容回归；这不表示本次重新发布无名固件。原无名 ZIP、对应原源码 ZIP 和此前真机记录保持原样。
- 词库数据沿用已测试的 preview.10，随本次单独打包为 `simple-offline-reading-dictionary-2026.11.21-preview.20.zip`，保留来源和许可。发布标签不表示重新生成了词库数据。
- 发布前须对最终完整镜像分段核对启动程序、分区、app 和机内资源，核对空白 NVS／未使用区及归档 SHA-256。本次文档更新本身不生成镜像，也不代替这些检查。
- 元数据／封面工具已有本地合成 EPUB 验证；不把个人书籍纳入交付。长期稳定性、续航及网页整包写入不因编译通过而视为验收。
- 不提供真实设备 MAC、USB 序列号、完整备份、NVS 读取结果或私人日志。校验清单只记录发布文件自身的信息。
