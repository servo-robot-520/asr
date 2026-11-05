#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "asr/asr_backend.hpp"

struct SherpaOnnxKeywordSpotter;
struct SherpaOnnxOnlineStream;

namespace asr {

    /// sherpa-onnx 流式关键词后端配置。
    ///
    /// 该后端不依赖 VAD/SenseVoice，直接在 16 kHz PCM 上跑 KWS 模型；适合只需
    /// 检测少数固定词/唤醒词的轻量场景。事件只发 Final（text=命中的关键词）。
    struct KeywordBackendConfig {
        std::string encoder; ///< streaming encoder ONNX
        std::string decoder; ///< streaming decoder ONNX
        std::string joiner; ///< streaming joiner ONNX
        std::string tokens; ///< tokens.txt
        std::string keywords_file; ///< text2token 生成的 keywords.txt
        int32_t max_active_paths{4}; ///< 搜索路径数
        int32_t num_trailing_blanks{1}; ///< 触发前的尾随 blank 数
        float keywords_score{1.0F}; ///< 关键词加分
        float keywords_threshold{0.25F}; ///< 检测阈值
        int32_t num_threads{1}; ///< ONNX Runtime 线程数
        std::string provider{"cpu"}; ///< 执行提供者
        bool debug{false}; ///< sherpa-onnx 调试输出
    };

    /// sherpa-onnx 关键词后端。
    class KeywordBackend final : public AsrBackend {
    public:
        explicit KeywordBackend(KeywordBackendConfig config);
        ~KeywordBackend() override;

        KeywordBackend(const KeywordBackend&) = delete;
        KeywordBackend& operator=(const KeywordBackend&) = delete;

        [[nodiscard]] std::string_view name() const noexcept override;
        bool initialize(std::string* error) override;
        [[nodiscard]] bool ok() const override;

        std::vector<BackendEvent> push(const float* samples, std::size_t count) override;
        std::vector<BackendEvent> flush() override;
        void reset() override;

    private:
        KeywordBackendConfig config_;
        const SherpaOnnxKeywordSpotter* spotter_{nullptr};
        const SherpaOnnxOnlineStream* stream_{nullptr};
    };

} // namespace asr
