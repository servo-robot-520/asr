#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "asr/asr_backend.hpp"
#include "asr/sense_voice_recognizer.hpp"
#include "asr/vad_segmenter.hpp"

namespace asr {

    /// SenseVoice 离线后端配置：VAD + 离线识别。
    struct SenseVoiceBackendConfig {
        SenseVoiceConfig recognizer;
        VadConfig vad;
    };

    /// SenseVoice 后端。
    ///
    /// 内部用 silero VAD 切句，再对每个完整语音段跑 SenseVoice；事件顺序为
    /// SpeechStart → Endpoint → Final。模型缺失时 ok() 为 false，节点继续运行。
    class SenseVoiceBackend final : public AsrBackend {
    public:
        explicit SenseVoiceBackend(SenseVoiceBackendConfig config);

        [[nodiscard]] std::string_view name() const noexcept override;
        bool initialize(std::string* error) override;
        [[nodiscard]] bool ok() const override;

        std::vector<BackendEvent> push(const float* samples, std::size_t count) override;
        std::vector<BackendEvent> flush() override;
        void reset() override;

    private:
        std::vector<BackendEvent> drain_segments();

        SenseVoiceBackendConfig config_;
        SenseVoiceRecognizer recognizer_;
        VadSegmenter vad_;
        bool speech_active_{false};
    };

} // namespace asr
