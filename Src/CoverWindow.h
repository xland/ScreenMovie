#pragma once

#include "DUI/DUI.h"
#include "SelectionLayer.h"

// 全屏覆盖窗口：承载选区层与倒计时数字。录制阶段按 ScreenCapture 同款做法整窗让出鼠标，
// 工具栏是独立窗口，不在此承载。
class CoverWindow final : public dui::Window {
public:
    CoverWindow();

    SelectionLayer* layer() noexcept { return layer_; }
    void setMouseThrough(bool v);
    void initWindowRects();
    void coverDesktop();

protected:
    bool OnNcHitTest(dui::Vec2 pt, dui::HitTestCode& hit) override;
    void OnCreate() override;
    void OnDpiChanged(const dui::DpiChanged&) override;

private:
    static LRESULT CALLBACK cursorSubclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                           UINT_PTR, DWORD_PTR ref);

    SelectionLayer* layer_{ nullptr };
    dui::Text* digit_{ nullptr };
    bool mouseThrough_{ false };
};
