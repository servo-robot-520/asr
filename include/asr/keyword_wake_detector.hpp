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
        /// 保存 KWS 配置并构造内部 KeywordBackend。
        explicit KeywordWakeDetector(KeywordBackendConfig config);

        /// 返回 `sherpa_onnx_keyword_wake`。
        [[nodiscard]] std::string_view name() const noexcept override;
        /// 加载 KWS 模型并解析 wake.keywords_file 的显示词。
        bool initialize(std::string* error) override;
        /// 模型和显示词列表是否都可用。
        [[nodiscard]] bool ok() const override;

        /// 送入 PCM，返回命中的唤醒词。
        std::vector<WakeEvent> push(const float* samples, std::size_t count) override;
        /// 重置内部 KWS 流。
        void reset() override;
        /// 返回唤醒词显示词列表。
        [[nodiscard]] const std::vector<std::string>& labels() const override;

    private:
        std::string keywords_file_; ///< 唤醒词表路径
        KeywordBackend backend_; ///< 复用的 KWS 解码实现
        std::vector<std::string> labels_; ///< `@` 后的显示词
    };

} // namespace asr
