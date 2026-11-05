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
        SenseVoiceConfig recognizer; ///< SenseVoice 模型/语言/线程
        VadConfig vad; ///< silero VAD 参数
    };

    /// SenseVoice 后端。
    ///
    /// 内部用 silero VAD 切句，再对每个完整语音段跑 SenseVoice；事件顺序为
    /// SpeechStart → Endpoint → Final。模型缺失时 ok() 为 false，节点继续运行。
    class SenseVoiceBackend final : public AsrBackend {
    public:
        /// 保存 VAD/识别配置；模型加载在 initialize() 中完成。
        explicit SenseVoiceBackend(SenseVoiceBackendConfig config);

        /// 返回 `sherpa_onnx_sense_voice`。
        [[nodiscard]] std::string_view name() const noexcept override;
        /// 初始化 SenseVoice 和 silero VAD。
        bool initialize(std::string* error) override;
        /// 两个模型是否都加载成功。
        [[nodiscard]] bool ok() const override;

        /// 送入 16 kHz PCM，返回 SpeechStart/Endpoint/Final/Failed 事件。
        std::vector<BackendEvent> push(const float* samples, std::size_t count) override;
        /// 冲刷 VAD 尾部并识别剩余语音。
        std::vector<BackendEvent> flush() override;
        /// 重置 VAD 和当前语音状态。
        void reset() override;

    private:
        /// 取出 VAD 已完成的语音段并逐个识别。
        std::vector<BackendEvent> drain_segments();

        SenseVoiceBackendConfig config_; ///< 构造时保存的配置
        SenseVoiceRecognizer recognizer_; ///< SenseVoice 离线识别器
        VadSegmenter vad_; ///< silero VAD 分段器
        bool speech_active_{false}; ///< 是否已发过 SpeechStart
    };

} // namespace asr
