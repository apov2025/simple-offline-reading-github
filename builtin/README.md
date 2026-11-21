# 机身自带的内容

内置资源会打包成机身存储分区镜像，随固件烧进设备，开机后位于 `/flash`。小纸专用书和字体位于 `pico/books/`、`pico/fonts/`；`storage/` 保留原书和字体供 Metalio 使用。这样不插 TF 卡也有字体可用、有书可读。

## 小纸专用书与字体

| 文件 | 内容 | 来源与处理 |
| --- | --- | --- |
| `pico/books/mengxibitan.epub` | 沈括《梦溪笔谈》，序与原书26卷 | 维基文库官方导出，公版原作，简体整理；保留自然段与原卷次，目录标注主题 |
| `pico/books/time-machine.epub` | H. G. Wells《The Time Machine》 | Standard Ebooks公版英文原作，保留12章、尾声、原注 |
| `pico/fonts/PicoHei-Regular.ttf` | 小纸内置Pico Hei，22047字形 | 保留共享字体全部原轮廓与字宽，仅以Noto/遍黑体的OFL字形补9字 |
| `pico/fonts/OFL.txt` | 完整字体许可及版权 | 随字体分发，包含原字体及两份补字来源 |

两书共约1.13 MB，小纸字体约6.86 MB；书内都有来源与许可、普通封面及小纸原生预渲染封面。配套字体覆盖两书全部可见文字。中文书是古代知识与观察的文言原著，不是现代白话译本；保留原文和校异，不宣称全文校勘。来源、版本、哈希、转换方法及许可详见[书籍来源](pico/books/SOURCES.md)和[字体来源](pico/fonts/SOURCES.md)。

## Metalio保留资源

| 文件 | 来源 | 处理 |
| --- | --- | --- |
| `storage/fonts/PicoHei-Regular.ttf` | 仓库里的寒蝉端黑体（`sdcard/fonts/ChillDuanSansVF.ttf`，OFL-1.1） | 用 `tools/gen_reading_font.py` 生成：取 400 字重，收 GBK 的全部汉字，约 6.9 MB |
| `storage/fonts/OFL.txt` | 同上 | 字体的许可，必须随字体一起分发 |
| `storage/books/hongloumeng.epub` | 古登堡计划 #24264 的文本（公版） | 用 `tools/txt_to_epub.py` 整理：去掉英文头尾、接回断行、识别 120 回、修正标点、转简体 |
| `storage/books/xiyouji.epub` | 古登堡计划 #23962 的文本（公版） | 同上，100 回 |

几点说明：

- **文件名用英文**。打包镜像的工具对中文文件名没有把握，而书库显示的书名、作者、年份取自 EPUB 里自带的信息，所以文件名不影响显示。
- **字体改了名**。源字体的许可规定，修改过的版本（裁剪也算）不能沿用原来的名字，所以叫 Pico Hei；设备界面上显示为"内置"。
- **内置字体收了哪些字**：英文和西欧字母、中英文标点、GBK 的全部汉字（约 2.1 万个）和 GB2312 里的符号，加上固件界面和这两本书里用到的字，共 2.2 万个字形。两本书里有 21 个极生僻的字源字体本身没有，会显示成缺字。
- **原文的缺漏没有补**：比如《红楼梦》这份文本个别回末句子不完整、少数引号不成对。
- **总大小**约 9.2 MB，存储分区是 11 MB。往里加书之前先算一下还放不放得下。

## 重新生成共享资源

```bash
python3 tools/gen_reading_font.py
```

```bash
python3 tools/txt_to_epub.py 红楼梦.txt --title 红楼梦 --author 曹雪芹 --year 1791 -o builtin/storage/books
```

第一条需要 `pip install fonttools`。第二条生成的文件名是中文的，放进 `storage/books/` 后要改成英文名；转简体需要 `pip install opencc-python-reimplemented`，没装的话程序会保持原文的繁体并提示。改了书之后重新跑一遍字体生成，书里新出现的字才会收进内置字体。

## 烧写

- `idf.py flash` 会连同这个分区一起烧，机身存储恢复成这里的内容（之前通过"传书"存进机身的书会被清掉）。
- 只更新程序、不动机身存储，用 `idf.py app-flash`。


## 小纸内置使用说明

`pico/manual/pico-guide.epub` 仅加入小纸存储镜像，位于机身 `/flash/manual/pico-guide.epub`，通过书库下拉面板的“使用说明”打开，不占普通书库列表。无名构建不包含它。源文稿是 `docs/pico-guide.md`，运行 `python3 tools/build_pico_guide.py` 可重新生成。更新已使用的设备时，先备份机身存储，仅新增说明书并核对其他文件不变；不要为此直接覆盖用户的整个机身存储。
