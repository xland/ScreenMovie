#pragma once

#include <cstdio>
#include <atomic>
#include <string>
#include "pch.h"
#include "gifski.h"
//D:\sdk\gifski\target\release\gifski.lib
//D:\sdk\gifski

namespace VideoGif {

    inline std::string toUtf8(const std::wstring& wide) {
        if (wide.empty()) return {};
        const int n = WideCharToMultiByte(CP_UTF8, 0, wide.data(), (int)wide.size(), nullptr, 0, nullptr, nullptr);
        std::string s(n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, wide.data(), (int)wide.size(), s.data(), n, nullptr, nullptr);
        return s;
    }

    struct GifParam
    {
        std::atomic<bool> isFinish;
        std::wstring path;
        int w;
        int h;
        int x;
        int y;
        UINT fps{16};
    };

    inline void drawCursor(HDC hMemDC, GifParam* param) {
        CURSORINFO cursorInfo = { sizeof(CURSORINFO) };
        if (!GetCursorInfo(&cursorInfo) || !(cursorInfo.flags & CURSOR_SHOWING) ||
            cursorInfo.hCursor == nullptr) return;
        int localX = cursorInfo.ptScreenPos.x - param->x;
        int localY = cursorInfo.ptScreenPos.y - param->y;
        ICONINFO iconInfo{};
        if (GetIconInfo(cursorInfo.hCursor, &iconInfo)) {
            localX -= iconInfo.xHotspot;
            localY -= iconInfo.yHotspot;
            // GetIconInfo 造出的位图归调用方所有，必须释放。
            if (iconInfo.hbmMask) DeleteObject(iconInfo.hbmMask);
            if (iconInfo.hbmColor) DeleteObject(iconInfo.hbmColor);
        }
        if (localX >= 0 && localX < param->w && localY >= 0 && localY < param->h) {
            DrawIconEx(hMemDC, localX, localY, cursorInfo.hCursor, 0, 0, 0, nullptr,
                       DI_NORMAL | DI_DEFAULTSIZE);
        }
        // 不能 DestroyIcon：hCursor 是系统共享光标句柄，销毁会破坏 user32 状态导致崩溃。
    }
    
    //这段代码是好的，但会让exe体积从632增大到1404
    //生成的gif体积很小
    inline void createGif(GifParam* param) {
        GifskiSettings setting{
            .width{(uint32_t)param->w},
            .height{(uint32_t)param->h},
            .quality{80},
            .fast{true},
            .repeat{0}//循环
        };
        auto path = toUtf8(param->path);
        gifski* encoder = gifski_new(&setting);
        if (!encoder) return;
        gifski_set_file_output(encoder, path.data());
        // 用32位位图采集，天然4字节对齐，无行填充问题
        uint32_t srcRowBytes = param->w * 4;
        std::vector<unsigned char> bgra_buffer(srcRowBytes * param->h);
        // gifski需要紧密排列的RGB数据
        uint32_t dstRowBytes = param->w * 3;
        std::vector<unsigned char> rgb_buffer(dstRowBytes * param->h);
        HDC hScreenDC = GetDC(nullptr);
        HDC hMemDC = CreateCompatibleDC(hScreenDC);
        HBITMAP hBitmap = CreateCompatibleBitmap(hScreenDC, param->w, param->h);
        HGDIOBJ hOldBitmap = SelectObject(hMemDC, hBitmap);
        BITMAPINFO bmi = { sizeof(BITMAPINFOHEADER), param->w, 0-param->h, 1, 32, BI_RGB, 0, 0, 0, 0, 0 };
        auto index{ 0 };
        const int frameIntervalMs = 1000 / param->fps;
        while (!param->isFinish) {
            auto tickStart = GetTickCount64();
            BitBlt(hMemDC, 0, 0, param->w, param->h, hScreenDC, param->x, param->y, SRCCOPY);
            drawCursor(hMemDC, param);
            GetDIBits(hMemDC, hBitmap, 0, param->h, (void*)bgra_buffer.data(), &bmi, DIB_RGB_COLORS);
            // BGRA → RGB，逐行转换到紧密排列的缓冲区
            for (int row = 0; row < param->h; row++) {
                const unsigned char* srcRow = bgra_buffer.data() + row * srcRowBytes;
                unsigned char* dstRow = rgb_buffer.data() + row * dstRowBytes;
                for (int col = 0; col < param->w; col++) {
                    dstRow[col * 3 + 0] = srcRow[col * 4 + 2]; // R
                    dstRow[col * 3 + 1] = srcRow[col * 4 + 1]; // G
                    dstRow[col * 3 + 2] = srcRow[col * 4 + 0]; // B
                }
            }
            double timestamp_sec = static_cast<double>(index) / param->fps;
            gifski_add_frame_rgb(encoder, index, (uint32_t)param->w, dstRowBytes, (uint32_t)param->h, rgb_buffer.data(), timestamp_sec);
            auto elapsed = GetTickCount64() - tickStart;
            int sleepTime = frameIntervalMs - static_cast<int>(elapsed);
            if (sleepTime > 0) {
                Sleep(sleepTime);
            }
            index += 1;
        }
        gifski_finish(encoder);
        SelectObject(hMemDC, hOldBitmap);
        DeleteObject(hBitmap);
        DeleteDC(hMemDC);
        ReleaseDC(nullptr, hScreenDC);
    }
}
