# 英文断词规则的来源

`hyph-en-us.pat.txt`（4938 条规则）和 `hyph-en-us.hyp.txt`（14 个例外词）取自 TeX 的 hyph-utf8 包，是美式英语的断词规则，TeX、浏览器和多数电子书阅读器用的都是这一套。内容没有改动。

`tools/gen_hyphen_table.py` 把它们转成固件里的查找表 `main/book/hyphen_en_table.h`。

原文件的版权与许可声明（按许可要求原样保留）：

```
title: Hyphenation patterns for American English
copyright: Copyright (C) 1990, 2004, 2005 Gerard D.C. Kuiken
notice: This file is part of the hyph-utf8 package.
    See http://www.hyphenation.org/tex for more information.
version: 2005-05-30
licence:
    Copying and distribution of this file, with or without modification,
    are permitted in any medium without royalty provided the copyright
    notice and this notice are preserved.
```

这套规则以 Donald Knuth 的 `hyphen.tex`（Liang 的断词规则）为基础并加以扩充。`hyphen.tex` 的声明是：可以不受限制地复制和分发；允许修改，但修改后的文件不能再叫 `hyphen.tex`。
