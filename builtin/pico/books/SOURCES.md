# 小纸默认书来源 / Pico default-book sources

## 梦溪笔谈

- 作者：北宋沈括。序及原书二十六卷，保持原卷次、自然段和标点；不含另行成篇的《补笔谈》《续笔谈》。不是现代白话译本，也不宣称完成全文校勘。天文、乐律、技艺、器用等主题采用原书分类。
- 来源：[维基文库](https://zh.wikisource.org/wiki/夢溪筆談)，[官方EPUB导出](https://ws-export.wmcloud.org/?lang=zh&page=夢溪筆談&format=epub-3)，取得于2026-11-21。主页面固定版本：[oldid=7168168](https://zh.wikisource.org/w/index.php?title=夢溪筆談&oldid=7168168)。官方导出SHA-256：`de9ca126ea081190c24301fecd86365485111975c31c2785e155a5dbb1a32cd6`。
- 原作为公有领域。维基文库页面声明CC BY-SA 4.0；官方导出还保留CC BY-SA 3.0/GNU FDL旧声明。对可能受保护的转录、校异、注释及本次整理贡献使用CC BY-SA 4.0，保留上游署名与修改说明；公版原作不因此新增限制。本书内`LICENSES/CC-BY-SA-3.0.txt`、`CC-BY-SA-4.0.txt`、`WIKISOURCE-ABOUT.txt`包含完整许可和导出署名，末章列有全部贡献者。
- 整理：去掉网页导航、重复目录、许可横幅；保留序与26卷全部正文段落；从官方导出的语言转换属性恢复空元素内的原字；隐藏校异提示和2条脚注移到末尾专章，未据此擅改正文。新建标准EPUB/NCX目录和纯文字封面。
- 简体转换采用[OpenCC Python Reimplementation](https://github.com/yichen0831/opencc-python) 0.1.7的`t2s`配置；若转换后得到GBK以外的罕用新字，保留转换前底本原字。保留的原字为：艣、螮、蝀、蹻、燖、璊、樠、煇、騃、饘、劚、鰋、駉、鴥、軨、騞、鉷、藭、鐍、斸、𩥇、騑。这是简体整理，不是现代改写。转换工具为Apache-2.0，许可附于`OpenCC-LICENSE.txt`；工具程序本身不随书运行。
- 内容：495自然段、101654正文字符；29个目录入口（序、26卷、校异注释、来源许可）。
- 成品`mengxibitan.epub`：619555字节，SHA-256 `28f6dce2de500c90a21d24bb0ead7b7f5e40224b5833ceb8e82f35568592ad42`。

## The Time Machine

- 作者：H. G. Wells；1895年原作。采用[Standard Ebooks版本](https://standardebooks.org/ebooks/h-g-wells/the-time-machine)，[源仓库固定提交](https://github.com/standardebooks/h-g-wells_the-time-machine/tree/fbd98543d4195ffdd8dbb90facb4309223bcb147)。原作在美国为公版；Standard Ebooks新增贡献以CC0 1.0贡献公有领域，完整上游声明保存在书内`LICENSES/STANDARD-EBOOKS.txt`和末章。本整理保留原始地域性版权提示，不宣称所有地区版权状态相同。
- 内容保持十二章、尾声、原注与来源许可，15个目录入口。正文全部自然段保留；去掉排版控制字符、合并HTML空白、三个连排长破折号以三个普通长破折号等值表示，行内强调以纯文本呈现。原词句及标点不另作改写。没有沿用原封面图像或标志。
- 成品`time-machine.epub`：511475字节，SHA-256 `bd90298475330b2b310801b1579c55af590a4ea21104092ce9fbda126d10366f`。

## 封面、格式与验证边界

两书封面均为霞鹜文楷GB标准字体纯文字排版，SIL OFL 1.1许可保存在各书`LICENSES/OFL.txt`；未把字体文件嵌入EPUB。预渲染封面位于`pico/cover.4bpp`：684×1216、415872字节、低半字节先行、不压缩；普通EPUB封面PNG同时保留。正文为语义自然段，无逐行`br`或`pre`硬折行。

每个spine项、EPUB目录与NCX条目均唯一；声明资源存在，内部链接与锚点均已校验。配套小纸字体覆盖两书全部可见文字。未宣称EPUBCheck认证、全文古籍校勘或真机验收。
