# WordLens 交接说明

## 项目目标

WordLens 是一个 Windows 10/11 桌面应用：周期性截取屏幕或当前窗口，调用
Tesseract 识别英文，通过词典和用户词表筛选出生词，并在顶部悬浮栏展示。用户可
查看中文释义、复制、标记为已掌握或忽略。

技术栈：C++17、Qt 6 Widgets、CMake、Ninja、MinGW、Tesseract 5。

## 目录与职责

- `src/AppController.*`：应用编排。负责截图、OCR、过滤、会话、配置和窗口间信号。
- `src/TesseractOcr.*`：Tesseract 子进程调用、截图预处理和 OCR 结果解析。
- `src/WordFilter.*`：英语词、常用词、已掌握词和忽略词的过滤。
- `src/WordSession.*`：本次运行的生词收件箱、过期和自动收起逻辑。
- `src/Dictionary.*`：`data/dict.bin` 的内存映射和二分查词。
- `src/OverlayBar.*`、`src/WordChip.*`：始终置顶的词条栏与单个词条交互。
- `src/DefinitionCard.*`：悬停释义卡片。
- `src/DetailsPanel.*`、`src/SettingsDialog.*`：生词列表和设置窗口。
- `data/`：运行所需的词表和离线词典，应提交到仓库。
- `tools/`：词典生成、会话检查和真实桌面 UI 冒烟脚本。
- `tools/bench/`：识别内核基准（4K 语料、Tesseract / RapidOCR / Windows OCR
  多后端对比、打分脚本和 `REPORT.md` 结论）。
- `scripts/`：Windows 构建与启动脚本。

## 识别内核约定

调 OCR 参数前先读 `tools/bench/REPORT.md`，改完用 `tools/bench/score.py` 重跑对比。

- **不要加 `tessedit_char_whitelist`。** 白名单不含空格时 LSTM 会丢掉空格，把整行
  合并成一个词，词典查不到，悬浮栏几乎是空的（F1 8.1%）。加回空格也只有 79.8%，
  因为数字和标点同样被压制。现在是 F1 99.3%。
- **不做二值化。** 灰度 + 深色反色直接交给 Tesseract；Otsu 阈值反而低约 1.4 个
  F1 点，还多花 150–350 ms。
- **默认不放大**（`ocrScalePercent = 100`）。4K@150% 的字号已经够大；1080p 上的
  12 px 小字才需要调回 150%。
- 单线程（`OMP_THREAD_LIMIT=1`）、`--psm 6`、关表格分析、`tessdata_fast`。
  `tessdata_best` 在屏幕文字上没有更准，慢 2.3 倍。
- 按空白行切段并行只有 1.3× 加速，却要 3× CPU 且损失 0.3 F1，已否决；
  RapidOCR 精度相当但慢 5×，Windows OCR 快 3× 但需要 en-US 识别包。

## 词条交互约定

- 单击词条复制单词。
- 词条悬停约 280 ms 显示释义卡片。
- 右键可标记已掌握或不再显示。
- 按住词条上下拖动至少 16 像素，松开后标记为已掌握。`WordChip` 会捕获鼠标，
  因此指针离开词条范围后仍应完成该操作；滑动确认时词条会变绿。
- “已掌握”会写入 `AppConfig::knownWords`，并由 `WordFilter` 在后续 OCR 中过滤。
  不要为滑动操作新增另一份状态或词表。

## 悬浮栏布局约定

- 左侧**只保留像素茄子**作为状态指示，不再有独立的状态点或固定宽度的状态文字。
  识别时茄子上下跳动并亮蓝灯，暂停时闭眼亮黄灯，引擎出错亮红灯；状态文字只在
  错误、暂停和临时提示（`flashMessage`）时出现在茄子右侧。
- 词条一律按 `sizeHint()` 的整颗宽度显示，`WordChip` 使用 `Fixed` 尺寸策略并实现
  `minimumSizeHint()`。不要再把词条塞进 `QScrollArea` 让布局压缩它：`paintEvent`
  按固定坐标绘制，压缩后文字会被裁切。
- 可见词条数由“窗口最大宽度 − 固定控件宽度”和 `maxChips` 共同决定，放不下的词
  收进紧跟词条的 `+N` 按钮；`+N` 点击打开生词列表。词条间距为 10 px。
- `updateWidth()` 里测量固定控件宽度时按控件逐个累加，不要依赖刚隐藏了
  `m_chipHost` 的行布局 `sizeHint()`，它可能还带着旧缓存，导致只剩一颗词条。
- 指针靠近悬浮栏时仍冻结词条的增删和整栏宽度；暂停/错误的状态文字出现或消失
  也等指针离开后再应用，避免按钮在指针下移动。
- 修改布局后确认：多词时每颗胶囊文字完整、间距清楚、`+N` 数量正确；茄子识别时
  会动、暂停时闭眼变色；空栏显示提示而不是空白。

## 构建与启动

开发机配置记录在 `scripts/build.ps1`：Qt 6.10.2、MinGW 13.1、CMake 和 Ninja。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/run.ps1
```

`WordLens.exe` 依赖 Qt 和 MinGW DLL。`CMakeLists.txt` 的 `POST_BUILD` 步骤会运行
`windeployqt`，并复制 MinGW 运行时到 `build/`，因此构建后可直接打开
`build/WordLens.exe`。修改此部署逻辑后，务必验证不依赖开发机 `PATH` 的直接启动。

Tesseract 的寻找顺序是：设置中的路径、系统 `PATH`、常见 MSYS2 路径、
`tools/tesseract/`。后者供本机或发布包使用，不应提交到 Git。

## 验证

```powershell
# 无界面收件箱、自动收起和词典门槛检查
powershell -NoProfile -ExecutionPolicy Bypass -File tools/test-session.ps1

# 会移动真实鼠标，请在空闲桌面执行
powershell -NoProfile -ExecutionPolicy Bypass -File tools/ui-smoke.ps1
```

修改词条交互时，除构建和 `test-session.ps1` 外，还要手动确认：普通点击仍复制；
上下滑动会移除词条；重启后该词仍不会出现；右键和释义卡片操作未回归。

## 数据与本机状态

- `data/dict.bin` 是项目提供的离线词典，保留并提交。
- `.cache/ECDICT/ecdict.csv` 是下载的原始词典，仅用于重新生成 `dict.bin`，不要提交。
- 用户设置保存在 `%LOCALAPPDATA%\WordLens\WordLens\wordlens.ini`，不在工作区。
- `build/` 是可再生的构建和部署目录，绝不提交。

## 修改原则

- 词典只显示可读释义；不要让无释义 OCR 碎片进入悬浮栏。
- 悬浮栏在指针靠近时会冻结列表重排，防止按钮在鼠标下移动。需要立即移除用户
  主动处理的词条时，应明确处理其可见状态。
- 保持 Windows/Qt 兼容性。核心库不依赖 Widgets；新的核心逻辑优先放入
  `wordlens_core`。
