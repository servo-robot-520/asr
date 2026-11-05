#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "asr/keyword_backend.hpp"
#include "asr/wake_word_detector.hpp"

namespace asr {

    /// 基于 sherpa-onnx KWS 的唤醒词检测器。
    ///
    /// 复用 KeywordBackend 的模型/流式解码，但只把命中的关键词转换成
    /// WakeEvent，不参与主 ASR 的 BackendEvent 流程。
    class KeywordWakeDetector final : public WakeWordDetector {
    public:
        explicit KeywordWakeDetector(KeywordBackendConfig config);

        [[nodiscard]] std::string_view name() const noexcept override;
        bool initialize(std::string* error) override;
        [[nodiscard]] bool ok() const override;

        std::vector<WakeEvent> push(const float* samples, std::size_t count) override;
        void reset() override;
        [[nodiscard]] const std::vector<std::string>& labels() const override;

    private:
        std::string keywords_file_;
        KeywordBackend backend_;
        std::vector<std::string> labels_;
    };

} // namespace asr
