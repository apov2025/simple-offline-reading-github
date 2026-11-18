# 来源、修改与许可

项目介绍署名：**简单离线阅读 by apov**。

这是基于上游固件继续开发的源码快照，保留上游文件版权声明。主要修改包括极简阅读器、EPUB 排版与书目信息、SD 恢复、锁屏封面工具及 Metalio 板级适配。此快照不是原作者的官方发布，也不把原作者的工作归为 apov 独立创作。

| 内容 | 来源 | 许可／说明 |
| --- | --- | --- |
| 小纸基础固件与相关驱动 | [MindReset/read_pico_firmware](https://github.com/MindReset/read_pico_firmware)，基线 `6a247cc` | 根目录 Apache-2.0；具体组件声明保留 |
| Metalio 板级代码 | [CloudZao/Metalio-E-INK4-Plus](https://github.com/CloudZao/Metalio-E-INK4-Plus)，来源提交及修改见 `boards/metalio/upstream/SOURCE.json` | MIT 为主；局部文件按自身声明 |
| epdiy 与继承的显示代码 | [vroland/epdiy](https://github.com/vroland/epdiy) | LGPL-3.0-or-later，保留完整源码、修改说明及 GPL/LGPL 全文；部分文件另有 SPDX 声明 |
| 内置阅读与界面字体、原字体 | [Warren2060/ChillDuanSans](https://github.com/Warren2060/ChillDuanSans) | SIL OFL-1.1；原声明包含 ChillType、Adobe 和 Figtree 作者。子集版本更名为 Pico Hei／Read Pico UI |
| 字形栅格化 | [nothings/stb](https://github.com/nothings/stb) | 见 `main/font/stb_truetype.h` 内许可 |
| JPEG 解码与 PNG 支持 | `main/book/vendor/` 及 ESP-IDF zlib | 保留各自原声明 |
| 英文断词规则 | TeX hyph-utf8 | 原许可与版权在 `tools/hyphen/README.md`、规则文件中 |
| 内置《红楼梦》《西游记》 | [Project Gutenberg #24264](https://www.gutenberg.org/ebooks/24264)、[#23962](https://www.gutenberg.org/ebooks/23962) | 由公版中文文本整理；处理说明见 `builtin/README.md` |
| ESP-IDF 与管理依赖 | [espressif/esp-idf](https://github.com/espressif/esp-idf)，`dependencies.lock` | 构建时获取，遵循各依赖自己的许可 |

总项目 LICENSE 不覆盖或替换第三方许可证。发布二进制时请同时提供本次对应源码包及构建说明，保留许可证目录；用户可以修改这些源码并重新构建、安装。固件不启用为阻止用户修改而设置的签名限制。

本包没有从实机复制任何用户存储。默认演示热点口令属于上游公开功能常量，不是私人 Wi-Fi 密码；日常阅读不启动该热点。个人添加的图书、外部字体和设备备份均不包含在发布内容中。

本项目不提供保修或支持服务；免费分享不构成兼容性、维护或设备恢复承诺。免责范围遵从适用法律及各项许可证，不能理解为改变或排除法律不得排除的责任。

定制开发主要由 AI 辅助完成。对 apov 的单独致谢／引用是可选的，不另增加署名要求；必须保留的版权和许可通知仍以各许可证为准。

Cloud Zao 标志由使用者提供并经黑白适配，用于区分设备默认锁屏。它不是本项目原创品牌；软件许可证不授予该标志的商标权，也不表示获得品牌方的背书。
