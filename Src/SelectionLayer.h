#pragma once

#include <functional>
#include <vector>

#include "DUI/DUI.h"
#include "ToolBar.h"   // ScreenRect / g_desktop / kBlue / kWhite 共享声明

// 选区层：铺满整个虚拟桌面，画四块遮罩 + 蓝色选区框 + 窗口高亮吸附。
class SelectionLayer final : public dui::Element {
public:
    enum class Stage { Select, Adjust, Countdown, Recording };
    enum class Handle { None, Move, LT, RT, LB, RB, T, B, L, R };

    void setStage(Stage s);
    Stage stage() const noexcept { return stage_; }
    void setLocked(bool v) noexcept { locked_ = v; }   // 选区完成后停止高亮吸附
    void refresh() { InvalidatePaint(); }              // 强制重绘（首帧遮罩）
    void initWindowRects(HWND self);
    void setOnAdjust(std::function<void(bool)> fn) { onAdjust_ = std::move(fn); }  // true=开始调整 false=结束
    void setOnCancel(std::function<void()> fn) { onCancel_ = std::move(fn); }

    // 按选区边界把整个覆盖窗口切成九宫格：外围八块各对应一个手柄，内部为 Move。
    Handle hitHandle(dui::Vec2 p) const;
    bool hasRect() const noexcept { return sel_.width() > 4 && sel_.height() > 4; }
    ScreenRect selection() const noexcept { return sel_; }
    void setSelection(ScreenRect r);
    void setCountdown(int n);
    void bindDigit(dui::Text* label) { digit_ = label; }
    void setOnSelected(std::function<void(ScreenRect)> fn) { onSelected_ = std::move(fn); }

    void OnPaint(dui::graphics::Painter& p, const dui::StyleContext&, float scale) override;
    bool HandlePointerDown(dui::PointerEvent& ev) override;
    bool HandlePointerMove(dui::PointerEvent& ev) override;
    bool HandlePointerUp(dui::PointerEvent& ev) override;

protected:
    dui::Vec2 MeasureContent(const dui::StyleContext&, const dui::Vec2&) const override {
        return {};   // 内容由 FlexGrow 撑满，不贡献自然尺寸
    }

private:
    float scale() const { return GetWindow()->Scale(); }
    ScreenRect toScreenRect(float x0, float y0, float x1, float y1) const;
    ScreenRect handleAt(Handle h, dui::Vec2 pos) const;

    struct EnumCtx { HWND self{}; std::vector<ScreenRect>* out{}; };
    static BOOL CALLBACK enumWindow(HWND hwnd, LPARAM lp);
    void initWinRects(HWND self);
    void highlightWindow(dui::Vec2 pos);

    Stage stage_{ Stage::Select };
    ScreenRect sel_{};
    dui::Vec2 press_{};
    bool dragging_{ false }, moved_{ false }, locked_{ false }, hoverInit_{ false };
    dui::Vec2 hoverPos_{};
    std::vector<ScreenRect> winRects_;
    Handle adjusting_{ Handle::None };
    ScreenRect adjustSel_{};
    std::function<void(bool)> onAdjust_;
    std::function<void()> onCancel_;
    int count_{ 0 }, digitLast_{ -1 };
    dui::Text* digit_{ nullptr };
    std::function<void(ScreenRect)> onSelected_;
};
