# 版本变更 / Changelog

按日期和作者简述对用户可见的功能变化；详细实现历史见 Git。使用方法见 [README](../README.md)。
User-visible changes by date and author; Git retains implementation history. See [README](../README.md) for usage.

## 待发布 / Unreleased — 2026-11-18

- Metalio：默认锁屏改为居中的 Cloud Zao 黑白标志，书籍封面锁屏保留。/ Metalio uses a centered monochrome Cloud Zao mark while keeping prepared book covers. · Metalio

- Metalio：按 UTF-8 读取存储卡文件名，修正中文字体名称显示为下划线及 `~1/~2` 短名称的问题。/ Read card filenames as UTF-8 on Metalio so Chinese font names retain their names instead of DOS aliases.

- 新增 Metalio E-Ink 4 Plus 独立 S31 构建，复用阅读器；支持 HOME/设置/全刷、音量翻页、双色温前光、SD 截图和封面浅睡。仅主机测试及编译通过，尚未烧录或完成硬件验收。
  Add a separate S31 build for Metalio with shared reading, HOME/settings/full refresh, volume paging, dual-temperature light, SD screenshots and cover light sleep. Host tests and builds passed; not flashed or hardware-accepted.
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

- 两款固件的软件关机留屏统一为“已关机”；无名锁屏移除底部操作提示，完整保留书籍封面。关机失败仍显示失败提示。
