# ScreenMovie 需求记录

> 状态：需求已确认（2026-11-24），进入实现阶段。本文为最终需求基线。

## 1. 项目目标

参照项目 `D:\project\ScreenCapture`，把其中的**桌面录屏**功能剥离出来，做成一个独立的新产品，即本项目 `D:\project\ScreenMovie`。

核心功能范围（来自用户首条说明）：

1. 录制桌面影像（桌面录屏）。
2. 生成 GIF（录屏内容导出为 GIF 动图）。
3. 生成 MP4（录屏内容导出为 MP4 视频）。

## 2. 已确认的技术约束

- **操作系统**：只需兼容 Windows，不考虑 macOS / Linux 等其他平台。
- **UI 框架**：必须使用用户自研的 DUI 框架（`D:\project\DUI`），不得替换为其他 GUI 框架。
  - DUI 是 C++20、仅 Windows（Win 1703+）、零依赖的 Direct3D 11 / DXGI / DirectWrite GUI 框架，产出 `DUI.lib` 静态库。
  - 本项目应以静态链接方式引用 DUI 库。
- **字体图标**：直接使用 ScreenCapture 中的字体图标（`ScreenCapture\Src\Res\iconfont.ttf`），不另做图标方案。
- **GIF 编解码库**：与 ScreenCapture 相同（实际编码走 `gifski`，源码在 `ScreenCapture\Src\Rec\VideoGif.hpp`；同项目的 `cgif` C 库未被使用，本项目不引入）。
- **exe 体积优先**：Release 用 `/O1`（MinSpace）+ `/OPT:REF` + `/OPT:ICF` + 关调试信息 + 关异常展开表（`/EHs-c-`，C4530 在 `pch.h` 抑制）。基线 1,272,320 B → 1,267,200 B。
  - 体积大头是静态 CRT（`/MT`，DUI 的 ADR-008 零依赖约束）与 gifski 的 Rust 运行时，编译开关可压缩空间有限。
  - ATL `CComPtr` / WRL `ComPtr` 都是 header-only 模板，不链接库、不增加 exe 体积；`= 0` / `= NULL` 初始化依赖 ATL 接口，替换成 WRL 收益极小且风险大，保留。
- **DUI 新增能力适配**（用户根据本项目反馈扩展了 DUI，2026-11-25）：本项目已改用这些能力，不再手写对应 Win32：
  - `WindowOptions.topmost / toolWindow / noActivate / excludeFromCapture` 声明浮层属性；`Window::SetMouseThrough()` 运行期切换鼠标穿透。
  - `dui::Screen`（`Core/Screen.h`）：`VirtualBounds()` 取虚拟桌面、`FromPoint()` + `workAreaPx` 做工具条工作区避让，替代 `GetSystemMetrics` / `MonitorFromRect`。
  - `Element::GetWindow()` + `Window::Scale()`：选区层直接取所属窗口 DPI，替代此前的全局 `g_scale`。
  - `Window::SetTimer()/KillTimer()`：倒计时与录制计时绑定窗口，随窗口销毁自动失效，替代 `App::SetTimer`。
  - `Window::BoundsPx()/SetBoundsPx()/MoveToPx()`：覆盖层铺满虚拟桌面、工具条按工作区避让，替代手写 `SetWindowPos`；`Window::Show()/Hide()` 替代 `ShowWindow`。
  - 仍保留的 Win32：窗口高亮吸附的 `EnumWindows + DwmGetWindowAttribute`（框架明确不提供）、临时文件 `CopyFileW/DeleteFileW`、`SetWindowRgn`（见下）。`Activate()/FlashFrame()` 本项目用不上（不抢焦点、无任务栏图标）。
- **覆盖层定版（核对 ScreenCapture 后，用户要求）**：不用桌面快照，回到 `transparent=true` 全透明窗口——这是 ScreenCapture 的方案（`WS_EX_NOREDIRECTIONBITMAP` + DirectComposition + 预乘 alpha swap chain，`Clear(0)` 清全透明，选区内什么都不画即"视觉挖孔"）。
  - 全屏 transparent 窗必须设**整窗矩形 `SetWindowRgn`**，否则 DWM 把透明区合成成白板（实测确认）。
  - 遮罩 = 四块半透明黑矩形（`0x77000000`）+ 蓝色选区框；选区内不画 → 桌面透出，录制时也不进成品（透明像素不遮挡桌面）。
  - 鼠标：选区阶段 `HTCLIENT` 正常接收拖拽；**选区完成后** `SetMouseThrough(true)` + 锁定选区（停止高亮吸附），鼠标穿透给下层程序；工具条是独立窗口照常接收点击。与 ScreenCapture 的 `onHitTest→HTTRANSPARENT` + `WS_EX_TRANSPARENT` 双保险同思路。
  - 曾尝试"抓屏快照当背景 + 不透明窗口"，因需 GDI 抓屏/BMP 编码且丢失实时性，按用户要求废弃。
- **字体图标 □ 修复**：`iconfont.ttf` 虽由 PostBuild 拷到 exe 目录，但 `LoadFontFamily(L"iconfont.ttf")` 的相对路径依赖工作目录（VS 调试时是工程根 → 加载失败 → PUA 码位静默回退成 □）。改为 `GetModuleFileNameW` 取 exe 同目录绝对路径。图标码位 `\ue654/\ue73b/\ue660/\ue62d` 与 ScreenCapture 一致，无误。

## 3. 用户操作方式（已确认）

1. **区域选择**：用户在屏幕上拖拽鼠标选取一块录屏区域；或者选取一个窗口区域录制。窗口区域高亮逻辑参照 ScreenCapture 中已有实现剥离。
2. **录屏工具条**：区域选好之后，在区域下方出现录屏工具条（参考截图：`Doc/Toolbar.png`）。
3. **工具条定位规则**：
   - 首选：选中区域**下方**；
   - 屏幕下方空间不足：出现在选中区域**上方**；
   - 上方和下方均不足：出现在选中区域**右下角**。
4. **工具条内容**（依据截图，从左到右）：
   - 文字"选择格式"；
   - 单选按钮：MP4 / GIF（截图中 GIF 为选中态）；
   - 画质下拉框（截图显示"标清"）—— **明确不做**；
   - 扬声器开关按钮（自动选默认设备，无需选设备）；
   - 麦克风开关按钮（自动选默认设备，无需选设备）；
   - 蓝色主按钮"开始录制"；
   - 次按钮"退出录制"。
   - 选择 GIF 时，扬声器/麦克风音频按钮**禁用**（GIF 不支持音频）。
5. **开始录制流程（倒计时 → 录制态）**：
   - 点击"开始录制"后，录制区域**正中间**先出现倒计时数字（参考截图：`Doc/Countdown.png`，深色半透明圆形底 + 大号数字，**共 3 秒**从 3 数到 1，截图中 `2` 为中间态）；倒计时不可取消，且不录入最终文件；
   - 倒计时结束后，工具条才切换为录制态（参考截图：`Doc/ToolbarRecording.png`）：
     - 简洁形态：左侧显示计时 `已录制时长 / 总时长`，右侧为红色"结束录制"按钮；
     - 顶部蓝色横线为录制进度指示。

## 4. 由工具条提取的录屏功能范围（已确认）

按工具条控件逐项映射，产品需实现以下功能：

1. **选区**：鼠标拖拽框选矩形区域 + 选取窗口区域（窗口高亮）。
2. **工具条**：按"下 → 上 → 右下角"规则定位，承载全部录制控制。
3. **输出格式**：MP4 / GIF 二选一（单选）。
4. **系统声音（扬声器）**：开关，自动使用默认设备；GIF 模式下禁用。
5. **麦克风**：开关，自动使用默认设备；GIF 模式下禁用。
6. **开始录制**：按当前选区与设置启动录制；先在选区正中间显示 3 秒倒计时（3 → 2 → 1，不可取消、不录入文件），倒计时结束后才真正开始捕获并切换工具条为录制态。
7. **退出 / 结束**（两种语境）：录制前工具条上是"退出"（放弃选区并退出程序，与参照项目一致）；录制中按钮是"结束录制"（停止录制并进入保存流程）。
8. **录制中状态**：显示已录制时长与总时长（截图为 `00:06 / 01:00`）+ 顶部进度指示；**时长上限沿用 ScreenCapture：MP4 120 分钟、GIF 6 分钟**，到时自动停止并弹出保存对话框。
9. **录制与导出**：捕获选区桌面画面，按所选格式编码；音频按开关混入 MP4（GIF 无音频）。编码直接在内存/临时文件中完成，不依赖编辑能力。

**录制后处理（最终确认）**：

- 录制过程中在 `C:\Users\[username]\AppData\Roaming\ScreenMovie` 下生成临时文件。
- 点击"结束录制"（或到时自动停止）后，**直接弹出保存文件对话框**；用户选定路径后把临时文件**拷贝**到该路径。
- 不保留"丢弃 / 存剪切板"能力；无历史记录、无保存路径记忆等额外要求。

**其他最终约束**：

- 无托盘图标；程序启动后直接进入屏幕选区状态。
- 无快捷键（不选 Win+Shift+S 等）。
- 无编辑能力（剪辑/裁剪/字幕/水印），仅"录制→导出"。
- 导出参数（GIF 帧率/尺寸/循环、MP4 编码器 H.264/码率/帧率）全部沿用 ScreenCapture 默认值。
- 无主题/语言/历史记录等额外要求。

**明确不做（用户指定）**：

- 清晰度/画质档位设置（标清、高清等）——工具条上对应的下拉框不实现。

## 5. 参照项目现状（供后续剥离时参考）

- 路径：`D:\project\ScreenCapture`。
- 工程：`Src\ScreenCapture.vcxproj`，C++ / Win32 程序，含托盘、设置、历史记录、更新等模块。
- 录屏相关代码位置（供后续剥离）：
  - `Src/Tool/ToolVideo.{h,cpp}`：录屏工具条两种形态（设置态 / 录制态）、格式切换、计时、保存。
  - `Src/Win/CapVideo.{h,cpp}`：屏幕录制调度、编码线程、开始/停止。
  - `Src/Win/CapVideo*` / `VideoMp4.hpp`：MP4 编码走 **Windows Media Foundation**（非 ffmpeg）；GIF 编码走 **gifski**（`VideoGif.hpp`，`cgif` 未被使用）。
  - `Src/Res/iconfont.ttf`：字体图标。
  - 参照项目录制态工具条提供"丢弃 / 存文件 / 存剪切板"，与本项目需求截图（仅计时 + 红色"结束录制"）不同，以本项目需求为准。
- 录屏相关实现需在需求明确后进一步定位分析（当前仅做需求记录，不改代码）。

## 6. 需求确认记录（已全部明确）

- 产品形态：无托盘，启动直接屏幕选区。
- 倒计时：3 秒，不可取消，不进文件。
- 录制时长：MP4 120 分钟 / GIF 6 分钟，到时自动结束并弹保存对话框。
- 停止方式：仅按钮，无快捷键；无暂停、鼠标显示开关等。
- 音频：扬声器/麦克风仅开关、默认设备；GIF 时禁用；仅 MP4 含音轨。
- 导出参数：沿用 ScreenCapture 默认值。
- 编辑能力：无。
- 保存：先写 `AppData\Roaming\ScreenMovie` 临时文件，结束时复制到用户选定路径；无命名规则/历史记录要求。
- 结束录制：弹保存对话框（不保留丢弃/剪贴板）。

## 7. 工作流程约定

1. 用户持续补充具体需求。
2. 本文件逐条记录，双方确认需求完全清楚后，才开始编码。
3. 编码阶段只实现明确记录的需求，不擅自扩大范围。
