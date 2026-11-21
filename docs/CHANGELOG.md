# 版本变更 / Changelog

## 2026.11.21-preview（小纸）

- 公开版本不带开发流水号；仅版本与构建描述重新编译，程序其余分段与实测构建一致。本次 Release 不附带无名固件。

- 默认长按正文进入整句划线并选中当前句；长按右侧全刷键切换划线／查词，页码左侧仅显示笔或放大镜图标。离书、锁屏恢复划线模式。
- 查词先允许上下挪开手指观察原行，停稳后可上下换本页可见行、左右换字，松手查询；双侧提示点仅用于查词，选中句框保持黑色粗框。
- 书库长按条目可隐藏，三击标题切换隐藏列表，隐藏列表长按可取消隐藏。保留文件和逐书进度；隐藏书不显示书封锁屏、不自动续读，锁屏唤醒回普通书库；此功能不是加密。
- 仅更新小纸，无名行为不变。编译与电脑回归已通过；已安装、完整读回校验并正常启动。用户确认本轮划线／查词切换、跨行选字、隐藏书库、锁屏唤醒与说明书正常；Wi-Fi传书已实测成功传入一本书。

## 2026.11.21-preview.19（候选）

- 支持重新选句取消划线，只扣除选中部分，保留其余原文和记录元数据。
- 选中句框加粗为纯黑，取消句框模式的两侧黑点；正文直接长按查词仍保留行提示点。
- 已在小纸完成应用烧录、读回校验和正常启动检查；本轮交互仍待用户确认。

## 2026.11.21-preview.18（候选）

- 移除系统 demo 页面与隐藏入口；Wi-Fi配网、传书使用阅读器原生页面，中键逐级返回，结束后固定回书库。
- 划线模式立即框出全部可见句子，选中加深框，确认后才落下细线；修复相邻段落不能连续选取。
- 保留 VCOM 出厂保护、USB安全退出、校时和正常阅读。
- 划线模式不再显示操作说明，仅保留取消/划线按钮和必要错误提示；保存线改为 DU 可稳定驱动的两像素实线。
- 已在小纸烧录、读回校验并正常启动；选框、跨段合并、快刷回页线条及原生传书返回仍待用户真机确认。

## 2026.11.21-preview.17（候选）

- 小纸支持 SD 文件夹锁屏／关机壁纸，独立轮换，书籍封面优先；附本地转换工具与壁纸 Agent Info。
- 小纸默认示例书改为《梦溪笔谈》（原文简体整理）与 The Time Machine（英文原版），整理段落和目录并保留来源；无名默认资源不变。
- 阅读长按右键改为本地整句划线，跨分页补全章内整句；浅色下划线随字号重排，SD记录原文、书名、章节与可信创建时间。
- 查词选中行左右都有提示点；释义卡重选仍支持相邻字词，常用英文词不省略。
- 主页下拉加入日期时分、一键校时反馈和连接已有Wi-Fi的传书入口；每天首次开机有界校时后关闭Wi-Fi。
- 目录底部仅保留“返回正文”，滑动翻目录页；说明书同步上述功能。
- 已安装小纸；用户确认已有Wi-Fi连接、手动校时和脱线锁屏唤醒正常。实际无线文件上传、跨天校时及新增划线/壁纸仍待日常实测。

## 2026.11.19-preview.16

- 小纸书库下拉新增“使用说明”，位于 USB传输上方；内置 EPUB 支持目录、阅读位置与无卡使用，不占普通书库列表。
- 说明书只介绍小纸操作；USB传输期间不显示入口，无名界面与内置内容保持不变。
- 包含上版锁行选词和行提示。

## 2026.11.19-preview.15（候选）

- 正文长按选中后锁定该行：上下挪开手指看清选框，左右移动调整目标，松手查询，无需回到框内。
- 选中行左侧空白处同步显示小黑点，提示长按已识别及选中的行；结束或取消时一并清除。
- 保留长按前的翻页判断和异常取消，不在拖动途中查询词库或保存记录。
- 包含读卡优化、单词本大字列表与详情，以及依据实际字形边界修正相邻左括号和左引号重叠；尚未安装。

## 2026.11.19-preview.14（候选）

- 减少字体缓存命中后的重复读取，打开 EPUB 时避免额外定位到文件末尾。
- USB对齐写入最多合并4KiB并逐批读回核对，保留异常停止与SD独占。
- 包含上版同次查词共用文件优化；单词本列表加大字词与释义，书名和次数移至右侧详情入口。
- 未增加后台预查询，实际速度和新版单词本待真机验证。

## 2026.11.19-preview.13（候选）

- 一次查词中的相关词探测和主词查询复用一次只读打开，减少大词库定位表的重复建立；结束后立即关闭。
- 词库、单词本和操作方式不变；尚未安装。

## 2026.11.19-preview.12

- 附近多个字词按原页位置、字号和字词边界比较，自动框出候选；不增加选择菜单。
- 设备面板隐藏 preview 后的内部编号。
- 修正取消与退出选词时的灰阶恢复，沿用小纸官方波形表和扫描时序；已安装并验证启动，残影效果待实测。

## 2026.11.19-preview.11（候选）

- 改进小字号长按命中，在原位置先用细框反馈选中字词，松手再查询。
- 降低大词库的 SD 定位开销，继续使用现有词库文件。
- 已安装并验证启动，交互待实测；USB 传输速度未在本版修改。

## 2026.11.19-preview.10（候选）

- 释义卡的重选改为角落小图标，回收原底部整行空间。
- 扩充 SD 英汉词库，加入可核对的词形关系与英文所有格查询；清理英文释义中的孤立格式字符。
- 单词本直接显示短释义并翻页浏览，点击条目不再弹出词卡。保留原记录和查询次数。
- 已安装小纸，烧录校验和启动检查通过，功能实测待反馈。

## 2026.11.19-preview.9

- 扩充 SD 离线词库，加入 HanziDict 原创中文单字释义，覆盖对应规范字表全部 8,105 字；保留已有词语、英文和原页连续查词。
- 小纸新增单词本彩蛋：书库长按全刷键进入，查过的词自动保存到 SD，相同查询合并并计数，记录最近书名；未收录词也保留，回看不重复计数。
- 用户已确认查词、单词本与锁屏唤醒正常。

## 2026.11.19-preview.8（候选）

- 小纸释义卡显示期间，可长按卡片外可见正文中的另一字词，松手直接替换释义，无需先退出或重选。保留原页排版与快照，轻点卡外仍关闭卡片。
- 长按卡片、空白或取消手势不替换释义。词库未扩充；本候选尚未烧录。

## 2026.11.19-preview.7（候选）

- 小纸支持正文长按字词、松手直接显示释义，卡内“重选”进入原页框选；长按右键仍可进入框选。
- 框选改为逐项点击选中、再次点击取消。确认后全部选框消失，释义叠加在原阅读页；点卡片外关闭卡片，不触发翻页。
- 保留连续四项上限、常见英文词隐藏选框、成语无拼音及紧凑释义。用户已确认两种入口、选字与退出正常。

## 2026.11.19-preview.6（候选）

- 小纸选字隐藏 I、the、by、under、in、of 等常见英文功能词的框；连续短语保留原文及四词上限，避免误筛长词和复合词。
- 成语卡省去条目及逐义拼音，单字、普通词组仍显示读音。
- 已更新试用包，尚未烧录实测。

## 2026.11.19-preview.5（候选）

- 小纸选字改为原页叠加框，不重排正文；长按进入快刷，短按松手后全刷。支持最多四个相邻汉字或英文单词，英文断词片段共用整词选择，未收录组合逐项查询，连字符复合词可查整体与组成部分。
- 选中一两个汉字会在同一张精简释义卡直接补充包含选区的完整词语或四字成语；限当前页连续汉字，最多四字，取词库已收录的最长匹配。
- 释义改为汉语拼音与简体中文为主的一屏短卡，长文只显示带省略号的摘录，删除部首、笔画、注音和旧原文词库；外置新词库随附可追溯来源与许可证，收录仍不完整。
- USB操作改为“开启”按钮，使用时显示“已开启”及“结束”，结束中禁止重复操作。沿用正常交还SD无需重启流程，待实机复测。
- 本版尚未安装；不改变无名的词典/USB功能范围。

## 2026.11.19-preview.4（候选）

- 小纸新增 SD 卡离线词典：长按全刷键放大选字、连续选词，查询后显示分页释义卡，返回保持阅读位置；短按全刷不变。
- 未收录的纯汉字组合可逐字查看；词库独立文件，缺失或损坏时提示返回。不联网，不做上下文判义或个性化。
- USB 开关改为黑白，开启不再强制全刷，后续状态只刷新局部；安全退出后先恢复 SD 和字体再回书库，正常路径不再重启。新版交还流程待实机复测。
- 按用户反馈撤销快速正文刷新中的目标二值处理，恢复原厂 DU 调用；残影效果仍待对照实测。
- 词典尚未安装或完成实机验证。

## 2026.11.19-preview.3（候选）

- 小纸新增手动 USB传输，电脑独占 SD 卡；退出等待读写结束并重启恢复阅读。用户已实测电脑识别、复制和退出正常，恢复后书籍与字体正常。
- 书库下拉改紧凑面板，显示设备/固件名称、版本、淡灰电池及大尺寸 USB 开关。
- 保留目录定位与原刷新策略，不将上一轮残影修正表述为已改善实机效果。

按日期和作者简述对用户可见的功能变化；详细实现历史见 Git。使用方法见 [README](../README.md)。
User-visible changes by date and author; Git retains implementation history. See [README](../README.md) for usage.

## 待发布 / Unreleased — 2026-11-18

- Metalio：默认锁屏改为居中的 Cloud Zao 黑白标志，书籍封面锁屏保留。/ Metalio uses a centered monochrome Cloud Zao mark while keeping prepared book covers. · Metalio

- Metalio：按 UTF-8 读取存储卡文件名，修正中文字体名称显示为下划线及 `~1/~2` 短名称的问题。/ Read card filenames as UTF-8 on Metalio so Chinese font names retain their names instead of DOS aliases.

- 新增 Metalio E-Ink 4 Plus 独立 S31 构建，复用阅读器；支持 HOME/设置/全刷、音量翻页、双色温前光、SD 截图和封面浅睡。仅主机测试及编译通过，尚未烧录或完成硬件验收；AI、联网传书、USB 读卡后续适配。
  Add a separate S31 build for Metalio with shared reading, HOME/settings/full refresh, volume paging, dual-temperature light, SD screenshots and cover light sleep. Host tests and builds passed; not flashed or hardware-accepted. AI, network transfer and USB mass storage are outside this package.
- 无卡检测脚的 Metalio 在自动恢复次数用尽后，可按 HOME 重试；睡眠挂载忙、按键读取和唤醒配置失败时保持可恢复，截图写入或关闭失败会清理不完整文件。Pico 原操作保持。
  HOME rearms exhausted SD retries on Metalio without a detect pin. Busy mounting and input/wake-setup errors exit sleep preparation; failed screenshot writes/closes remove partial files. Pico controls are preserved.

## 待发布 / Unreleased — 2026-11-17

- 浅睡等待在每次进睡前检查PMU按键队列，避免共用中断被清除后漏掉已到达的按键；涵盖软休眠失败后的浅睡回退。已通过主机复现测试；11月18日烧录后用户确认三轮唤醒正常、字体无跳变，尚不能据此确认偶发故障根因。
  Check the PMU key queue before each light sleep, including host-sleep fallback, so clearing the shared IRQ cannot hide a queued key. Host regression passed; after flashing on November 18, the user confirmed three normal wake cycles without font transitions. Correspondence to the intermittent device failure remains unverified.
- 卡恢复时先加载已选SD字体，再排版完整书库，减少书籍恢复后再次换字体的刷新；卡没就绪时仍可显示内置书。
  Restore the selected SD font before laying out the recovered library, avoiding a later font-switch refresh. Built-in books remain available while the card is unavailable.

## 待发布 / Unreleased — 2026-11-16

- 修复阅读器书库在 TF 卡挂载失效后只显示内置书、不会恢复的问题：先关闭文件和卡上字体，再有限重试挂载，成功后重新列书；触摸持续按住、读书或正在挂载期间不重挂载。已烧录，用户确认三轮锁屏唤醒后卡书正常；异常恢复尚未在真机触发，偶发无法唤醒另行排查。
  Recover a failed TF mount from the idle reader library after closing consumers, with bounded retries and rescanning on success. Avoid remounting during held touches, reading or an active probe. Flashed; three lock/wake/library cycles passed with user confirmation. Fault recovery was not triggered on hardware. Intermittent failure to wake remains under investigation.

## 2026-11-15 · 阅读器第一版 / Reader v1

- 性能优化（2026-11-16 已烧录，真机启动与阅读操作通过，用户反馈残影可接受）：续读省去开机图和中间等待页的刷新，保留清屏与首帧全刷；排版复用本次操作的字宽，读取书目和锁屏封面复用已打开的 EPUB；优化条目校验并保留损坏检查。长章节后台分页拆小，失败时回书库；等待续读时中键可取消。
  Performance changes (flashed on 2026-11-16; hardware boot and reading operations verified, ghosting accepted by the user): resume skips splash and intermediate waiting-page refreshes while retaining the physical clear and first full refresh; layout reuses advances within each operation, metadata and lock covers reuse the open EPUB, and entry checksums are faster without dropping corruption checks. Background pagination uses smaller batches and returns to the library on failure; the middle key cancels a pending resume.

- 开机直接进书库：列出 TF 卡 `books` 文件夹里的 EPUB，按文件名显示书名和已读百分比，点书名开读。
  Boot straight into the library: EPUBs under `books` on the TF card, shown by file name with percent read; tap a title to read.
- 阅读页只有正文和一行页脚；点右侧或左滑下一页，点左侧或右滑上一页，跨章自动接上，进度自动保存。
  The reading page is body text plus one footer line; tap right or swipe left for the next page, tap left or swipe right for the previous; chapters chain automatically and progress is saved.
- 三个触摸键：左键开关设置面板（字号、字体、刷新档，一层平铺，点一下生效）；中键返回上一级直到书库；右键全刷一次清除残影。
  Touch keys: left toggles the settings panel (size, font, refresh profile; one flat sheet, one tap applies); middle goes back one level until the library; right does one full refresh to clear ghosting.
- 电源键：单击锁屏，再单击唤醒（默认改为软休眠）；唤醒后自动回到锁屏前读的那本书、那一页。
  Power key: one press locks, another wakes (soft sleep is now the default); waking reopens the book and page that were open when locking.
- 书库显示作者和年份。书名、作者以 EPUB 自带的信息为准，文件名 `书名 - 作者 (年份).epub` 只作补充；修正了文件名是"作者 - 书名"时两者显示反的问题。
  The library shows author and year. Title and author come from the EPUB's own metadata, with file names of the form `<title> - <author> (<year>).epub` only filling gaps; fixes the two being swapped for "<author> - <title>" file names.
- 书库：长书名折行显示完整（最多三行）；按最近阅读排序；底部左右两角是"上一页""下一页"，中间是页码。
  Library: long titles wrap to show in full (up to three lines); ordered by most recently read; "previous" and "next" sit in the bottom corners with the page number between them.
- 修正：部分 EPUB 整本打不开（压缩包带一个无害的标记位，或书里有一张超过 4 MB 的大图）。实测 26 本里原先有 8 本打不开，现在都能读。
  Fix: some EPUBs would not open at all (a harmless flag bit in the archive, or one image over 4 MB inside the book). Of 26 real books, 8 failed before and all read now.
- 阅读器只读文字：书里的图片不显示、不留占位框，只有图片的章节直接跳过。
  The reader is text only: book images are neither shown nor given a placeholder, and chapters holding only images are skipped.
- 内置字体换成全字版（GBK 全部汉字，约 6.9 MB），机身存储分区加大到 11 MB，升级需要完整烧写一次。
  The built-in font now covers every Han character of GBK (about 6.9 MB); the storage partition grows to 11 MB, so upgrading needs one full flash.
- 读书时锁屏，屏幕显示这本书的封面（封面图由整理程序提前做好放在书里）。
  Locking while reading shows the book's cover (pre-rendered into the book by the preparation tool).
- 新增电脑上的整理程序 `tools/prepare_books.py`：判断并写好书名、作者、年份，统一文件名，重新打包。阅读器本身不整理书。
  New desktop tool `tools/prepare_books.py`: decides and writes title, author and year, unifies file names and repacks. The reader itself does not tidy books.
- 英文排版：放不下的单词按 TeX 的美式英语规则断词并加连字符；对齐只加宽词间空格，不再拉散字母，空格要拉得太宽时这一行不对齐；词内撇号不断开；中文字体里的全角弯引号在英文里不再留出空洞。
  English layout: words that do not fit hyphenate by the TeX American English patterns; justification widens word spaces only and no longer letter-spaces, leaving a line ragged when spaces would grow too wide; apostrophes inside words never break; fullwidth curly quotes from a Chinese font no longer leave holes in English.
- 修正：标点挤压原先按字号判断全角，真实字体下不生效；现在按一个汉字的实际宽度判断。
  Fix: punctuation compression judged fullwidth against the size and did nothing with real fonts; it now measures against the actual width of a Han character.
- 设置面板去掉了底部的按键说明。
  The settings panel no longer shows the key hint line.
- 机身自带一个中英文都全的字体和两本书（《红楼梦》《西游记》），不插卡开机就能读；内置字体是默认字体，拔卡时卡上的字体自动退回它。机身存储分区由 5 MB 加大到 8 MB，升级需要完整烧写一次。
  The device now ships with one font covering Chinese and Latin and two books, so it reads without a card; the built-in font is the default and card fonts fall back to it when the card is pulled. The storage partition grows from 5 MB to 8 MB, so upgrading needs one full flash.
- 真机试用后的调整：去掉截图（截图后 TF 卡会掉线）；刷新档默认"快速"；默认字体自动挑一个中文英文都支持的。
  After trying it on hardware: screenshots removed (the TF card dropped out after one); the refresh profile defaults to Fast; the default font is one that covers both Chinese and Latin.
- TF 卡目录约定为 `books`、`fonts`，缺了开机自动建；`book`、`font` 这两种单数写法也能读到。
  The TF card layout is `books` and `fonts`, created at boot when missing; the singular `book` and `font` are read too.
- 中文排版（clreq 的几条要点）：行首行尾禁则、标点挤压、两端对齐；英文单词、省略号和破折号不拆开。段落默认顶格不缩进，靠段间距分段。
  Chinese layout (key clreq rules): line-start and line-end prohibition, punctuation compression, justification; Latin words, ellipses and dashes stay whole. Paragraphs start flush left by default, set apart by spacing.
- 内置界面字体补上了阅读器界面用到的字，拔掉 TF 卡后界面不再缺字。
  The built-in UI font now covers the reader's interface text, so nothing is missing with the TF card removed.
- 卡没插或卡上没有书时显示"开始使用"引导页，逐项标出卡、字体、书是否就绪。
  With no card or no books, a "getting started" page marks whether the card, fonts and books are ready.
- 拔卡再插回后书库自动刷新。
  The library refreshes on its own when the card is re-inserted.
- 硬件演示菜单不再是开机页，在书库页长按中键进入。
  The hardware demo menu is no longer the boot page; long-press the middle key in the library to open it.

## 2026-10-09

- 锁屏：未单独选择时默认关机（EN=0，长按约 1 秒开机）。已保存的浅睡或深睡不变。
  Power-key lock defaults to power-off when no mode is stored. A saved light or deep choice stays.
- 空闲 2 分钟自动锁屏，睡眠档仍按已选的浅睡、深睡或关机。传书页停留期间不计时。
  Lock after 2 minutes idle, using the saved light, deep, or power-off mode. The transfer page does not count idle time.

## 2026-09-28 · UNSaWEN

- 阅读：进入书架先确认是否续读；ZIP目录连续读取、字宽测量不生成位图，当前章先排起始/续读页，其余分页分批补齐。
  Confirm resume before opening the last book; read ZIP directories sequentially, measure advances without rasterizing, and paginate the initial/resume pages before completing the chapter incrementally.
- EPUB插图：默认占位，点击后单独预览本地JPEG/PNG，返回保持正文位置；标题前的章首图片及同资源单独标为标题图，其他重复图片提示本次已读章节中最早出现的位置，开章不再反复解码重复图片。
  Show image placeholders and load local JPEG/PNG previews only on tap, returning to the same text position; label opening title images and matching resources separately, retaining earliest-visited hints for other repeated illustrations and avoid decoding images on chapter opening.
- 手势：滑动翻页阈值由120缩短到64像素，保留抬手提交、轻点容差和短拖取消。
  Reduce swipe distance from 120 to 64 pixels while retaining release-to-commit, tap tolerance and short-drag cancellation.
- EPUB：ZIP 条目、资源项及章节上限提高到 32768，使用固定窗口扫描ZIP目录和紧凑索引查找，标题按实际长度保存，OPF/导航上限4 MiB；打开和排版显示等待提示，失败显示具体原因。
  Raise EPUB ZIP, manifest and spine limits to 32768, scan ZIP directories through a fixed window, use compact indexed lookup and actual-length titles, and allow 4 MiB OPF/navigation metadata; show parsing/layout wait hints and failure reasons.
- WiFi：TF 卡支持上传完整 TTF 字体，32 MiB 上限、同名确认、提交前结构校验及中断恢复；传书期间暂停卡上字体读取，离页恢复。
  Upload complete TTF fonts to TF over WiFi with a 32 MiB limit, confirmed replacement, pre-commit structural checks and interruption recovery; suspend card font reads during transfer and resume on exit.
- 部署：随仓库提供未修改的 ChillDuanSans VF v1.30、原 OFL-1.1 许可和来源校验；内置子集命名为 Read Pico UI，保留原版权与许可。
  Bundle unmodified ChillDuanSans VF v1.30, original OFL-1.1 terms and source verification; name the embedded subset Read Pico UI and retain attribution and licensing.

## 2026-09-27 · UNSaWEN

- EPUB 图片位置增加“[图片]”占位，避免纯图片章节显示为空白；暂不解码图片。
  Show “[图片]” placeholders at EPUB image positions so image-only chapters are visible; image decoding is not yet supported.

- 修复长篇 EPUB 因条目或章节超过 512 而无法打开的问题；上限提高到 2048，单资源解压上限仍为 2 MiB。
  Fix long EPUBs failing to open above 512 entries or chapters; raise the limit to 2048 while retaining the 2 MiB uncompressed resource limit.

- 图书阅读：支持 TF 卡或内置存储的 UTF-8/GBK TXT 与纯文本 EPUB，目录、逐书续读、字号、手势翻页及默认关闭的实验晃动翻页。
  Read UTF-8/GBK TXT and text-only EPUB from TF or internal storage, with a TOC, per-book resume, font sizes, gestures and optional experimental shake-to-turn.
- 图书管理：增加拼音/首字母/英文搜索、来源筛选、名称/最近阅读排序、详情，以及分别确认的单本/批量删除和清进度。
  Add pinyin/initials/English search, source filters, name/recent sorting, details and separately confirmed single/batch deletion or progress reset.
- WiFi 传书：支持设备热点和已有网络、触屏/网页配网、连接与网址二维码；网页管理当前存储，支持确认替换、取消和重试；停止后返回进入前的位置。
  Transfer over the device hotspot or an existing network, with touchscreen/web provisioning and connection/URL QR codes. Manage current storage in the browser, confirm replacements, cancel or retry uploads, and return to the entry location on stop.
- 可靠性：处理保存失败与部分成功重试、中文文件名和覆盖中断恢复；修复显示欠载后相位队列残留导致的死锁；检测 TF 失效后停止相关消费者并回退字体，插回后需显式重挂。
  Handle save failures, partial-operation retries, Chinese filenames and interrupted replacements. Fix a display queue deadlock after underrun; stop affected consumers and fall back fonts on TF loss, requiring explicit remount after reinsertion.

- 两款固件的软件关机留屏统一为“已关机”；无名锁屏取消所有文字遮挡，完整保留书籍封面。关机失败继续显示失败提示。

## 小纸目录与电量候选版（未实机验收）

- 设置面板加入 EPUB 目录，支持层级、小节锚点、目录翻页与返回；无目录时回退章节列表。
- 小纸书库和设置显示电量；正文仅低电量显示，20%出现、25%解除，随正常重绘更新。
- 推荐可选 SD 字体霞鹜文楷 GB Regular v1.530，保留现有默认字体。
- 修复复合字形内部相邻索引覆盖造成的错误渲染或越界；加入真实渲染回归。

## 小纸界面调整候选（2026-11-19）

- 电池改为细长轮廓、连续黑色填充，充电闪电标记放在电池外侧；书库、设置、正文页脚统一中心线对齐。
- 目录改为正文顶部下拉调出，移除设置中的目录按钮；目录翻页、跳转、返回方式不变。

- 按用户最终要求，去掉所有阅读/电池百分比与设置中的“电量”标签；保留电池图形、正文页码与进度保存。

## 2026.11.19-preview.2（待实机验收）

- 目录按当前章节/小节定位到所在页，并高亮显示；章内锚点批量定位并缓存，避免反复解压。
- 书库移除电池，顶部下拉可查看机型、版本和电池；电池改浅灰，保留图标而不显示百分比。
- 小纸快速正文在实际DU前阈值化目标帧，修正显示黑白但历史帧记录灰阶造成的驱动不一致；原波形、相数、电压保持不变。局部刷新不重置整页清理周期，只有成功清理才重置。实际残影改善仍待实测。
- 目录/设置/设备信息及返回正文时使用灰阶刷新；正文低电量图标显示期间也使用灰阶刷新。
