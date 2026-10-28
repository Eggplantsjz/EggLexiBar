# WordLens

顶部悬浮的英文生词栏：自动识别屏幕上的英文，只留下你可能不认识的词，鼠标停一下就能看到中文释义。

## 功能

- **全屏 / 当前窗口 / 光标周围** 三种识别范围，后台线程调用 Tesseract，界面不会卡。
- 静态画面自动跳过 OCR（缩略图差异判断），避免持续占用 CPU。
- 生词筛选：`words_alpha.txt` 词典校验 + `common-10000.txt` 常用词过滤 + 用户“已掌握 / 不再显示”词表。
- **只保留查得到释义的词**：离线词典里没有条目、只有 `abbr.` 缩写释义、或只有人名 / 地名译名的识别碎片（如 `clk`、`nese`、`hile`）不会占用坑位；`clang-format` 这类没有词条的复合词也一样。
- 顶部悬浮栏：词条胶囊（出现次数、新词高亮）、状态灯、暂停、列表、设置、隐藏；词条超过上限时显示 `+N`。
- **生词收件箱**：新词入队，旧词不会被新扫描冲掉；未处理的词一直保留，直到“认识 / 忽略 / 清空”。
- **自动降噪**：同一个词出现 3 次以上、停留超过 20 秒却从未悬停或点击，就自动移到列表里的“疑似已掌握”，可在列表一键恢复。
- **悬停词条约 0.3 秒弹出释义卡片**：音标 + 中文释义；词形变化（复数、-ing、-ed 等）会自动回退到原形。
- 卡片可直接点 **✓ 认识 / ✕ 忽略 / 复制**；单击词条仍然复制，上划或下滑词条可立即标记为已掌握，右键可“标记为已掌握 / 不再显示”。
- 生词列表窗口：搜索、排序、状态列、复制、批量标记、恢复已收起的词、清空。
- 托盘图标 + 全局快捷键 `Ctrl+Alt+W`；单实例运行；配置自动保存。

## 运行环境

- Windows 10 / 11（x64）
- Qt 6.5+（Widgets）
- CMake 3.21+ 与 Ninja（或 Visual Studio 生成器）
- Tesseract 5.x（运行时依赖，需包含 `eng.traineddata`）

开发机上使用的组合：Qt 6.10.2 (MinGW 13.1) + Tesseract 5.5.3（`D:\msys64\ucrt64\bin\tesseract.exe`）。

## 构建

```powershell
# 构建（默认使用 D:\Qt\6.10.2\mingw_64，可在脚本里改）
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1

# 启动
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/run.ps1
```

手动构建：

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release `
      -DCMAKE_PREFIX_PATH=D:/Qt/6.10.2/mingw_64
cmake --build build
```

Tesseract 检测顺序：设置里指定的路径 → `PATH` 中的 `tesseract` → `D:\msys64\ucrt64\bin\tesseract.exe` 等常见位置。
也可以把 Tesseract 整个目录放到 `tools/tesseract/`，构建时会被复制到输出目录。

## 使用

- 状态栏默认在屏幕顶部居中，可拖动；位置会记住。
- 鼠标滚轮在词条上横向滚动。
- 鼠标停在词条上：约 0.3 秒后弹出释义卡片；卡片可以停留，直接点“认识 / 忽略 / 复制”，鼠标移开约 0.24 秒后收起。
- 单击词条：复制单词；上划或下滑词条：标记为已掌握并不再显示；右键：更多操作。
- 右侧按钮依次是：暂停识别、生词列表、设置、隐藏。
- 词条超过“状态栏最多显示”的数量时，右侧出现 `+N`，点击打开完整列表。
- 指针靠近状态栏时，词条的增删和排序会暂时冻结（避免按钮在指针下移走）；指针移开后自动补齐。
- 未处理的词默认一直保留，不会被下一屏识别结果顶掉；点“认识 / 忽略”或清空才会离开收件箱。
- 自动收起的词仍保留在生词列表里，状态显示为“疑似已掌握”，选中后点“恢复显示”即可放回悬浮栏。
- `Ctrl+Alt+W` 显示 / 隐藏；托盘图标单击同样可以切换。
- 隐藏状态栏不会停止识别，只是不显示。

设置项包括识别间隔、识别范围、识别精度、页面模式、置信度下限、最小词长、Tesseract 路径、常用词过滤、自动降噪阈值、词条数量、字号、不透明度、词条保留策略（默认“未处理的生词一直保留”）、自动隐藏。

关闭“未处理的生词一直保留”后，词条会按“过期时间”自动消失；自动降噪也可以单独关闭或调整“出现 N 次 / 停留 N 秒”的阈值。

配置保存在 `%LOCALAPPDATA%\WordLens\WordLens\wordlens.ini`。

## 离线词典

`data/dict.bin` 由 `tools/build-dict.py` 从 [ECDICT](https://github.com/skywind3000/ECDICT)（MIT License）生成，只保留 `words_alpha.txt` 中可能被识别到的单词，约 19 万条、12.5 MB。

重新生成：

```powershell
# 下载 ecdict.csv 到 .cache/ECDICT/ecdict.csv（约 66 MB）
python tools/build-dict.py
```

文件格式（小端）：

```
magic[8] = "WLDICT1\0"
uint32 count
uint32 blobOffset = 16 + count * 12
record[count]: uint32 keyOffset, uint32 defOffset, uint16 keyLength, uint16 defLength
blob: 小写单词 + 释义（"音标\n释义1\n释义2..."）
```

运行时用 `QFile::map` 内存映射 + 二分查找，不复制数据，启动开销可以忽略。

查词命令行工具：

```powershell
build\wlocr.exe --define debug copilot
```

## 识别内核与性能

识别管线：截图 → 灰度 → 深色主题反色 → Tesseract（LSTM、单线程、`--psm 6`、关表格分析）→ 解析 TSV。

两个关键决定都有实测依据，见 [`tools/bench/REPORT.md`](tools/bench/REPORT.md)：

- **不使用 `tessedit_char_whitelist`**。字符白名单不含空格时，LSTM 会把整行合并成一个
  词（`everydistributedsystemis...`），词典查不到，悬浮栏几乎是空的；把空格加进去也只能
  到 79.8 F1，因为数字和标点同样被压制。完全去掉白名单后 F1 从 8.1% 升到 99.3%。
- **不做 Otsu 二值化，默认不放大**。抗锯齿的灰度边缘比阈值图更容易区分 `rn`/`m`、
  `l`/`I`，二值化反而低约 1.4 个 F1 点，并多花 150–350 ms。

在 4K（3840×2160，150% 缩放）基准语料上实测：

| 场景 | 耗时 |
| --- | --- |
| Tesseract 默认多线程 | 约 37 s |
| 旧内核（白名单 + 二值化 + 放大 1.5 倍） | 约 2.7 s（F1 8.1%） |
| 现内核（灰度 + 原分辨率 + psm 6） | 约 2.1 s（F1 99.3%） |
| 深色背景简单文字 | 约 0.8 s |
| 按空白行切分 4 段并行 | 1.3× 加速，但 CPU 占用 3×、F1 −0.3，未采用 |

因此默认 `OMP_THREAD_LIMIT=1`，并优先建议使用“当前窗口”或“光标周围 900×560”缩小识别范围。画面没有变化时会直接复用上次结果，不再调用 Tesseract。
1080p 屏幕上 12 px 小字可以在设置里把“识别精度”调回 150%。

## 测试

```powershell
# 收件箱规则（自动收起 / 一直保留 / 恢复），无界面，秒级完成
powershell -NoProfile -ExecutionPolicy Bypass -File tools/test-session.ps1

# 真实桌面：悬停卡片、移入卡片、点复制按钮
powershell -NoProfile -ExecutionPolicy Bypass -File tools/test-card-actions.ps1

# 真实桌面：设置窗、生词列表、右键菜单、点词复制、隐藏按钮
powershell -NoProfile -ExecutionPolicy Bypass -File tools/ui-smoke.ps1

# 真实桌面：临时把自动收起阈值调到 2 次 / 6 秒，验证状态列出现“疑似已掌握”
# 结束后自动还原配置文件，并关掉测试用的浏览器页
powershell -NoProfile -ExecutionPolicy Bypass -File tools/test-snooze.ps1
```

后三个脚本会驱动真实鼠标，运行时不要同时操作鼠标；`ui-smoke.ps1` 结束时会点“隐藏”，需要重新启动或按 `Ctrl+Alt+W` 才能继续测试。

识别内核的对比测试独立放在 `tools/bench/`，改 OCR 参数前后都应该重跑：

```powershell
# 渲染 4K 基准语料并生成 ground truth（只需一次）
python tools/bench/make_corpus.py

# 各后端的识别结果
python tools/bench/run_tesseract.py
python tools/bench/run_tesseract_bands.py
python tools/bench/run_rapidocr.py
powershell -NoProfile -ExecutionPolicy Bypass -File tools/bench/run_winocr.ps1

# 打分：词袋 P/R/F1、稀有词召回、合并行比例、耗时
python tools/bench/score.py
```

`run_rapidocr.py` 需要 `tools/bench/.venv`（见 `tools/bench/requirements.txt`）。

## 已知限制

- 只识别英文（`eng.traineddata`），不支持中英混排识别。
- 释义来自离线词典；词形回退只覆盖常见后缀（`-s/-es/-ed/-ing/-er/-est/-ly` 等）。
- **栏里只会出现能弹出释义卡片的词**：词典里查不到的词、`clang-format` 这类没有词条的复合词、只有 `abbr.` 缩写释义的词，以及只有 `[人名]` / `[地名]` / `[网络]` 译名的词都会被丢弃。
- 词典门槛按“释义是否可读”判断：以词性（`n.` / `vt.` / `interj.` 等）或学科标签（`[计]` / `[医]` / `[法]` 等）开头的释义才算数。个别冷僻的技术缩写（如 `aff`、`ars`）因为确有 `[计]` 释义仍会显示，点 ✕ 忽略即可持久屏蔽。
- 如果希望连查不到的词也显示，可以删掉 `data/dict.bin` 再运行（此时悬停没有释义）。
- 自动降噪只在画面变化后触发；停在静态页面阅读时不会把词收走。
- 尚未打包 Tesseract 运行时，发布时需要一并复制 `tesseract.exe`、DLL 和 `tessdata`。
- 全屏 4K 密集文本仍需要数秒，建议配合识别范围使用。

## 目录结构

```
src/                应用与核心源码
  AppController.*   采集 → OCR → 过滤 → UI 主流程
  OverlayBar.*      顶部悬浮栏
  WordChip.*        单词胶囊
  DefinitionCard.*  释义卡片
  Dictionary.*      离线词典（mmap + 二分）
  TesseractOcr.*    tesseract 子进程调用与预处理
  WordFilter.*      词典 / 常用词 / 已掌握过滤
  DetailsPanel.*    生词列表
  SettingsDialog.*  设置
  HotkeyFilter.*    全局热键
  main_cli.cpp      wlocr 命令行工具
data/               词典数据（words_alpha / common-10000 / dict.bin）
tools/              构建词典、UI 冒烟测试等辅助脚本
  bench/            识别内核基准：语料生成、多后端 runner、打分与报告
scripts/            构建 / 运行脚本
```
