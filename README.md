# ScreenMovie

Windows 桌面录屏工具：拖拽框选或吸附窗口区域，录制桌面画面并导出 MP4 或 GIF。

基于 [DUI](https://github.com/) 框架（C++20 / Direct3D 11 / 零依赖）构建，编码核心沿用 ScreenCapture 的 Media Foundation（MP4）与 gifski（GIF）。

## 使用

1. 启动后鼠标变为十字，在屏幕上拖拽选取录制区域；把光标移到某个窗口上会吸附为该窗口的可见边框。
2. 选好后区域附近出现工具条：
   - 选择格式：MP4 / GIF（默认 GIF）；选 GIF 时音频开关禁用。
   - 扬声器 / 麦克风：仅 MP4 有效，开关状态，自动使用默认设备。
   - 开始录制 / 退出。
3. 点击"开始录制"后，选区中央显示 3 秒倒计时；倒计时结束后开始录制，工具条切换为"已录制时长 / 时长上限 + 结束录制"。
   - 时长上限：MP4 120 分钟，GIF 6 分钟，到时自动结束。
4. 点击"结束录制"（或到时）后弹出保存对话框，把临时文件复制到指定路径。

工具条按"下方 → 上方 → 选区右下角"顺序自动避让屏幕边缘；工具条与倒计时均不会被录入成品。

## 构建

用 Visual Studio 2026（C++20，x64）打开 `ScreenMovie.slnx` 构建即可（Debug / Release）。

依赖：

- `D:\project\DUI`：DUI 框架，需先构建 `DUI\DUI.vcxproj` 的对应配置。
- `D:\sdk\gifski`：GIF 编码库（头文件 `gifski.h` 与 `target\release\gifski.lib`）。
- 系统库：Media Foundation、Winsock、ntdll 等（工程已配置）。

构建产物：`Build\ScreenMovie\<配置>\ScreenMovie.exe`，字体图标 `iconfont.ttf` 自动拷贝到同目录。

详细需求与截图见 `Doc/Requirements.md`。
