#pragma once

#include "DUI/DUI.h"
#include "Recorder.h"
#include "CoverWindow.h"
#include "ToolBar.h"

// 主应用：启动进选区态，选好后弹工具栏，录制完成弹保存对话框。
class MainApp final : public IToolBarHost {
public:
    int run();

    RecordFormat format() const noexcept override { return format_; }
    bool useSpeaker() const noexcept override { return useSpeaker_; }
    bool useMic() const noexcept override { return useMic_; }
    void setFormat(RecordFormat f) noexcept override { format_ = f; }
    void setSpeaker(bool on) noexcept override { useSpeaker_ = on; }
    void setMic(bool on) noexcept override { useMic_ = on; }

    void onSelectionDone(ScreenRect rect);
    void onCancel() override;
    void onStart() override;
    void onStop() override;

private:
    void onCountdownTick();
    void onRecordTick();
    void saveAndFinish(const std::wstring& tempPath);

    dui::App app_;
    CoverWindow cover_;
    ToolBar toolbar_;
    Recorder recorder_;
    ScreenRect selection_{};
    RecordFormat format_{ RecordFormat::Mp4 };
    bool useSpeaker_{ false }, useMic_{ false };
    unsigned countdownTimer_{ 0 }, recordTimer_{ 0 };
    int countdown_{ 0 }, elapsed_{ 0 };
};
