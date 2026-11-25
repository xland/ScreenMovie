#include "ToolBar.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

ScreenRect g_desktop;   // 虚拟桌面物理矩形（ToolBar.h 声明，MainApp::run() 中初始化）

namespace {

// 字体文件在 exe 同目录（PostBuild 拷贝）。相对路径依赖工作目录，VS 调试时
// 工作目录常是工程根 → 加载失败 → PUA 码位静默回退成 □，故用 exe 绝对路径。
static dui::String iconFontPath() {
    wchar_t dir[MAX_PATH]{};
    GetModuleFileNameW(nullptr, dir, MAX_PATH);
    wchar_t* slash = wcsrchr(dir, L'\\');
    if (slash) slash[1] = L'\0';
    return dui::String(dir) + L"iconfont.ttf";
}
static const dui::String kIconFamily = dui::LoadFontFamily(iconFontPath().c_str());

bool captureExclusionSupported() {
    OSVERSIONINFOW vi{ sizeof(vi) };
    auto rtlGetVersion = reinterpret_cast<LONG(WINAPI*)(OSVERSIONINFOW*)>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
    return rtlGetVersion != nullptr && rtlGetVersion(&vi) == 0 && vi.dwBuildNumber >= 19041;
}

dui::String formatTime(int seconds) {
    wchar_t buf[32];
    swprintf_s(buf, L"%02d:%02d", seconds / 60, seconds % 60);
    return buf;
}

constexpr float kToolBarHeight = 32.f;

static const dui::Style kToolBar = dui::Style()
    .FlexDirection(dui::FlexDirection::Row).AlignItems(dui::Align::Stretch)
    .Background(kPanel);

static const dui::Style kChoice = dui::Style()
    .Height(kToolBarHeight).FontSize(13.f).Color(kInk)
    .Padding(dui::Thickness(10.f, 4.f, 10.f, 0.f))
    .TextAlign(dui::TextAlign::Center).Cursor(dui::Cursor::Hand);

static const dui::Style kIconBtn = dui::Style()
    .Height(kToolBarHeight).FontFamily(kIconFamily.c_str()).FontSize(18.f).Color(0xFF8A9099)
    .Padding(dui::Thickness(10.f, 4.f, 10.f, 0.f))
    .TextAlign(dui::TextAlign::Center).Cursor(dui::Cursor::Hand);

static const dui::Style kStartBtn = dui::Style()
    .Height(kToolBarHeight).FontSize(13.f).Color(kWhite).Background(kBlue)
    .Padding(dui::Thickness(12.f, 4.f, 12.f, 0.f))
    .TextAlign(dui::TextAlign::Center).Cursor(dui::Cursor::Hand).FlexShrink(0.f);

static const dui::Style kQuitBtn = dui::Style()
    .Height(kToolBarHeight).FontSize(13.f).Color(kInk).Background(kWhite)
    .Padding(dui::Thickness(10.f, 4.f, 10.f, 0.f))
    .TextAlign(dui::TextAlign::Center).Cursor(dui::Cursor::Hand).FlexShrink(0.f);

static const dui::Style kStopBtn = dui::Style()
    .Height(kToolBarHeight).FontSize(13.f).Color(kWhite).Background(kRed)
    .Padding(dui::Thickness(12.f, 4.f, 12.f, 0.f))
    .TextAlign(dui::TextAlign::Center).Cursor(dui::Cursor::Hand).FlexShrink(0.f);

static const dui::Style kChoiceSelected = dui::Style().Background(0xFFD9E9FF).Color(kBlue);
static const dui::Style kChoiceHover = dui::Style().Background(0xFFE7EEF8).Opacity(0.9f);
static const dui::Style kIconSelected = dui::Style().Background(0xFFD9E9FF).Color(kBlue);
static const dui::Style kIconHover = dui::Style().Color(kBlue);
static const dui::Style kStartHover = dui::Style().Background(0xFF4096FF);
static const dui::Style kQuitHover = dui::Style().Background(0xFFEDEFF3);
static const dui::Style kStopHover = dui::Style().Background(0xFFF0666F);

}  // namespace

ToolBar::ToolBar()
    : dui::Window(dui::WindowOptions{
          .title = L"ScreenMovie ToolBar", .width = 420.f, .height = kToolBarHeight,
          .frameless = true, .transparent = true,
          .center = false, .resizable = false, .doubleClickMaximize = false,
          .topmost = true, .toolWindow = true, .noActivate = true }) {
    Root()->OnPointerDown([this](dui::PointerEvent& ev) {
        if (mode_ == Mode::Setting && ev.button == dui::PointerButton::Right) {
            ev.handled = true;
            host_->onCancel();
        }
    }, dui::Phase::Tunnel);
}

dui::Button* ToolBar::addButton(const wchar_t* text, const dui::Style& style) {
    return Root()->AddChild<dui::Button>(text, style);
}

void ToolBar::excludeFromCapture() {
    // 19041 之前 DWM 把该标记当成"受保护内容"，录制里会变成一块黑，
    // 而不是露出下面的应用画面；ScreenCapture 也做了同样的版本闸门。
    if (!captureExclusionSupported()) return;
    SetWindowDisplayAffinity(NativeHandle(), WDA_EXCLUDEFROMCAPTURE);
}

void ToolBar::syncFormat() {
    const RecordFormat fmt = host_->format();
    if (mp4_) mp4_->SetState(dui::State::Selected, fmt == RecordFormat::Mp4);
    if (gif_) gif_->SetState(dui::State::Selected, fmt == RecordFormat::Gif);
    setAudioEnabled(fmt == RecordFormat::Mp4);
}

void ToolBar::setAudioEnabled(bool enabled) {
    for (auto* btn : { speaker_, mic_ }) {
        if (!btn) continue;
        btn->SetState(dui::State::Disabled, !enabled);
        btn->When(dui::State::Disabled, dui::Style().Color(0xFFC9CDD4));
    }
}

void ToolBar::rebuild() {
    for (dui::Element* child : std::vector<dui::Element*>(Root()->Children())) Root()->RemoveChild(child);
    mp4_ = gif_ = speaker_ = mic_ = nullptr;
    timer_ = progress_ = nullptr;
}

void ToolBar::applyStyle() {
    Root()->SetStyle(dui::Style(kToolBar)
        .Height(kToolBarHeight)
        .Gap(0.f));
}

void ToolBar::fitWidth() {
    dui::RunLayout(Root(), { 4096.f, kToolBarHeight }, Scale());
    widthDip_ = std::max(80.f, std::ceil(Root()->Measured().x));
    Resize(widthDip_, kToolBarHeight);
}

void ToolBar::showSetting() {
    mode_ = Mode::Setting;
    rebuild();
    applyStyle();
    mp4_ = addButton(L"MP4", kChoice);
    gif_ = addButton(L"GIF", kChoice);
    for (dui::Button* btn : { mp4_, gif_ }) {
        btn->When(dui::State::Selected, kChoiceSelected);
        btn->When(dui::State::Hover, kChoiceHover);
    }
    mp4_->OnClick([this](dui::PointerEvent&) {
        host_->setFormat(RecordFormat::Mp4);
        syncFormat();
    });
    gif_->OnClick([this](dui::PointerEvent&) {
        host_->setFormat(RecordFormat::Gif);
        syncFormat();
    });

    Root()->AddChild<dui::Panel>(dui::Style()
        .Width(1.f).Height(18.f).AlignSelf(dui::Align::Center).Background(0xFFDDE0E5));

    speaker_ = addButton(L"\ue654", kIconBtn);
    mic_ = addButton(L"\ue73b", kIconBtn);
    speaker_->When(dui::State::Selected, kIconSelected);
    mic_->When(dui::State::Selected, kIconSelected);
    speaker_->When(dui::State::Hover, kIconHover);
    mic_->When(dui::State::Hover, kIconHover);
    speaker_->OnClick([this](dui::PointerEvent& ev) {
        if (speaker_->HasState(dui::State::Disabled)) { ev.handled = true; return; }
        host_->setSpeaker(!host_->useSpeaker());
        speaker_->SetState(dui::State::Selected, host_->useSpeaker());
    });
    mic_->OnClick([this](dui::PointerEvent& ev) {
        if (mic_->HasState(dui::State::Disabled)) { ev.handled = true; return; }
        host_->setMic(!host_->useMic());
        mic_->SetState(dui::State::Selected, host_->useMic());
    });
    speaker_->SetState(dui::State::Selected, host_->useSpeaker());
    mic_->SetState(dui::State::Selected, host_->useMic());

    dui::Button* start = addButton(L"开始录制", kStartBtn);
    start->When(dui::State::Hover, kStartHover);
    start->OnClick([this](dui::PointerEvent&) { host_->onStart(); });
    dui::Button* quit = addButton(L"退出", kQuitBtn);
    quit->When(dui::State::Hover, kQuitHover);
    quit->OnClick([this](dui::PointerEvent&) { host_->onCancel(); });

    syncFormat();
    fitWidth();
}

void ToolBar::showRecording(int elapsed, int limit) {
    mode_ = Mode::Recording;
    elapsed_ = elapsed;
    limit_ = limit;
    rebuild();
    applyStyle();
    timer_ = Root()->AddChild<dui::Text>(L"", dui::Style()
        .AlignSelf(dui::Align::Center).FontSize(14.f).Color(kInk).Width(124.f)
        .TextAlign(dui::TextAlign::Center));
    progress_ = Root()->AddChild<dui::Text>(L"", dui::Style()
        .AlignSelf(dui::Align::Center).FontSize(12.f).Color(0xFF8A9099).Width(36.f)
        .TextAlign(dui::TextAlign::Center));
    dui::Button* stop = addButton(L"结束录制", kStopBtn);
    stop->When(dui::State::Hover, kStopHover);
    stop->OnClick([this](dui::PointerEvent&) { host_->onStop(); });
    updateTimer(elapsed, limit);
    fitWidth();
}

void ToolBar::updateTimer(int elapsed, int limit) {
    if (!timer_) return;
    const dui::String text = formatTime(elapsed) + L" / " + formatTime(limit);
    timer_->SetText(text.c_str());
    if (progress_) progress_->SetText((std::to_wstring(elapsed * 100 / std::max(1, limit)) + L"%").c_str());
}

void ToolBar::position(const ScreenRect& sel) {
    // 选区中心落在哪块屏，就用哪块屏的工作区避让（Screen::FromPoint 物理像素）。
    const dui::ScreenInfo scr = dui::Screen::FromPoint(
        static_cast<float>((sel.left + sel.right) / 2),
        static_cast<float>((sel.top + sel.bottom) / 2));
    const dui::Rect work = scr.workAreaPx;

    const long toolW = static_cast<long>(std::ceil(dui::Screen::DipToPx(scr, widthDip_)));
    const long toolH = static_cast<long>(std::ceil(dui::Screen::DipToPx(scr, kToolBarHeight)));
    const long gap = static_cast<long>(std::ceil(dui::Screen::DipToPx(scr, 8.f)));
    long x = std::min(sel.right, static_cast<long>(work.Right())) - toolW;
    x = std::clamp(x, static_cast<long>(work.x),
                   static_cast<long>(std::max(work.x, work.Right() - toolW)));
    long y = sel.bottom + gap;
    if (y + toolH > work.Bottom()) {
        y = sel.top - gap - toolH;
        if (y < work.y) y = sel.bottom - toolH - static_cast<long>(dui::Screen::DipToPx(scr, 3.f));
    }
    y = std::clamp(y, static_cast<long>(work.y),
                   static_cast<long>(std::max(work.y, work.Bottom() - toolH)));
    SetBoundsPx({ static_cast<float>(x), static_cast<float>(y),
                  static_cast<float>(toolW), static_cast<float>(toolH) });
}
