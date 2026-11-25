#pragma once

#include "DUI/DUI.h"
#include "Recorder.h"

// 虚拟桌面物理矩形（可为负坐标）与全局桌面范围（在 main.cpp 中定义 / 初始化）。
struct ScreenRect {
    long left{}, top{}, right{}, bottom{};
    long width() const noexcept { return right - left; }
    long height() const noexcept { return bottom - top; }
};
extern ScreenRect g_desktop;

constexpr dui::Color kBlue = 0xFF1677FF;
constexpr dui::Color kPanel = 0xFFF7F8FA;
constexpr dui::Color kInk = 0xFF1F2329;
constexpr dui::Color kRed = 0xFFE34D59;
constexpr dui::Color kWhite = 0xFFFFFFFF;

// 工具栏宿主：由主应用实现，避免 ToolBar 反向依赖 main.cpp 里的具体类型。
class IToolBarHost {
public:
    virtual ~IToolBarHost() = default;
    virtual RecordFormat format() const noexcept = 0;
    virtual bool useSpeaker() const noexcept = 0;
    virtual bool useMic() const noexcept = 0;
    virtual void setFormat(RecordFormat f) noexcept = 0;
    virtual void setSpeaker(bool on) noexcept = 0;
    virtual void setMic(bool on) noexcept = 0;
    virtual void onStart() = 0;
    virtual void onCancel() = 0;
    virtual void onStop() = 0;
};

// 独立工具栏窗口：覆盖窗口整体鼠标穿透时，工具栏仍可接收鼠标。
class ToolBar final : public dui::Window {
public:
    enum class Mode { Setting, Recording };

    ToolBar();

    void setHost(IToolBarHost* host) noexcept { host_ = host; }

    void showSetting();
    void showRecording(int elapsed, int limit);
    void updateTimer(int elapsed, int limit);
    void position(const ScreenRect& sel);

    void show() { Show(); }
    void hide() { Hide(); }
    void excludeFromCapture();

private:
    void applyStyle();
    void fitWidth();
    void rebuild();
    dui::Button* addButton(const wchar_t* text, const dui::Style& style);
    void syncFormat();
    void setAudioEnabled(bool enabled);

    IToolBarHost* host_{ nullptr };
    Mode mode_{ Mode::Setting };
    int elapsed_{ 0 }, limit_{ 0 };
    float widthDip_{ 420.f };
    dui::Button* mp4_{ nullptr };
    dui::Button* gif_{ nullptr };
    dui::Button* speaker_{ nullptr };
    dui::Button* mic_{ nullptr };
    dui::Text* timer_{ nullptr };
    dui::Text* progress_{ nullptr };
};
