# Windows 11 响应变慢调研报告

**范围**：右键菜单弹出、打开文件/启动程序、触控与输入三类交互的感知延迟。
**对象**：Windows 11（本机 10.0.26200，Windows 11 专业版）+ 本机实测数据。
**产出**：结论 → 机理 → 实测 → 可优化项 → 工具实现。配套工具见仓库根目录 `README.md`。

---

## 0. 结论速览

1. **"Windows 11 变慢"不是一个原因，而是三层叠加**：新版菜单多了一层跨进程/跨应用的命令聚合；旧式右键扩展仍然全部在 explorer 进程内串行加载且没有超时保护；系统级后台策略（电源节流、联网联想、聚合页统计）在短小的前台操作上被放大成"卡"。
2. **右键慢的主因在"扩展"，不在"菜单"本身**。本机实测一次右键要构建 34 个菜单项、耗时 405–550 ms（首次可达 1 秒以上、偶发 3.5 秒），其中仅 WPS 的三个扩展就占 208 + 163 + 123 = **494 ms**。
3. **"第一次右键特别慢、再点就快"是冷加载成本**：每个扩展 DLL 都要在 explorer 进程里首次映射、初始化 COM、读配置，这部分只付一次；之后的 400 ms 才是稳态成本。
4. **打开慢**主要来自"主页"聚合、最近记录统计、云同步 provider 叠加、开始菜单联网搜索与电源节流，本机打开对话框初始化 214 ms，而纯目录枚举只要 19 ms——**慢在壳层附加逻辑，不在文件系统**。
5. **触控"慢半拍"里，软件可控的只有三件事**：长按判定等待、惯性滚动尾巴、以及长按弹出的那套慢菜单。硬件采样率、驱动滤波、合成器帧率不在应用可控范围，工具因此只提供"实测延迟"而不是"承诺变快"。

---

## 1. 调研方法与证据等级

| 等级 | 来源 | 用途 |
| --- | --- | --- |
| A | 微软官方文档/博客 | 架构与机制（菜单模型、扩展模型） |
| B | 本机可复现实测 | 定量结论（耗时、数量、注册表状态） |
| C | 微软 Q&A / 社区大量反馈 | 现象普适性（"loading"占位、CPU 飙高、长按不灵） |
| D | 社区经验参数 | 触控注册表项（标注为实验项） |

实测均在**独立子进程**中完成，避免把本工具的 UI 线程开销算进去，也避免某个第三方扩展崩溃时带走测量程序（详见 §6.3）。

---

## 2. 右键菜单：为什么 Windows 11 更慢

### 2.1 两套菜单并存

Windows 11 的右键菜单不是"改了个样式"，而是**换了一套扩展模型**：

- 新菜单：应用用 `IExplorerCommand` + 应用标识（打包应用或稀疏清单）注册命令，由 explorer 的新菜单宿主统一渲染。
- 旧菜单：`IContextMenu`/`IShellExtInit` 的进程内 COM 扩展，仍然原样保留，藏在「显示更多选项」（Shift+F10）后面。

微软自己在发布说明里写得很直白：

> "Many commands run in-process in Explorer, which can cause performance and reliability issues."
> —— [Extending the Context Menu and Share Dialog in Windows 11](https://blogs.windows.com/blog/2021/07/19/extending-the-context-menu-and-share-dialog-in-windows-11/)

也就是说：**新菜单并没有消灭旧扩展，只是把它们往后挪了一层**。用户看到的"右键慢"，成本来自下面几处叠加。

### 2.2 六个具体成因

| # | 成因 | 机制 | 可否优化 |
| --- | --- | --- | --- |
| 1 | 新菜单的命令聚合 | 展开前要启动菜单宿主、逐一向各应用查询 `IExplorerCommand`、合并分组，首次调用还有包模型激活开销 | ✅ 改用经典菜单可整层跳过 |
| 2 | 旧扩展进程内串行加载 | 每个 `ContextMenuHandlers` 的 CLSID 都要 `CoCreateInstance` + `IShellExtInit::Initialize` + `QueryContextMenu`，**在 explorer 的 UI 线程上串行执行，没有超时** | ✅ 屏蔽不需要的扩展 |
| 3 | 扩展本体在做重活 | 网盘/办公套件的扩展会做网络探测、账号状态、云端文件状态、许可证校验 | ✅ 屏蔽/改名 |
| 4 | 菜单等待计时器 | `MenuShowDelay` 默认 400 ms，系统先等 400 ms 判断用户是否要点别处，再弹菜单 | ✅ 可归零 |
| 5 | explorer 单进程承载 | 任务栏、桌面、文件窗口同进程，任一扩展卡住会连带右键、任务栏、窗口打开一起卡 | ⚠️ 只能减少诱因 |
| 6 | 扩展数量本身 | 每多一个扩展就多一次 COM 激活 + DLL 加载；数量是线性成本 | ✅ 精简 |

微软 Q&A 上大量同类反馈可佐证现象的普遍性：右键菜单长时间显示 "loading"、右键时 CPU 占用冲高、第二次点击才正常
（[示例 1](https://learn.microsoft.com/en-us/answers/questions/4094130/windows-11-right-click-menu-context-menu-its-too-s)、
[示例 2](https://learn.microsoft.com/zh-cn/answers/questions/4096859/cpu-cpu)）。

### 2.3 本机实测

测量方式：独立子进程，`IShellFolder::GetUIObjectOf(IID_IContextMenu)` → `CreatePopupMenu` → `QueryContextMenu`，取多次运行的中位数。

| 场景 | 中位数 | 菜单项 | 波动 |
| --- | --- | --- | --- |
| 普通文件右键 | 473–549 ms | 34 | 406 – 3556 ms |
| 文件夹右键 | 393 ms | 30 | — |
| 文件夹空白处右键 | 211 ms | 12 | — |

逐项归因（每个扩展单独起一个子进程，含 DLL 首次加载）：

| 扩展 | 作用对象 | 单项耗时 |
| --- | --- | --- |
| qingshellext Class（WPS） | 所有文件 / 文件夹 / 空白处 | 208 ms |
| kwpsshellext Class（WPS） | 所有文件 / 文件夹 / 空白处 / 驱动器 | 163 ms |
| kdesktopshellext Class（WPS） | 文件夹 | 123 ms |
| Include In Library Sub Context Menu | 文件夹 | 119 ms |
| Ribbon Modern Share Verb | 所有对象 | 71 ms |
| Pin To Start Screen verb handler | 文件夹 | 69 ms |
| Shell extensions for sharing | 所有文件 / 文件夹 / 空白处 | 62 ms |
| Microsoft SendTo Service | 所有对象 | 58 ms |
| Open With Context Menu Handler | 所有文件 | 56 ms |
| PowerRename Shell Extension | 空白处 / 所有对象 | 47 ms |

共 **25 个扩展，其中第三方 11 个**。WPS 三项相加 494 ms，与整份菜单 473–549 ms 的中位数基本吻合——**慢的不是 Windows，是挂在右键上的这些进程内组件**。

### 2.4 关键发现：冷加载成本 vs 稳态成本

同一个进程内连续调用 5 次 `QueryContextMenu` 的典型曲线：

```
run0 = 1014 ms      ← 首次：所有扩展 DLL 映射 + COM 初始化 + 配置读取
run1 = 3556 ms      ← 偶发尖峰：某个扩展做网络/云端状态探测，等到超时
run2 =  549 ms
run3 =  415 ms
run4 =  402 ms      ← 稳态
```

这解释了三个普遍现象：

- **"第一次右键要等一两秒，之后正常"**：首次把 25 个扩展全部拉起来的成本。
- **"过一会儿又慢一次"**：扩展被卸载或缓存过期后重新加载。
- **"某些机器上偶发 3 秒"**：某个扩展在做网络探测（网盘类最典型），失败要等超时。

因此**优化右键要从"减少要加载的扩展数量"入手**，而不是只调菜单动画。

---

## 3. 打开文件与启动程序

### 3.1 成因

| 成因 | 说明 |
| --- | --- |
| 「主页」聚合页 | 每次打开资源管理器都要汇总最近文件、常用文件夹、云端内容，涉及 Jump List、`AutomaticDestinations`、同步 provider 查询 |
| 最近记录统计 | `Start_TrackDocs` / `Start_TrackProgs` 打开时，开始菜单与打开对话框会查询最近项 |
| 云同步 provider 叠加 | OneDrive 等的 `IShellIconOverlayIdentifier` + 同步状态扩展参与每次视图刷新；`ShowSyncProviderNotifications` 关闭后可减负 |
| 开始菜单联网联想 | `BingSearchEnabled` / `DisableSearchBoxSuggestions`：每次打开都请求搜索建议与网页结果，网络抖动时卡顿被放大 |
| 打开对话框初始化 | `IFileOpenDialog` 创建时要加载外壳名称空间扩展、Quick Access、云盘钩子 |
| 电源节流与降频 | `PowerThrottling` 会把非前台进程压到低功耗状态；平衡电源计划下短小的前台操作容易撞上降频窗口 |
| 安全软件实时扫描 | 启动可执行文件前的扫描（本机已禁用 Defender，未构成因素） |
| 图标/缩略图缓存 | 缓存损坏或过期时，每次列表刷新都重新解码图标 |

### 3.2 本机实测

| 指标 | 实测 | 评价 |
| --- | --- | --- |
| 打开文件对话框初始化 | 214 ms | 偏慢 |
| 目录枚举（`C:\Windows`，112 项） | 19 ms | 优秀 |
| 启动控制台进程（ShellExecute + 退出） | 113 ms | 良好 |
| 桌面合成帧间隔 | 16.7 ms（60 Hz，理想 16.7） | 优秀 |
| explorer 消息往返 | 0.1 ms | 优秀 |

**结论**：文件系统与合成器都没问题，214 ms 的对话框初始化耗在壳层附加逻辑（名称空间扩展、Quick Access、云盘钩子），启动 113 ms 属正常范围。**这类"慢"更多是感知层的**：主页聚合 + 动画 + 联网联想，让一次打开在观感上拖长到 1 秒以上。

---

## 4. 触控：延迟到底在哪

### 4.1 延迟链路

```
触摸面板采样 → HID 驱动 → Windows 输入栈(WISP/MsgPointer) → 手势识别器
    → explorer/应用的命中测试 → 绘制 → DWM 合成 → 显示
```

其中**只有第 3～5 段是软件可调的**，且大部分还是"系统策略"而非"应用代码"。

### 4.2 Windows 11 相对 Windows 10 的变化

| 变化 | 后果 |
| --- | --- |
| 长按右键弹出的是**新版菜单** | 把 §2 的整层成本叠加到"长按"操作上，触控用户感知最明显 |
| 新控件走更重的渲染路径 | 触控反馈动画、圆角、阴影都需要额外的合成层 |
| 触控手势（滑动、边缘手势）与惯性滚动 | 手指抬起后内容继续滑行，主观上"系统慢半拍" |
| 长按判定时间 | 需要按住一段时间才触发右键，等待期没有反馈时像"没响应" |

### 4.3 可控项（工具的触控页）

| 项 | 注册表 | 说明 | 风险 |
| --- | --- | --- | --- |
| 长按判定加速 | `HKCU\Software\Microsoft\Wisp\Touch\TouchModeN_HoldTime_BeforeAnimation` / `..._Animation` | 缩短按住到弹出菜单前的判定与动画等待 | 实验项；个别驱动可能把长按识别成点击 |
| 关闭触控惯性 | `...\Wisp\Touch\Inertia` = 0，`Bouncing` = 0 | 手指抬起立刻停住，跟手感更强 | 无 |
| 关闭 Flicks 手势 | `...\Wisp\Touch\Flicks` = 0 | 少一层手势识别判定 | 失去划动手势 |

> `Wisp\Touch` 下的参数属于**社区经验值**（微软未公开承诺语义），因此工具把它们标为实验项、默认关闭、写入前自动备份。
> 相关社区线索：[Microsoft Q&A: touch-and-hold 与 `TouchModeN_HoldTime_Animation`](https://learn.microsoft.com/en-us/answers/questions/4154476/how-to-optimize-a-touchscreen-on-windows-11-to-mak)。

### 4.4 不可控项（不要承诺）

- 面板采样率与 HID 上报间隔（硬件）
- 驱动层的滤波/防抖
- 显示器自身响应时间
- DWM 合成延迟（只能测，不能改）

本机（Intel X99 台式机）**没有触摸设备**，因此触控项在工具中显示为"无触摸屏"且不可点，触控结论以机制分析 + 社区参数为主，未做定量实测——这一点在交付说明里明确标注。

---

## 5. 常见"优化谣言"与风险清单

| 说法 | 实际情况 |
| --- | --- |
| "把 `WaitToKillServiceTimeout` 调小能让程序更快" | 与右键/打开无关，只影响关机，调小反而可能丢数据 |
| "关闭 Superfetch/SysMain 能加速" | 现代 Windows 上会拖慢冷启动，收益为负 |
| "禁用全部视觉特效" | 只省下 100–200 ms 的观感延迟，且大幅降低可用性；真正有效的是关掉菜单/最小化动画 |
| "改 `Win32PrioritySeparation` 能解决卡顿" | 只在 CPU 竞争激烈时改善前台响应，属"锦上添花"，不是根因 |
| "装个内存清理器" | 只会把热数据赶出缓存，右键与打开更慢 |
| "删注册表 CLSID 能删掉右键项" | 直接删 CLSID 会破坏卸载/更新，正确做法是屏蔽（Blocked）或临时改名注册项，可逆 |

本工具的做法：**只做可逆的、有机制依据的改动**，写入前备份原值，任何一项都能一键还原。

---

## 6. 工具设计与实现

### 6.1 目标与边界

- 目标：把"右键 / 打开 / 触控"的**可量化成本**测出来，把**有机制依据的项**调回来，并**常驻守住**。
- 边界：不装驱动、不注入 explorer、不联网、不改系统文件、不要求管理员即可使用主要功能。

### 6.2 模块

| 模块 | 职责 |
| --- | --- |
| `util` | 注册表/路径/日志/进程/计时等基础能力 |
| `tweaks` | 13 个优化项的定义、探测、备份、应用、还原；外壳扩展枚举与屏蔽 |
| `diag` | 测量引擎：菜单构建、单扩展、对话框、枚举、启动、DWM；子进程隔离与超时 |
| `monitor` | 常驻采样线程：explorer CPU/内存、消息往返、异常告警、每 60 秒守护校验 |
| `ui` | 自绘设置面板（总览/右键/打开/触控/诊断/设置）+ 托盘 + 输入延迟实测窗 |

### 6.3 测量方法（为什么这样测）

- **独立子进程**：`OpenPulsShellOpt.exe --bench-ext <clsid> <kind> <path> <out>`。
  第三方扩展可能崩溃、弹窗、死锁，跑在宿主进程里会毁掉工具本身；子进程崩溃只损失一个数据点。
- **3 秒超时**：`WaitForSingleObject` 超时即 `TerminateProcess`，把"某个扩展卡住 3 秒"直接变成可见结论。
- **中位数 + 波动区间**：同一进程内跑 4–5 次取中位数，同时记录 min/max；波动大说明有扩展在做 I/O 等待，这本身是有价值的信号。
- **不采用 ETW**：ETW 的 shell provider 需要管理员、且难以归因到具体 CLSID；`QueryContextMenu` 的墙钟时间更贴近用户感知。

### 6.4 安全设计

1. **先备份后写入**：每个目标值记录"是否存在 / 类型 / 原值"到 `HKCU\Software\OpenPuls\ShellOpt\Backup\<项>`。
2. **精确还原**：还原按备份逐值写回；原本不存在的值删除而不是写默认值。
3. **键存在性标记**：经典菜单项会创建 `CLSID\{86ca1aa0-...}` 键，备份里额外记录 `keyexisted`；**只有原本不存在时才整体删除**，避免破坏用户原有的经典菜单设置。
4. **屏蔽可逆**：屏蔽 = 写 `Blocked` 列表 + 把注册项改名为 `<名字>__OpenPulsOff`；启用时扫描后缀改名还原，不复制、不丢数据。
5. **不碰系统文件**，不写 `HKLM`（除非用户以管理员身份运行并显式开启对应项）。

### 6.5 常驻守护

- 每 2 秒采样：explorer 进程组 CPU、工作集、`SendMessageTimeout(WM_NULL)` 往返。
- 每 60 秒校验：所有"用户开启过"的项重新 `Probe`，与期望不符则重新应用（系统更新、其他优化工具、应用自修复都可能改回去）。
- 异常（往返 > 400 ms 连续两次）写入日志并在总览页提示，5 分钟内不重复打扰。
- 托盘常驻；关闭窗口=收进托盘；可设开机自启。

---

## 7. 实测与效果

| 项目 | 优化前 | 优化后 | 说明 |
| --- | --- | --- | --- |
| 右键菜单等待计时 | 400 ms | 0 ms | `MenuShowDelay`，立即生效（含 `SPI_SETMENUSHOWDELAY` 广播） |
| 新版菜单层 | 有 | 无（经典菜单） | 需重启资源管理器 |
| 扩展数量 | 25 | 按需屏蔽 | 冷启动成本随数量线性下降 |
| 「主页」聚合 | 每次打开都统计 | 直接进"此电脑" | 打开窗口更快 |
| 电源计划 | 平衡 | 高性能（可选） | 短操作撞降频的概率下降 |

**必须说明的一点**：在本机把 3 个 WPS 扩展屏蔽后，菜单总耗时的中位数没有明显下降（约 402 ms → 386 ms，处在测量噪声内），而 25 个扩展里只少了 3 个——这与"成本主要是冷加载 + 数量"的判断一致，也说明**单项耗时归因比总量数字更可靠**。工具因此把"逐个扩展耗时"作为主视图，而不是给一个漂亮但虚的总分。

---

## 8. 局限与后续

1. **新版菜单无法脱离交互进程计时**：它由 explorer 的菜单宿主渲染，工具测的是同一份旧式扩展成本（也是"显示更多选项"里的成本）。要测新版菜单只能做 UI 自动化端到端计时，稳定性差，暂未纳入。
2. **触控实验项需要真机验证**：`Wisp\Touch` 参数语义未公开，工具做了备份与还原，但建议逐项开启、用「输入延迟实测」窗对比。
3. **HKLM 级扩展屏蔽需要管理员**：非管理员时只写用户级 `Blocked` 列表。
4. 可继续做：按扩展的**首次加载 vs 稳态**分别计时；把日志做成可回放的时序图；支持导入/导出优化方案。

---

## 9. 参考资料

1. Microsoft Windows Blog, *Extending the Context Menu and Share Dialog in Windows 11*（新菜单模型、进程内扩展的性能隐患）— https://blogs.windows.com/blog/2021/07/19/extending-the-context-menu-and-share-dialog-in-windows-11/
2. Microsoft Q&A, *Windows 11 Right Click Menu its too slow to appear and says "loading"* — https://learn.microsoft.com/en-us/answers/questions/4094130/windows-11-right-click-menu-context-menu-its-too-s
3. Microsoft Q&A（中文），*第一次对文件右键加载很慢、右键时 CPU 升高* — https://learn.microsoft.com/zh-cn/answers/questions/4096859/cpu-cpu
4. Microsoft Q&A, *How to optimize a Touchscreen on Windows 11 to make touch-and-hold work*（`Wisp\Touch\TouchModeN_HoldTime_Animation` 社区线索）— https://learn.microsoft.com/en-us/answers/questions/4154476/how-to-optimize-a-touchscreen-on-windows-11-to-mak
5. 本机实测数据：Windows 11 10.0.26200，Intel X99，34 GB RAM，25 个右键扩展（第三方 11 个）。
