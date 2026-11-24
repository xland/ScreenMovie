#pragma once

#include <windows.h>
#include <atomic>
#include <memory>
#include <string>
#include <thread>

namespace VideoMp4 { struct DESKTOPCAPTUREPARAMS; }
namespace VideoGif { struct GifParam; }

enum class RecordFormat { Mp4, Gif };

// 录制调度：区域已由选层定好，只管编码线程与临时文件。
class Recorder {
public:
    Recorder();
    ~Recorder();

    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;

    void start(RecordFormat format, RECT screenRect, bool useSpeaker, bool useMic);
    // 停止并等待编码线程收尾，返回临时文件路径；没在录制或无有效帧时返回空串。
    std::wstring stop();
    bool isRecording() const noexcept { return recording_.load(); }

    // 时长上限（秒）：MP4 120 分钟，GIF 6 分钟。
    static int limitSeconds(RecordFormat format) noexcept {
        return format == RecordFormat::Gif ? 6 * 60 : 120 * 60;
    }

    static std::wstring dataDirectory();

private:
    void startMp4(RECT screenRect, bool useSpeaker, bool useMic);
    void startGif(RECT screenRect);

    RecordFormat format_{ RecordFormat::Mp4 };
    std::atomic<bool> recording_{ false };
    std::thread captureThread_;
    std::unique_ptr<VideoMp4::DESKTOPCAPTUREPARAMS> mp4Param_;
    std::unique_ptr<VideoGif::GifParam> gifParam_;
};
