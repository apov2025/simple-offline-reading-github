# 机身自带的内容

`storage/` 里的东西会在编译时打包成机身存储分区的镜像，随固件一起烧进设备，开机后出现在 `/flash` 下。这样不插 TF 卡也有字体可用、有书可读。

| 文件 | 来源 | 处理 |
| --- | --- | --- |
| `storage/fonts/PicoHei-Regular.ttf` | [寒蝉端黑体（GitHub）](https://github.com/Warren2060/ChillDuanSans)（`sdcard/fonts/ChillDuanSansVF.ttf`，OFL-1.1） | 用 `tools/gen_reading_font.py` 生成：取 400 字重，收 GBK 的全部汉字，约 6.9 MB |
| `storage/fonts/OFL.txt` | 同上 | 字体的许可，必须随字体一起分发 |
| `storage/books/hongloumeng.epub` | 古登堡计划 #24264 的文本（公版） | 用 `tools/txt_to_epub.py` 整理：去掉英文头尾、接回断行、识别 120 回、修正标点、转简体 |
| `storage/books/xiyouji.epub` | 古登堡计划 #23962 的文本（公版） | 同上，100 回 |

几点说明：

- **文件名用英文**。打包镜像的工具对中文文件名没有把握，而书库显示的书名、作者、年份取自 EPUB 里自带的信息，所以文件名不影响显示。
- **字体改了名**。源字体的许可规定，修改过的版本（裁剪也算）不能沿用原来的名字，所以叫 Pico Hei；设备界面上显示为"内置"。
- **内置字体收了哪些字**：英文和西欧字母、中英文标点、GBK 的全部汉字（约 2.1 万个）和 GB2312 里的符号，加上固件界面和这两本书里用到的字，共 2.2 万个字形。两本书里有 21 个极生僻的字源字体本身没有，会显示成缺字。
- **原文的缺漏没有补**：比如《红楼梦》这份文本个别回末句子不完整、少数引号不成对。
- **总大小**约 9.2 MB，存储分区是 11 MB。往里加书之前先算一下还放不放得下。

## 重新生成

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
