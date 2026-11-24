#include <windows.h>
#include <dwmapi.h>

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "DUI/DUI.h"
#include "Recorder.h"

namespace {

constexpr dui::Color kMask = 0x77000000;
constexpr dui::Color kBlue = 0xFF1677FF;
constexpr dui::Color kPanel = 0xFFF7F8FA;
constexpr dui::Color kInk = 0xFF1F2329;
constexpr dui::Color kRed = 0xFFE34D59;
constexpr dui::Color kWhite = 0xFFFFFFFF;

static const dui::String kIconFamily = dui::LoadFontFamily(L"iconfont.ttf");

struct ScreenRect {
    long left{}, top{}, right{}, bottom{};
    long width() const noexcept { return right - left; }
    long height() const noexcept { return bottom - top; }
};

ScreenRect g_desktop;   // 虚拟桌面物理矩形（多屏可为负坐标）
float g_scale = 1.f;     // 覆盖窗口的 DPI 缩放（OnCreate / OnDpiChanged 更新）
class MainApp;

dui::String formatTime(int seconds) {
    wchar_t buf[32];
    swprintf_s(buf, L"%02d:%02d", seconds / 60, seconds % 60);
    return buf;
}

// ---------------------------------------------------------------------------
// 选区层：铺满整个虚拟桌面，画四块遮罩 + 蓝色选区框 + 窗口高亮吸附。
// ---------------------------------------------------------------------------
class SelectionLayer final : public dui::Element {
public:
    enum class Stage { Select, Countdown, Recording };

    void setStage(Stage s) { stage_ = s; InvalidatePaint(); }
    bool hasRect() const noexcept { return sel_.width() > 4 && sel_.height() > 4; }
    ScreenRect selection() const noexcept { return sel_; }
    void setSelection(ScreenRect r) { sel_ = r; InvalidatePaint(); }
    void setCountdown(int n) {
        count_ = n;
        if (digit_) {
            const float s = g_scale;
            digit_->SetStyle(dui::Style()
                .Position(dui::Position::Absolute)
                .Left(static_cast<float>((sel_.left + sel_.right) / 2 - g_desktop.left) / s - 48.f)
                .Top(static_cast<float>((sel_.top + sel_.bottom) / 2 - g_desktop.top) / s - 48.f)
                .Width(96.f).Height(96.f).FontSize(48.f).Color(kWhite)
                .TextAlign(dui::TextAlign::Center));
        }
        InvalidatePaint();
    }
    void bindDigit(dui::Text* label) { digit_ = label; }
    void setOnSelected(std::function<void(ScreenRect)> fn) { onSelected_ = std::move(fn); }

    void OnPaint(dui::graphics::Painter& p, const dui::StyleContext&, float scale) override {
        if (stage_ == Stage::Select) {
            const dui::Rect b = Bounds();
            const float s = g_scale;
            const float l = static_cast<float>(sel_.left - g_desktop.left) / s;
            const float t = static_cast<float>(sel_.top - g_desktop.top) / s;
            const float r = static_cast<float>(sel_.right - g_desktop.left) / s;
            const float bo = static_cast<float>(sel_.bottom - g_desktop.top) / s;
            p.FillRoundedRect(dui::Rect{b.x, b.y, b.w, std::max(0.f, t - b.y)}, 0.f, kMask, 1.f, scale);
            p.FillRoundedRect(dui::Rect{b.x, bo, b.w, std::max(0.f, b.y + b.h - bo)}, 0.f, kMask, 1.f, scale);
            p.FillRoundedRect(dui::Rect{b.x, t, std::max(0.f, l - b.x), std::max(0.f, bo - t)}, 0.f, kMask, 1.f, scale);
            p.FillRoundedRect(dui::Rect{r, t, std::max(0.f, b.x + b.w - r), std::max(0.f, bo - t)}, 0.f, kMask, 1.f, scale);
            if (hasRect()) {
                p.StrokeRoundRect(dui::Rect{l, t, r - l, bo - t}, 0.f, 2.f, kBlue, scale);
            }
        } else if (stage_ == Stage::Countdown && count_ > 0 && digit_) {
            const float s = g_scale;
            const float cx = static_cast<float>((sel_.left + sel_.right) / 2 - g_desktop.left) / s;
            const float cy = static_cast<float>((sel_.top + sel_.bottom) / 2 - g_desktop.top) / s;
            p.FillEllipse(dui::Vec2{cx, cy}, 46.f, 46.f, 0x99000000, scale);
            const wchar_t digit[2] = { static_cast<wchar_t>(L'0' + count_), 0 };
            if (digitLast_ != count_) {
                digit_->SetText(digit);
                digitLast_ = count_;
            }
        }
        // Stage::Recording：什么都不画，避免遮罩 / 倒计时残影被录入。
    }

    bool HandlePointerDown(dui::PointerEvent& ev) override {
        if (stage_ != Stage::Select) return false;
        dragging_ = true;
        SetCapture();
        press_ = ev.pos;
        sel_ = toScreenRect(press_.x, press_.y, ev.pos.x, ev.pos.y);
        InvalidatePaint();
        return true;
    }

    bool HandlePointerMove(dui::PointerEvent& ev) override {
        if (stage_ != Stage::Select) return false;
        if (dragging_) {
            const ScreenRect next = toScreenRect(press_.x, press_.y, ev.pos.x, ev.pos.y);
            if (next.left != sel_.left || next.top != sel_.top ||
                next.right != sel_.right || next.bottom != sel_.bottom) {
                sel_ = next;
                InvalidatePaint();
            }
        } else {
            highlightWindow(ev.pos);
        }
        return true;
    }

    bool HandlePointerUp(dui::PointerEvent& ev) override {
        if (stage_ != Stage::Select || !dragging_) return false;
        dragging_ = false;
        sel_ = toScreenRect(press_.x, press_.y, ev.pos.x, ev.pos.y);
        InvalidatePaint();
        if (onSelected_ && hasRect()) onSelected_(sel_);
        return true;
    }

protected:
    dui::Vec2 MeasureContent(const dui::StyleContext&, const dui::Vec2& avail) const override {
        return avail;
    }

private:
    float scale() const { return g_scale; }

    ScreenRect toScreenRect(float x0, float y0, float x1, float y1) const {
        const float s = scale();
        const long l = g_desktop.left + static_cast<long>(std::min(x0, x1) * s);
        const long t = g_desktop.top + static_cast<long>(std::min(y0, y1) * s);
        const long r = g_desktop.left + static_cast<long>(std::max(x0, x1) * s);
        const long b = g_desktop.top + static_cast<long>(std::max(y0, y1) * s);
        return { std::max(l, g_desktop.left), std::max(t, g_desktop.top),
                 std::min(r, g_desktop.right), std::min(b, g_desktop.bottom) };
    }

    struct HighlightCtx { POINT pt; RECT found{}; bool hit{}; };

    static BOOL CALLBACK enumWindow(HWND hwnd, LPARAM lp) {
        auto* ctx = reinterpret_cast<HighlightCtx*>(lp);
        if (!IsWindowVisible(hwnd)) return TRUE;
        LONG cloaked = 0;
        DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
        if (cloaked) return TRUE;
        RECT rc{};
        if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &rc, sizeof(rc)))) return TRUE;
        if (PtInRect(&rc, ctx->pt)) {
            ctx->found = rc;
            ctx->hit = true;
            return FALSE;
        }
        return TRUE;
    }

    void highlightWindow(dui::Vec2 pos) {
        const float s = scale();
        HighlightCtx ctx{ { g_desktop.left + static_cast<long>(pos.x * s),
                            g_desktop.top + static_cast<long>(pos.y * s) } };
        EnumWindows(enumWindow, reinterpret_cast<LPARAM>(&ctx));
        if (!ctx.hit) return;
        const ScreenRect next{
            std::max(ctx.found.left, g_desktop.left), std::max(ctx.found.top, g_desktop.top),
            std::min(ctx.found.right, g_desktop.right), std::min(ctx.found.bottom, g_desktop.bottom) };
        if (next.left != sel_.left || next.top != sel_.top ||
            next.right != sel_.right || next.bottom != sel_.bottom) {
            sel_ = next;
            InvalidatePaint();
        }
    }

    Stage stage_{ Stage::Select };
    ScreenRect sel_{};
    dui::Vec2 press_{};
    bool dragging_{ false };
    int count_{ 0 }, digitLast_{ -1 };
    dui::Text* digit_{ nullptr };
    std::function<void(ScreenRect)> onSelected_;
};

// ---------------------------------------------------------------------------
// 全屏覆盖窗口：承载选区层，选好后让鼠标穿透并隐藏绘制。
// ---------------------------------------------------------------------------
class CoverWindow final : public dui::Window {
public:
    CoverWindow()
        : dui::Window(dui::WindowOptions{
              .title = L"ScreenMovie", .width = 800.f, .height = 600.f,
              .frameless = true, .transparent = true,
              .center = false, .resizable = false, .doubleClickMaximize = false }) {}

    SelectionLayer* layer() noexcept { return layer_; }

    void coverDesktop() {
        const HWND hwnd = NativeHandle();
        SetWindowPos(hwnd, HWND_TOPMOST,
                     g_desktop.left, g_desktop.top,
                     g_desktop.width(), g_desktop.height(),
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE,
            GetWindowLongPtrW(hwnd, GWL_EXSTYLE) | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE);
        // 整层都摘出捕获：倒计时、遮罩、选区框都不会被录进文件。
        if (auto* fn = reinterpret_cast<decltype(&SetWindowDisplayAffinity)>(
                GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetWindowDisplayAffinity"))) {
            fn(hwnd, 0x11);  // WDA_EXCLUDEFROMCAPTURE
        }
    }

    void setMouseThrough(bool through) {
        const HWND hwnd = NativeHandle();
        LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        if (through) SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex | WS_EX_TRANSPARENT);
        else SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex & ~static_cast<LONG_PTR>(WS_EX_TRANSPARENT));
    }

    void hide() { ShowWindow(NativeHandle(), SW_HIDE); }

protected:
    void OnCreate() override {
        g_scale = Scale();
        SetCursor(LoadCursorW(nullptr, IDC_CROSS));
        dui::Panel* root = Root();
        root->SetStyle(dui::Style().FlexDirection(dui::FlexDirection::Column));
        layer_ = root->AddChild<SelectionLayer>(dui::Style().FlexGrow(1.f));
        digit_ = root->AddChild<dui::Text>(L"", dui::Style()
            .Position(dui::Position::Absolute)
            .Width(96.f).Height(96.f)
            .FontSize(48.f).Color(kWhite)
            .TextAlign(dui::TextAlign::Center));
        layer_->bindDigit(digit_);
        coverDesktop();
    }

    void OnDpiChanged(const dui::DpiChanged& d) override { g_scale = d.newScale; coverDesktop(); }

private:
    SelectionLayer* layer_{ nullptr };
    dui::Text* digit_{ nullptr };
};

// ---------------------------------------------------------------------------
// 工具条窗口：两种形态（设置 / 录制中），置顶且不进录制画面。
// ---------------------------------------------------------------------------
class ToolWindow final : public dui::Window {
public:
    enum class Mode { Setting, Recording };

    explicit ToolWindow()
        : dui::Window(dui::WindowOptions{
              .title = L"ScreenMovie", .width = 460.f, .height = 64.f,
              .frameless = true, .transparent = true,
              .center = false, .resizable = false, .doubleClickMaximize = false }),
          app_(nullptr) {}

    void showSetting();
    void showRecording(int elapsed, int limit);
    void updateTimer(int elapsed, int limit);
    void position(const ScreenRect& sel);
    void setAudioEnabled(bool enabled);

protected:
    void OnCreate() override {
        const HWND hwnd = NativeHandle();
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE,
            GetWindowLongPtrW(hwnd, GWL_EXSTYLE) | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW);
        if (auto* fn = reinterpret_cast<decltype(&SetWindowDisplayAffinity)>(
                GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetWindowDisplayAffinity"))) {
            fn(hwnd, 0x11);  // WDA_EXCLUDEFROMCAPTURE
        }
        showSetting();
    }

private:
    void rebuild();
    dui::Button* addButton(const wchar_t* text, const dui::Style& style);
    void syncFormat();

    class     MainApp* app_;
    friend class MainApp;
    Mode mode_{ Mode::Setting };

    int elapsed_{ 0 }, limit_{ 0 };
    dui::Panel* body_{ nullptr };
    dui::Button* mp4_{ nullptr };
    dui::Button* gif_{ nullptr };
    dui::Button* speaker_{ nullptr };
    dui::Button* mic_{ nullptr };
    dui::Text* timer_{ nullptr };
    dui::Text* progress_{ nullptr };
};

// ---------------------------------------------------------------------------
// 主应用：启动进选区态，选好后弹工具条，录制完成弹保存对话框。
// ---------------------------------------------------------------------------
class MainApp {
public:
    int run();

    RecordFormat format() const noexcept { return format_; }
    bool useSpeaker() const noexcept { return useSpeaker_; }
    bool useMic() const noexcept { return useMic_; }
    void setFormat(RecordFormat f) noexcept { format_ = f; }
    void setSpeaker(bool on) noexcept { useSpeaker_ = on; }
    void setMic(bool on) noexcept { useMic_ = on; }

    void onSelectionDone(ScreenRect rect);
    void onCancel();
    void onStart();
    void onStop();

private:
    void onCountdownTick();
    void onRecordTick();
    void saveAndFinish(const std::wstring& tempPath);

    dui::App app_;
    CoverWindow cover_;
    ToolWindow tool_;
    Recorder recorder_;
    ScreenRect selection_{};
    friend class ToolWindow;
    RecordFormat format_{ RecordFormat::Gif };
    bool useSpeaker_{ false }, useMic_{ false };
    unsigned countdownTimer_{ 0 }, recordTimer_{ 0 };
    int countdown_{ 0 }, elapsed_{ 0 };
};

static const dui::Style kToolBar = dui::Style()
    .FlexDirection(dui::FlexDirection::Row).AlignItems(dui::Align::Center)
    .Padding(dui::Thickness(8.f, 10.f)).Gap(8.f)
    .Background(kPanel).CornerRadius(8.f);

static const dui::Style kChoice = dui::Style()
    .FontSize(13.f).Color(kInk).Padding(dui::Thickness(5.f, 10.f))
    .CornerRadius(5.f).TextAlign(dui::TextAlign::Center).Cursor(dui::Cursor::Hand);

static const dui::Style kChoiceOn = dui::Style()
    .Background(0xFFD9E9FF).Color(kBlue);

static const dui::Style kIconBtn = dui::Style()
    .FontFamily(kIconFamily.c_str()).FontSize(18.f).Color(0xFF8A9099)
    .Padding(dui::Thickness(4.f, 6.f)).CornerRadius(5.f)
    .TextAlign(dui::TextAlign::Center).Cursor(dui::Cursor::Hand);

static const dui::Style kStartBtn = dui::Style()
    .FontSize(13.f).Color(kWhite).Background(kBlue)
    .Padding(dui::Thickness(6.f, 14.f)).CornerRadius(5.f)
    .TextAlign(dui::TextAlign::Center).Cursor(dui::Cursor::Hand).FlexShrink(0.f);

static const dui::Style kQuitBtn = dui::Style()
    .FontSize(13.f).Color(kInk).Background(kWhite)
    .Padding(dui::Thickness(6.f, 12.f)).CornerRadius(5.f)
    .TextAlign(dui::TextAlign::Center).Cursor(dui::Cursor::Hand).FlexShrink(0.f);

static const dui::Style kStopBtn = dui::Style()
    .FontSize(13.f).Color(kWhite).Background(kRed)
    .Padding(dui::Thickness(6.f, 14.f)).CornerRadius(5.f)
    .TextAlign(dui::TextAlign::Center).Cursor(dui::Cursor::Hand).FlexShrink(0.f);

dui::Button* ToolWindow::addButton(const wchar_t* text, const dui::Style& style) {
    auto* btn = body_->AddChild<dui::Button>(text, style);
    return btn;
}

void ToolWindow::syncFormat() {
    const RecordFormat fmt = app_->format();
    if (mp4_) mp4_->SetStyle(fmt == RecordFormat::Mp4 ? dui::Style(kChoice).Background(0xFFD9E9FF).Color(kBlue) : kChoice);
    if (gif_) gif_->SetStyle(fmt == RecordFormat::Gif ? dui::Style(kChoice).Background(0xFFD9E9FF).Color(kBlue) : kChoice);
}

void ToolWindow::setAudioEnabled(bool enabled) {
    for (auto* btn : { speaker_, mic_ }) {
        if (!btn) continue;
        btn->SetState(dui::State::Disabled, !enabled);
        btn->When(dui::State::Disabled, dui::Style().Color(0xFFC9CDD4));
    }
}

void ToolWindow::rebuild() {
    if (!body_) return;
    for (dui::Element* child : std::vector<dui::Element*>(body_->Children())) body_->RemoveChild(child);
    mp4_ = gif_ = speaker_ = mic_ = nullptr;
    timer_ = progress_ = nullptr;
}

void ToolWindow::showSetting() {
    mode_ = Mode::Setting;
    if (!body_) {
        body_ = Root()->AddChild<dui::Panel>(kToolBar);
    } else {
        rebuild();
        body_->SetStyle(kToolBar);
    }
    body_->AddChild<dui::Text>(L"选择格式", dui::Style().FontSize(12.f).Color(0xFF646A73));
    mp4_ = addButton(L"MP4", kChoice);
    gif_ = addButton(L"GIF", kChoice);
    mp4_->OnClick([this](dui::PointerEvent&) {
        app_->setFormat(RecordFormat::Mp4);
        syncFormat();
    });
    gif_->OnClick([this](dui::PointerEvent&) {
        app_->setFormat(RecordFormat::Gif);
        syncFormat();
    });

    body_->AddChild<dui::Panel>(dui::Style().Width(1.f).Height(18.f).Background(0xFFDDE0E5));

    speaker_ = addButton(L"\ue654", kIconBtn);
    mic_ = addButton(L"\ue73b", kIconBtn);
    speaker_->OnClick([this](dui::PointerEvent& ev) {
        if (speaker_->HasState(dui::State::Disabled)) { ev.handled = true; return; }
        app_->setSpeaker(!app_->useSpeaker());
        speaker_->SetStyle(app_->useSpeaker() ? dui::Style(kIconBtn).Color(kBlue) : kIconBtn);
    });
    mic_->OnClick([this](dui::PointerEvent& ev) {
        if (mic_->HasState(dui::State::Disabled)) { ev.handled = true; return; }
        app_->setMic(!app_->useMic());
        mic_->SetStyle(app_->useMic() ? dui::Style(kIconBtn).Color(kBlue) : kIconBtn);
    });

    addButton(L"\ue660 开始录制", kStartBtn)->OnClick([this](dui::PointerEvent&) { app_->onStart(); });
    addButton(L"\ue62d 退出", kQuitBtn)->OnClick([this](dui::PointerEvent&) { app_->onCancel(); });

    syncFormat();
    setAudioEnabled(app_->format() == RecordFormat::Mp4);
}

void ToolWindow::showRecording(int elapsed, int limit) {
    mode_ = Mode::Recording;
    elapsed_ = elapsed;
    limit_ = limit;
    rebuild();
    body_->SetStyle(dui::Style(kToolBar).Gap(14.f));
    timer_ = body_->AddChild<dui::Text>(L"", dui::Style()
        .FontSize(14.f).Color(kInk).Width(124.f).TextAlign(dui::TextAlign::Center));
    progress_ = body_->AddChild<dui::Text>(L"", dui::Style().FontSize(12.f).Color(0xFF8A9099));
    addButton(L"结束录制", kStopBtn)->OnClick([this](dui::PointerEvent&) { app_->onStop(); });
    updateTimer(elapsed, limit);
}

void ToolWindow::updateTimer(int elapsed, int limit) {
    if (!timer_) return;
    const dui::String text = formatTime(elapsed) + L" / " + formatTime(limit);
    timer_->SetText(text.c_str());
    if (progress_) progress_->SetText((std::to_wstring(elapsed * 100 / std::max(1, limit)) + L"%").c_str());
}

void ToolWindow::position(const ScreenRect& sel) {
    const RECT scr = { sel.left, sel.top, sel.right, sel.bottom };
    HMONITOR mon = MonitorFromRect(&scr, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{ sizeof(MONITORINFO) };
    GetMonitorInfoW(mon, &mi);

    const long toolW = static_cast<long>(DipToPx(460.f));
    const long toolH = static_cast<long>(DipToPx(64.f));
    const long gap = static_cast<long>(DipToPx(8.f));
    long x = sel.right - toolW;
    long y = sel.bottom + gap;
    if (y + toolH > mi.rcWork.bottom) {
        y = sel.top - gap - toolH;
        if (y < mi.rcWork.top) y = sel.bottom - toolH - static_cast<long>(DipToPx(3.f));
    }
    x = std::clamp(x, mi.rcWork.left, std::max(mi.rcWork.left, mi.rcWork.right - toolW));
    y = std::clamp(y, mi.rcWork.top, std::max(mi.rcWork.top, mi.rcWork.bottom - toolH));
    SetWindowPos(NativeHandle(), HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
}

void MainApp::onSelectionDone(ScreenRect rect) {
    selection_ = rect;
    cover_.layer()->setSelection(rect);
    tool_.showSetting();
    tool_.position(rect);
    ShowWindow(tool_.NativeHandle(), SW_SHOW);
}

void MainApp::onCancel() {
    recorder_.stop();
    app_.Quit(0);
}

void MainApp::onStart() {
    if (!cover_.layer()->hasRect() || recorder_.isRecording()) return;
    ShowWindow(tool_.NativeHandle(), SW_HIDE);
    cover_.layer()->setStage(SelectionLayer::Stage::Countdown);
    cover_.setMouseThrough(true);
    countdown_ = 3;
    cover_.layer()->setCountdown(countdown_);
    countdownTimer_ = app_.SetTimer(1000, [this] { onCountdownTick(); });
}

void MainApp::onCountdownTick() {
    --countdown_;
    if (countdown_ > 0) {
        cover_.layer()->setCountdown(countdown_);
        return;
    }
    app_.KillTimer(countdownTimer_);
    countdownTimer_ = 0;
    cover_.layer()->setStage(SelectionLayer::Stage::Recording);
    cover_.hide();

    elapsed_ = 0;
    tool_.showRecording(0, Recorder::limitSeconds(format_));
    tool_.position(selection_);
    ShowWindow(tool_.NativeHandle(), SW_SHOW);
    const RECT recordRect{ selection_.left, selection_.top, selection_.right, selection_.bottom };
    recorder_.start(format_, recordRect, format_ == RecordFormat::Mp4 && useSpeaker_,
                    format_ == RecordFormat::Mp4 && useMic_);
    recordTimer_ = app_.SetTimer(1000, [this] { onRecordTick(); });
}

void MainApp::onRecordTick() {
    ++elapsed_;
    tool_.updateTimer(elapsed_, Recorder::limitSeconds(format_));
    if (elapsed_ >= Recorder::limitSeconds(format_)) onStop();
}

void MainApp::onStop() {
    if (recordTimer_) { app_.KillTimer(recordTimer_); recordTimer_ = 0; }
    const std::wstring path = recorder_.stop();
    saveAndFinish(path);
}

void MainApp::saveAndFinish(const std::wstring& tempPath) {
    if (tempPath.empty()) {
        app_.Quit(0);
        return;
    }
    static const dui::FileTypeFilter mp4Filters[] = { { L"MP4 视频", L"*.mp4" } };
    static const dui::FileTypeFilter gifFilters[] = { { L"GIF 动图", L"*.gif" } };
    const bool isMp4 = format_ == RecordFormat::Mp4;
    auto result = tool_.SaveFile(dui::SaveFileOptions{
        .title = L"保存录制",
        .filters = isMp4 ? std::span<const dui::FileTypeFilter>(mp4Filters)
                         : std::span<const dui::FileTypeFilter>(gifFilters),
        .defaultExtension = isMp4 ? L"mp4" : L"gif" });
    if (result.IsOk()) {
        CopyFileW(tempPath.c_str(), result.Value().c_str(), FALSE);
    }
    DeleteFileW(tempPath.c_str());
    app_.Quit(0);
}

int MainApp::run() {
    tool_.app_ = this;
    cover_.layer()->setOnSelected([this](ScreenRect r) { onSelectionDone(r); });
    if (!app_.AddWindow(cover_)) return 1;
    app_.AddWindow(tool_);
    ShowWindow(tool_.NativeHandle(), SW_HIDE);
    return app_.Run();
}

}  // namespace

int WINAPI wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int) {
    g_desktop = {
        GetSystemMetrics(SM_XVIRTUALSCREEN),
        GetSystemMetrics(SM_YVIRTUALSCREEN),
        GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN),
        GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN) };

    MainApp app;
    return app.run();
}
