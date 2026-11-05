#pragma once

#include <cstddef>
#include <expected>
#include <string>

struct SherpaOnnxOfflineRecognizer;

namespace asr {

    /// SenseVoice 离线识别参数。
    struct SenseVoiceConfig {
        std::string model; ///< model.int8.onnx 路径
        std::string tokens; ///< tokens.txt 路径
        std::string language{"auto"}; ///< auto / zh / en / ja / ko / yue
        bool use_itn{true}; ///< 是否启用逆文本归一化
        int32_t num_threads{1}; ///< ONNX Runtime 线程数
        std::string provider{"cpu"}; ///< 执行提供者，通常为 cpu
        bool debug{false}; ///< 是否打开 sherpa-onnx 调试输出
    };

    /// 一次离线识别结果。
    struct RecognitionResult {
        std::string text; ///< 识别文本
        std::string language; ///< 识别语言，已去掉 SenseVoice 的标记包装
    };

    /// sherpa-onnx SenseVoice 的 RAII 封装。
    ///
    /// 本类不发布 ROS 消息、不处理线程；调用方负责把完整的一句话 PCM
    /// （16 kHz mono float）交给 recognize()。初始化失败时 ok() 为 false，
    /// 节点可以继续运行并上报错误。
    class SenseVoiceRecognizer {
    public:
        SenseVoiceRecognizer() = default;
        ~SenseVoiceRecognizer();

        SenseVoiceRecognizer(const SenseVoiceRecognizer&) = delete;
        SenseVoiceRecognizer& operator=(const SenseVoiceRecognizer&) = delete;

        /// 加载模型；失败时 error 写入英文原因。
        [[nodiscard]] bool initialize(const SenseVoiceConfig& config, std::string* error);

        /// 模型是否已成功加载。
        [[nodiscard]] bool ok() const {
            return recognizer_ != nullptr;
        }

        /// 识别一句话；samples 必须为 16 kHz mono float。
        [[nodiscard]] std::expected<RecognitionResult, std::string>
        recognize(const float* samples, std::size_t count) const;

    private:
        const SherpaOnnxOfflineRecognizer* recognizer_{nullptr};
    };

} // namespace asr
