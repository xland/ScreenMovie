#include "pch.h"

#include <algorithm>
#include <vector>

#include "Recorder.h"
#include "Rec/VideoMp4.hpp"
#include "Rec/VideoGif.hpp"

using namespace Microsoft::WRL;

namespace {

struct DupTarget {
    CComPtr<IDXGIAdapter1> ad;
    UINT output{ 0 };
    RECT mon{};
};

void collectDupTargets(const RECT& rect, std::vector<DupTarget>& targets) {
    HMONITOR hMon = MonitorFromRect(&rect, MONITOR_DEFAULTTONEAREST);
    if (!hMon) return;
    CComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory)) || !factory) return;
    for (UINT i = 0;; ++i) {
        CComPtr<IDXGIAdapter1> ad;
        if (FAILED(factory->EnumAdapters1(i, &ad)) || !ad) break;
        for (UINT j = 0;; ++j) {
            CComPtr<IDXGIOutput> output;
            if (FAILED(ad->EnumOutputs(j, &output)) || !output) break;
            DXGI_OUTPUT_DESC desc{};
            if (FAILED(output->GetDesc(&desc)) || desc.Monitor != hMon) continue;
            targets.push_back({ ad, j, desc.DesktopCoordinates });
        }
    }
}

RECT toLocalRect(const RECT& rect, const RECT& mon) {
    long l = std::max(rect.left, mon.left) - mon.left;
    long t = std::max(rect.top, mon.top) - mon.top;
    long r = std::min(rect.right, mon.right) - mon.left;
    long b = std::min(rect.bottom, mon.bottom) - mon.top;
    l = (l + 3) & ~3l;
    t = (t + 1) & ~1l;
    return { l, t, l + std::max(4l, (r - l) & ~3l), t + std::max(2l, (b - t) & ~1l) };
}

void showRecordError() {
    MessageBoxW(nullptr, L"录制失败，请检查图形设置后重试。", L"ScreenMovie",
        MB_OK | MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND);
}

}  // namespace

std::wstring Recorder::dataDirectory() {
    wchar_t* roaming = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming)) && roaming) {
        dir = roaming;
        CoTaskMemFree(roaming);
    } else {
        dir = L"C:\\";
    }
    dir += L"\\ScreenMovie";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

Recorder::Recorder() = default;

Recorder::~Recorder() {
    stop();
}

void Recorder::start(RecordFormat format, RECT screenRect, bool useSpeaker, bool useMic) {
    if (recording_.load()) return;
    format_ = format;
    recording_.store(true);
    if (format == RecordFormat::Mp4) startMp4(screenRect, useSpeaker, useMic);
    else startGif(screenRect);
}

void Recorder::startMp4(RECT screenRect, bool useSpeaker, bool useMic) {
    mp4Param_ = std::make_unique<VideoMp4::DESKTOPCAPTUREPARAMS>();

    long rcLeft = (screenRect.left + 3) & ~3l;
    long rcTop = (screenRect.top + 1) & ~1l;
    const long rcW = std::max(4l, (screenRect.right - rcLeft) & ~3l);
    const long rcH = std::max(2l, (screenRect.bottom - rcTop) & ~1l);
    mp4Param_->rx = { rcLeft, rcTop, rcLeft + rcW, rcTop + rcH };
    mp4Param_->f = dataDirectory() + L"\\temp.mp4";
    mp4Param_->EndMS = 0;
    mp4Param_->fps = 30;
    mp4Param_->vbrm = 2;
    mp4Param_->vbrq = 50;
    mp4Param_->Qu = 50;
    mp4Param_->MustEnd = false;
    VideoMp4::setAudio(mp4Param_.get(), useSpeaker, useMic);

    captureThread_ = std::thread([this] {
        HRESULT hr = MFStartup(MF_VERSION);
        if (FAILED(hr)) { showRecordError(); recording_.store(false); return; }
        hr = CoInitializeEx(0, COINIT_APARTMENTTHREADED);
        if (FAILED(hr)) { MFShutdown(); recording_.store(false); return; }

        std::vector<DupTarget> targets;
        collectDupTargets(mp4Param_->rx, targets);
        const RECT deskRx = mp4Param_->rx;
        int code = 0;
        for (auto codec : { MFVideoFormat_HEVC, MFVideoFormat_H264 }) {
            mp4Param_->VIDEO_ENCODING_FORMAT = codec;
            if (targets.empty()) {
                mp4Param_->rx = deskRx;
                code = VideoMp4::DesktopCapture(*mp4Param_);
            }
            for (auto& t : targets) {
                if (mp4Param_->MustEnd) break;
                mp4Param_->ad = t.ad;
                mp4Param_->nOutput = t.output;
                mp4Param_->rx = toLocalRect(deskRx, t.mon);
                code = VideoMp4::DesktopCapture(*mp4Param_);
                if (code != -2) break;
            }
            if (mp4Param_->MustEnd) break;
            if (code != -5 && code != -10) break;
        }
        if (code != 0 && !mp4Param_->MustEnd) showRecordError();
        CoUninitialize();
        MFShutdown();
        recording_.store(false);
    });
}

void Recorder::startGif(RECT screenRect) {
    gifParam_ = std::make_unique<VideoGif::GifParam>();
    gifParam_->x = screenRect.left;
    gifParam_->y = screenRect.top;
    gifParam_->w = screenRect.right - screenRect.left;
    gifParam_->h = screenRect.bottom - screenRect.top;
    gifParam_->path = dataDirectory() + L"\\temp.gif";

    captureThread_ = std::thread([this] {
        VideoGif::createGif(gifParam_.get());
        recording_.store(false);
    });
}

std::wstring Recorder::stop() {
    if (!recording_.load()) return L"";

    std::wstring filePath;
    if (mp4Param_) {
        mp4Param_->MustEnd = true;
        filePath = mp4Param_->f;
    }
    if (gifParam_) {
        gifParam_->isFinish = true;
        filePath = gifParam_->path;
    }
    if (captureThread_.joinable()) captureThread_.join();

    if (mp4Param_ && mp4Param_->framesWritten == 0) {
        DeleteFileW(filePath.c_str());
        filePath.clear();
    }
    mp4Param_.reset();
    gifParam_.reset();
    recording_.store(false);
    return filePath;
}
