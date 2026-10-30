# WordLens 移动端移植与跨设备同步方案

> 目标：手机 / 平板能用同一套产品（识别屏幕英文 → 只留生词 → 悬浮展示 → 标记已掌握），
> 并且和 Windows 端的词表互通。
>
> 结论先说：**可行，但不建议把现在的 Qt 代码直接编译到 Android。**
> 推荐「共享数据资产与同步协议，平台各自实现外壳」：Windows 端保持现状，Android 端用
> Kotlin 原生实现，两端靠一份追加式操作日志（op log）同步词表。

---

## 1. 为什么不能“同一套代码搬过去”

现有代码里有四处是 Windows/Qt 桌面专属的，Android 上没有等价能力：

| 能力 | 现在怎么做 | Android 的现实 | 处理方式 |
| --- | --- | --- | --- |
| 截屏 | `QScreen::grabWindow(0)`（`AppController.cpp`） | 只能 `MediaProjection`，**每次识别会话都要用户点一次系统授权**（Android 14+ 不可长期持有） | 前台服务 + `ImageReader` |
| OCR | `QProcess` 起 `tesseract.exe` 子进程 | 没有子进程；Tesseract 需要 NDK 编译，tess-two 已停更 | 优先用无障碍读文本，兜底 ML Kit |
| 悬浮窗 | 无边框 + `WindowStaysOnTopHint` 的 `QWidget` | 必须 `TYPE_APPLICATION_OVERLAY` + `SYSTEM_ALERT_WINDOW` 授权，由前台服务托管 | 原生 overlay + Compose/自定义 View |
| 交互 | 鼠标悬停 0.3 s 出卡片、右键菜单、拖拽 | 没有悬停/右键，全部要改成点按、长按、滑动 | 重做交互层 |

而真正可以复用的部分（词典、过滤规则、会话语义、存储结构）不到 1500 行，重写成 Kotlin
比给 Qt 打 JNI 补丁更省事，也更好维护。

现有可复用资产：

- `data/dict.bin`（12.8 MB，格式固定：`magic + count + 12 字节记录表 + blob`），Kotlin 用
  `RandomAccessFile` + 二分查找 100 行就能读，不必重新生成。
- `data/words_alpha.txt`、`data/common-10000.txt`（4 MB / 100 KB），直接进 assets。
- `tools/build-dict.py`：词典生成管线两端共用，Android 只是消费产物。
- `WordFilter` / `WordSession` / `WordStore` 的**行为语义**（同义词形回退、`hasUsableSense`、
  记忆曲线自动降噪、未处理词一直保留）。
- ECDICT 是 MIT，可以随 APK 分发。

---

## 2. 三条路线对比

| | A. Qt for Android | **B. Kotlin 原生（推荐）** | C. Rust/C++ 内核 + 双端薄壳 |
| --- | --- | --- | --- |
| 代码复用 | 核心 C++ 全复用 | 复用数据资产 + 规则语义 | 内核全复用，含同步 |
| 悬浮窗 | Qt 没有系统级 overlay，需要自己包 `SurfaceView`，风险高 | 标准做法，成熟 | 同 B |
| OCR | Tesseract NDK 编译，参数调优要重跑基准 | 无障碍零成本 / ML Kit | 同 B |
| 触摸 UI | Qt Widgets 不适合触摸，得写 QML | Compose 原生 | 同 B |
| APK 体积 | Qt 运行库 +20~40 MB | ~25 MB（含 ML Kit 模型） | 略小 |
| 工期 | 4~6 周且坑多 | **5~7 周可控** | 8~12 周（要先重构 PC 端） |
| iOS 可复用 | 否 | 否 | 是 |

**建议：现在走 B。** 如果确定要做 iOS，再考虑 C（把内核抽成 Rust crate，PC/Android/iOS
三端共用），但那是一次单独的重构项目，不应该阻塞手机端上线。

---

## 3. 目标架构

```
┌──────────────────────┐        ┌───────────────────────┐
│ Windows (Qt/C++)     │        │ Android (Kotlin)      │
│                      │        │                       │
│ capture  → Tesseract │        │ Accessibility /       │
│ filter   → session   │        │ MediaProjection+MLKit │
│ overlay/pet (Widgets)│        │   → filter → session  │
│                      │        │   → overlay (Compose) │
│ WordStore (SQLite)   │        │ Room (同 schema)       │
└──────────┬───────────┘        └───────────┬───────────┘
           │        SyncLog（append-only ops）│
           └───────────────┬─────────────────┘
                           ▼
              ┌───────────────────────────┐
              │ ops/<device>.jsonl        │
              │ 传输：网盘文件夹 / 局域网  │
              │      直连 / 自建中继(可选) │
              └───────────────────────────┘
```

两端**不共享 UI 代码、不共享 OCR 结果**，只共享“用户学会了哪些词”这类小状态。

---

## 4. Android 端选型

- **minSdk 26（Android 8）**，targetSdk 35（Android 15）。低于 8 的设备 overlay 行为差异太大。
- **采集（默认）：`AccessibilityService`**
  - 直接读 `AccessibilityNodeInfo` 的文本，等于拿到“干净文本”，没有 OCR 误差，几乎不耗电。
  - 可按包名白名单/黑名单控制（浏览器、阅读器、IDE 类应用最有效）。
  - 局限：游戏、视频、Flutter/Canvas 自绘界面拿不到文本 → 用模式二兜底。
- **采集（兜底）：`MediaProjection` + `ImageReader`**
  - 用户点“开始识别”→ 系统弹窗授权 → 前台服务持有 `VirtualDisplay`。
  - 只截取屏幕下半部/中间区域，降采样到 1080p 级别再做 OCR（和 PC 端 `ocrScalePercent`
    的结论一致：屏幕文字不需要放大）。
  - **静态画面跳过**：沿用 PC 的缩略图差异判断，画面没变就不 OCR。
- **OCR：ML Kit Text Recognition v2（拉丁字母，本地模型）**
  - 免费、离线、精度足够；APK 增加约 20 MB；不出来 CJK，正好只识别英文。
  - 若要求完全开源 / 上 F-Droid：换 **PaddleOCR-mobile（NCNN 推理）**，精度相当、包体
    +8 MB、速度略慢。先用 ML Kit，保留 `OcrEngine` 接口以便替换。
- **悬浮窗**：`ForegroundService` + `WindowManager.addView(TYPE_APPLICATION_OVERLAY)`
  - 窗口加 `FLAG_SECURE`：**非常重要**，这样自己的悬浮栏不会被 MediaProjection 录进去，
    否则 OCR 会读到自己的词条，形成反馈环。
  - 交互：胶囊条默认贴顶/贴边，可拖动；点一下展开词表卡片；左右滑动翻页；长按单词 =
    右键菜单（已掌握 / 不再显示 / 复制）。
  - 手机上“茄子吃词”仍然可做（同样的空闲计时 + 飞行动画），但默认关闭，省电。
- **词典**：直接读 `dict.bin`（12.8 MB 放 assets，首次启动拷到 `filesDir`）。
  - `RandomAccessFile` + 记录表二分查找；`hasUsableSense()` / 词形回退 / OCR 混淆修复
    用 Kotlin 重写，配同一份 golden cases。
  - 可选优化：把 `dict.bin` 转成 SQLite（`word TEXT PRIMARY KEY`），体积会涨到 ~25 MB，
    但省掉手写解码。建议先读 bin，简单且省空间。
- **存储**：Room，表结构与 `WordStore` 完全一致（`known` / `blocked` / `filter_state`），
  方便以后做“直接拷库”的迁移，也方便写同一套测试。
- **设置**：DataStore Preferences，字段与 `AppConfig` 对齐（截图间隔、词长、常用词阈值、
  自动降噪阈值……）。
- **技术栈**：Kotlin 2.x + Compose + Coroutines/Flow；不要引入 Koin/Hilt 这类重框架，
  这个应用规模用不上。

### 手机上必须重新设计的点

| PC | Android |
| --- | --- |
| 悬停 280 ms 出释义 | 点击词条展开释义卡片（点外部收起） |
| 右键菜单 | 长按词条 |
| 上下拖动标记已掌握 | 上滑词条删除（与 PC 一致的手势隐喻） |
| 全局快捷键 `Ctrl+Alt+W` | 通知栏常驻开关 / 快捷方式磁贴 |
| 托盘图标 | 前台服务常驻通知（Android 强制要求） |
| 多显示器拖拽 | 不需要，改为“贴边停靠” |

---

## 5. 同步机制

### 5.1 同步什么

| 数据 | 同步 | 原因 |
| --- | --- | --- |
| 已掌握词表 `known` | ✅ | 核心价值：电脑上标过的词，手机上不再出现 |
| 忽略词表 `blocked` | ✅ | 同上 |
| 学习相关设置（词长、常用词阈值、自动降噪阈值） | ✅（可选开关） | 让体验一致 |
| 自动降噪状态 `filter_state` | ❌ | 带时间戳、设备相关，混在一起会互相干扰 |
| 本次运行的收件箱 / 悬浮栏内容 | ❌ | 屏幕内容因设备而异 |
| 词典 `dict.bin` | ❌ | 随版本发布，版本号不同步 |
| 窗口位置、透明度、字号 | ❌ | 设备本地偏好 |

原则：**只同步“结论”，不同步“过程”。** OCR 结果、盒子坐标、命中次数一律留在本机。

### 5.2 数据模型：追加式操作日志（op log）

每台设备只写自己的文件，永不修改别人的文件，因此网盘同步不会产生冲突副本。

```
sync/
  ops/
    pc-8f3a.jsonl
    pixel-2c71.jsonl
  devices.json          # 设备名、最后同步时间、公钥（可选）
```

每行一个操作：

```json
{"v":1,"dev":"pc-8f3a","seq":1042,"ts":1761800000123,"op":"known.add","word":"elapsed"}
{"v":1,"dev":"pc-8f3a","seq":1043,"ts":1761800040980,"op":"blocked.add","word":"clk"}
{"v":1,"dev":"pixel-2c71","seq":77,"ts":1761801000456,"op":"known.remove","word":"elapsed"}
```

- `op`：`known.add` / `known.remove` / `blocked.add` / `blocked.remove` / `pref.set`。
- `ts`：毫秒时间戳，配合 `dev` 做决胜（LWW，最后一个写入者胜）。
- `seq`：设备内自增，用于去重与断点续传。
- 删除用**墓碑**（`known.remove`），而不是物理删除，否则另一台设备下次合并会把词加回来。

合并规则（两端相同）：

```
for each op in all files, ordered by (ts, dev, seq):
    key = op.op.split('.')[0] + ':' + op.word
    if op.seq <= seen[key].seq and op.dev == seen[key].dev: skip   # 幂等
    if (op.ts, op.dev) >= (seen[key].ts, seen[key].dev):
        seen[key] = op
apply seen → 本地 known/blocked 表
```

结果：任意顺序、重复应用都不改变最终状态（可交换、幂等），这是它能安全跑在网盘上的原因。

### 5.3 传输分三档，按需要逐级加

**档 1：共享文件夹（v1，零后端，推荐先做）**

- 用户在设置里选一个文件夹：OneDrive / Dropbox / Google Drive / Nextcloud / **Syncthing** 都行。
- 每台设备每 30 s 或每次词表变更后写自己的 `ops/<dev>.jsonl`；启动时和文件变化时
  （`QFileSystemWatcher` / Android `WorkManager` + SAF 变更监听）读入所有文件合并。
- 优点：不需要账号、不需要服务器、离线可用、隐私最好。Syncthing 还能做到纯局域网同步。
- 成本：PC 端 1~2 天，Android 端 2~3 天（SAF 目录授权、后台刷新）。

**档 2：局域网直连（v2，体验最好）**

- 两端在同一 Wi-Fi：PC 显示二维码（`wordlens://pair?dev=…&pk=…&addr=192.168.1.7:7788`），
  手机扫码后建立 TLS 连接，交换 op。
- TLS 自签证书 + SPKI pin，配对时写入的 32 字节密钥做互信；不依赖公网、不需要账号。
- 适合“每天回家手机电脑自动同步”的场景；mDNS（`_wordlens._tcp`）做发现。
- 成本：两端各 2~3 天。

**档 3：托管中继（v3，可选）**

- 只有在用户明确要“跨网络、免开电脑”时才做：一个 200 行的 Go/Rust 服务（SQLite + REST），
  或直接用 Supabase 之类。op 仍然 E2E 加密（XChaCha20，密钥来自用户口令），服务器只存密文。
- 成本：1~2 周（含部署、账号、配额），不建议进 v1。

### 5.4 冲突与边界情况

| 场景 | 结果 |
| --- | --- |
| PC 标“已掌握”，手机标“忽略”同一个词 | 两个集合都生效；展示时以 `blocked` 优先，`known` 不显示词条 |
| 两端离线各自标了同一个词 | 两条 op 都保留，合并后仍是“已掌握”，无副作用 |
| 用户在列表里“恢复显示” | 生成 `known.remove`，带新时间戳，覆盖旧的 add |
| 设备时钟不准 | 时间戳只用于同词决胜；`(ts, dev)` 排序，偏差几分钟无影响 |
| 日志变长 | 每 1000 条压缩成快照（`snapshot.json` + 保留 90 天墓碑），或直接保留（词表操作很少） |

### 5.5 隐私

词表就是“我在学哪些单词”，属于弱敏感数据，但仍按敏感处理：

- 网盘文件夹方案：op 文件用配对密钥加密（`age`/XChaCha20-Poly1305），文件名不含明文。
- 不上传 OCR 原文、截图、窗口标题。
- 设置页明确写“同步只包含已掌握/忽略的词表”。

---

## 6. 分阶段计划

假设 1 名熟悉 Qt + Android 的开发者，按“可交付”切分。

### M0 · 同步内核 + PC↔PC（3~4 天）

- 在 `wordlens_core` 新增 `SyncLog`（读写 JSONL、合并、墓碑、幂等测试）。
- `AppController`：所有 `knownWords` / `blockedWords` 变更处发 op（当前散落在
  `markKnown`、`blockKnown`、列表批量操作、恢复显示）。
- 启动时合并 + `WordStore` 回写；`QFileSystemWatcher` 增量再合并。
- 设置页加“同步文件夹”一项。
- 验收：两台 PC 指向同一网盘目录，A 标已掌握 → B 30 s 内消失；离线各自操作后重连结果一致；
  `tools/test-session.ps1` 增加同步用例。

### M1 · Android MVP（2~3 周）

1. 工程骨架：AGP + Kotlin + Compose，`minSdk 26`，单 Activity + 前台服务。
2. 词库层：`dict.bin` 读取、`words_alpha`/`common` 加载、过滤规则、golden tests
   （直接复用 PC 的 30 个样例词与期望结果）。
3. 采集层：`AccessibilityService`（默认）+ `MediaProjection`（可选模式）。
4. OCR：ML Kit 包装成 `OcrEngine` 接口；置信度/词长过滤与 PC 一致。
5. 悬浮窗：overlay 服务、胶囊条、翻页、展开卡片、复制/已掌握/忽略；`FLAG_SECURE`。
6. 存储与设置：Room + DataStore，字段对齐 `AppConfig`。
7. 同步：读写 SAF 目录下的 `ops/<dev>.jsonl`，与 PC 互通。
- 验收：手机上识别网页 → 出现生词 → 标已掌握 → PC 端 1 分钟内不再出现该词。

### M2 · 对齐与打磨（1~2 周）

- 自动降噪、未处理词保留、排序（按难度）与 PC 完全一致。
- 耗电与发热：静态帧跳过、识别间隔自适应（前台 1.5 s / 后台 5 s）、省电模式。
- 权限引导：无障碍 → 悬浮窗 → 通知 → 电池优化白名单，一页说明清楚为什么需要。
- 折叠屏/平板：宽度分档（手机单行 4~5 词，平板 8~10 词），横竖屏切换不丢状态。
- 可选：茄子吃词动画（默认关）。

### M3 · 发布（3~5 天）

- 签名 APK + GitHub Releases；`privacy.md`（录屏/无障碍用途声明）。
- 若要上 Google Play：`MediaProjection` 前台服务类型声明、`SYSTEM_ALERT_WINDOW` 说明、
  数据安全表单；无障碍权限需要“核心功能”正当性说明，审核较严，建议先走 APK 分发。
- 若要上 F-Droid：把 ML Kit 换成 PaddleOCR-mobile，去掉闭源依赖。

总计约 **5~7 周**（不含托管中继服务）。

---

## 7. 与现有代码的衔接点

| 现有文件 | 需要做的事 |
| --- | --- |
| `src/Settings.h` | 新增 `syncFolder`、`syncEnabled`、`deviceId`、`syncPrefs` |
| `src/WordStore.*` | 增加 `applyOp()` / `setKnown(word, bool)`，让写入都经过同步层 |
| `src/AppController.cpp` | 已知/忽略/恢复的 4 处入口统一走 `SyncLog` |
| `tools/build-dict.py` | 产物同时进 Android `assets/`（CMake 里加一条拷贝规则） |
| `tools/test-session.ps1` | 抽成 JSON golden cases，Android 侧 JVM 单测读同一份文件 |
| `data/` | 词典与词表体积不变，Android 直接复用 |

---

## 8. 风险与对策

| 风险 | 影响 | 对策 |
| --- | --- | --- |
| MediaProjection 每次要授权（14+） | 体验断点 | 以无障碍为主路径；录屏模式明确提示“本次会话有效” |
| 无障碍权限被 Play 审核质疑 | 上架受阻 | 先 APK 分发；Play 版只保留录屏模式 |
| 持续 OCR 耗电 | 手机发热、掉电 | 静态帧跳过 + 区域限定 + 间隔自适应 + 只在亮屏且解锁时运行 |
| 悬浮窗挡内容 | 用户反感 | 可拖动、半透明、贴边自动收成小圆点、闲置淡出 |
| 词典 12.8 MB + ML Kit 模型 | APK 体积 ~35 MB | 可接受；或用 App Bundle 按 ABI/语言拆分 |
| 手机/PC 识别结果不同 | 用户困惑“为什么电脑有手机没有” | 词表同步、词条不同步，文档写清楚；两端规则用同一份 golden cases |
| 多设备同时“恢复显示”与“标记已掌握” | 状态来回跳 | LWW + `(ts, dev)` 决胜，结果稳定不抖动 |
| 现网 `wordlens.ini` 与 sqlite 双存储 | 迁移复杂 | M0 先把 sqlite 定为唯一真源，ini 只留窗口位置等本地项 |

---

## 9. 需要拍板的三件事

1. **Android 端接受 Kotlin 重写 UI 吗？** 这是方案 B 的前提；不接受就只能走 Qt/QML 路线，
   工期更长且悬浮窗风险高。
2. **同步先做哪一档？** 建议先做“网盘/Syncthing 文件夹”（无需账号、当天可验证），
   局域网直连放第二期；托管服务除非要“跨网络免开电脑”，否则不做。
3. **要不要 iOS？** 要的话现在就把内核设计成可移植（Rust 或纯 C++ 无 Qt），
   否则 Android 的 Kotlin 实现以后要再写一遍。
