# 小纸内置字体 / Pico built-in font

`PicoHei-Regular.ttf`由共享`builtin/storage/fonts/PicoHei-Regular.ttf`复制并只补9个字形，用于小纸专用资源。共享字体保持原样。

## 来源与许可

所有来源字体及合并后的字体均为SIL Open Font License 1.1。完整原版权、补字版权、修改说明与三份上游OFL均保存在同目录`OFL.txt`，须随字体分发。字体内部name表也保留原版权并追加两份补字来源。字体名称继续使用Pico Hei，不使用上游保留名Duan、Source、Plangothic或遍黑。

1. 基础字体：寒蝉端黑体，仓库`sdcard/fonts/ChillDuanSansVF.ttf`由`tools/gen_reading_font.py`取400字重及原字符范围生成共享PicoHei。基础PicoHei SHA-256：`97ebd7c75164745c3569023a31f64e197269db08da124868068a8fc7a48ea222`，22038字形。
2. [Noto Sans CJK SC](https://github.com/notofonts/noto-cjk)，固定提交`f8d157532fbfaeda587e826d4cd5b21a49186f7c`的可变TTF源；SHA-256：`990c807e79c25662a5a9ecf7f971baeb2bf2eab9a559e5ecf15cdfdb8561d21f`。按wght=400取静态轮廓，仅补`• 㕲 㪷 㸳 㽂 䗫 𩥇`。
3. [遍黑体Plangothic P1官方发布V2.9.5795](https://github.com/Fitzgerald-Porthmouth-Koenigsegg/Plangothic_Project/releases/tag/V2.9.5795)，`PlangothicP1-Regular.ttf` SHA-256：`550b5d0775b15405946b18f4843df439a51e69508d7e6778d94c1f7a53dc5ad6`，与官方发布asset digest相符。仅补`𦉩 U+26269`与`𩎃 U+29383`，来源版权为Copyright (c) 2024 by Fitzgerald P. Köeingsegg。

## 生成约束

三个源均为1000UPM，不缩放；使用fontTools `getGlyphSet(location={'wght':400})`获取可变源的400字重静态轮廓，经DecomposingRecordingPen拆解后用TTGlyphPen写入。只在基础字体原字形序列后追加9字，给format4/12的适用Unicode cmap添加映射；使用补字自身的hmtx度量。输出不带可变字体表。

生成后重新打开文件，逐一验证原22038字形编译后glyf字节、原所有hmtx度量、4张cmap所有已有映射及原字形顺序完全相同。新增9字均有真实轮廓，最终22047字形。《梦溪笔谈》包括标题、校异和来源的3762种非空白字符全部覆盖；英文书可见文字亦全部覆盖。该检查是字体数据验证，设备上的最终观感仍以实际渲染为准。

成品6857492字节；SHA-256：`e9d6d193d02c43c88815e915d4aec3b79305c04ee477ad4dbbeb9707112433a5`。
