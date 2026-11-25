#include "SelectionLayer.h"

#include <dwmapi.h>

#include <algorithm>
#include <cmath>

namespace {

constexpr dui::Color kMask = 0x77000000;

}  // namespace

void SelectionLayer::setStage(Stage s) {
    stage_ = s;
    if (s == Stage::Recording && digit_) digit_->SetText(L"");   // 清掉残留的倒计时数字
    InvalidatePaint();
}

void SelectionLayer::initWindowRects(HWND self) { initWinRects(self); }

// 按选区边界把整个覆盖窗口切成九宫格：外围八块各对应一个手柄，内部为 Move。
// 例如左上角块 = 从选区左上角一直延伸到屏幕左上角（窗口左上角为原点）。
SelectionLayer::Handle SelectionLayer::hitHandle(dui::Vec2 p) const {
    if (stage_ != Stage::Adjust || !hasRect()) return Handle::None;
    const float s = scale();
    const float l = static_cast<float>(sel_.left - g_desktop.left) / s;
    const float t = static_cast<float>(sel_.top - g_desktop.top) / s;
    const float r = static_cast<float>(sel_.right - g_desktop.left) / s;
    const float b = static_cast<float>(sel_.bottom - g_desktop.top) / s;
    const bool left = p.x < l, right = p.x > r;
    const bool top = p.y < t, bottom = p.y > b;
    if (left && top) return Handle::LT;
    if (right && top) return Handle::RT;
    if (left && bottom) return Handle::LB;
    if (right && bottom) return Handle::RB;
    if (top) return Handle::T;      // 正上方整片：x 在选区左右范围内，y < 选区上边
    if (bottom) return Handle::B;
    if (left) return Handle::L;
    if (right) return Handle::R;
    return Handle::Move;             // 选区内部
}

void SelectionLayer::setSelection(ScreenRect r) { sel_ = r; InvalidatePaint(); }

void SelectionLayer::setCountdown(int n) {
    count_ = n;
    if (digit_) {
        const float s = GetWindow()->Scale();
        digit_->SetStyle(dui::Style()
            .Position(dui::Position::Absolute)
            .Left(static_cast<float>((sel_.left + sel_.right) / 2 - g_desktop.left) / s - 48.f)
            .Top(static_cast<float>((sel_.top + sel_.bottom) / 2 - g_desktop.top) / s - 48.f)
            .Width(96.f).Height(96.f).FontSize(48.f).Color(kWhite)
            .TextAlign(dui::TextAlign::Center));
    }
    InvalidatePaint();
}

void SelectionLayer::OnPaint(dui::graphics::Painter& p, const dui::StyleContext&, float scale) {
    // OnPaint 的 scale 即所属窗口当前 DPI 缩放（无需再读全局状态）。
    // 遮罩窗口全程保留：四块纱一直画，鼠标穿透由 OnNcHitTest 控制；
    // 录制裁剪只在选区内，纱在选区外不会进成品。
    {
        // 用窗口客户区尺寸作画布，绝不依赖选区层自身的布局盒子（flex 未撑开时为 0 → 白板）。
        const dui::Vec2 cs = GetWindow()->ClientSizeDip();
        const dui::Rect b{ 0.f, 0.f, cs.x, cs.y };
        const float l = static_cast<float>(sel_.left - g_desktop.left) / scale;
        const float t = static_cast<float>(sel_.top - g_desktop.top) / scale;
        const float r = static_cast<float>(sel_.right - g_desktop.left) / scale;
        const float bo = static_cast<float>(sel_.bottom - g_desktop.top) / scale;
        p.FillRoundedRect(dui::Rect{b.x, b.y, b.w, std::max(0.f, t - b.y)}, 0.f, kMask, 1.f, scale);
        p.FillRoundedRect(dui::Rect{b.x, bo, b.w, std::max(0.f, b.y + b.h - bo)}, 0.f, kMask, 1.f, scale);
        p.FillRoundedRect(dui::Rect{b.x, t, std::max(0.f, l - b.x), std::max(0.f, bo - t)}, 0.f, kMask, 1.f, scale);
        p.FillRoundedRect(dui::Rect{r, t, std::max(0.f, b.x + b.w - r), std::max(0.f, bo - t)}, 0.f, kMask, 1.f, scale);
        if (hasRect()) {
            p.StrokeRoundRect(dui::Rect{l, t, r - l, bo - t}, 0.f, 2.f, kBlue, scale);
        }
    }
    if (stage_ == Stage::Countdown && count_ > 0 && digit_) {
        const float cx = static_cast<float>((sel_.left + sel_.right) / 2 - g_desktop.left) / scale;
        const float cy = static_cast<float>((sel_.top + sel_.bottom) / 2 - g_desktop.top) / scale;
        p.FillEllipse(dui::Vec2{cx, cy}, 46.f, 46.f, 0x99000000, scale);
        const wchar_t digit[2] = { static_cast<wchar_t>(L'0' + count_), 0 };
        if (digitLast_ != count_) {
            digit_->SetText(digit);
            digitLast_ = count_;
        }
    }
}

bool SelectionLayer::HandlePointerDown(dui::PointerEvent& ev) {
    if (ev.button == dui::PointerButton::Right &&
        (stage_ == Stage::Select || stage_ == Stage::Adjust)) {
        ev.handled = true;
        if (onCancel_) onCancel_();
        return true;
    }
    if (stage_ == Stage::Adjust) {
        const Handle h = hitHandle(ev.pos);
        if (h == Handle::None) return false;
        adjusting_ = h;
        adjustSel_ = sel_;
        press_ = ev.pos;
        // 按下即把该角 / 那条边落到鼠标处（其余端点保持按下前的位置）。
        sel_ = handleAt(h, ev.pos);
        InvalidatePaint();
        SetCapture();
        if (onAdjust_) onAdjust_(true);   // 隐藏工具条
        return true;
    }
    if (stage_ != Stage::Select) return false;
    dragging_ = true;
    moved_ = false;
    SetCapture();
    press_ = ev.pos;
    // 不在按下时清空 sel_：若这是"单击高亮窗口"，直接沿用高亮矩形作选区。
    return true;
}

bool SelectionLayer::HandlePointerMove(dui::PointerEvent& ev) {
    if (stage_ == Stage::Adjust) {
        if (adjusting_ == Handle::None) return false;
        const ScreenRect n = handleAt(adjusting_, ev.pos);
        if (n.left != sel_.left || n.top != sel_.top ||
            n.right != sel_.right || n.bottom != sel_.bottom) {
            sel_ = n;
            InvalidatePaint();
        }
        return true;
    }
    if (stage_ != Stage::Select) return false;
    if (dragging_) {
        // 超过 3 DIP 才算拖拽框选；否则视为单击高亮窗口。
        if (!moved_ && (std::abs(ev.pos.x - press_.x) > 3.f ||
                        std::abs(ev.pos.y - press_.y) > 3.f)) {
            moved_ = true;
        }
        if (moved_) {
            const ScreenRect next = toScreenRect(press_.x, press_.y, ev.pos.x, ev.pos.y);
            if (next.left != sel_.left || next.top != sel_.top ||
                next.right != sel_.right || next.bottom != sel_.bottom) {
                sel_ = next;
                InvalidatePaint();
            }
        }
    } else {
        if (locked_) return false;   // 选区完成后停止高亮吸附
        // 忽略窗口出现时系统发的"鼠标仍在原处"的那次 move：
        // 否则鼠标停在大窗口上会把整屏纱瞬间挖光，看起来像没有遮罩。
        if (!hoverInit_) {
            hoverInit_ = true;
            hoverPos_ = ev.pos;
        } else if (ev.pos.x != hoverPos_.x || ev.pos.y != hoverPos_.y) {
            hoverPos_ = ev.pos;
            highlightWindow(ev.pos);
        }
    }
    return true;
}

bool SelectionLayer::HandlePointerUp(dui::PointerEvent& ev) {
    if (stage_ == Stage::Adjust) {
        if (adjusting_ == Handle::None) return false;
        adjusting_ = Handle::None;
        if (onAdjust_) onAdjust_(false);   // 重新定位并显示工具条
        return true;
    }
    if (stage_ != Stage::Select || !dragging_) return false;
    dragging_ = false;
    if (moved_) {
        sel_ = toScreenRect(press_.x, press_.y, ev.pos.x, ev.pos.y);
        InvalidatePaint();
    }
    // 单击：sel_ 保持为悬停高亮的窗口矩形，直接确认。
    if (onSelected_ && hasRect()) onSelected_(sel_);
    return true;
}

ScreenRect SelectionLayer::toScreenRect(float x0, float y0, float x1, float y1) const {
    const float s = scale();
    const long l = g_desktop.left + static_cast<long>(std::min(x0, x1) * s);
    const long t = g_desktop.top + static_cast<long>(std::min(y0, y1) * s);
    const long r = g_desktop.left + static_cast<long>(std::max(x0, x1) * s);
    const long b = g_desktop.top + static_cast<long>(std::max(y0, y1) * s);
    return { std::max(l, g_desktop.left), std::max(t, g_desktop.top),
             std::min(r, g_desktop.right), std::min(b, g_desktop.bottom) };
}

// 按鼠标绝对位置（客户区 DIP）算选区其余端点：角手柄改被抓的两个端点，
// 边手柄改一条边；只有 Move 用相对位移。其余三个端点保持按下前的快照。
ScreenRect SelectionLayer::handleAt(Handle h, dui::Vec2 pos) const {
    const float s = scale();
    const long px = g_desktop.left + static_cast<long>(pos.x * s);
    const long py = g_desktop.top + static_cast<long>(pos.y * s);
    ScreenRect n = adjustSel_;
    switch (h) {
        case Handle::Move: {
            const long dxL = static_cast<long>((pos.x - press_.x) * s);
            const long dyL = static_cast<long>((pos.y - press_.y) * s);
            n.left += dxL; n.right += dxL; n.top += dyL; n.bottom += dyL;
            break;
        }
        case Handle::LT: n.left = px; n.top = py; break;
        case Handle::RT: n.right = px; n.top = py; break;
        case Handle::LB: n.left = px; n.bottom = py; break;
        case Handle::RB: n.right = px; n.bottom = py; break;
        case Handle::T:  n.top = py; break;
        case Handle::B:  n.bottom = py; break;
        case Handle::L:  n.left = px; break;
        case Handle::R:  n.right = px; break;
        default: break;
    }
    n.left = std::max(n.left, g_desktop.left); n.top = std::max(n.top, g_desktop.top);
    n.right = std::min(n.right, g_desktop.right); n.bottom = std::min(n.bottom, g_desktop.bottom);
    // 保证至少 8px，避免翻面
    if (n.right - n.left < 8) {
        if (h == Handle::LT || h == Handle::LB || h == Handle::L) n.left = n.right - 8;
        else n.right = n.left + 8;
    }
    if (n.bottom - n.top < 8) {
        if (h == Handle::LT || h == Handle::RT || h == Handle::T) n.top = n.bottom - 8;
        else n.bottom = n.top + 8;
    }
    return n;
}

// 仿 ScreenCapture::CutMask::initWinRect：进选区态时枚举一次可见窗口矩形缓存，
// 悬停只遍历缓存。必须排除覆盖窗口自身，否则会命中全屏的自己 → 纱全挖掉。
BOOL CALLBACK SelectionLayer::enumWindow(HWND hwnd, LPARAM lp) {
    auto* ctx = reinterpret_cast<EnumCtx*>(lp);
    if (hwnd == ctx->self) return TRUE;
    if (!IsWindowVisible(hwnd)) return TRUE;
    LONG cloaked = 0;
    DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    if (cloaked) return TRUE;
    RECT rc{};
    if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &rc, sizeof(rc)))) return TRUE;
    if (rc.right - rc.left <= 6 || rc.bottom - rc.top <= 6) return TRUE;
    ctx->out->push_back({
        std::max(rc.left, g_desktop.left), std::max(rc.top, g_desktop.top),
        std::min(rc.right, g_desktop.right), std::min(rc.bottom, g_desktop.bottom) });
    return TRUE;
}

void SelectionLayer::initWinRects(HWND self) {
    winRects_.clear();
    EnumCtx ctx{ self, &winRects_ };
    EnumWindows(enumWindow, reinterpret_cast<LPARAM>(&ctx));
}

void SelectionLayer::highlightWindow(dui::Vec2 pos) {
    const float s = scale();
    const long px = g_desktop.left + static_cast<long>(pos.x * s);
    const long py = g_desktop.top + static_cast<long>(pos.y * s);
    for (const auto& rc : winRects_) {
        if (px > rc.left && py > rc.top && px < rc.right && py < rc.bottom) {
            if (rc.left != sel_.left || rc.top != sel_.top ||
                rc.right != sel_.right || rc.bottom != sel_.bottom) {
                sel_ = rc;
                InvalidatePaint();
            }
            return;
        }
    }
}
