## 文档维护规则

1. 本文1-80行禁止修改，80行后的内容与实际不符时，更新本文以反映实际，本文超过500行必须主动压缩至500行以内

## 核心原则

1. 先理解再动手：通读任务+关联代码 → 追踪端到端数据流 → 理解问题全貌
2. 最小必要实现，不写非必要代码
3. 干活前先读本文，遵守文末「协作习惯（用户本人）」的要求
4. 汇报要短：先结论后论据，说明改了什么、怎么验证的；只讲「有什么问题、值不值得修」，不写长篇推演。
5. 过时/失效的内容直接删除，不要保留「已删除/已作废/已卸载」之类的标记——本文只反映当前真实状态

## 决策阶梯

写任何代码前，从第一级开始，停在第一个满足条件的台阶：

1. 需要存在吗？ → 否：跳过
2. 代码库已有？ → 复用，不重写
3. 标准库能做？ → 用它
4. 平台原生特性覆盖？ → 用它
5. 已安装依赖能解决？ → 用它（安装新依赖、安装任何工具软件，均须先获用户授权）
6. 一行代码能搞定？ → 就一行
7. 走到这里：写最少能工作的代码

阶梯覆盖设计、编码、评审三阶段。选方案前必须读完任务和涉及代码，追踪完整执行路径。方案上可以懒，阅读上不可以懒。

## 设计

少写代码的方式是更早说不。

- **质疑需求**：阶梯第一级在设计阶段最有力。"需要这功能吗？"——已有方案能覆盖，新功能整个跳过。
- **用问题回应**：复杂需求先给最简方案，反问"X 做了；Y 已覆盖。真的要完整 X？要就说。"不等答案，给默认。
- **不建"将来可能用"的架构**：无第二个实现 → 不建接口；无第二个产品 → 不建工厂；未变过的值 → 不加配置。
- **数据结构选对，代码自然短**：DB 约束替代应用层校验——DB 能做的，代码不重复。
- **仅一处调用的抽象是冗余，删除。**

## 编码

- 不引入无明确需求的抽象。
- 不引入新依赖，除非现有方案无法满足。
- 不要无调用方的样板代码。
- 删优于加。简单优于聪明。文件越少越好。
- 最短能工作的 diff 即赢家——前提是已理解问题。在最错处做最小改动不是懒，是第二个 bug。
- 两个标准库方案等长时，选正确处理边界条件的。懒 = 少写代码，≠ 用更脆弱的算法。
- 修 bug = 治根本，不治症状。检查所有调用方，在共享函数修一次——一处 guard 比每处各加 diff 更短。只修点名路径让其他调用方继续破损。
- 非平凡逻辑必须附带一个可运行检查：最小能证明逻辑出错的断言或小测试。不要测试框架、不要 fixtures。一行平凡代码不需要。
- 刻意简化：省略边界条件（全局锁、O(n²)、朴素启发式）时，用注释标注上限和升级路径。

## 评审

评审只盯一个问题：**哪些代码可以不写。**

- 范围：只查过度工程和复杂度。正确性 bug、安全漏洞、性能交常规评审。
- 一致性：同一口径只留一份源头，别处一律引用而非复述——默认值可配置后，工具 schema 的参数描述、界面 placeholder、README 表格里的数字都要跟着它走，源头改了它们必须同改，否则模型与界面拿到的说明会过期（如命令默认超时的字面 120）。
- 输出：每行一个发现，格式：**位置 → 标签 → 砍什么 → 替代方案**。
- 无可砍时说：**`Lean already. Ship.`**

五个标签，按可砍代码量从大到小：

| 标签 | 命中场景 | 替代方案 |
|------|---------|---------|
| `删除:` | 死代码、未用灵活性、投机功能 | 无 |
| `标准库:` | 手写代码替代了标准库已有功能 | 标准库函数名 |
| `原生:` | 库或代码做了平台原生的事 | 平台特性名 |
| `冗余:` | 单一实现抽象、无人设配置、单调用方层 | 内联，等第二个出现 |
| `精简:` | 同逻辑可更短 | 给出更短写法 |

评审终点不是"代码好"，是"diff 变短了"。

## 安全红线

以下永不在砍代码讨论范围，设计/编码/评审三阶段均不可触碰：

| 红线 | 说明 |
|------|------|
| 信任边界输入验证 | 路径穿越、SQL 注入、XSS——永为必经检查点 |
| 防数据丢失的错误处理 | 涉及数据写入/删除时，错误处理不可省略 |
| 安全性 | 身份验证、授权、加密——不可妥协 |
| 可访问性 | 原生元素自带无障碍，自定义组件需额外保障 |
| 真实硬件校准 | 平台非理想环境：时钟漂移、传感器误差等 |

## 项目实况：Windows 启动面板（windowTAG）

纯 Win32 + C++17 单文件应用（`src/main.cpp`），GDI 自绘 + GDI+（系统 gdiplus.dll）抗锯齿圆角，零第三方依赖，目标 exe ≤200KB。界面两级：标签（NT_TAG）→ 链接（NT_LINK）；"全部"= id 0 的虚拟标签。资源类型泛化：exe/lnk/文件夹/文件/URL/命令行+参数，一律 `ShellExecuteExW`（Windows 默认方式）打开。视觉：双主题（`[ui] theme`，0=深色默认，取用户提供的 Global 配色表；1=浅色），同一组语义槽位 `C_BG/C_BAR/C_CARD/C_HOVER/C_INSET/C_TEXT/C_MUT/C_ACC/C_ONTXT/C_SPLIT/C_EDGE` 由 `ApplyTheme()` 赋值；胶囊标签、浮起卡片、内凹输入框、窗口/弹窗 layered alpha 淡入淡出。窗口外圆角：Win11 走 DWM `DWMWA_WINDOW_CORNER_PREFERENCE`（抗锯齿），不支持的系统和无边框弹窗用 `SetWindowRgn` 圆角区域兜底，`WM_SIZE` 时重算。内容面板矩形由 `PanelRect()` 统一提供，卡片布局/滚动范围都以它为基准，绘制时用 `IntersectClipRect` 裁剪到面板内。卡片标题两行折行（`WrapText`，优先空格断行，超两行末行补省略号）。设置项（图标大小 小/中/大、排序 添加顺序/名称升/名称降、主题、选中标签）全部写入 conf.ini 的 `[ui]` 并在启动时读回。注意：EDIT 背景必须返回实心刷（HOLLOW 刷走 PatBlt 会整块变黑）；菜单是自绘的（`MItem` + `MPopup`：WS_EX_LAYERED + UpdateLayeredWindow 逐像素 alpha，SDF 算圆角抗锯齿和软阴影，色槽 C_MENU/C_MENUH），不要再退回系统 `TrackPopupMenu`（深色主题下是白底灰框）；菜单是多窗口一套（`g_panels` + `MPanel::child/src`），只有根面板抢激活（`SW_SHOW`+`SetForegroundWindow`），子菜单一律 `SW_SHOWNOACTIVATE` 且 `MenuOpenSub` 换条目时先关旧子菜单——否则父面板会收到 `WA_INACTIVE` 误收起整套菜单、且后续子菜单打不开；`WM_ACTIVATE` 只按"激活窗口是否本进程"判断收起；菜单展开期间主窗口的 `WM_LBUTTONDOWN`/`WM_CONTEXTMENU` 只用于收起（`!g_menuDone`）；弹窗类的 `hbrBackground` 必须为 `nullptr` 且 `WM_ERASEBKGND` 返回 1（否则系统用 `COLOR_BTNFACE` 先擦一帧，暗色主题下就是白闪），`FadeIn` 里 `ShowWindow` 后必须 `UpdateWindow` 把首帧画完再提 alpha；淡入淡出必须手动 layered alpha（AnimateWindow 增删 WS_EX_LAYERED 会闪系统老边框）；`data/` 目录必须在写 ini 前 `CreateDirectoryW` 创建，否则所有写入静默失败。

### 环境

- 仓库 `github.com/kaozb/windowTAG`，唯一分支 `main`，GitHub Actions 全权构建
- 开发在 Linux 沙箱，交叉编译器 `g++-mingw-w64-x86-64`（已 apt 安装，仅做编译校验，产物仅供预览）
- 正式 exe 只在 CI（windows-latest + MSVC /MT /LTCG）产出

### 调试手段

- 编译校验命令（改完 `src/main.cpp` 必跑一遍）：

```sh
x86_64-w64-mingw32-windres --codepage 65001 -I src src/res.rc -O coff -o build/res.o
x86_64-w64-mingw32-g++ -std=c++17 -O1 -municode -mwindows -DUNICODE -D_UNICODE \
  -D_WIN32_WINNT=0x0A00 src/main.cpp build/res.o -o dist/Launcher-preview.exe \
  -luser32 -lgdi32 -lgdiplus -lshell32 -lcomdlg32 -ldwmapi -lws2_32 -static -s
```

- MinGW 与 MSVC 差异是坑源：MSVC 默认非 UTF-8（`cl` 须 `/utf-8`、`rc` 须 `/c 65001`，否则中文字面量编译期变乱码）；`windows.h` 的 min/max 宏会打碎 `std::max`（须 `NOMINMAX`）。MinGW 预览通过 ≠ MSVC 通过，两边语法都要过
- 界面问题（DWM、无边框、NC 区域、圆角）MinGW 预览看不出，须 CI 出包后在用户真机验证；用户是真机测试员，报问题要给出可观察的复现路径

### 构建与部署

- 推 `main` → Actions（`.github/workflows/build.yml`）自动：读 `VERSION` 生成 tag `v{基数}.{run_number}` → 覆写 `src/version.h` → MSVC 编译 → `gh release create` 挂 exe
- 换版本段位改 `VERSION` 文件（当前 `0.1`）；`src/version.h` 是 CI 生成物，仓库里只是占位，不要手工维护
- 本地 MSVC 构建：`build.bat`（VS 开发者命令行）
- 用户从 GitHub Releases 下载 exe，绿色单文件；数据在 exe 旁 `conf.ini` + `data/links.ini`，中文依赖 UTF-16 BOM（`EnsureUnicodeIni` 预写）

### 关键机制（改界面/窗口必读）

- 无边框窗口四件套：样式无 `WS_CAPTION`（只留 `WS_THICKFRAME|WS_SYSMENU|MIN|MAX`）、`WM_NCCALCSIZE` 返回 0（最大化时只内缩边框量、不含 CAPTION，否则顶部露出一条系统标题栏空位）、`WM_NCHITTEST` 自定义边缘、创建后 `SWP_FRAMECHANGED` 强制首帧重排——缺一会出现系统标题栏残留或内容溢出；`WM_NCLBUTTONDBLCLK` 拦下 `HTCAPTION`（双击标题栏不最大化）
- Win11 观感走 DWM 属性（动态加载 dwmapi）：圆角 `DWMWCP_ROUND`、描边 `C_EDGE`、标题栏染天蓝兜底；Win10 自动跳过
- 编辑/热键/监听三对话框自绘：`WS_POPUP` + `g_dlgBtns`/`g_dlgEdits` + `DrawDlgBtns`/`FindDlgBtn`，弹出居中主窗口，三对话框互斥（共享按钮数组）
- 消息监听：标题栏第 4 枚按钮（播放，`g_rcBtns[3]`）= 纯启动/停止开关，反馈走托盘气泡（TrayTip，随机端口在气泡里告知）；设置（端口、来源 IP 前缀、弹窗停留秒数）在主菜单「监听设置…」，确定即保存、监听中自动重启生效；conf.ini `[listen]`（port 默认 22222、0=随机，allow=来源 IP 字面前缀白名单，逗号分隔，空=全放行，toast 默认 5 秒、0=不自动消失仅点击可关）；body 解析 `{"msg":"..."}`（宽松取值+转义，容忍 UTF-8 BOM）成功回 200 并 `PostMessage(WM_MSG)` → `ShowToast` 主显示器正中 TOPMOST 不抢焦点、按 toast 秒数自动淡出、点击即关（PostMessage 失败须 delete[]，堆指针归主线程释放）；白名单拦截回 403（带来源 IP），格式错回 400（回显收到的 body），另有 Expect: 100-continue 应答；winsock2.h 必须在 windows.h 之前包含，链接加 ws2_32；`ListenThread` 的监听 socket 由 CreateThread 参数传入（线程专属局部变量）——Stop 500ms 超时后线程可能还活在 recv 超时里，此时立刻 Start 不能让旧线程摸到新 socket（双 accept 竞态）；`g_nodes` 的 push_back/erase 会挪动元素使 `g_view` 指针悬挂，任何改 g_nodes 的路径收尾必须 `Refresh()`（如 `EnsureCurTag` 新建标签后）
- 快捷键：仅键盘组合走 RegisterHotKey（conf.ini `[hot]` mod/vk）；鼠标触发方案与失焦自动隐藏冲突，已实现后又移除——勿再加回（conf.ini 遗留的 `[hot] mouse` 键直接忽略）
- 开机自启：主菜单「开机自启」勾选项，默认开（conf.ini `[behavior] autostart`）；`ApplyAutoStart` 写/删 `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` 值 `windowTAG`（带引号的 exe 全路径），每次启动且开关为开时重写（exe 挪位自动跟上）；注册表 API 需 advapi32（build.bat 与 CI 链接行都已有）
- 链接拖拽排序：网格单元格左键按下只 `SetCapture` 记 `g_pressIdx`，移动超 `S(6)` 才进拖拽（仅排序=添加顺序时生效，名称排序下保持纯单击语义）；`DragMove` 实时换位（`g_view` 与 `g_nodes` 同步 rotate，元素地址不变指针不失效），拖拽卡片描主色边；松手 `WM_LBUTTONUP` 未拖则按单击启动（单击行为不变），拖过则 `SaveOrders` 把 `Node.order` 重排落盘（links.ini `[node.X] order`，`LoadNodes` 按 order 稳定排序，旧文件缺省=读入序）；新建节点 order=`g_nodes.size()`；捕获被打断走 `WM_CAPTURECHANGED` 丢弃按压
- 主窗口绘制在 `PaintAll`，布局在 `LayoutNow`（`g_rcTabs` 布局区），命中在 `HitTest`；`S()` 按 DPI 缩放所有尺寸；标签行右侧无加号（添加标签走主菜单）

### 协作习惯（用户本人）

- 中文交流；汇报短，先结论后论据，说清改了什么、怎么验证
- 界面要圆角、优先系统 API、exe 越小越好、内存占用敏感；拒绝搜索框、次数统计、分组层这类冗余 UI
- 改完代码直接提交推送 main，等 Actions 出 Release 交付；沙箱装编译工具先问一句即可（MinGW 已获授权）
- 发布后把 Release 链接和体积一并报告
