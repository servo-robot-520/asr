#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct SherpaOnnxVoiceActivityDetector;

namespace asr {

    /// silero VAD 参数。
    struct VadConfig {
        std::string model; ///< silero_vad.onnx 路径
        float threshold{0.25F}; ///< 语音概率阈值
        float min_silence_duration{0.5F}; ///< 判句尾的最短静音
        float min_speech_duration{0.25F}; ///< 最短有效语音
        float max_speech_duration{10.0F}; ///< 单段最长语音
        int32_t window_size{512}; ///< VAD 输入窗口样本数
        float buffer_seconds{30.0F}; ///< VAD 内部缓冲秒数
        int32_t num_threads{1}; ///< ONNX Runtime 线程数
        std::string provider{"cpu"}; ///< 执行提供者
        bool debug{false}; ///< 是否打开 sherpa-onnx 调试输出
    };

    /// 一段由 VAD 切分出的语音。
    struct VadSegment {
        int64_t start{0}; ///< 段起点在输入流中的样本序号
        std::vector<float> samples; ///< 16 kHz mono float 样本
    };

    /// sherpa-onnx silero VAD 的 RAII 封装。
    ///
    /// accept() 可以接收任意长度块，内部按 window_size 切分后送入 VAD；
    /// 调用方用 detected() 判断是否进入语音，用 empty()/pop() 取已完成的段。
    class VadSegmenter {
    public:
        VadSegmenter() = default;
        ~VadSegmenter();

        VadSegmenter(const VadSegmenter&) = delete;
        VadSegmenter& operator=(const VadSegmenter&) = delete;

        /// 加载 VAD 模型；失败时 error 写入英文原因。
        [[nodiscard]] bool initialize(const VadConfig& config, std::string* error);

        /// VAD 是否已成功加载。
        [[nodiscard]] bool ok() const {
            return vad_ != nullptr;
        }

        /// 送入任意长度的 16 kHz mono float 样本。
        void accept(const float* samples, std::size_t count);

        /// 当前是否处于语音中。
        [[nodiscard]] bool detected() const;

        /// 是否有已经完成的语音段。
        [[nodiscard]] bool empty() const;

        /// 取出最早的完整语音段；没有时返回空。
        [[nodiscard]] std::optional<VadSegment> pop();

        /// 冲刷尾部并强制结束当前段；用于采集 eos 或节点停止。
        void flush();

        /// 清空检测状态；用于播放门控或采集序号跳变。
        void reset();

    private:
        const SherpaOnnxVoiceActivityDetector* vad_{nullptr};
        std::vector<float> pending_;
        int32_t window_size_{512};
    };

} // namespace asr
