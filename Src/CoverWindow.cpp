#include "CoverWindow.h"

#include <commctrl.h>

CoverWindow::CoverWindow()
    : dui::Window(dui::WindowOptions{
          .title = L"ScreenMovie", .width = 800.f, .height = 600.f,
          .frameless = true, .transparent = true,
          .center = false, .resizable = false, .doubleClickMaximize = false,
          .topmost = true, .toolWindow = true, .noActivate = true }) {}

void CoverWindow::initWindowRects() { layer_->initWindowRects(NativeHandle()); }

bool CoverWindow::OnNcHitTest(dui::Vec2, dui::HitTestCode& hit) {
    if (!mouseThrough_) return false;
    hit = static_cast<dui::HitTestCode>(-1);   // HTTRANSPARENT，ScreenCapture 同款
    return true;
}

void CoverWindow::setMouseThrough(bool v) {
    mouseThrough_ = v;
    HWND hwnd = NativeHandle();
    if (hwnd != nullptr) {
        LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        if (v) {
            ex |= WS_EX_LAYERED;
            ex |= WS_EX_TRANSPARENT;
        } else {
            ex &= ~static_cast<LONG_PTR>(WS_EX_TRANSPARENT);
            ex &= ~static_cast<LONG_PTR>(WS_EX_LAYERED);
        }
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex);
        if (v) {
            if (GetCapture() == hwnd) ReleaseCapture();
            SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
        }
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
    coverDesktop();   // 重建合成后重新设 rgn 并触发遮罩重绘
}

void CoverWindow::coverDesktop() {
    SetBoundsPx(dui::Rect{ static_cast<float>(g_desktop.left), static_cast<float>(g_desktop.top),
                           static_cast<float>(g_desktop.width()), static_cast<float>(g_desktop.height()) });
    // 全屏 transparent 窗必须设整窗矩形 rgn，否则 DWM 把透明区合成成白板。
    SetWindowRgn(NativeHandle(),
                 CreateRectRgn(0, 0, g_desktop.width(), g_desktop.height()), TRUE);
    if (layer_) layer_->refresh();   // rgn 设置会重建合成，强制重绘遮罩
}

LRESULT CALLBACK CoverWindow::cursorSubclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                             UINT_PTR, DWORD_PTR ref) {
    auto* self = reinterpret_cast<CoverWindow*>(ref);
    if (msg == WM_SETCURSOR && LOWORD(lp) == HTCLIENT && self->layer_ != nullptr) {
        POINT pt{};
        GetCursorPos(&pt);
        ScreenToClient(hwnd, &pt);
        const float s = self->Scale();
        const dui::Vec2 dip{ pt.x / s, pt.y / s };
        const SelectionLayer::Handle h = self->layer_->hitHandle(dip);
        LPCWSTR idc = nullptr;
        switch (h) {
            case SelectionLayer::Handle::LT: case SelectionLayer::Handle::RB: idc = IDC_SIZENWSE; break;
            case SelectionLayer::Handle::RT: case SelectionLayer::Handle::LB: idc = IDC_SIZENESW; break;
            case SelectionLayer::Handle::T:  case SelectionLayer::Handle::B:  idc = IDC_SIZENS; break;
            case SelectionLayer::Handle::L:  case SelectionLayer::Handle::R:  idc = IDC_SIZEWE; break;
            case SelectionLayer::Handle::Move: idc = IDC_SIZEALL; break;
            default: break;
        }
        if (idc != nullptr) {
            SetCursor(LoadCursorW(nullptr, idc));
            return TRUE;   // 阻止 DUI 默认箭头覆盖
        }
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

void CoverWindow::OnCreate() {
    SetCursor(LoadCursorW(nullptr, IDC_CROSS));
    SetWindowSubclass(NativeHandle(), &CoverWindow::cursorSubclass, 1,
                      reinterpret_cast<DWORD_PTR>(this));
    dui::Panel* root = Root();
    root->SetStyle(dui::Style().FlexDirection(dui::FlexDirection::Column));
    layer_ = root->AddChild<SelectionLayer>(dui::Style()
        .FlexGrow(1.f).Cursor(dui::Cursor::Crosshair));
    digit_ = root->AddChild<dui::Text>(L"", dui::Style()
        .Position(dui::Position::Absolute)
        .Width(96.f).Height(96.f)
        .FontSize(48.f).Color(kWhite)
        .TextAlign(dui::TextAlign::Center));
    layer_->bindDigit(digit_);
}

void CoverWindow::OnDpiChanged(const dui::DpiChanged&) { coverDesktop(); }
