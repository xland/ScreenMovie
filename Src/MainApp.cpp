#include "MainApp.h"

#include <algorithm>
#include <cstdio>

namespace {

std::wstring defaultFileName(const wchar_t* ext) {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t name[48];
    swprintf_s(name, L"%04u%02u%02u%02u%02u%02u%03u.%s",
        static_cast<unsigned>(st.wYear), static_cast<unsigned>(st.wMonth),
        static_cast<unsigned>(st.wDay), static_cast<unsigned>(st.wHour),
        static_cast<unsigned>(st.wMinute), static_cast<unsigned>(st.wSecond),
        static_cast<unsigned>(st.wMilliseconds), ext);
    return name;
}

}  // namespace

void MainApp::onSelectionDone(ScreenRect rect) {
    selection_ = rect;
    cover_.layer()->setLocked(true);          // 停止高亮吸附
    cover_.layer()->setStage(SelectionLayer::Stage::Adjust);
    cover_.layer()->setSelection(rect);
    // 未开始录制前整窗接收鼠标（可调整选区），不穿透。
    ToolBar* tb = &toolbar_;
    tb->showSetting();
    tb->position(rect);
    tb->show();
}

void MainApp::onCancel() {
    recorder_.stop();
    app_.Quit(0);
}

void MainApp::onStart() {
    if (!cover_.layer()->hasRect() || recorder_.isRecording()) return;
    selection_ = cover_.layer()->selection();   // 同步调整后的选区
    toolbar_.hide();
    cover_.layer()->setStage(SelectionLayer::Stage::Countdown);
    cover_.setMouseThrough(true);             // 开始录制后整窗鼠标穿透，工具栏由独立窗口接收
    // 宿主不设 WDA_EXCLUDEFROMCAPTURE：ScreenCapture 也避免对整屏窗口用这个标记，
    // DXGI 桌面复制会把它整块摘空；四块纱在录制区外，裁剪本来就取不到。
    countdown_ = 3;
    cover_.layer()->setCountdown(countdown_);
    countdownTimer_ = cover_.SetTimer(1000, [this] { onCountdownTick(); });
}

void MainApp::onCountdownTick() {
    --countdown_;
    if (countdown_ > 0) {
        cover_.layer()->setCountdown(countdown_);
        return;
    }
    cover_.KillTimer(countdownTimer_);
    countdownTimer_ = 0;
    cover_.layer()->setStage(SelectionLayer::Stage::Recording);
    // 覆盖层全程保留：四块纱持续绘制；录制裁剪只在选区内，纱不会进成品；
    // 鼠标穿透也一直保持。

    elapsed_ = 0;
    ToolBar* tb = &toolbar_;
    tb->showRecording(0, Recorder::limitSeconds(format_));
    tb->position(selection_);
    tb->show();
    const RECT recordRect{ selection_.left, selection_.top, selection_.right, selection_.bottom };
    recorder_.start(format_, recordRect, format_ == RecordFormat::Mp4 && useSpeaker_,
                    format_ == RecordFormat::Mp4 && useMic_);
    recordTimer_ = cover_.SetTimer(1000, [this] { onRecordTick(); });
}

void MainApp::onRecordTick() {
    ++elapsed_;
    toolbar_.updateTimer(elapsed_, Recorder::limitSeconds(format_));
    if (elapsed_ >= Recorder::limitSeconds(format_)) onStop();
}

void MainApp::onStop() {
    if (recordTimer_) { cover_.KillTimer(recordTimer_); recordTimer_ = 0; }
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
    const std::wstring defaultName = defaultFileName(isMp4 ? L"mp4" : L"gif");
    auto result = cover_.SaveFile(dui::SaveFileOptions{
        .title = L"保存录制",
        .initialPath = defaultName.c_str(),
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
    // 必须在任何窗口创建前初始化：coverDesktop() 首次读取 g_desktop。
    const dui::Rect virtualBounds = dui::Screen::VirtualBounds();
    g_desktop = {
        static_cast<long>(virtualBounds.x), static_cast<long>(virtualBounds.y),
        static_cast<long>(virtualBounds.Right()), static_cast<long>(virtualBounds.Bottom()) };

    if (!app_.AddWindow(cover_)) return 1;
    cover_.coverDesktop();
    if (!app_.AddWindow(toolbar_)) return 1;
    toolbar_.Hide();
    toolbar_.excludeFromCapture();
    toolbar_.setHost(this);
    toolbar_.SetTopmost(true);   // 独立窗口压在覆盖层之上，接收工具栏鼠标
    cover_.layer()->setOnSelected([this](ScreenRect r) { onSelectionDone(r); });
    cover_.layer()->setOnAdjust([this](bool begin) {
        ToolBar* tb = &toolbar_;
        if (begin) {
            tb->hide();                         // 调整期间隐藏工具条
        } else {
            selection_ = cover_.layer()->selection();
            tb->position(selection_);           // 重新定位
            tb->show();
        }
    });
    cover_.layer()->setOnCancel([this] { onCancel(); });
    cover_.initWindowRects();
    return app_.Run();
}
