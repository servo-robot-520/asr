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
        /// 保存配置；真正的模型加载在 initialize() 中完成。
        explicit KeywordBackend(KeywordBackendConfig config);
        /// 释放 KWS stream 和 spotter。
        ~KeywordBackend() override;

        KeywordBackend(const KeywordBackend&) = delete;
        KeywordBackend& operator=(const KeywordBackend&) = delete;

        /// 返回 `sherpa_onnx_keyword`。
        [[nodiscard]] std::string_view name() const noexcept override;
        /// 加载 KWS 模型、创建 spotter 和 stream。
        bool initialize(std::string* error) override;
        /// spotter 和 stream 是否都可用。
        [[nodiscard]] bool ok() const override;

        /// 送入 16 kHz PCM，返回命中的关键词 Final 事件。
        std::vector<BackendEvent> push(const float* samples, std::size_t count) override;
        /// 流式 KWS 没有尾部冲刷，返回空。
        std::vector<BackendEvent> flush() override;
        /// 重置 KWS stream。
        void reset() override;

    private:
        KeywordBackendConfig config_; ///< 构造时保存的模型/阈值配置
        const SherpaOnnxKeywordSpotter* spotter_{nullptr}; ///< sherpa-onnx KWS 句柄
        const SherpaOnnxOnlineStream* stream_{nullptr}; ///< 当前 KWS 流
    };

} // namespace asr
